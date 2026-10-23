import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("nand_analysis", Path(__file__).parents[1] / "tools/analyze_nand_export.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.image = self.root / "image.bin"
        self.record = self.root / "image.bin.json"
        self.data = bytes(1020) + b"MPBUNDLE" + bytes(1020)
        self.info = {"schema": "iris-rpc-read-result/v1", "complete": True,
            "plan": {"size": 2048, "chunk_bytes": 1024, "offset": 0, "device_id": "device", "boot_id": "1"},
            "bytes": 2048, "sha256": hashlib.sha256(self.data).hexdigest(),
            "chunks": [hashlib.sha256(self.data[i:i+1024]).hexdigest() for i in (0,1024)]}
        self.image.write_bytes(self.data)
        self.save_record()

    def tearDown(self):
        self.temp.cleanup()

    def save_record(self):
        self.record.write_text(json.dumps(self.info), encoding="utf-8")

    def test_signature_crossing_chunk_boundary_counted_once(self):
        result = module.analyze(self.image, self.record)
        self.assertEqual(result["signature_counts"]["MPBUNDLE"], 1)
        self.assertEqual(result["first_signature_offsets"]["MPBUNDLE"], [1020])

    def test_tampered_host_data_rejected(self):
        self.image.write_bytes(b"x" + self.data[1:])
        with self.assertRaises(ValueError):
            module.analyze(self.image, self.record)

    def test_incomplete_export_rejected(self):
        self.info["complete"] = False
        self.save_record()
        with self.assertRaises(ValueError):
            module.analyze(self.image, self.record)
