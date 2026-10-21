# VP-0200: physical output-mode transitions

## 2026-10-21 tester evidence

The supplied `vp-log-2026-10-21.zip.txt` is a ZIP containing one `vp.log`.
The separately supplied configuration has one Advanced `Under_30` Display
profile, monitor `auto`, `restore_previous_mode: true`, and shared refresh
switching `fullscreen_only`. Its allowed order is:

1. 1920x1080p50
2. 1920x1080p59.94
3. 1920x1080p60
4. 3840x2160p30
5. 3840x2160p29.97
6. 3840x2160p25
7. 3840x2160p24
8. 3840x2160p23.976

The configuration is valid. At 00:21:09 and 00:23:01, a 59.94 source ranked
1080p59.94 and 1080p60 before configured-order fallbacks. Each 1080p request
left the reported target signal at 3840x2160 with the requested refresh.
Verification rejected it and rollback succeeded. The old selector then chose
2160p30 before the later 2160p29.97 entry. 23.976 selections and restorations
were verified in the same log.

The tester reports that GDI full-mode requests previously produced real 1080p
signal timing on their RTX 3060 Ti / 511.17 driver. The supplied log confirms
the CCD failures; it does not independently establish GDI success or the
reported 200% DPI sizing fault. Raw tester files are not committed.

## Mode transaction

- Auto retains the existing refresh-only CCD path and switching permission.
- Advanced resolution changes use one `ChangeDisplaySettingsExW` call with
  `CDS_FULLSCREEN`, selecting a reported progressive 32-bit GDI mode with a
  tightly equivalent rate. Broadcast 59.94 selects the enumerated 59 selector.
  Desktop position and orientation are preserved; the registry is not updated.
- Rate-only transitions retain CCD. Custom DXGI timings without an equivalent
  GDI selector retain the existing CCD route. The log identifies the API route.
- Read-back requires real CCD source and target modes. The target active raster
  and signal vSync frequency are never inferred from desktop dimensions or the
  requested path rate. New Advanced modes must also be progressive.
- Acceptance requires two consecutive matching source raster, target raster and
  rate observations. A rejected API call is checked for side effects before
  continuing. Any unverified change is rolled back and independently verified.
  A failed rollback stops further attempts and retains restore ownership.
- Restoration uses the same route selection. Original target timing and scaling
  are captured for CCD restoration, including originally GPU-scaled desktops.
  A failure of one raster/rate combination does not exclude other rates at that
  raster.

## Cadence ordering

The existing native/close two-pass ranking runs first. Remaining exact integer
cadence multiples and divisors precede arbitrary fallbacks: multiples first,
then smaller factors, with configured order breaking ties. Remaining available
modes retain configured order. For the supplied 59.94 profile the sequence is
1080p59.94, 1080p60, 2160p29.97, then 1080p50, 2160p30, 2160p25,
2160p24 and 2160p23.976. This prefers exact half-rate over an unrelated fallback;
it does not make half-rate output equivalent to displaying every source frame.
One available allowed mode still acts as fixed output for any valid cadence.
Preview and playback share the ranking helper.

## Fullscreen geometry

VP Renderer fullscreen hosts are created with scoped per-monitor DPI awareness.
Physical monitor bounds are reapplied after display notifications and DPI
changes. Swapchain initialization re-reads the client size after mode selection;
subsequent notifications coalesce resizing onto the renderer thread. Logs show
monitor/client and client/swapchain sizes after transitions. The legacy main UI
and DirectShow host creation keep their prior DPI context. Changing renderer
backend recreates the fullscreen host only after the previous renderer retires;
ordinary source/profile restarts retain it.

## Outstanding bench qualification

A Release build establishes compilation, not physical HDMI behavior. On the
reported rig, cycle 4K23.976 -> 1080p59.94 -> 4K23.976 repeatedly at the existing
200% desktop scaling. Check the physical receiver signal, desktop source,
fullscreen client, swapchain and native OSD dimensions on every drop, then stop
and confirm the original complete mode is restored. Also qualify rejected-mode
rollback, custom timings without a GDI selector, external display changes,
non-primary monitor placement and switching to/from DirectShow. Keep the
existing configuration unchanged for the reproduction.

Windows API references: [ChangeDisplaySettingsExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw),
[SetDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setdisplayconfig),
and [mixed DPI window contexts](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-improvements-for-desktop-applications).
