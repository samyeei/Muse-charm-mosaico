"""Validate a character pack with the official GSP runtime pinned by its contract.

Checks the envelope, runtime loading and all eight visible animation states.
This is host evidence, not ESP32 memory/timing or Recovery installation evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import zipfile

from PIL import Image
from character_pack import STATES, parse_json, validate_pack


def validate_runtime(pack: Path, contract: Path, sim: Path, bind_header: Path, output: Path) -> dict:
    metadata = parse_json(contract.read_bytes())
    manifest = validate_pack(pack, compat_version=metadata["compat_version"], template_id=metadata["template_id"])
    bindings = dict((name.lower(), int(value)) for name, value in re.findall(
        r"^#define GSP_BIND_(\w+) (\d+)$", bind_header.read_text(), re.MULTILINE))
    if not all(name in bindings for name in STATES):
        raise ValueError("firmware bind header does not expose all eight states")
    version = subprocess.run([str(sim), "--capabilities"], check=True, capture_output=True, text=True, timeout=15)
    expected_runtime = metadata.get("runtime_version")
    if not expected_runtime or json.loads(version.stdout).get("runtime_version") != expected_runtime:
        raise ValueError(f"expected pinned simulator runtime {expected_runtime!r}")
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "muse-character-runtime-check/v1", "manifest": manifest,
              "pack_sha256": hashlib.sha256(pack.read_bytes()).hexdigest(), "states": {},
              "scope": "Official host runtime and animation display; not hardware/install acceptance."}
    with tempfile.TemporaryDirectory(prefix="muse-runtime-check-") as temporary:
        work = Path(temporary)
        with zipfile.ZipFile(pack) as archive:
            (work / "bundle.gspb").write_bytes(archive.read("ui_apps.bin")[64:])
        ready = work / "ready.json"
        with (output / "runtime.log").open("wb") as log:
            process = subprocess.Popen([str(sim), "--bundle", str(work / "bundle.gspb"), "--headless",
                                        "--frames", "0", "--fps", "5", "--fail-on-error",
                                        "--api-listen", "tcp://127.0.0.1:0", "--ready-file", str(ready)],
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 20
                while not ready.exists():
                    if process.poll() is not None:
                        raise ValueError(f"native runtime rejected package; see {output / 'runtime.log'}")
                    if time.monotonic() >= deadline:
                        raise TimeoutError("native runtime did not become ready")
                    time.sleep(0.02)
                endpoint = json.loads(ready.read_text())["api"]
                if not endpoint.startswith("tcp://127.0.0.1:"):
                    raise ValueError("unexpected simulator endpoint")
                with socket.create_connection(("127.0.0.1", int(endpoint.rsplit(":", 1)[1])), timeout=10) as connection:
                    stream = connection.makefile("rb")
                    sequence = 0

                    def call(method, params):
                        nonlocal sequence
                        sequence += 1
                        body = json.dumps({"jsonrpc": "2.0", "id": sequence, "method": method, "params": params}).encode()
                        connection.sendall(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
                        while True:
                            length = None
                            while True:
                                line = stream.readline(1024)
                                if not line:
                                    raise RuntimeError("simulator disconnected")
                                if line == b"\r\n":
                                    break
                                if line.lower().startswith(b"content-length:"):
                                    length = int(line.split(b":", 1)[1])
                            if length is None or not 0 < length <= 1048576:
                                raise ValueError("invalid simulator response length")
                            payload = stream.read(length)
                            if len(payload) != length:
                                raise RuntimeError("short simulator response")
                            reply = json.loads(payload)
                            if reply.get("id") != sequence:
                                continue
                            if "error" in reply:
                                raise ValueError(f"{method}: {reply['error']}")
                            result = reply.get("result")
                            if isinstance(result, int) and result < 0:
                                raise ValueError(f"{method}: runtime error {result}")
                            return result

                    for state in STATES:
                        for name in STATES:
                            call("set_visible", {"bind_id": bindings[name], "visible": name == state})
                        pixels = set()
                        for index in range(24):
                            call("wait", {"frames": 1})
                            shot = output / f"{state}-{index:02}.png"
                            call("screenshot", {"path": str(shot.resolve()), "format": "png"})
                            with Image.open(shot) as image:
                                if image.size != (480, 480):
                                    raise ValueError("unexpected screen dimensions")
                                crop = image.convert("RGB").crop((90, 75, 390, 375))
                                if crop.getbbox() is None:
                                    raise ValueError(f"{state}: empty character region")
                                pixels.add(hashlib.sha256(crop.tobytes()).hexdigest())
                        if len(pixels) < 2:
                            raise ValueError(f"{state}: no visible animation change")
                        report["states"][state] = {"samples": 24, "distinct_character_pixels": len(pixels)}
                    call("goto_scene", {"scene": 1})
                    call("wait", {"frames": 1})
                    call("screenshot", {"path": str((output / "settings.png").resolve()), "format": "png"})
                    call("quit", {})
                    stream.close()
                if process.wait(timeout=10) != 0:
                    raise ValueError("native runtime reported an error")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    report["passed"] = True
    (output / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--sim", type=Path, required=True)
    parser.add_argument("--bind-header", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(validate_runtime(args.pack, args.contract, args.sim, args.bind_header, args.out)))


if __name__ == "__main__":
    main()
