# Historical PR #75 timing diagnostics

The visible comparison experiment has been retired. See
../anevard-docs/RENDER_LOAD_OSD_STATS.md for the integrated measurement contract
and ../anevard-docs/RENDER_LOAD_INTERPRETATION.md for the OSD fields.

Core-stage and original PR pass-sum measurements remain available in the Alpha
presentation telemetry log. The latter is an uncorrelated estimate and is not
used as authoritative GPU frame cost. Staged and legacy timing alternate; the
core result is extracted from the same staged query, not a third sampling mode.
