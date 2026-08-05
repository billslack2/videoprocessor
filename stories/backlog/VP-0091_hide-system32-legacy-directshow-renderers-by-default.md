# VP-0091: Hide System32 DirectShow renderers by default

## Status

Backlog (2026-08-05). The DirectShow Filter Mapper discovers several Windows
renderers alongside VP's Alpha renderer and optional third-party madVR. The
current friendly-name-only UI makes duplicated Windows registrations especially
ambiguous.

## User story

As a VideoProcessor user, I want the renderer picker to hide Windows System32
DirectShow renderers by default, so a normal installation presents VP's Alpha
renderer and, when installed, madVR, rather than several legacy Windows
choices with overlapping friendly names.

## Problem statement

The raw DirectShow enumeration currently finds, among others, Windows-hosted
`Video Renderer` registrations, Enhanced Video Renderer, and Video Mixing
Renderer 9. Two distinct CLSIDs both register the friendly name `Video
Renderer`, so the existing UI cannot distinguish them by label alone. These
are hosted by `C:\Windows\System32\quartz.dll` or `evr.dll` on the deployed
x64 system. madVR is separately registered from `C:\madvr210\madVR64.ax` and
must remain available when installed.

DeckLink renderer registrations are already excluded independently and must
remain excluded regardless of this setting.

## Scope

1. Add an application configuration option named `hide_legacy_renderers`.
   Its default is `true`; an absent, invalid, or unreadable value must use that
   safe default. In VP's INI configuration syntax the documented opt-out is:

   ```ini
   hide_legacy_renderers=false
   ```

2. During DirectShow renderer discovery, resolve each candidate CLSID's
   registered in-process server path and normalize it sufficiently for a
   case-insensitive comparison. When `hide_legacy_renderers=true`, exclude a
   candidate only when its registered server is under the current Windows
   `System32` directory. Do not infer this from the friendly name, CLSID age,
   or Microsoft publisher.
3. Keep non-DirectShow renderers unaffected. `VideoProcessor Renderer
   (Alpha)` remains visible whenever its optional VP plugin is available.
   A third-party renderer such as madVR remains visible because its registered
   server is outside System32.
4. Preserve the existing DeckLink exclusion as a separate, unconditional UI
   policy. Setting `hide_legacy_renderers=false` restores System32 candidates
   only; it does not reveal DeckLink renderer registrations.
5. Make configuration and renderer-combo rebuilding deterministic: changing
   the setting and restarting/reloading VP must not leave a stale selected
   renderer or leak renderer-ID allocations.
6. Add concise discovery diagnostics that identify the friendly name, CLSID,
   registered server path, and final inclusion/exclusion reason. The log must
   make duplicate `Video Renderer` registrations distinguishable without
   needing to alter their persisted configuration names.
7. Document the option in the canonical configuration reference and the
   distributed example configuration. Explain that it hides registered
   DirectShow filters by hosting path, not a capability claim; use
   `hide_legacy_renderers=false` to expose them for troubleshooting.

## Expected behavior

| Configuration | Alpha plugin | madVR installed | Renderer picker result |
| --- | --- | --- | --- |
| default (`true`) | yes | yes | Alpha and `DirectShow - madVR` |
| default (`true`) | yes | no | Alpha only |
| `false` | yes | yes | Alpha, madVR, and the System32 DirectShow candidates; DeckLink remains hidden |

The exact set of restored System32 filters can vary with Windows registration,
but discovery must retain its existing Filter Mapper eligibility requirement.

## Non-goals

- Do not uninstall, unregister, modify, or otherwise change any Windows,
  madVR, or DeckLink filter registration.
- Do not claim that System32-hosted filters lack a particular color, HDR, or
  media-format capability; this is a default UI-curation policy.
- Do not automatically select madVR or change the user's saved renderer.
- Do not replace the existing DeckLink filter with a broad path-based rule.
- Do not rename duplicate friendly names in this story. CLSID-aware display
  labels may be considered separately if users need to select restored legacy
  filters by name.

## Validation and acceptance criteria

- With no explicit setting, a build on the deployed x64 machine hides the
  System32-hosted `Video Renderer` registrations, Enhanced Video Renderer,
  and Video Mixing Renderer 9 from the combo.
- With madVR registered at its third-party path, its `DirectShow - madVR`
  entry remains available with the default setting.
- With `hide_legacy_renderers=false`, the same System32 registrations return;
  the two `Video Renderer` entries may still have the same friendly label but
  their distinct CLSIDs appear in diagnostics.
- DeckLink renderer entries remain hidden in both configurations.
- A fresh installation with Alpha available and madVR absent shows Alpha as
  the only renderer choice by default.
- Focused configuration/discovery tests cover default-true, explicit-false,
  malformed-value fallback, System32 matching, a third-party path, and the
  independent DeckLink exclusion.
- Build x64 Release and verify the report in the VP debug log records each
  candidate's final decision and reason.

## Related stories

- VP-0021: Renderer format negotiation parity and truthful capability
  reporting.
- VP-0050: Alpha-first renderer ordering.
- VP-0086: Comprehensive configuration usage reference.
