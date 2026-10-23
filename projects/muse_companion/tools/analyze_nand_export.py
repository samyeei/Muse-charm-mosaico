"""Verify private NAND export artifacts and report data signatures, never writes a device."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

PAGE_BYTES = 2048
SIGNATURES = {
    "MPBUNDLE": b"MPBUNDLE", "littlefs": b"littlefs", "GSPB": b"GSPB",
    "MOSGSP": b"MOSGSP\x00\x01", "PNG": b"\x89PNG\r\n\x1a\n",
    "JPEG": b"\xff\xd8\xff", "ZIP": b"PK\x03\x04",
}


def analyze(image: Path, record: Path) -> dict:
    info = json.loads(record.read_text(encoding="utf-8"))
    if info.get("schema") != "iris-rpc-read-result/v1" or info.get("complete") is not True:
        raise ValueError("export is incomplete or has an unsupported record")
    plan = info["plan"]
    size, chunk = plan["size"], plan["chunk_bytes"]
    hashes = info["chunks"]
    if type(size) is not int or type(chunk) is not int or not (0 < chunk <= 4000):
        raise ValueError("invalid export dimensions")
    if size != info["bytes"] or size != image.stat().st_size or len(hashes) * chunk != size:
        raise ValueError("export dimensions/hash count mismatch")
    digest = hashlib.sha256()
    counts = {name: 0 for name in SIGNATURES}
    offsets = {name: [] for name in SIGNATURES}
    pending_page = bytearray()
    non_ff_pages = 0
    zero_pages = 0
    carry = b""
    with image.open("rb") as stream:
        for index, expected in enumerate(hashes):
            data = stream.read(chunk)
            if len(data) != chunk or hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"export chunk {index} failed integrity check")
            digest.update(data)
            chunk_start = index * chunk
            window = carry + data
            base = chunk_start - len(carry)
            for name, magic in SIGNATURES.items():
                position = window.find(magic)
                while position >= 0:
                    absolute = base + position
                    if absolute + len(magic) > chunk_start:
                        counts[name] += 1
                        if len(offsets[name]) < 32:
                            offsets[name].append(absolute + plan["offset"])
                    position = window.find(magic, position + 1)
            carry = window[-7:]
            pending_page.extend(data)
            while len(pending_page) >= PAGE_BYTES:
                page = pending_page[:PAGE_BYTES]
                del pending_page[:PAGE_BYTES]
                non_ff_pages += any(byte != 255 for byte in page)
                zero_pages += not any(page)
        if stream.read(1) or pending_page:
            raise ValueError("export is not a complete page-aligned range")
    if digest.hexdigest() != info["sha256"]:
        raise ValueError("whole export hash mismatch")
    return {
        "schema": "muse-nand-analysis/v1", "image": str(image.resolve()),
        "record": str(record.resolve()), "bytes": size, "sha256": digest.hexdigest(),
        "device_id": plan["device_id"], "boot_id": plan["boot_id"],
        "page_bytes": PAGE_BYTES, "pages": size // PAGE_BYTES,
        "pages_with_non_ff_data": non_ff_pages, "all_zero_pages": zero_pages,
        "signature_counts": counts, "first_signature_offsets": offsets,
        "limitations": [
            "Signatures alone do not validate archive/file formats or identify personal data.",
            "Raw data-page export excludes NAND OOB, ECC and bad-block markers.",
            "A successful export does not prove a restore procedure or authorize formatting.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--record", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.image, args.record or Path(str(args.image) + ".json"))
    with args.out.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
    print(json.dumps({k: result[k] for k in ("bytes", "sha256", "signature_counts")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
