"""Verify a completed opt-in RAM native window without renumbering source gaps."""
import argparse, collections, hashlib, json
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def inspect(directory):
    directory = directory.resolve(strict=True)
    rows = [json.loads(line) for line in (directory / 'native-window.jsonl').read_text().splitlines() if line]
    limits, events = rows[0], rows[1:]
    if limits.get('schema') != 1 or limits.get('kind') != 'limits':
        raise ValueError('Unknown native window schema')
    if [e['index'] for e in events] != list(range(1, len(events) + 1)):
        raise ValueError('Missing or duplicated event ledger indexes')
    counts = collections.Counter(e['outcome'] for e in events)
    files, errors, gaps = [], [], []
    for event in events:
        if event['outcome'] != 'written':
            continue
        index = event['index']
        binary = directory / f'frame-{index:06}.bin'
        sidecar = directory / f'frame-{index:06}.txt'
        try:
            if binary.stat().st_size != event['bytes']:
                raise ValueError('Native byte size mismatch')
            lines = sidecar.read_text().splitlines()
            layout = [int(v) for v in lines[0].split()]
            if layout[:7] != [event[n] for n in ['width', 'height', 'stride0', 'stride1', 'format', 'encoding', 'colorspace']] or layout[10] != event['sampleRange']:
                raise ValueError('Native layout/range differs from event ledger')
            identity = [int(v) for v in lines[1].split()[1:]]
            names = ['transportGeneration', 'acceptedSequence', 'sourceFrameNumber', 'captureTimestamp',
                     'sourceFormatGeneration', 'viewportGeneration', 'rendererGeneration', 'analysisDomainKey',
                     'policyGeneration', 'continuityGeneration']
            if lines[1].split()[0] != 'identity_v1' or identity[:10] != [event[n] for n in names]:
                raise ValueError('Sidecar identity differs from event ledger')
            files.append({'index': index, 'sourceFrameNumber': event['sourceFrameNumber'],
                          'acceptedSequence': event['acceptedSequence'], 'bytes': binary.stat().st_size,
                          'nativeSHA256': digest(binary), 'sidecarSHA256': digest(sidecar)})
        except (OSError, ValueError, IndexError) as error:
            errors.append({'index': index, 'error': str(error)})
    for before, after in zip(files, files[1:]):
        if after['acceptedSequence'] != before['acceptedSequence'] + 1 or (
                before['sourceFrameNumber'] and after['sourceFrameNumber'] != before['sourceFrameNumber'] + 1):
            gaps.append({'before': before['index'], 'after': after['index'],
                         'acceptedDelta': after['acceptedSequence'] - before['acceptedSequence'],
                         'sourceDelta': after['sourceFrameNumber'] - before['sourceFrameNumber']})
    return {'schema': 1, 'directory': str(directory), 'limits': limits, 'outcomes': dict(counts),
            'files': files, 'errors': errors, 'retainedGaps': gaps,
            'verifiedWrittenFiles': not errors and len(files) == counts['written'],
            'retainedWindowContiguous': bool(files) and not gaps and not errors and not limits['unloggedOffers'],
            'completeStreamClaim': False,
            'note': 'RAM evictions are intentional. Other outcomes are omissions. Original source identities remain authoritative; no frames are filled or renumbered.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = inspect(args.directory)
    text = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(text + '\n')
    else:
        print(text)
    raise SystemExit(0 if result['verifiedWrittenFiles'] else 1)
