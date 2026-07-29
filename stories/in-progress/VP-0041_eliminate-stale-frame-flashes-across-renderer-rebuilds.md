# VP-0041: Eliminate stale-frame flashes across renderer rebuilds

## Status

In Progress. Pull request #18 merged into the default integration branch
`v1.1.014-beta` on July 28, 2026 as merge commit `ab81c309`. Release x64 built
successfully and all 181 tests passed. A basic launch from the feature build
location also succeeded.

The implementation places a GUI-owned opaque black child window above the
active render target before renderer stop/rebuild and keeps it visible through
old-surface retirement. The cover is removed only after the current renderer
generation reports live-frame evidence: successful swap-chain submission for
Alpha or successful downstream acceptance for DirectShow/madVR. Fullscreen
hosts also paint a deterministic black background, and lifecycle logs record
black-show, old-surface retirement, and first-live-frame reveal.

Real-world validation found that the stale-frame issue still exists, although
less frequently, so this story was reopened on July 28, 2026. Implementation
resumed on branch `codex/vp-0041-reopen` from the current default branch
`v1.1.014-beta`, using the isolated worktree
`C:\Users\bslac\vp\worktrees\vp-0041-reopen`.

The deployed rotated logs establish that the original black cover is a child
of the render target: in fullscreen sessions the logged cover HWND has the
fullscreen target as its parent. DirectShow then creates its own child video
window during graph construction. That later renderer child can temporarily
rise above the earlier cover. The original cover is also destroyed when its
fullscreen target is replaced, and its GDI paint is followed immediately by
renderer teardown without a compositor-present synchronization boundary.
The logs do not encode the exact instant at which the visible stale frame was
observed, but they confirm both remaining lifecycle races.

Commit `4e3af03` changes the cover to a stable dialog-owned, non-activating
popup positioned over the render target. It survives fullscreen target
replacement and remains above renderer-owned child windows. The stop and
first-current-frame reveal boundaries now use `DwmFlush` only at lifecycle
transitions, after black is painted and before it is removed, so teardown and
reveal cannot overtake the compositor. Transition logs include the cover
owner, synchronization result, and synchronization duration.

The fix completed a clean x64 Release build with the repository's v142/Visual
Studio 2019 toolchain, and all 196 tests passed. It remains in progress pending
real-world HDMI-resync and repeated Alpha/madVR transition validation.

Acceptance still requires repeated Alpha-only, madVR-only, Alpha/madVR
handoff, refresh-rate/mode-change, and actual HDMI-resync validation.
Persistent shader caching remains enabled.

Inspect `C:\Videoprocessor\vp\logs\vp_debug.log` and its numbered rotated logs
for reproduction evidence. Treat the shader-cache timing as a useful regression
clue, not as proof that cached shader objects contain video pixels.

## User story

As a VP user switching refresh rates, display modes, or renderers, I want the
transition to show black or the current live image, never an arbitrary frame
from an earlier renderer generation.

## Reported behavior

After Alpha's shader cache was enabled:

- Alpha flashes an apparently random old video frame during refresh-rate or
  mode changes that cause HDMI resync or renderer rebuild.
- After Alpha has been used, the stale frame can also flash during a madVR
  mode change.
- The stale image remains associated with the running VP process and can
  reappear across renderer rebuilds.
- Restarting VP clears the behavior.
- The old frame is visible for approximately one or two seconds during the
  transition.

This is not merely a delayed current frame: the image appears to come from
older content and survives ordinary renderer replacement.

## Initial assessment

Libplacebo's persistent `pl_cache` is intended to hold compiled GPU/shader
objects, not decoded or rendered video images. A disk shader cache should
therefore be incapable of replaying a source frame by itself.

The fact that the image can later appear with madVR and disappears only when
the VP process exits points more strongly to process-lifetime presentation
state, including:

- the shared render HWND retaining its last paint or presentation surface;
- a DXGI flip, DirectFlip, independent-flip, or MPO plane remaining associated
  with that HWND during display resync;
- asynchronous swap-chain/device teardown leaving the previous scanout visible;
- a fullscreen/windowed output surface being hidden, detached, or destroyed
  too late;
- the host control repainting without an explicit black transition;
- a stale queued or retained frame crossing a renderer-generation boundary; or
- Windows/display hardware holding the last scanout until the replacement
  renderer submits its first frame.

The present stop path clears renderer queues and destroys the renderer before
the GUI restores its logo, but queue clearing cannot clear a compositor,
swap-chain, overlay-plane, or physical scanout image. The existing Alpha
windowed-composition work in VP-0037 is relevant, but this report also covers
fullscreen/mode-transition and cross-renderer ownership.

## Required investigation

1. Reproduce with visually identifiable frame markers so the flashed frame's
   source sequence and renderer generation can be established.
2. Test these process-lifetime sequences separately:
   - fresh process using only madVR;
   - fresh process using only Alpha;
   - Alpha followed by madVR;
   - madVR followed by Alpha;
   - repeated Alpha/madVR switching.
3. Repeat Alpha tests with persistent shader caching enabled and with
   `diagnostic_disable_shader_cache` enabled. Also distinguish loading an
   existing cache from creating/saving one for the first time.
4. Exercise explicit renderer restart, refresh-rate switch, resolution switch,
   EOTF/output-contract rebuild, fullscreen entry/exit, windowed preview, and
   actual HDMI resync.
5. Determine whether the stale image is:
   - submitted by VP after a new generation begins;
   - presented by the old renderer after stop begins;
   - retained in the old swap chain or Windows composition plane;
   - ordinary Win32 control paint state; or
   - held by the display until a new valid scanout arrives.
