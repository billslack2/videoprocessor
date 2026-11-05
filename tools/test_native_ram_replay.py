import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import subtitle_causal_report

TOOL = Path(__file__).with_name('export_native_ram_replay.py')


class NativeRamReplayCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='vp-native-ram-replay-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.capture = self.root / 'capture'
        self.capture.mkdir()
        self.output = self.root / 'export'
        self.events = []
        self.limits = {'kind': 'limits', 'schema': 1, 'unloggedOffers': 0,
                       'acquisitionTap': 'accepted-input', 'pictureBoundsAvailable': False,
                       'semanticSettingsAvailable': False, 'completeStreamClaim': False}

    def add(self, outcome, accepted, source, timestamp, **changes):
        e = dict(index=len(self.events) + 1, outcome=outcome, transportGeneration=1,
                 acceptedSequence=accepted, sourceFrameNumber=source, captureTimestamp=timestamp,
                 sourceFormatGeneration=2, viewportGeneration=3, rendererGeneration=4,
                 analysisDomainKey=5, policyGeneration=6, continuityGeneration=7, bytes=4,
                 width=2, height=2, stride0=2, stride1=0, format=0, encoding=0, colorspace=0,
                 sampleRange=0, frameDuration100ns=417083, discontinuity=0, rgbBitAlignment=0)
        e.update(changes)
        self.events.append(e)
        if outcome == 'written':
            index = e['index']
            (self.capture / f'frame-{index:06}.bin').write_bytes(bytes([index % 256, 2, 3, 4]))
            identity = [e[n] for n in subtitle_causal_report.IDENTITY_FIELDS] + [0, 0, e['frameDuration100ns'], e['discontinuity'], 0, e['rgbBitAlignment']]
            (self.capture / f'frame-{index:06}.txt').write_text('2 2 2 0 0 0 0 0 0 0 0\nidentity_v1 ' + ' '.join(map(str, identity)) + '\n')
            if 'captureClockTicksPerSecond' in e and 'captureClockProvenance' in e:
                with (self.capture / f'frame-{index:06}.txt').open('a') as side:
                    side.write(f"capture_clock_v1 {e['captureClockTicksPerSecond']} {e['captureClockProvenance']}\n")
        self.flush()
        return e

    def flush(self):
        (self.capture / 'native-window.jsonl').write_text('\n'.join(json.dumps(e) for e in [self.limits, *self.events]) + '\n')
        frames = [dict(index=e['index'], file=f"frame-{e['index']:06}.bin", bytes=e['bytes'],
                       acceptedSequence=e['acceptedSequence'], sourceFrameNumber=e['sourceFrameNumber'],
                       analysisDomainKey=e['analysisDomainKey']) for e in self.events if e['outcome'] == 'written']
        (self.capture / 'frames.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in frames))

    def run_cli(self, expected=0):
        p = subprocess.run([sys.executable, str(TOOL), str(self.capture), '--output', str(self.output)],
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(expected, p.returncode, p.stdout + p.stderr)
        return p

    def result(self):
        return json.loads((self.output / 'replay.json').read_text())

    def test_preserves_evictions_pressure_and_original_gap_manifest(self):
        self.add('ram-evicted', 10, 100, 100000)
        self.add('written', 11, 101, 141708)
        self.add('ingress-busy', 12, 102, 183416)
        self.add('written', 14, 104, 266832)
        self.add('written', 15, 105, 308540)
        before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.capture.iterdir()}
        self.run_cli()
        r = self.result()
        self.assertEqual([2, 4, 5], [f['index'] for f in r['frames']])
        self.assertEqual([[2], [4, 5]], [s['indices'] for s in r['segments']])
        self.assertEqual(['ram-evicted', 'ingress-busy'], [e['outcome'] for e in r['omissions']])
        self.assertEqual(3, r['gaps'][0]['acceptedSequenceDelta'])
        self.assertEqual(125124, r['gaps'][0]['captureTimestampDeltaRaw'])
        self.assertEqual(3, r['gaps'][0]['recordedOmissions'][0]['index'])
        lines = (self.output / 'capture-manifest.txt').read_text().splitlines()
        self.assertEqual('3', lines[0])
        for line, frame in zip(lines[1:], r['frames']):
            integers = list(map(int, line.split()))
            self.assertEqual([frame['index'], *frame['identity_v1']], integers)
            self.assertEqual(17, len(integers))
        # Existing native input consumer finds the exact same original identity mappings.
        loaded = subtitle_causal_report.load_native(self.capture)
        for frame in r['frames']:
            self.assertIn(tuple(frame['identity_v1'][:10]), loaded)
        self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.capture.iterdir()})
        self.assertEqual((self.capture / 'native-window.jsonl').read_bytes(), (self.output / 'native-window.jsonl').read_bytes())
        self.assertFalse(r['completeStreamClaim'])
        self.assertFalse(r['pictureOrCaptionAuthoritySupplied'])
        self.assertFalse(r['timing']['timestampsRewritten'])

    def test_recorded_clock_roundtrip_and_clock_change_split_without_inference(self):
        tick = 2**60 + 19
        self.add('written', 1, 1, tick, captureClockTicksPerSecond=1000000, captureClockProvenance=1)
        self.add('written', 2, 2, tick+41708, captureClockTicksPerSecond=10000000, captureClockProvenance=1)
        self.run_cli()
        r = self.result()
        self.assertEqual([[1], [2]], [s['indices'] for s in r['segments']])
        self.assertEqual([1000000, 10000000], [f['captureClock']['ticksPerSecond'] for f in r['frames']])
        self.assertIn('capture-clock-change', r['gaps'][0]['reasons'])
        self.assertIsNone(r['timing']['captureClockTicksPerSecond'])
        self.assertEqual([tick, tick+41708], [f['identity_v1'][3] for f in r['frames']])
        self.assertFalse(r['frames'][0]['captureClock']['unadjustedHardwareTimeClaim'])
        loaded = subtitle_causal_report.load_native(self.capture)
        self.assertTrue(all(tuple(f['identity_v1'][:10]) in loaded for f in r['frames']))

    def test_unknown_clock_stays_unknown_and_legacy_has_no_inferred_rate(self):
        self.add('written', 1, 1, 100000)
        self.add('written', 2, 2, 141708, captureClockTicksPerSecond=0, captureClockProvenance=0)
        self.run_cli()
        r = self.result()
        self.assertNotIn('captureClock', r['frames'][0])
        self.assertIsNone(r['frames'][1]['captureClock']['ticksPerSecond'])
        self.assertEqual('unavailable', r['frames'][1]['captureClock']['provenance'])
        self.assertIsNone(r['timing']['captureClockTicksPerSecond'])

    def test_clock_sidecar_mismatch_or_incomplete_pair_rejects_export(self):
        self.add('written', 1, 1, 100000, captureClockTicksPerSecond=1000000, captureClockProvenance=1)
        side = self.capture/'frame-000001.txt'
        original = side.read_text()
        side.write_text(original.replace('capture_clock_v1 1000000', 'capture_clock_v1 10000000'))
        self.run_cli(1)
        self.assertFalse(self.output.exists())
        side.write_text(original.replace('capture_clock_v1 ', 'capture_clock_v1invalid '))
        self.run_cli(1)
        self.assertFalse(self.output.exists())
        side.write_text(original)
        del self.events[0]['captureClockProvenance']
        self.flush()
        self.run_cli(1)
        self.assertFalse(self.output.exists())

    def test_corrupt_written_file_fails_without_export(self):
        self.add('written', 10, 100, 100000)
        (self.capture / 'frame-000001.bin').write_bytes(b'bad')
        self.run_cli(1)
        self.assertFalse(self.output.exists())

    def test_foreign_sidecar_identity_fails_without_export(self):
        self.add('written', 10, 100, 100000)
        p = self.capture / 'frame-000001.txt'
        p.write_text(p.read_text().replace('100000', '100001'))
        self.run_cli(1)
        self.assertFalse(self.output.exists())

    def test_pending_or_empty_window_is_not_replay_input(self):
        self.add('pending', 10, 100, 100000)
        self.run_cli(1)
        self.events[0]['outcome'] = 'ram-evicted'
        self.flush()
        self.run_cli(1)
        self.assertFalse(self.output.exists())

    def test_context_reset_and_timestamp_reversal_split_segments(self):
        self.add('written', 10, 100, 100000)
        self.add('written', 11, 101, 141708, continuityGeneration=8)
        self.add('written', 12, 102, 120000, continuityGeneration=8)
        self.run_cli()
        r = self.result()
        self.assertEqual([[1], [2], [3]], [s['indices'] for s in r['segments']])
        self.assertIn('context-or-layout-change', r['gaps'][0]['reasons'])
        self.assertIn('capture-timestamp-unavailable-or-nonmonotonic', r['gaps'][1]['reasons'])
        self.assertEqual([100000, 141708, 120000], [f['identity']['captureTimestamp'] for f in r['frames']])

    def test_uint64_timestamp_and_existing_output_are_preserved(self):
        tick = 2**60 + 123
        self.limits['unloggedOffers'] = 1
        self.add('written', 10, 100, tick)
        self.run_cli()
        r = self.result()
        self.assertTrue(r['sequenceCoverageUncertain'])
        self.assertEqual(tick, r['frames'][0]['identity']['captureTimestamp'])
        self.assertIn(str(tick), (self.output / 'capture-manifest.txt').read_text())
        before = (self.output / 'replay.json').read_bytes()
        self.run_cli(1)
        self.assertEqual(before, (self.output / 'replay.json').read_bytes())
        self.assertEqual([0, 0], r['frames'][0]['layout'][7:9])
        self.assertFalse(r['pictureBoundsAvailable'])


if __name__ == '__main__':
    unittest.main()
