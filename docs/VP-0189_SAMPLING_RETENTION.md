# VP-0189: retain scope through harmless compound sampling changes

The Agency logs show established bounds 0,276-3840,1884 withdrawn for
8,276-3832,1888 and 0,272-3840,1888 despite current safe excluded bands.
These are approximately 0.66% and 0.50% aspect differences. The stable-aspect
deadband does not prevent a separate presentation-retention veto.

The provisional sampling predicate previously required identical horizontal
bounds and limited the combined height difference to one scan step. Permit
contained horizontal proposals and apply the existing one-step tolerance to
each vertical edge independently. Retain the existing rectangle; do not adopt
inward side measurements or establish a new aspect. At 2160 lines a step is
four pixels; at 1080 it is two. Current excluded-band checks, generation/base
binding, larger-expansion rejection, and the existing thin-border tolerance
remain in force. No thresholds, timers, or profile settings change.

Regression evidence:
- Both targeted tests failed before the implementation change, after verifying
  the exact provisional coordinates and safe bars from constructed pixels.
- P010 and P210 fixtures exercise extraction, retention, inspection-latch
  resolution and presentation recovery across twenty source sequences.
- Negative fixtures reject larger vertical changes, broad colored content,
  outward side proposals and the existing stale/context mismatch cases.
- First full x64 Release run: 1394/1395 passed. The unrelated configuration-cache
  SameSizeEditWithRestoredWriteTimeInvalidatesCache test failed (expected two,
  read one); an immediate isolated rerun passed without source changes. Preserve
  both results, and record the final clean-build full-suite result at deployment.

This branch also includes the previously tested bounded-recovery and precise
build-label corrections, rebased onto verified beta tip
16a197625a81aae27c971f13c0b5da0a69f5e9f4. Live playback of the original shot is
still needed to validate the visual result; synthetic fixtures reproduce the
measured boundaries, not the full HDMI frame.