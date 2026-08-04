# VP-0086: Comprehensive configuration usage reference

## Status

In Progress — 2026-08-04.

Readiness review completed against `billslack2/videoprocessor` default branch
`v1.1.015-beta` at `0a19c5f`. The public-configuration model is recoverable
from the active startup schemas, renderer settings readers, validation code,
checked-in sample configuration, and the existing
`VideoProcessor-Test::ConfigurationReferenceMatchesPublicFieldInventory`
coverage test. The current HTML and a checked-in public-field inventory already
provide stable anchors for a complete documentation pass; no pipeline, API,
resource-lifetime, or platform unknown blocks this documentation-only change.

Implementation branch/worktree:

- Branch: `codex/vp-0086-comprehensive-configuration-reference`
- Worktree: `C:\Users\bslac\vp\vp-0086-comprehensive-configuration-reference`
- Base: `origin/v1.1.015-beta` (`0a19c5f`)

Initial progress: reconciling the public inventory with the parser and the
Alpha/libplacebo settings reader, then expanding the canonical HTML to one
value-level entry per public setting. Validation will include the inventory
coverage test, example parsing, link/anchor checks, and rendered desktop-width
review.

## User story

As a VideoProcessor user configuring capture and the Alpha/libplacebo
renderer, I want the HTML configuration reference to explain every supported
public setting and every accepted value in practical terms, so I know what a
choice changes, what `AUTO` actually does, and what trade-offs I am making
without reading source or guessing from a sample file.

## Problem

The existing `CONFIGURATION.html` has broad tables that list groups of field
names and enum tokens. It does not consistently explain the values themselves,
their pipeline effect, their valid scope, or the difference between omission,
`AUTO`, and an explicit value. This is most visible for the Alpha renderer's
libplacebo-backed options:

- tone mapping and gamut mapping algorithms;
- peak detection and contrast recovery;
- SDR target/black level, output transfer/range/primaries/presentation;
- upscalers, downscalers, quality levels, debanding, and dithering; and
- renderer/session/profile overrides.

Users cannot safely choose between values such as `bt2390`, `spline`,
`reinhard`, scaler names, or output modes from a token list alone. A complete
public reference must state what each option does, when it is useful, and its
likely image-quality/performance consequences.

## Public documentation boundary

Document only the current canonical, supported public configuration surface.
Do not mention backward compatibility, hidden keys, old section/key names,
deprecated aliases, parser fallbacks, or abandoned experiments. If the current
parser accepts a value that the product does not intend to support, either
exclude it from the public allowlist or create a separate decision before
documenting it; do not accidentally promote it by mentioning it in the HTML.

## Required documentation design

### 1. Establish an auditable public schema

Before editing prose, build a field/value inventory from the active parser,
renderer settings structures, validation code, sample configuration, and
current HTML. For every public key, record:

- canonical section/key and applicability (shared VP, renderer-only, profile,
  shader rule, shortcut/event action, or diagnostic);
- type, units, accepted syntax/range, and omitted default;
- every accepted public enum/list token;
- whether `AUTO` is accepted and its exact resolved behavior;
- precedence and profile override behavior;
- live/rebuild/restart behavior; and
- invalid-value behavior and useful log/OSD diagnostics.

The inventory is an implementation artifact used to prove coverage. It need
not be shipped if it would expose internal fields, but the final HTML must be
checked against it so no supported public field or value silently lacks an
explanation.

### 2. One detailed entry per setting

Replace summary-only tables with an entry per public key. Each entry must
include:

- purpose and pipeline stage;
- accepted syntax and default/omitted behavior;
- a value-by-value table, not merely a comma-separated token list;
- exact `AUTO` behavior, including when it differs from omission;
- effects on image quality, latency, GPU/CPU cost, or renderer rebuilds where
  applicable;
- important interactions and constraints;
- a conservative example; and
- troubleshooting/fallback behavior when relevant.

Group entries for readability, but no group may substitute for a field's own
definition.

### 3. Alpha/libplacebo renderer deep reference

For every public libplacebo-backed control, document VP's accepted values and
their meaning in VP's pipeline rather than pasting library API terminology.
At minimum cover the following if they are public in the shipped parser:

