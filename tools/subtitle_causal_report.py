"""Offline diagnostic correlation; never a runtime or qualification verdict."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

IDENTITY_FIELDS = ("transportGeneration", "acceptedSequence", "sourceFrameNumber",
    "captureTimestamp", "sourceFormatGeneration", "viewportGeneration", "rendererGeneration",
    "analysisDomainKey", "policyGeneration", "continuityGeneration")
CERTIFICATE_BITS = ("HeldPose", "CurrentTemplate", "StrongPose", "CurrentScan",
    "IndependentBars", "UntrackedBars", "UnrevalidatedBars", "UnmaskedBars", "NonlocalScan",
    "CurrentRawCue", "CompleteSearch", "CompleteInk", "SharedPicture", "MeasuredEdges",
    "CurrentContext", "LineCoverage", "SameCurrentInk", "NoNovelOwnedInk")
PIXEL_STAGES = ("None", "GlyphOwnerOrFloor", "GlyphComponentBudget", "GlyphSurvivingCore",
    "GlyphStrongCore", "CleanupBlackTransition", "CleanupUnownedChange",
    "CleanupProjectionConflict", "DestinationNovelty")


def identity(record):
    return tuple(record[k] for k in IDENTITY_FIELDS) if all(isinstance(record.get(k), int) for k in IDENTITY_FIELDS) else None


def read_jsonl(path):
    with path.open(encoding="utf-8-sig") as stream:
        return [json.loads(line) for line in stream if line.strip()]


def certificate_status(evaluated, failed):
    return {name: ("unknown" if not evaluated & (1 << bit) else
                   "failed" if failed & (1 << bit) else "passed")
            for bit, name in enumerate(CERTIFICATE_BITS)}


def load_native(root):
    native = collections.defaultdict(list)
    index = root / "frames.jsonl"
    if not index.is_file():
        return native
    for frame in read_jsonl(index):
        raw = (root / frame["file"]).resolve()
        if raw.parent != root.resolve():
            raise ValueError("Native frame reference escapes capture directory")
        sidecar = raw.with_suffix(".txt")
        if not sidecar.is_file():
            continue
        lines = [line.split() for line in sidecar.read_text(encoding="utf-8-sig").splitlines()]
        ids = next((line[1:] for line in lines if line and line[0] == "identity_v1"), [])
        if len(ids) < len(IDENTITY_FIELDS):
            continue
        key = tuple(map(int, ids[:len(IDENTITY_FIELDS)]))
        # Index and sidecar must agree; the index alone lacks full identity.
        if any(frame.get(k, key[IDENTITY_FIELDS.index(k)]) != key[IDENTITY_FIELDS.index(k)]
               for k in ("acceptedSequence", "sourceFrameNumber", "analysisDomainKey")):
            raise ValueError("Frame index and identity_v1 sidecar disagree")
        native[key].append({"file": str(raw), "sidecar": str(sidecar),
                            "exists": raw.is_file(), "declaredBytes": frame.get("bytes"),
                            "actualBytes": raw.stat().st_size if raw.is_file() else None})
    return native


def native_coverage(record, native):
    key = identity(record)
    if key is None:
        return {"status": "identity-incomplete"}
    matches = native.get(key, [])
    if len(matches) != 1:
        return {"status": "missing" if not matches else "ambiguous"}
    item = matches[0]
    status = "exact" if item["exists"] else "file-missing"
    if item["exists"] and "declaredBytes" in item and item.get("actualBytes") != item["declaredBytes"]:
        status = "size-mismatch"
    return {"status": status, **item}


def report(rows, native, companion_rows=None):
    stages = collections.Counter(r.get("stage", "") for r in rows)
    seeds = collections.defaultdict(list)
    current_keys = collections.defaultdict(set)
    def token(r, current):
        return (r.get("rendererInstanceKey"), r["counters"][0], current)
    for r in (companion_rows if companion_rows is not None else rows):
        if r.get("stage") == "worker-cue-pixel-seed":
            seeds[token(r, r["counters"][1])].append(r)
    for r in rows:
        if r.get("stage") == "worker-cue-pixel-witness":
            current_keys[token(r, r["acceptedSequence"])].add(identity(r))
    failures, certificates = [], []
    for r in rows:
        stage = r.get("stage")
        if stage == "worker-cue-pixel-witness":
            c, m = r["counters"], r["metrics"]
            candidates = seeds[token(r, r["acceptedSequence"])]
            # A revision/explicit current-source link locates the companion. Full
            # identities then establish native coverage; accepted sequence alone never does.
            candidates = {identity(s): s for s in candidates if identity(s) is not None and
                          s["acceptedSequence"] == c[1]}
            baseline = next(iter(candidates.values())) if len(candidates) == 1 and len(
                current_keys[token(r, r["acceptedSequence"])]) == 1 else None
            current_native = native_coverage(r, native)
            seed_native = native_coverage(baseline, native) if baseline else {"status": "unknown"}
            subtype = r.get("resultCode", 0)
            failures.append({"currentIdentity": dict(zip(IDENTITY_FIELDS, identity(r))) if identity(r) else None,
                "baselineIdentity": {k: baseline[k] for k in IDENTITY_FIELDS} if baseline else None,
                "revision": c[0], "reason": r.get("reason"),
                "subtype": PIXEL_STAGES[subtype] if 0 <= subtype < len(PIXEL_STAGES) else "unknown",
                "point": m[:2], "neighbor": m[2:4], "seedYUV": m[4:7], "currentYUV": m[7:10],
                "seedNeighborYUV": m[10:13], "currentNeighborYUV": m[13:16],
                "thresholds": m[16:19], "classes": c[2:6], "zones": r.get("flags", 0),
                "neighborZones": m[19], "currentNative": current_native, "baselineNative": seed_native,
                "nativePairIdentityAndSizeMatched": current_native["status"] == seed_native["status"] == "exact",
                "nativePixelHashesVerified": False})
        elif stage == "subtitle-held-certificate":
            c = r["counters"]
            certificates.append({"identity": {k: r.get(k) for k in IDENTITY_FIELDS}, "cue": c[2],
                "evaluatedMask": c[0], "failedMask": c[1], "checks": certificate_status(c[0], c[1]),
                "invalidMask": bool(c[1] & ~c[0]),
                "unknownEvaluatedMask": c[0] & ~((1 << len(CERTIFICATE_BITS)) - 1),
                "unknownFailedMask": c[1] & ~((1 << len(CERTIFICATE_BITS)) - 1),
                "currentNative": native_coverage(r, native)})
    return {"qualificationClaim": False, "nativePixelHashesVerified": False,
        "nativeCoverageScope": "Exact identity and declared byte size only; native pixel hashes were not read or verified.",
        "stageCounts": dict(stages),
        "stageReasons": dict(collections.Counter(r.get("stage", "") + ": " + r.get("reason", "") for r in rows)),
        "telemetryAvailability": {name: "present" if stages[name] else "absent; not inferred"
            for name in ("worker-cue-pixel-witness", "worker-cue-pixel-seed", "subtitle-held-certificate")},
        "pixelFailures": failures, "heldCertificates": certificates,
        "nativeSampling": "Sparse exact identities only; missing intervening sources are not reconstructed."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--first-accepted", type=int)
    parser.add_argument("--last-accepted", type=int)
    args = parser.parse_args(argv)
    if args.output.exists():
        parser.error("Output already exists; choose a fresh directory")
    root = args.capture_root.resolve()
    all_rows = read_jsonl(root / "analysis.jsonl")
    rows = [r for r in all_rows
            if (args.first_accepted is None or r.get("acceptedSequence", -1) >= args.first_accepted)
            and (args.last_accepted is None or r.get("acceptedSequence", -1) <= args.last_accepted)]
    native = load_native(root)
    result = report(rows, native, all_rows)
    result["selection"] = {"firstAccepted": args.first_accepted, "lastAccepted": args.last_accepted,
        "seedCompanions": "Resolved from the full capture, even outside the selected current-source window."}
    summary = root / "capture-summary.json"
    result["captureSummary"] = json.loads(summary.read_text(encoding="utf-8-sig")) if summary.is_file() else None
    result["counterMeaning"] = "Capture summary session counters are local; lifetime counters are cumulative and may predate this capture."
    manifest = {"captureRoot": str(root), "nativePixelsCopied": False, "metadataReferences": []}
    sidecars = sorted({Path(item["sidecar"]) for matches in native.values() for item in matches})
    for path in [root / "analysis.jsonl", root / "frames.jsonl", summary, root / "preservation.json", *sidecars]:
        if path.is_file():
            manifest["metadataReferences"].append({"path": str(path), "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    args.output.mkdir(parents=True)
    (args.output / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    with (args.output / "selected-events.jsonl").open("w", encoding="utf-8") as stream:
        for row in rows:
            if row.get("stage", "").startswith(("worker-cue", "subtitle-", "submitted-", "presentation")):
                stream.write(json.dumps(row) + "\n")
    print(json.dumps({"output": str(args.output.resolve()), "records": len(rows), "qualificationClaim": False}))


if __name__ == "__main__":
    main()
