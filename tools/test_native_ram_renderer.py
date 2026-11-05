"""Planner-only contracts: no renderer process, GPU call, or VP launch."""
import json
from pathlib import Path
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from export_native_ram_replay import export
from replay_native_ram_renderer import prepare, execute, describe_omissions
import test_native_ram_replay as fixture_module


class NativeRendererPlanTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture_module.NativeRamReplayCliTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root, self.capture = self.fixture.root, self.fixture.capture

    def add(self, accepted, source, tick, outcome='written', **changes):
        event = self.fixture.add(outcome, accepted, source, tick, **changes)
        if outcome == 'written':
            width, height, stride = 320, 180, 896
            event.update(bytes=stride*height, width=width, height=height, stride0=stride, stride1=0,
                         format=3, encoding=3, colorspace=4, sampleRange=1)
            binary = self.capture / f"frame-{event['index']:06}.bin"
            binary.write_bytes(bytes(stride*height))
            side = binary.with_suffix('.txt')
            identity = side.read_text().splitlines()[1]
            side.write_text(f'{width} {height} {stride} 0 3 3 4 0 0 0 1\n' + identity + '\n')
            self.fixture.flush()
        return event

    def manifest(self):
        target = self.root / 'export'
        export(self.capture, target)
        return target / 'replay.json'

    def test_plan_retains_original_ticks_gaps_and_cold_segments_without_processes(self):
        tick = (1 << 60) + 3
        self.add(10, 100, tick)
        self.add(11, 101, tick+41708, outcome='ingress-busy')
        self.add(12, 102, tick+83416)
        with patch('subprocess.run', side_effect=AssertionError('Planner launched a process')):
            plan = prepare(self.manifest(), self.root/'plan', 1000000, 'reconstruction', 2.35)
        self.assertEqual([[1], [3]], [[f['index'] for f in s['frames']] for s in plan['segments']])
        self.assertEqual([tick, tick+83416], [s['frames'][0]['identity_v1'][3] for s in plan['segments']])
        self.assertEqual('ingress-busy', plan['omissions'][0]['outcome'])
        self.assertTrue(plan['runtimeTransportIdentityRecreated'])
        self.assertFalse(plan['sourcePixelsOrTimestampsRewritten'])
        self.assertFalse(plan['sidecarPicturePlanesSupplied'])
        self.assertFalse(plan['expectedAspectSupplied'])
        self.assertEqual(0, plan['acquisitionPrerollFrames'])
        self.assertIn('switch_refresh_rate: never', Path(plan['config']).read_text())

    def test_omissions_keep_startup_interior_tail_and_not_offered_distinct(self):
        rows = [dict(originalIndex=n, input=dict(offeredWallNs=n*10), submissions=[]) for n in range(1, 7)]
        rows[0]['submissions'] = [dict(wallNs=25)]
        rows[3]['submissions'] = [dict(wallNs=55)]
        rows[5]['input'] = None
        report = describe_omissions(rows)
        self.assertEqual([(2, 'offered-before-first-submission'), (3, 'interior-unsubmitted'),
                          (5, 'after-last-submitted-source'), (6, 'not-offered')],
                         [(r['originalIndex'], r['category']) for r in report['missing']])
        self.assertFalse(report['causeInferred'])
        self.assertEqual('no-submission-in-segment', describe_omissions([rows[1]])['missing'][0]['category'])

    def test_preload_is_explicit_and_bounded(self):
        self.add(1, 1, 1000000)
        manifest = self.manifest()
        plan = prepare(manifest, self.root/'preloaded', 1000000, 'off', 2.35, 2048)
        self.assertEqual(2048, plan['preloadMiB'])
        for value in (-1, 16385):
            with self.assertRaisesRegex(ValueError, 'preload MiB'):
                prepare(manifest, self.root/f'bad-preload-{value}', 1000000, 'off', 2.35, value)

    def test_missing_timestamp_and_invalid_clock_are_not_invented(self):
        self.add(1, 1, 0)
        manifest = self.manifest()
        with self.assertRaisesRegex(ValueError, 'clock Hz'):
            prepare(manifest, self.root/'bad-clock', 0, 'off', 2.35)
        with self.assertRaisesRegex(ValueError, 'timing'):
            prepare(manifest, self.root/'bad-tick', 1000000, 'off', 2.35)

    def test_changed_segment_list_rejects_before_any_gpu_work(self):
        self.add(1, 1, 1000000)
        self.add(3, 3, 1083416)
        manifest = self.manifest()
        data = json.loads(manifest.read_text())
        data['segments'][0]['indices'].append(2)
        manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'segmentation'):
            prepare(manifest, self.root/'bad', 1000000, 'classic', 2.35)

    def test_unsupported_encoding_rejected_not_silently_converted(self):
        event = self.add(1, 1, 1000000)
        event['encoding'] = 2
        side = self.capture/'frame-000001.txt'
        side.write_text(side.read_text().replace('896 0 3 3 4', '896 0 3 2 4'))
        self.fixture.flush()
        with self.assertRaisesRegex(ValueError, 'V210 only'):
            prepare(self.manifest(), self.root/'unsupported', 1000000, 'off', 2.35)

    def test_later_busy_gpu_keeps_completed_segment_receipt(self):
        self.add(1, 1, 1000000)
        self.add(3, 3, 1083416)
        output = self.root/'planned'
        plan = prepare(self.manifest(), output, 1000000, 'off', 2.35)
        host, dll = self.root/'host.exe', self.root/'test.dll'
        host.write_bytes(b'unit mock host'); dll.write_bytes(b'unit mock test DLL')
        plugin = self.root/'plugins/vprenderer/VideoProcessorVPRenderer.dll'
        plugin.parent.mkdir(parents=True); plugin.write_bytes(b'unit mock plugin')
        def completion(*args, **kwargs):
            directory = Path(kwargs['env']['VP_GENERIC_CAPTURE_OUTPUT'])
            frame = plan['segments'][0]['frames'][0]
            (directory/'inputs.jsonl').write_text(json.dumps(dict(index=frame['index'], originalIdentity=frame['identity_v1']))+'\n')
            (directory/'records.jsonl').write_text(json.dumps(dict(stage='submitted-presentation', flags=1, source=frame['identity_v1'][2], timestamp=frame['identity_v1'][3]))+'\n')
            (directory/'summary.json').write_text(json.dumps(dict(inputs=1,submittedEvents=1,distinctSubmittedInputs=1,foreignSubmitted=0,diagnosticRecordsLost=0)))
            return SimpleNamespace(returncode=0)
        with patch('replay_native_ram_renderer.require_closed_and_idle', side_effect=[[], [], ValueError('GPU busy')]), patch('subprocess.run', side_effect=completion):
            result = execute(plan, host, dll, plugin.parent.parent, 'standard', output)
        self.assertFalse(result['completed'])
        self.assertFalse(result['integrityAndSomeRendererCoverage'])
        self.assertEqual(2, result['deferred']['segment'])
        self.assertTrue(result['runs'][0]['integrityAndSomeRendererCoverage'])
        self.assertTrue((output/'progress.json').is_file())
        self.assertTrue((output/'producer.json').is_file())
        self.assertFalse((output/'segment-0002/native-renderer-host.exe').exists())

    def test_zero_exit_without_actual_renderer_evidence_is_not_coverage(self):
        self.add(1, 1, 1000000)
        output = self.root/'planned'
        plan = prepare(self.manifest(), output, 1000000, 'off', 2.35)
        host, dll = self.root/'host.exe', self.root/'test.dll'
        host.write_bytes(b'unit mock host'); dll.write_bytes(b'unit mock DLL')
        plugin = self.root/'plugins/vprenderer/VideoProcessorVPRenderer.dll'
        plugin.parent.mkdir(parents=True); plugin.write_bytes(b'unit mock plugin')
        with patch('replay_native_ram_renderer.require_closed_and_idle', return_value=[]), patch('subprocess.run', return_value=SimpleNamespace(returncode=0)):
            result = execute(plan, host, dll, plugin.parent.parent, 'standard', output)
        self.assertFalse(result['integrityAndSomeRendererCoverage'])
        self.assertFalse(result['allOriginalInputsOffered'])
        self.assertFalse(result['allOriginalInputsObservedSubmitted'])


if __name__ == '__main__':
    unittest.main()
