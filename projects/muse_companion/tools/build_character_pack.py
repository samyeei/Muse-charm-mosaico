"""PNG sequences -> lossless APNG import -> pinned GSPC -> stored .cpack.

Runs on Python 3.10+ on Linux/Windows; requires Pillow and GSPC 0.6.1.
Input paths are resolved relative to the frame manifest, never guessed.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import subprocess
import tempfile
import zipfile
from pathlib import Path

from PIL import Image
from character_pack import STATES, SCHEMA, digest, parse_json, validate_pack, wrap_image

SCRIPT_DIR = Path(__file__).resolve().parent


def load_frames(manifest_path: Path) -> dict[str, list[Image.Image]]:
    metadata = parse_json(manifest_path.read_bytes())
    if metadata.get("schema") != "muse-character/v1":
        raise ValueError("unsupported frame manifest")
    if metadata.get("canvas") != {"width": 300, "height": 300}:
        raise ValueError("expected a 300 x 300 canvas")
    if metadata.get("background") != "#000000":
        raise ValueError("expected the agreed black background")
    loop = metadata.get("loop", {})
    if loop.get("frames") != 20 or loop.get("fps") != 5 or loop.get("duration_s") != 4:
        raise ValueError("expected 20 frames, 5 FPS, 4 seconds")
    animations = metadata.get("animations", {})
    if set(animations) != set(STATES):
        raise ValueError("expected exactly eight animation states")
    base = manifest_path.parent.resolve()
    result = {}
    for state in STATES:
        info = animations[state]
        files = info.get("files", [])
        if info.get("frames") != 20 or len(files) != 20:
            raise ValueError(f"{state}: expected 20 frames")
        frames = []
        for item in files:
            relative = Path(item["file"])
            source = (base / relative).resolve()
            if relative.is_absolute() or not source.is_relative_to(base):
                raise ValueError("frame path leaves manifest directory")
            raw = source.read_bytes()
            if len(raw) != item["bytes"] or digest(raw) != item["sha256"]:
                raise ValueError(f"{state}: frame hash/length mismatch: {relative}")
            with Image.open(source) as frame:
                if frame.format != "PNG" or frame.size != (300, 300):
                    raise ValueError(f"{state}: invalid frame format/size")
                rgba = frame.convert("RGBA")
                if rgba.getchannel("A").getextrema() != (255, 255):
                    raise ValueError(f"{state}: frames must have an opaque black canvas")
                rgb = rgba.convert("RGB")
                frames.append(rgb.copy())
        result[state] = frames
    return result


def save_apng(path: Path, frames: list[Image.Image], frame_ms: int = 200) -> None:
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=frame_ms, loop=0, disposal=0, blend=0)
    # Confirm the lossless intermediate preserves pixels and playback timing;
    # do not silently accept palette quantization or coalesced frame timing.
    with Image.open(path) as animation:
        if animation.n_frames != len(frames):
            raise ValueError("APNG export changed the frame count")
        for index, expected in enumerate(frames):
            animation.seek(index)
            if animation.info.get("duration") != frame_ms or animation.convert("RGB").tobytes() != expected.tobytes():
                raise ValueError(f"APNG export changed frame {index} pixels/timing")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--template", type=Path, default=SCRIPT_DIR / "character_template")
    parser.add_argument("--gspc", type=Path, required=True)
    parser.add_argument("--character-version", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--quality", type=int, default=85, choices=range(1, 96))
    parser.add_argument("--idle-frame-ms", type=int, default=200, choices=range(200, 1001),
                        help="Idle playback interval; source remains 20 frames. Default 200 ms.")
    parser.add_argument("--sim", type=Path, help="Optional official GSP 1.5.1 native animation validation before publishing")
    parser.add_argument("--runtime-out", type=Path, help="Fresh directory for native validation screenshots/logs; required with --sim")
    args = parser.parse_args()
    if bool(args.sim) != bool(args.runtime_out):
        parser.error("--sim and --runtime-out must be supplied together")
    frames = load_frames(args.manifest)
    contract = parse_json((args.template / "contract.json").read_bytes())
    for name, expected in contract["files"].items():
        source = (args.template / name).resolve()
        if not source.is_relative_to(args.template.resolve()) or digest(source.read_bytes()) != expected:
            raise ValueError(f"fixed template changed: {name}")
    if contract.get("partition_bytes") != 3 * 1024 * 1024:
        raise ValueError("unsupported partition budget")
    version = subprocess.run([str(args.gspc), "--version"], check=True, capture_output=True, text=True).stdout.strip()
    if version != "gspc 0.6.1":
        raise ValueError(f"expected pinned gspc 0.6.1, got {version!r}")
    with tempfile.TemporaryDirectory(prefix="muse-cpack-") as temp:
        work = Path(temp)
        scene = copy.deepcopy(parse_json((args.template / "main.json").read_bytes()))
        hits = set()
        mode_hits = 0
        for obj in scene["objects"]:
            name = obj.get("name")
            if name == "character_animated":
                if obj.get("type") != "progress" or obj.get("min") != 0 or obj.get("max") != 100:
                    raise ValueError("invalid resource-owned character mode marker")
                obj["value"] = 100
                mode_hits += 1
            elif obj.get("type") == "image" and name in STATES:
                apng = work / f"{name}.png"
                save_apng(apng, frames[name], args.idle_frame_ms if name == "idle" else 200)
                obj.update(image=str(apng), codec="lossless", animation_codec="jpeg",
                           quality=args.quality, store_scale=1.0, fit="stretch", max_fps=5)
                hits.add(name)
            elif "image" in obj:
                obj["image"] = str((args.template / obj["image"]).resolve())
        if hits != set(STATES):
            raise ValueError("UI template does not expose all eight character objects")
        if mode_hits != 1:
            raise ValueError("UI template must expose one character mode marker")
        for name, data in (("main.json", scene), ("settings.json", parse_json((args.template / "settings.json").read_bytes()))):
            data["font"] = str((args.template / data["font"]).resolve())
            (work / name).write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        bundle_path = work / "bundle.gspb"
        subprocess.run([str(args.gspc), "compile", str(work / "main.json"), str(work / "settings.json"),
                        "--profile", str(args.template / "companion_rgb565.yaml"), "--deployable",
                        "--symbol", "bundle", "--gen-dir", str(work / "generated"), "-o", str(bundle_path)], check=True)
        image = wrap_image(bundle_path.read_bytes())
        manifest = {"schema": SCHEMA, "character_version": args.character_version,
                    "compat_version": contract["compat_version"], "template_id": contract["template_id"],
                    "image_size": len(image), "image_sha256": digest(image)}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        staging = args.out.with_suffix(args.out.suffix + ".partial")
        try:
            with zipfile.ZipFile(staging, "w", compression=zipfile.ZIP_STORED) as archive:
                for filename, payload in (("manifest.json", json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()),
                                          ("ui_apps.bin", image)):
                    entry = zipfile.ZipInfo(filename, date_time=(2026, 1, 1, 0, 0, 0))
                    entry.compress_type = zipfile.ZIP_STORED
                    archive.writestr(entry, payload)
            validate_pack(staging, compat_version=contract["compat_version"], template_id=contract["template_id"])
            if args.sim:
                from validate_character_runtime import validate_runtime
                headers = list((work / "generated").rglob("companion_binds.h"))
                if len(headers) != 1:
                    raise ValueError("compiler did not produce one companion binding header")
                validate_runtime(staging, args.template / "contract.json", args.sim, headers[0], args.runtime_out)
            with staging.open("r+b") as handle:
                os.fsync(handle.fileno())
            os.replace(staging, args.out)
        finally:
            staging.unlink(missing_ok=True)
    print(json.dumps({"pack": str(args.out), "size": args.out.stat().st_size,
                      "sha256": digest(args.out.read_bytes()), "manifest": manifest,
                      "native_runtime_validated": bool(args.sim)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
