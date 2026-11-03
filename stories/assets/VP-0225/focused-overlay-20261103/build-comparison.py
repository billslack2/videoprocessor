"""Build a five-checkpoint comparison from real renderer captures, never a motion recording."""
import argparse, hashlib, json
from pathlib import Path
import cv2
from PIL import Image, ImageDraw, ImageFont

cv2.setNumThreads(1)
PHASES = ['Settled caption', 'Player UI onset', 'Player UI checkpoint 2', 'Player UI checkpoint 3', 'Caption restored']

def read_run(directory):
    receipt = json.loads((directory / 'receipt.json').read_text(encoding='utf-8-sig'))
    assert receipt['status'] == 'completed', f'Incomplete run: {directory}'
    assert receipt.get('visual') and receipt.get('capturePairsVerified'), f'Unverified visual captures: {directory}'
    assert receipt.get('binariesStable') and receipt.get('sourceStable'), f'Unstable evidence: {directory}'
    inventory = {item['name']: item['sha256'].lower() for item in receipt['captures']}
    pngs = list((directory / 'captures').glob('*.png'))
    assert len(pngs) == 5
    shots = []
    for png in pngs:
        sidecar = png.with_suffix('.json')
        for path in [png, sidecar]:
            assert hashlib.sha256(path.read_bytes()).hexdigest() == inventory[path.name], f'Hash mismatch: {path}'
        metadata = json.loads(sidecar.read_text(encoding='utf-8-sig'))
        assert metadata['schema'] == 'vp-rendered-output-capture-v1'
        assert metadata['authoritative_stage'] == 'after-render-before-present'
        raw = cv2.imread(str(png), cv2.IMREAD_UNCHANGED)
        assert raw is not None and raw.shape[:2] == (metadata['height'], metadata['width'])
        # The renderer encodes 10-bit output codes into a 16-bit PNG. This preview
        # reduces those stored code values to 8-bit; it does not infer HDR display luminance.
        if raw.dtype.name == 'uint16':
            raw = (raw >> 8).astype('uint8')
        raw = cv2.cvtColor(raw, cv2.COLOR_BGRA2RGB if raw.shape[2] == 4 else cv2.COLOR_BGR2RGB)
        shots.append((int(metadata['source_sequence']), Image.fromarray(raw), png, metadata))
    shots.sort(key=lambda item: item[0])
    assert len({shot[0] for shot in shots}) == 5, 'Repeated source in checkpoint captures'
    return receipt, shots

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    before_receipt, before = read_run(args.before)
    after_receipt, after = read_run(args.after)
    args.output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 19)
    small = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 16)
    frames, rows = [], []
    baseline_failed = not before_receipt['passed']
    evidence = 'Before fails / after passes' if baseline_failed and after_receipt['passed'] else 'No demonstrated red-to-green result; inspect test coverage and outputs'
    for index, (old, new) in enumerate(zip(before, after)):
        frame = Image.new('RGB', (1280, 460), '#17191c')
        draw = ImageDraw.Draw(frame)
        draw.text((12, 7), f'{index+1}/5  {PHASES[index]} — five checkpoints, NOT continuous video', font=font, fill='white')
        draw.text((12, 35), f'BEFORE: 43ba61d4 / source {old[0]} / test {"PASS" if before_receipt["passed"] else "FAIL"}', font=small, fill='#f8c995')
        draw.text((652, 35), f'AFTER: {after_receipt["source"][:8]} / source {new[0]} / test {"PASS" if after_receipt["passed"] else "FAIL"}', font=small, fill='#b4dfa5')
        for x, shot in [(0, old), (640, new)]:
            frame.paste(shot[1].resize((640, 360), Image.Resampling.LANCZOS), (x, 64))
        draw.text((12, 432), evidence + ' | Captured output codes; visual run excludes timing qualification.', font=small, fill='#cccccc')
        path = args.output / f'checkpoint-{index+1:02d}.png'
        frame.save(path)
        frames.append(frame)
        rows.append({'phase': PHASES[index], 'before_source': old[0], 'after_source': new[0],
                     'before_png': str(old[2].resolve()), 'after_png': str(new[2].resolve()), 'comparison_png': str(path.resolve()),
                     'before_decision': old[3]['presentation_decision'], 'after_decision': new[3]['presentation_decision']})
    contact = Image.new('RGB', (1280, 460 * 5))
    for index, frame in enumerate(frames): contact.paste(frame, (0, index * 460))
    contact.save(args.output / 'contact-sheet.png')
    frames[0].save(args.output / 'checkpoint-comparison.gif', save_all=True, append_images=frames[1:], duration=1500, loop=0, disposal=2)
    report = {'kind': 'five-output-checkpoints-not-continuous-video', 'before': str(args.before.resolve()), 'after': str(args.after.resolve()),
              'before_test_passed': before_receipt['passed'], 'after_test_passed': after_receipt['passed'],
              'red_to_green': baseline_failed and after_receipt['passed'], 'performance_qualified': False,
              'preview_mapping': 'Stored 16-bit PNG output code values reduced to 8-bit; no HDR luminance inference.', 'checkpoints': rows}
    (args.output / 'comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({key: report[key] for key in ['kind', 'before_test_passed', 'after_test_passed', 'red_to_green']}))
    print(args.output.resolve())

if __name__ == '__main__': main()
