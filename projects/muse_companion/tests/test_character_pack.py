import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from character_pack import digest, validate_pack, wrap_image


class CharacterPackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "test.cpack"
        bundle = b"GSPB" + bytes(4) + struct.pack("<I", 16) + bytes(4)
        self.image = wrap_image(bundle)
        self.manifest = dict(schema="muse-cpack/v1", character_version="test-v1", compat_version="compat-v1",
                             template_id="template-v1", image_size=len(self.image), image_sha256=digest(self.image))

    def write(self, *, extra=False, compression=zipfile.ZIP_STORED):
        with zipfile.ZipFile(self.path, "w", compression=compression) as archive:
            archive.writestr("manifest.json", json.dumps(self.manifest))
            archive.writestr("ui_apps.bin", self.image)
            if extra:
                archive.writestr("../unexpected", b"x")

    def check(self, **kwargs):
        return validate_pack(self.path, compat_version="compat-v1", template_id="template-v1", **kwargs)

    def test_valid_envelope(self):
        self.write()
        self.assertEqual(self.check(sha256=digest(self.path.read_bytes()), size=self.path.stat().st_size), self.manifest)

    def test_compatible_hash_does_not_override_compatibility(self):
        self.manifest["compat_version"] = "other"
        self.write()
        with self.assertRaisesRegex(ValueError, "incompatible"):
            self.check(sha256=digest(self.path.read_bytes()))

    def test_placeholder_without_mosgsp_rejected(self):
        self.image = bytes(512 * 1024)
        self.manifest.update(image_size=len(self.image), image_sha256=digest(self.image))
        self.write()
        with self.assertRaisesRegex(ValueError, "MOSGSP"):
            self.check()

    def test_inner_corruption_with_updated_outer_hash_rejected(self):
        self.image = self.image[:-1] + b"x"
        self.manifest["image_sha256"] = digest(self.image)
        self.write()
        with self.assertRaisesRegex(ValueError, "bundle hash"):
            self.check(sha256=digest(self.path.read_bytes()))

    def test_extra_paths_and_compression_rejected(self):
        self.write(extra=True)
        with self.assertRaises(ValueError): self.check()
        self.write(compression=zipfile.ZIP_DEFLATED)
        with self.assertRaises(ValueError): self.check()

    def test_wrong_outer_size_and_hash_rejected(self):
        self.write()
        with self.assertRaises(ValueError): self.check(size=1)
        with self.assertRaises(ValueError): self.check(sha256="0" * 64)


if __name__ == "__main__":
    unittest.main()