- `tone_mapping`: each algorithm's intent, highlight handling, expected
  contrast/color trade-off, recommended starting point, and when `AUTO` is
  selected;
- `gamut_mapping`: perceptual/relative/soft-clip/desaturate-style choices,
  out-of-gamut behavior, and interaction with target primaries/LUTs;
- `peak_detect` and related tuning: dynamic-analysis consequences, performance,
  stability, and when it is bypassed;
- `contrast_recovery` and related controls: what artifacts it can improve or
  create and safe ranges/defaults;
- upscaler/downscaler/quality: algorithm characteristics, scaling-ratio
  relevance, sharpness/ringing/aliasing trade-offs, and approximate GPU-cost
  guidance;
- debanding and dithering: where they run, their visual purpose, and cost or
  noise trade-offs;
- SDR target nits/black nits, transfer, range, primaries, and presentation:
  values, calibration/display implications, and interactions with LUTs; and
- profile selection/persistence, screen/viewport behavior, refresh switching,
  output diagnostics, and renderer-specific shortcut/event behavior.

If a libplacebo value is intentionally passed through unchanged, state that
precisely, name the supported value set for the bundled version, and explain
VP's validation/fallback behavior. Do not imply that arbitrary upstream
libplacebo tokens are accepted.

### 4. Practical guides and examples

Add short task-oriented starting points that link to—not replace—the full
reference:

- conservative SDR input to SDR output;
- HDR input tone mapped to calibrated SDR/projector output;
- SDR BT.2020 target/profile selection;
- scaling a lower-resolution source versus native-resolution content;
- shader/NLS selection and the difference from renderer scaling;
- a profile override example showing base, viewport, display, and queue
  settings; and
- a troubleshooting table for invalid values, unintended `AUTO` resolution,
  no visible scaler change, wrong output colorspace/range, and settings that
  require renderer reconstruction.

Examples must be valid against the public parser and conservative by default.
They must not overwrite or prescribe a user's calibration, projector settings,
or LUT choice.

## Scope

1. Revise the canonical shipped `CONFIGURATION.html` (and a separate renderer
   HTML page only if the current product explicitly ships one) to meet this
   story. Keep cross-links and headings stable where practical.
2. Align the source sample configuration comments with the HTML terminology,
   but keep the sample concise; it is a starting configuration, not the full
   reference.
3. Add a repeatable documentation-coverage check. It may be a test, generated
   allowlist comparison, or review script, but it must catch a public setting
   or enum token being added without a corresponding reference entry.
4. Validate HTML links, anchors, code examples, table rendering, and spelling.
   Review the rendered file at desktop/browser width; do not accept a
   source-only markup check.

## Non-goals

- Do not change rendering behavior, defaults, parser compatibility, or add
  new libplacebo controls merely to make the documentation longer.
- Do not document private tuning knobs, old syntax, or unsupported external
  library options.
- Do not replace detailed value guidance with links to generic upstream docs;
  VP must explain its own accepted values and behavior.

## Acceptance criteria

- Every supported public configuration field is discoverable from the HTML and
  has a complete individual reference entry.
- Every public enum/list token has a plain-language, value-specific meaning;
  libplacebo-backed tone mapping and scaler choices include visible trade-offs
  and performance guidance.
- Every `AUTO` setting documents exact resolution/omission behavior and how a
  user can verify the result in VP logs/OSD when applicable.
- The document clearly distinguishes shared settings, renderer-only settings,
  profiles, shaders, and diagnostics without mentioning legacy/hidden syntax.
- All examples parse as valid canonical configuration and rendered HTML passes
  link/anchor/visual review.
- A coverage check prevents future public keys or tokens from being silently
  undocumented.

## Related stories

- VP-0045: Namespace built-in renderer configuration as `vpvr`.
- VP-0049: Original canonical configuration reference; this story remedies its
  incomplete value-level guidance.
- VP-0019: SDR BT.2020 display profiles and OSD reporting.
- VP-0083: Alpha anamorphic presentation profiles; document it here once its
  public configuration contract is accepted.

