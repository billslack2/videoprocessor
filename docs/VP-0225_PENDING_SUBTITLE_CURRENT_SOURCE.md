# Current video during pending subtitle processing

VP-0225 and VP-0222 integration, 2026-11-02.

## Failure being repaired

The deployed combined source 53058498 retains a post-RC6 whole-source subtitle
canvas gate introduced by 01c1e13f. Pending inspection, a positive caption whose
processed output is not ready, or an infeasible fixed-size placement can consume
fresh input while holding the last submitted whole picture. The live incident
contains a 5.796-second Classic wait across sources 201 through 338. An intervening
configuration/native-context reset is recorded; volume causality is not established
by the logs. The user also reports recurring stalls with dense subtitle activity.

A separate Off-mode defect alternates FIT top 190 with raw-safe top 138 while the
physical measurement remains unchanged. A narrower confirmed FIT was suppressing
current raw visibility. That precedence is corrected without changing detection.
The integrated UNKNOWN pan path also now follows an advancing current controller
instead of pinning a prior pose; current raw bounds still clamp it.

## Pending processing is not a video submission veto

Ready processed subtitle canvases keep their existing behavior. A pending canvas
instead requests an explicitly unprocessed, output-only presentation of the current
source. It does not borrow an older complete subtitle frame, invent absence, erase
raw captions, or acquire new detected/native/full-raster authority.

An independently source-bound raw visibility witness carries direct-sample status,
source generation/sequence, viewport epoch, format, analysis domain, native base and
visible bounds. When it matches a valid accepted native base, the raw presentation
contains the union of that base and current visible pixels, aligned outward and
bounded by the input raster. This reuses completed retention evidence; it adds no
new detector or text scan. If the witness/base is unavailable or stale, complete
current input pixels are the conservative presentation. This is not evidence of a
full-raster active picture and must not update accepted native aspect or boundaries.

Ordinary optional aspect fill must not trim those protected pixels. Explicit user
fixed-aspect cropping remains an explicit presentation setting. Caption observation
state remains as observed; raw fallback has no processed/native/FIT ownership proof.

Temporal output must select the exact current raw source, bypass previous subtitle
packet repeats and mixing, and preserve required source conversion/deinterlacing.
Raw processing must not reuse a cleaned/composed cache entry. Geometry/admission
handling must distinguish this output-only state from native picture acquisition.

## Evidence and tests

Native tests cover identity/base faults, current raw coverage, full-input fallback,
no authority mutation, ready/OFF/fixed-setting controls, Off FIT precedence, and
controller progression. Actual renderer checks must join fallback admission to the
submitted original source and verify caption/source pixels, current source progress,
and no stale composition or repeated whole picture under caption/overlay load.
Both shipped dependencies and motion paths remain required. No passing qualification
or live-scene acceptance is claimed by this design note.

Trace flag 32 identifies raw subtitle fallback; flag 64 identifies an exact independent
raw visibility witness. On those rows only, counters 3..7 are witness source sequence,
source generation, viewport epoch, source format key and analysis domain. Metrics
8..11 contain raw bounds and previous[] contains its native base. Candidate presence
retains flag 8 and engage-pose retention flag 16. Ordinary row meanings are unchanged.
