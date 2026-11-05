"""CPU-only CLI contracts; requires the standalone Release replay executable."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from export_native_ram_replay import export
from inspect_native_ram_recording import digest
from replay_native_ram_analysis import run
from test_native_ram_replay import NativeRamReplayCliTests

EXE = Path(os.environ.get('VP_NATIVE_RAM_ANALYSIS_EXE', str(Path(__file__).resolve().parents[1] / 'artifacts/native-ram-analysis/build/NativeRamAnalysisReplay.exe')))


class NativeWorkerReplayTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(EXE.is_file(), 'Build tools/native_ram_analysis_replay.vcxproj first')
        self.fixture = NativeRamReplayCliTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root, self.capture = self.fixture.root, self.fixture.capture

    def add(self, accepted, source, timestamp, outcome='written', **changes):
        e = self.fixture.add(outcome, accepted, source, timestamp, **changes)
        if outcome == 'written':
            width, height = 320, 180
            # Authored P010 with black bars and textured picture; no sidecar planes.
            y = [64 if row < 20 or row >= 160 else 320 + (x * 3 + row * 5) % 200
                 for row in range(height) for x in range(width)]
            pixels = struct.pack('<' + 'H' * len(y), *(v << 6 for v in y)) + struct.pack('<H', 512 << 6) * (width * height // 2)
            e.update(bytes=len(pixels), width=width, height=height, stride0=width * 2,
                     stride1=width * 2, format=0, encoding=14, colorspace=4, sampleRange=1)
            binary = self.capture / f"frame-{e['index']:06}.bin"
            binary.write_bytes(pixels)
            sidecar = binary.with_suffix('.txt')
            identity = sidecar.read_text().splitlines()[1]
            sidecar.write_text(f'{width} {height} {width*2} {width*2} 0 14 4 0 0 0 1\n' + identity + '\n')
            self.fixture.flush()
        return e

    def replay(self, name='result'):
        manifest = self.root / (name + '-export')
        export(self.capture, manifest)
        receipt = run(manifest / 'replay.json', EXE, self.root / name)
        self.assertTrue(receipt['passed'], receipt)
        rows = [json.loads(line) for line in (self.root / name / 'frames.jsonl').read_text().splitlines()]
        return receipt, rows

    def test_original_sources_gaps_resets_and_large_timestamps(self):
        tick = (1 << 60) + 3
        self.add(10, 100, tick)
        self.add(11, 101, tick + 417083)
        self.add(12, 102, tick + 834166, outcome='ingress-busy')
        self.add(13, 103, tick + 1251249)
        self.add(1, 1, 7, transportGeneration=2, sourceFormatGeneration=3, continuityGeneration=8)
        before = {p.name: digest(p) for p in self.capture.iterdir()}
        receipt, rows = self.replay()
        self.assertEqual([1, 2, 4, 5], [r['index'] for r in rows])
        self.assertEqual([tick, tick + 417083, tick + 1251249, 7], [r['identity_v1'][3] for r in rows])
        self.assertEqual(3, len(receipt['segments']))
        self.assertEqual([True, False, True, True], [r['contextReset'] for r in rows])
        self.assertEqual('ingress-busy', receipt['omissions'][0]['outcome'])
        self.assertTrue(all(r['exactCurrentIdentity'] for r in rows))
        self.assertTrue(all(r['workMs'] >= 0 and r['queueMs'] >= 0 for r in rows))
        self.assertFalse(receipt['videoDeadlineClaim'])
        self.assertEqual(before, {p.name: digest(p) for p in self.capture.iterdir()})

    def test_sidecar_picture_planes_never_supply_geometry(self):
        self.add(1, 1, 100)
        _, rows = self.replay('zero')
        side = self.capture / 'frame-000001.txt'
        side.write_text(side.read_text().replace('0 14 4 0 0 0 1', '0 14 4 61 111 0 1'))
        _, other = self.replay('foreign-planes')
        self.assertTrue(rows[0]['physicalAvailable'])
        self.assertEqual(rows[0]['physicalBounds'], other[0]['physicalBounds'])
        self.assertEqual([0, 20, 320, 160], other[0]['physicalBounds'])

    def test_corruption_and_changed_segmentation_fail_before_worker(self):
        self.add(1, 1, 100)
        self.add(3, 3, 300)
        target = self.root / 'export'
        export(self.capture, target)
        manifest = target / 'replay.json'
        data = json.loads(manifest.read_text())
        data['segments'][0]['indices'].append(2)
        manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'segments'):
            run(manifest, EXE, self.root / 'bad-segments')
        self.assertFalse((self.root / 'bad-segments').exists())
        target2 = self.root / 'export2'
        export(self.capture, target2)
        binary = self.capture / 'frame-000001.bin'
        pixels = bytearray(binary.read_bytes()); pixels[30] ^= 1; binary.write_bytes(pixels)
        with self.assertRaisesRegex(ValueError, 'original capture'):
            run(target2 / 'replay.json', EXE, self.root / 'corrupt')

    def test_native_consumer_rechecks_bytes_and_identity(self):
        self.add(1, 1, 100)
        target = self.root / 'export'
        data = export(self.capture, target)
        f = data['frames'][0]
        pins = self.root / 'pins.txt'
        pins.write_text(f"1 {f['sha256']} {f['sidecarSha256']}\n")
        binary = self.capture / 'frame-000001.bin'
        pixels = bytearray(binary.read_bytes()); pixels[30] ^= 1; binary.write_bytes(pixels)
        result = subprocess.run([str(EXE), str(self.capture), data['captureManifest']['path'], str(pins), str(self.root / 'bad.jsonl')], capture_output=True, text=True)
        self.assertEqual(2, result.returncode)
        self.assertIn('pixels differ', result.stderr)
        self.assertNotIn('"kind":"frame"', (self.root / 'bad.jsonl').read_text())
        manifest = Path(data['captureManifest']['path'])
        words = manifest.read_text().splitlines()[1].split()
        words[2] = '999'  # Replace acceptedSequence, retaining the original sidecar.
        manifest.write_text('1\n' + ' '.join(words) + '\n')
        result = subprocess.run([str(EXE), str(self.capture), str(manifest), str(pins), str(self.root / 'foreign.jsonl')], capture_output=True, text=True)
        self.assertEqual(2, result.returncode)
        self.assertIn('original manifest identity', result.stderr)


if __name__ == '__main__':
    unittest.main()
