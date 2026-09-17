import unittest
from analyze_crop_log import analyze

def crop(time, seq, fields):
    return f'2026-09-16 {time} | Alpha source crop: sequence={seq} frame_generation=1 {fields}'

class CropLogTests(unittest.TestCase):
    def test_exact_field_and_window_predecessor(self):
        result=analyze([crop('00:30:19', 3, 'applied=1 shift_applied=0 rect=0,40-3840,2116'),
                        crop('00:30:20', 4, 'shift_applied=1 applied=0 rect=0,0-3840,2160'),
                        crop('00:30:20', 4, 'applied=0 shift_applied=0 rect=0,0-3840,2160')],
                       '2026-09-16 00:30:20','2026-09-16 00:30:20')
        self.assertEqual(1,result['counts']['applied_changes'])
        self.assertEqual(1,len(result['source_changes']))

    def test_restart_not_counted_as_a_toggle(self):
        result=analyze([crop('00:30:19', 999, 'applied=1'),crop('00:30:20', 1, 'applied=0')])
        self.assertEqual(0,result['counts'].get('applied_changes',0))

    def test_missing_applied_is_not_shift_applied(self):
        result=analyze([crop('00:30:19', 2, 'shift_applied=1')])
        self.assertEqual(1,result['counts']['crop_records_without_applied'])
        self.assertIn('unavailable',result['limitations'][3])

    def test_zero_proof_summary_keeps_reason_and_event(self):
        result=analyze(['2026-09-16 00:30:20 | Alpha crop recovery: schema=1 event=5 generation=1 sequence=40 epoch=2 phase=summary duration_ms=54000 proof=0/7 episode_proof=0/7 applied_changes=1 evidence_flips=9 proof_resets=3 gate_names=unsafe-excluded-bands,trusted-contract'])
        e=result['events']['0/1/2/5']
        self.assertEqual('54000',e['duration_ms'])
        self.assertEqual('0/7',e['proof'])
        self.assertEqual(1,result['diagnostic_gate_record_counts']['unsafe-excluded-bands'])

    def test_layout_movement_with_unchanged_applied_is_visible(self):
        result=analyze(['2026-09-16 00:30:20 | Alpha final layout: sequence=1 generation=1 presentation=0,40-3840,2116 picture=0,0-3840,2160 mapping=linear',
                        '2026-09-16 00:30:21 | Alpha final layout: sequence=2 generation=1 presentation=0,40-3840,2116 picture=10,0-3830,2160 mapping=linear'])
        self.assertEqual(1,result['counts']['layout_changes'])

    def test_ordinary_changes_do_not_overwrite_closed_recovery(self):
        base = '2026-09-17 05:40:36 | Alpha crop recovery: generation=2 epoch=0 '
        result = analyze([base + 'event=4 sequence=4112 phase=end duration_ms=111782 proof=7/7',
                          base + 'event=4 sequence=4113 phase=change recovery=0 episode=inactive duration_ms=0',
                          base + 'event=0 sequence=4114 phase=change recovery=0 episode=inactive duration_ms=0'])
        self.assertEqual(1, len(result['events']))
        self.assertEqual('111782', result['events']['0/2/0/4']['duration_ms'])
        self.assertEqual('end', result['events']['0/2/0/4']['last_phase'])

    def test_applied_fill_rectangle_is_reported(self):
        result = analyze([crop('00:30:19', 1, 'applied=1 rect=a fill_applied=1 fill_rect=b'),
                          crop('00:30:20', 2, 'applied=1 rect=a fill_applied=1 fill_rect=c')])
        self.assertEqual(1, result['counts']['source_geometry_changes'])
        self.assertEqual('c', result['source_changes'][0]['fill_rect'])
        self.assertEqual(0, result['counts']['applied_changes'])

if __name__=='__main__': unittest.main()
