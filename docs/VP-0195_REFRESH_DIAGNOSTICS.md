# VP-0195: Display mode and refresh diagnostics

The reported projector uses a 1920x1080 Windows output. Its 1920x816 setting
describes the cropped screen area, not a custom Windows mode.

## Collecting evidence

Use a build containing VP-0195, keep the user's existing configuration, and
reproduce one refresh change with VP Renderer selected. Collect the current
VP debug log and the matching configuration. Include the input cadence and
whether the problem affects refresh, framing, or both. Capture 23.976 and
24.000 separately if available. Preserve the preceding rotated log if the
application restarted after the failure.

Search for `refresh-diagnostic id=`. One ID connects a switch and its restore.
Inventory is emitted even when the requested refresh is already active.
The logs run during renderer initialization and refresh/restore transitions,
not per frame. Verification logs only the first observation, changed
observations, and the bounded verification result.

| Record | Evidence |
| --- | --- |
| `switch begin` | Target device/window, client size, captured dimensions and rational rate, input interlace flag, refresh policy, screen aspect |
| `phase=before-switch` | Adapter/source/target IDs, desktop size, signal active and total pixels, rational path/vsync rate, scan, scaling, rotation |
| `matched_output` | DXGI output identity and GPU description/vendor/device |
| `mode_list` | Format, enumeration flags, HRESULT, count, attempts and purpose |
| `mode=` | Dimensions, exact rational and decimal rate, format, scan, scaling, stereo, inclusion/exclusion reason |
| `ranking` | Eligible rate's rank or why cadence policy excludes it |
| `apply begin` / `apply returned` | Attempt, exact requested rate, API flags, Windows result, elapsed time |
| `verify observation` / `verify complete` | Query validity, requested/observed fractions, polls, elapsed time and match/timeout |
| `phase=after-apply` / `switch-verified` / `switch-unverified` | Actual desktop and signal state after the request and after verification |
| `rollback` / `restore` | Requested original rate, API results, verification and resulting desktop/signal state |

Numbers are Windows API enum values. DXGI formats 28 and 24 represent the
8-bit RGBA and 10-bit RGB formats queried. DXGI scan values are 0 unspecified,
1 progressive, 2 upper-field-first, 3 lower-field-first; scaling is
0 unspecified, 1 centered, 2 stretched. DISPLAYCONFIG scan/scaling values
belong to a different enum: use Microsoft's definitions for those fields.
`integer_rate` is a GDI label, not proof of an exact fractional refresh.

The default DXGI list (flags 0) supplies candidates. The expanded list
(scaling and interlaced flags, value 3) is diagnostic only: its presence
does not admit a mode to selection or prove the projector can accept it.
The expanded list also repeats modes in the default list, so seeing a mode
there alone is not proof that it was excluded; compare both lists.

## Changes and validation scope

- Preserve current refresh ranking and mode-switch behavior.
- Retry DXGI mode enumeration up to three times when its count changes;
  publish only the number actually returned. Clear partial results on
  failure/disappearance and reject unreasonable or inconsistent counts.
- Terminate adapter/output enumeration on unexpected API failures rather
  than continuing indefinitely. Log the actual HRESULT.
- Existing refresh policy tests plus regression cases cover mode-list
  growth/shrinkage, disappearing outputs, retry exhaustion and invalid counts.
- A Release build and these tests cannot establish behavior on the reporting
  user's GPU/projector. The actual projector log remains necessary.

## 1080p review findings

1. **No fixed 4K requirement in refresh selection.** It uses the active desktop
   dimensions. A 1920x1080 desktop is eligible on the same basis as other sizes.
   The screen aspect setting affects layout, not the Windows display mode.
2. **Potentially missing modes.** The default DXGI query omits modes requiring
   scaling and interlaced modes. The expanded inventory exposes this distinction
   without enabling 1080i as a progressive fallback. Input 1080i is a different
   issue: its frame rate is doubled to request progressive field-rate output.
3. **Resolution/scan verification gap.** `ApplyDisplayRefreshRate` requests that
   the source mode remain, but uses `SDC_ALLOW_CHANGES`, which permits Windows
   to adjust source/target mode information. Selection discards mode scan and
   scaling metadata after extracting rates. Verification checks rate only, so
   a matching refresh is not proof that resolution or progressive scan was
   preserved. Snapshots now make that visible; stricter switching/rollback is
   a separate behavioral change requiring display testing.
4. **Target-only topology can choose a lower resolution.**
   `DisplayTopologySession::ApplyMaximumMode` ranks refresh before pixel area
   and does not exclude interlaced modes. If target-only mode is used, a
   driver-advertised 720p120 could beat 1080p60. The existing `Display maximum
   mode applied` line and the new `before-switch` snapshot identify that path.
5. **Cadence fallbacks are limited.** For progressive 25/29.97/30, native modes
   rank before doubled modes. Progressive 23.976/24 does not try 47.952/48/72
   or 60 as a fallback. A nearby rate such as 24 for 23.976 can be selected
   within the existing 0.5% tolerance. This is a policy limitation, not evidence
   that such a mode exists or is needed on this projector.
6. **Failed rollback ownership gap.** If Windows accepts a candidate, subsequent
   verification fails and rollback also fails, `m_changed` may still be false.
   The scoped destructor then has no pending refresh restore. The new logs
   identify this sequence; this pre-existing recovery gap is not repaired by
   diagnostic instrumentation.

None of the potential hardware failures above was reproduced on the reporting
projector. Keep the mode-selection and recovery findings distinct from the
enumeration robustness fixes delivered with the diagnostics.

## Windows references

- [GetDisplayModeList1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-getdisplaymodelist1): scaling flags and mode-count changes.
- [SetDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setdisplayconfig): `SDC_ALLOW_CHANGES` may adjust supplied modes.
