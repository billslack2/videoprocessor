import json
from pathlib import Path
import tempfile
import unittest
import subtitle_causal_report as causal


def record(stage, seq=8, generation=2, frame=108):
    r = dict(zip(causal.IDENTITY_FIELDS, (generation, seq, frame, 8000, 3, 4, 5, 6, 7, 9)))
    r.update(stage=stage, rendererInstanceKey=99, counters=[11, 1, 0, 0, 0, 0, 0, 0],
             metrics=[0] * 24, resultCode=5, reason="cleanup", flags=0)
    return r


class CausalReportTests(unittest.TestCase):
    def test_same_accepted_different_generation_or_physical_frame_never_joins(self):
        current = record("worker-cue-pixel-witness")
        native = {causal.identity(current): [{"file": "one.bin", "exists": True}]}
        self.assertEqual("exact", causal.native_coverage(current, native)["status"])
        for field in causal.IDENTITY_FIELDS:
            other = dict(current); other[field] += 1
            self.assertEqual("missing", causal.native_coverage(other, native)["status"], field)

    def test_missing_or_ambiguous_baseline_never_claims_native_pair(self):
        current = record("worker-cue-pixel-witness")
        native = {causal.identity(current): [{"file": "current.bin", "exists": True}]}
        self.assertFalse(causal.report([current], native)["pixelFailures"][0]["nativePairIdentityAndSizeMatched"])
        seed = record("worker-cue-pixel-seed", seq=1, frame=101); seed["counters"][1] = 8
        native[causal.identity(seed)] = [{"file": "seed.bin", "exists": True}]
        self.assertTrue(causal.report([current, seed], native)["pixelFailures"][0]["nativePairIdentityAndSizeMatched"])
        self.assertTrue(causal.report([current], native, [current, seed])["pixelFailures"][0]["nativePairIdentityAndSizeMatched"])
        collision = dict(seed); collision["transportGeneration"] = 8
        self.assertFalse(causal.report([current, seed, collision], native)["pixelFailures"][0]["nativePairIdentityAndSizeMatched"])

    def test_unevaluated_certificate_check_is_unknown(self):
        states = causal.certificate_status(3, 2)
        self.assertEqual("passed", states["HeldPose"])
        self.assertEqual("failed", states["CurrentTemplate"])
        self.assertEqual("unknown", states["StrongPose"])
        self.assertEqual("unknown", states["NoNovelOwnedInk"])
        future = record("subtitle-held-certificate")
        future["counters"][:2] = [(1 << 20) | 3, (1 << 20) | 2]
        result = causal.report([future], {})
        certificate = result["heldCertificates"][0]
        self.assertEqual(1 << 20, certificate["unknownEvaluatedMask"])
        self.assertEqual(1 << 20, certificate["unknownFailedMask"])
        self.assertFalse(result["nativePixelHashesVerified"])


    def test_sidecar_full_identity_sparse_coverage_and_output_refusal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); r = record("worker-cue-pixel-witness")
            (root / "frame-000001.bin").write_bytes(b"pixels")
            (root / "frame-000001.txt").write_text("3840 2160 10240\nidentity_v1 " +
                " ".join(map(str, causal.identity(r))) + " 20 0 417083 0 0 0\n")
            (root / "frames.jsonl").write_text(json.dumps({"file": "frame-000001.bin", "bytes": 6,
                "acceptedSequence": 8, "sourceFrameNumber": 108, "analysisDomainKey": 6}) + "\n")
            (root / "analysis.jsonl").write_text(json.dumps(r) + "\n")
            native = causal.load_native(root)
            self.assertEqual("exact", causal.native_coverage(r, native)["status"])
            missing = dict(r); missing["acceptedSequence"] += 1; missing["sourceFrameNumber"] += 1
            self.assertEqual("missing", causal.native_coverage(missing, native)["status"])
            output = root / "result"
            causal.main(["--capture-root", str(root), "--output", str(output)])
            saved = (output / "report.json").read_bytes()
            manifest = json.loads((output / "manifest.json").read_text())
            sidecars = [r for r in manifest["metadataReferences"] if r["path"].endswith("frame-000001.txt")]
            self.assertEqual(1, len(sidecars))
            self.assertEqual(64, len(sidecars[0]["sha256"]))

            with self.assertRaises(SystemExit):
                causal.main(["--capture-root", str(root), "--output", str(output)])
            self.assertEqual(saved, (output / "report.json").read_bytes())
            self.assertFalse(list(output.glob("*.bin")))
            (root / "frame-000001.bin").write_bytes(b"cut")
            self.assertEqual("size-mismatch", causal.native_coverage(r, causal.load_native(root))["status"])


if __name__ == "__main__":
    unittest.main()
