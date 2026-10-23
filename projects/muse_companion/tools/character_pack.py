"""Portable, bounded character-pack validation. No device writes."""
from __future__ import annotations

import hashlib
import json
import struct
import zipfile
from pathlib import Path

SCHEMA = "muse-cpack/v1"
STATES = ("idle", "listening", "thinking", "speaking", "sleepy", "error", "happy", "boot")
PARTITION_BYTES = 3 * 1024 * 1024
MAX_MANIFEST_BYTES = 8192
MAX_PACK_BYTES = PARTITION_BYTES + MAX_MANIFEST_BYTES + 4096
IMAGE_HEADER = struct.Struct("<8sHHI32s16s")
IMAGE_MAGIC = b"MOSGSP\x00\x01"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def unique_object(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def parse_json(data: bytes):
    return json.loads(data.decode("utf-8"), object_pairs_hook=unique_object)


def validate_image(image: bytes) -> None:
    if not IMAGE_HEADER.size + 16 <= len(image) <= PARTITION_BYTES:
        raise ValueError("resource image size outside partition budget")
    magic, version, header_size, size, sha, reserved = IMAGE_HEADER.unpack_from(image)
    if magic != IMAGE_MAGIC or version != 1 or header_size != IMAGE_HEADER.size:
        raise ValueError("invalid MOSGSP header")
    if reserved != bytes(16) or size != len(image) - header_size:
        raise ValueError("invalid MOSGSP length/reserved bytes")
    bundle = image[header_size:]
    if hashlib.sha256(bundle).digest() != sha:
        raise ValueError("inner GSP bundle hash mismatch")
    if bundle[:4] != b"GSPB" or struct.unpack_from("<I", bundle, 8)[0] != len(bundle):
        raise ValueError("invalid GSPB header/length")


def validate_pack(path: Path, *, compat_version: str, template_id: str,
                  sha256: str | None = None, size: int | None = None) -> dict:
    stat_size = path.stat().st_size
    if not 0 < stat_size <= MAX_PACK_BYTES or (size is not None and stat_size != size):
        raise ValueError("character pack size mismatch/outside budget")
    payload = path.read_bytes()
    if sha256 is not None and digest(payload) != sha256:
        raise ValueError("character pack hash mismatch")
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) != 2 or {e.filename for e in entries} != {"manifest.json", "ui_apps.bin"}:
            raise ValueError("pack must contain exactly manifest.json and ui_apps.bin")
        for e in entries:
            if e.compress_type != zipfile.ZIP_STORED or e.flag_bits & (1 | 8):
                raise ValueError("compressed/encrypted/streaming ZIP entries are unsupported")
            limit = MAX_MANIFEST_BYTES if e.filename == "manifest.json" else PARTITION_BYTES
            if not 0 < e.file_size <= limit or e.compress_size != e.file_size:
                raise ValueError("ZIP entry size outside budget")
        manifest = parse_json(archive.read("manifest.json"))
        image = archive.read("ui_apps.bin")  # zipfile also verifies each entry's CRC32.
    required = {"schema", "character_version", "compat_version", "template_id", "image_size", "image_sha256"}
    if not isinstance(manifest, dict) or set(manifest) != required:
        raise ValueError("unexpected pack manifest fields")
    if manifest["schema"] != SCHEMA:
        raise ValueError("unsupported pack schema")
    if not compat_version or "TBD" in compat_version or manifest["compat_version"] != compat_version:
        raise ValueError("incompatible firmware interface")
    if not template_id or manifest["template_id"] != template_id:
        raise ValueError("incompatible UI template")
    version = manifest["character_version"]
    if not isinstance(version, str) or not 1 <= len(version) <= 63 or any(
        c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for c in version
    ):
        raise ValueError("invalid character version")
    if type(manifest["image_size"]) is not int or manifest["image_size"] != len(image):
        raise ValueError("manifest image length mismatch")
    if manifest["image_sha256"] != digest(image):
        raise ValueError("manifest image hash mismatch")
    validate_image(image)
    return manifest


def wrap_image(bundle: bytes) -> bytes:
    image = IMAGE_HEADER.pack(IMAGE_MAGIC, 1, IMAGE_HEADER.size, len(bundle),
                              hashlib.sha256(bundle).digest(), bytes(16)) + bundle
    validate_image(image)
    return image