6. Verify renderer stop completion, render-thread join, queue/source-reference
   release, swap-chain destruction, D3D device destruction, fullscreen-window
   destruction, and host-window repaint ordering.
7. Confirm no Alpha cadence-repeat frame, shader input texture, display LUT
   texture, subtitle-analysis state, NLS state, native OSD texture, or other
   retained GPU resource crosses a renderer generation.
8. Establish whether reusing the same HWND allows a retired DXGI/MPO surface to
   return during the next mode transition. If so, test explicit surface
   detachment or HWND recreation rather than relying on repaint alone.

## Required diagnostics

Add concise lifecycle logging at state transitions, not per frame:

- VP process ID, renderer type, renderer generation, and transition reason;
- render HWND, parent/root/owner, target monitor, and fullscreen/windowed state;
- requested and actual display mode before and after switching;
- swap-chain model, buffer count, format, composition/direct-flip status where
  observable, and creation/destruction generation;
- last source sequence submitted and last presentation completed by the old
  generation;
- first source sequence accepted and first presentation completed by the new
  generation;
- queue size and retained-frame count at stop and after clear;
- shader-cache enabled/load/save state and object count, without implying that
  cache objects are frames;
- transition-black or placeholder presentation time;
- host-window hide/show, repaint, destruction/recreation, and first-live-frame
  reveal events.

For diagnosis builds, support an inexpensive frame identity such as source
sequence plus a bounded luminance/hash sample at the final submission boundary.
It must be possible to prove whether a visible stale frame was submitted by
the new renderer. Do not hash full 4K frames on the normal hot path.

## Required behavior

1. Every renderer rebuild and display-mode transition establishes an explicit
   generation boundary. Frames, repeats, textures, and presentations from the
   retired generation cannot become visible afterward.
2. Before releasing or changing the old presentation contract, VP must place
   the output into a deterministic transition state: preferably black, or an
   intentional VP placeholder in windowed mode.
3. The replacement output must remain black/hidden until the renderer has
   accepted and presented a live frame belonging to the current generation.
4. Renderer handoff must transfer exclusive ownership of the render target.
   Alpha's retired swap chain or presentation plane must not remain visible
   while madVR owns the target, and vice versa.
5. If painting black on the existing HWND cannot evict a retired flip/MPO
   surface, detach/destroy that presentation surface or recreate the target
   HWND using a lifecycle proven to do so.
6. Do not fix the symptom by deleting or disabling a valid shader cache unless
   testing demonstrates an actual libplacebo cache-lifetime defect. Compiled
   shader reuse should remain available.
7. Do not replay an old video frame merely to cover HDMI resync. A brief black
   transition is correct and safer.
8. Queue, source-buffer, native OSD, NLS, LUT, refresh-switching, and output
   color contracts must continue to work after the transition.

## Candidate implementation direction

The likely small fix is a transition barrier owned by the GUI/renderer
lifecycle:

1. stop accepting frames for the retiring generation;
2. hide or explicitly black its presentation target;
3. stop/join and release the renderer and all retained frame ownership;
4. complete any required compositor/display synchronization;
5. rebuild and switch mode;
6. accept only current-generation frames; and
7. reveal the target after its first successful current-generation present.

This is a hypothesis to validate. Do not add unconditional sleeps or rely on
`Invalidate()` alone: neither proves that a DirectFlip/MPO surface or physical
scanout was replaced. Avoid `DwmFlush` or GPU flush/wait operations on the
steady-state frame path; if needed, bound them to lifecycle transitions.

## Verification

1. Use content with embedded frame/generation markers and record mode switches
   at high frame rate to identify every visible transition image.
2. Run the sequence matrix from the investigation with shader cache enabled
   and disabled.
3. Perform at least 25 repeated refresh/mode changes for Alpha-only,
   madVR-only, and Alpha-to-madVR sessions without restarting VP.
4. Verify windowed, windowed-fullscreen, and exclusive-fullscreen modes where
   supported, including Direct/flip and composed Alpha presentation.
5. Exercise 23.976/24, 50, 59.94/60 transitions and SDR/HDR/LLDV-related
   renderer rebuilds that trigger real HDMI resync.
6. Confirm the visible sequence is old live frame, deterministic black, then
   first current-generation frame. No older source identity may appear after
   the generation boundary.
7. Confirm no new crash, device loss, blank-screen hang, excessive transition
   delay, queue starvation, dropped-frame accounting error, or renderer
   restart loop.
8. Confirm shader-cache load/save still shortens or stabilizes shader startup
   as intended and cannot retain video-frame resources.

## Acceptance criteria

- No stale video frame from a retired renderer generation appears during any
  tested renderer rebuild, refresh-rate switch, resolution switch, fullscreen
  transition, or Alpha/madVR handoff.
- HDMI resync shows only deterministic black/placeholder output until a live
  current-generation frame is successfully presented.
- Logs establish old-surface retirement and first-current-frame presentation
  for every transition without frame-by-frame noise.
- Persistent shader caching remains enabled unless independently proven faulty.
- Restarting VP is no longer required to clear hidden presentation state.
- Alpha and madVR steady-state playback, output contracts, refresh selection,
  queue health, latency, NLS, LUT, OSD, and frame accounting do not regress.

## Dependencies and boundaries

- VP-0037 defines the accepted Alpha windowed-preview and renderer-handoff
  composition behavior.
- This story covers shared lifecycle ownership plus Alpha-specific GPU
  teardown. It must not alter madVR internals, but VP may black, hide, detach,
  or recreate the host presentation target around graph replacement.
- If evidence shows the old scanout is retained entirely by the display after
  VP has submitted black and destroyed all old presentation state, document the
  driver/display boundary and create a narrowly scoped follow-up rather than
  adding arbitrary delays.
