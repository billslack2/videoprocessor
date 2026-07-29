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
`v1.1.015-beta`, using the isolated worktree
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

Pull request #26 merged the hardening into `v1.1.015-beta` on July 29, 2026 as
merge commit `63e959f`. A clean x64 Release build from the merged integration
worktree completed successfully and all 196 tests passed. The matching
executable and VP Renderer runtime were deployed to `C:\Videoprocessor\vp`;
active configuration, state, launcher, shaders, shader cache, and logs were
preserved.

The subsequent deployed integration candidate also contained the merged
VP-0054 DirectShow liveness work (`1b231356`). It failed real-world validation
again on July 29, 2026. The user had not used the Alpha renderer in the process
lifetime: during a madVR-only channel change, VP exposed a baseball frame from
content watched approximately five minutes earlier. This proves that Alpha
and its shader cache are not prerequisites for the stale-frame defect.

The deployed log records the relevant madVR lifecycle:

- At `01:09:15`, a channel/signal transition changed the effective video state
  from `SDR / REC.709` to `UNKNOWN / UNKNOWN`, causing renderer generation 9 to
  stop under the black transition cover.
- Generation 10 started under black, but VP removed the cover at `01:09:19`
  with `evidence=downstream-accepted`. In fact, the log records reveal before
  `DELIVERY THREAD: BUFFERING COMPLETE`, so delivery to madVR had not started.
  The scheduled
  `post-renderer-start` graph reset then stopped and restarted the graph at
  `01:09:22`, after the cover had already been removed.
- A second madVR stop/start created generation 11 at `01:09:39-01:09:41`.
  Again the cover was removed on downstream acceptance, three seconds before
  its scheduled graph reset ran at `01:09:44`.

Source inspection establishes two remaining correctness gaps. First,
`DirectShowVideoRenderer::OnVideoFrame` marks
`m_hasPresentedLiveFrame=true` when `m_liveSource->OnVideoFrame` returns
success. With the buffered live source, that success means only that the
capture frame entered VP's raw queue; it is neither downstream acceptance nor
presentation. The `downstream-accepted` evidence label is therefore
incorrect, and the observed reveal-before-buffering log ordering confirms the
cover can be removed before madVR receives a new sample. Second, the
transition cover does not span the delayed post-start graph reset, so madVR
can expose retained presentation state during that reset. The replacement
must keep black visible across every associated graph reset and require
current-epoch downstream-delivery plus presentation-grade evidence before
reveal.

The same run ended in a confirmed crash at `01:10:15`. Immediately before the
crash, VP detected a frame-counter discontinuity (`33944 -> 34021`) while the
raw queue was backing up, purged 27 raw frames and one converted sample, and
reset the live queue while madVR remained active. Windows Application Error
and WER both identify a fatal access violation (`0xc0000005`) in
`C:\madvr210\madVR64.ax` version `0.92.17.0`, offset `0x1f041d`, report ID
`b6036bad-5509-4751-bd81-fd066db057b6`. The crash may be a queue-reset /
delivery race related to the same graph lifecycle, but a shared root cause is
not yet proven.

Implementation and diagnosis now continue on
`codex/vp-0041-madvr-reopen`, based on current
`origin/v1.1.015-beta` commit `c00b990` (including the merged Alpha configured
queue-limit work), in
`C:\Users\bslac\vp\worktrees\vp-0041-madvr-reopen`.

The July 29 combined candidate consists of `6bcf9d8` plus review-hardening
commit `217e4fc`. DirectShow reveal now requires five successful downstream
deliveries from the current queue epoch; raw enqueue and `S_FALSE` no longer
count. The transition cover remains up across pending resets. Internal timing
and discontinuity resets are routed through the GUI-owned recovery
coordinator. Capture admission is closed before graph control, DirectShow
Stop/flush runs before waiting for admitted callbacks, and source reset/run
occurs only after the ingress drain, preventing both teardown races and the
unbuffered madVR `Receive` deadlock.

Threading, DirectShow, and MPC/mpv-style lifecycle reviewers completed
blocker-only reviews. After the Stop-before-drain correction, all three
approved the lifecycle and concurrency changes. A clean x64 Release rebuild
from `217e4fc` completed with `VERSION_DIRTY=false`; all 214 tests passed. The
branch was pushed as `origin/codex/vp-0041-madvr-reopen`.

Real-world testing of the earlier temporary candidate exposed an additional
fullscreen-only case during YouTube TV channel changes on Apple TV 4K. VP
logged continuous 59.94 capture/conversion/delivery with a nine-sample
(approximately 150 ms) queue and no capture-state, frame-counter, epoch,
renderer-generation, or reset event. A frame watched about five minutes
earlier therefore cannot have remained in VP's queue. The current evidence
instead points to madVR/DXGI exclusive/DirectFlip/MPO resurfacing a retained
presentation plane. The combined deployment uses
`windowed_fullscreen_mode: true` as a composed-fullscreen mitigation while
preserving `fullscreen: true`. If it still reproduces, the next diagnostic
build should add bounded capture/pre-Deliver fingerprints or an opt-in
per-frame barcode so a physical recording can distinguish source pixels from
renderer/driver surface reuse.

The combined clean binaries were deployed at `01:51:38` to
`C:\Videoprocessor\vp` and launched successfully:

- `VideoProcessor.exe` SHA-256
  `262147366259380DC293D2644354BE8CE016E91DEE327EE4FCEFE14853F5D436`;
- `vprenderer\VideoProcessorVPRenderer.dll` SHA-256
  `E69D56745B7547993F57785EDC53AF728C30D13CAF42AE78079CEFCB8F307EDE`.

The previous executable, renderer plug-in, and configuration were backed up
with suffix `20260729-015138`. Existing configuration values/comments, state,
logs, shaders, and shader cache were preserved; the only configuration edit
was enabling composed windowed fullscreen.

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
- A madVR-only process can reproduce the issue during a channel change, with
  a frame from content watched approximately five minutes earlier.
- The deployed transition cover can be removed before its delayed
  `post-renderer-start` graph reset, leaving that reset uncovered.
- DirectShow's `downstream-accepted` reveal evidence is currently asserted
  when a capture frame is merely enqueued in VP, before buffered delivery to
  madVR begins.

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
9. Trace channel/capture epochs through raw enqueue, conversion, delivery, and
   renderer acceptance so an old capture epoch can never qualify as the first
   live frame of a new renderer generation.
10. Make the black transition barrier encompass the delayed
    `post-renderer-start` graph reset rather than revealing before that reset.
11. Reproduce the `madVR64.ax` access violation under frame-counter
    discontinuity recovery and determine whether live-queue reset races an
    in-flight madVR delivery or segment transition.

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
- capture/channel epoch attached to the first accepted sample and used in the
  reveal decision;
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
   For DirectShow/madVR, downstream acceptance alone is insufficient, and the
   barrier must remain active across any scheduled post-start graph reset.
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
9. Force frame-counter discontinuity recovery during madVR playback and verify
   that queue purge/new-segment delivery cannot race an in-flight renderer
   call or crash `madVR64.ax`.

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
- VP-0043 intentionally owns the delayed madVR stop/reset/run re-prime.
  VP-0041 must extend the black transition barrier across that re-prime rather
  than removing or weakening it.
- VP-0054 owns generation-aware reset arbitration, background graph control,
  and liveness recovery. Coordinate current-epoch reveal and queue-reset
  serialization with that implementation.
- This story covers shared lifecycle ownership plus Alpha-specific GPU
  teardown. It must not alter madVR internals, but VP may black, hide, detach,
  or recreate the host presentation target around graph replacement.
- If evidence shows the old scanout is retained entirely by the display after
  VP has submitted black and destroyed all old presentation state, document the
  driver/display boundary and create a narrowly scoped follow-up rather than
  adding arbitrary delays.

## 2026-07-29 apartment-safe recovery deployment

The reopened madVR path was refactored and deployed from
`codex/vp-0041-madvr-reopen` at commit `ec59c0b`:

- backend reset requests now enter a typed, generation-bound coordinator
  instead of being polled from capture callbacks;
- one joinable reset worker owns reset arbitration, ingress closure/drain,
  completion identity, wake retry, and failure coverage;
- reset completion is validated by binding token, renderer generation,
  transition token, and target revision before ingress can reopen;
- a pure transition model makes the black shield authoritative through reset,
  replacement, first-current-frame evidence, and failure;
- DirectShow graph/filter COM objects are created, controlled, and released on
  one permanent MTA owner thread;
- UI-originated build/start/stop, resize, event, paint, video-state,
  configuration, shader, and profile operations are asynchronous and
  serialized on that owner, eliminating backend-to-UI synchronous reset and
  graph-control calls;
- DirectShow stop closes admission, stops the graph, drains admitted capture
  callbacks, tears down on the owner apartment, and only then publishes
  `STOPPED`;
- shutdown cancels queued-not-started work and services only synchronous
  `SendMessage` traffic while joining, preventing both HWND deadlock and
  unrelated posted-message reentrancy;
- MPC Video Renderer and EVR are selected by CLSID before asynchronous build,
  madVR selects the HDR-capable wrapper, and other filters select the generic
  wrapper;
- DirectShow terminal graph events publish `FAILED` so the UI closes ingress
  before teardown, and covered failures retain the physical shield.

Threading, DirectShow, and player-lifecycle reviewers approved the final
integrated result. A clean x64 Release rebuild completed and the full test
suite passed `256/256`.

Deployment:

- executable: `C:\Videoprocessor\vp\VideoProcessor.exe`;
- plugin: `C:\Videoprocessor\vp\vprenderer\VideoProcessorVPRenderer.dll`;
- configuration was preserved, including `renderer: DirectShow - madVR`,
  `fullscreen: true`, and `windowed_fullscreen_mode: true`;
- rollback backups:
  `VideoProcessor.exe.pre-VP0041-apartment-safe-20260729-095053.bak` and
  `VideoProcessorVPRenderer.dll.pre-VP0041-apartment-safe-20260729-095053.bak`.

The deployed process started successfully with madVR. Live logging confirmed
multiple graph-reset cycles completed through black cover, owner-thread
stop/reset/run, current-epoch downstream preroll, and shielded reveal without
a crash. The story remains in progress pending the user's YouTube TV
fullscreen channel-change/exit reproduction matrix.

## 2026-07-29 madVR-to-Alpha hang follow-up

Live testing of `ec59c0b` reproduced a total hang when switching from madVR
to Alpha. Reset and restart operations before that switch completed normally.
The final log entry was old-renderer detachment, before
`old-surface-retired`.

Review found that `GraphStop()` made `STOPPED` observable before ingress drain
and graph teardown were terminal. A pending DirectShow window notification
could therefore make the UI destroy the renderer while its graph-owner stop
command was still active. The destructor then joined that active command and
could deadlock with madVR's window-message teardown.

Commit `1ff1bc9` fixes that lifecycle boundary:

- `STOPPED` is created only after ingress drain and resource-complete graph
  teardown;
- its HWND wake is an executor completion hook, issued only after the stop
  command has returned and released its captures;
- the DirectShow notification handler pins shared renderer ownership through
  callback dispatch;
- normal retirement cancels late event-drain commands and joins without a
  second graph teardown;
- both normal and forced joins service synchronous sent messages without
  dispatching unrelated posted UI work;
- the graph-owner apartment drains its own helper-window messages before
  `CoUninitialize`;
- incomplete resource release reports `FAILED` and retains the forced-cleanup
  path instead of claiming terminal retirement.

Threading, DirectShow/COM, and player-lifecycle reviewers approved the final
revision with no deployment blocker. The x64 Release solution build and full
test suite passed `257/257`, including a deterministic test proving lifecycle
completion cannot overtake the command or its captured lifetime.

The exact clean commit build was deployed to `C:\Videoprocessor\vp` with
matching executable/plugin hashes. Rollback backups use timestamp
`20260729-100130`. VP started successfully as process 19300 with version
`v1.1.012-beta-ffmpeg-4.4.8-115-g1ff1bc9`. The story remains in progress
pending live fullscreen madVR-to-Alpha and YouTube TV channel-change
validation.

## 2026-07-29 asynchronous DirectShow retirement barrier

Live fullscreen testing showed that `1ff1bc9` still hung on madVR-to-Alpha.
The same process completed one earlier madVR retirement, then stopped at
`Renderer teardown: detached renderer before destruction` during the Alpha
switch. There was no Windows Error Reporting crash record; the UI was again
blocked in final wrapper/executor/apartment shutdown after graph resources had
already reached terminal teardown.

Commit `fceec1b` removes that final UI wait:

- only DirectShow renderers opt into a single managed MTA retirement worker;
- the UI transfers a strong renderer reference and immediately returns to its
  normal message loop, allowing madVR fullscreen/helper-window traffic to
  complete;
- explicit idempotent `Retire()` makes owner-thread join and COM apartment exit
  happen on the worker even if a transient UI callback still holds a
  `shared_ptr`;
- the black shield and fullscreen host remain alive, and `UpdateState` cannot
  construct Alpha or another DirectShow graph until a keyed
  `WM_MESSAGE_RENDERER_RETIRED` completion;
- stale completions, failed retirement, startup fallback, failure presentation,
  and fatal/normal close paths preserve the same barrier;
- Alpha retains its existing synchronous destruction path until it has an
  independently audited idempotent retirement contract.

Threading, DirectShow/COM, and player-lifecycle reviewers approved the revised
barrier with no deployment blocker. The exact x64 Release commit build passed
`258/258` tests, including a blocked-retirement test proving the UI-facing
handoff returns while the worker owns shutdown and a transient UI lifetime pin
cannot move blocking retirement back to the UI thread.

The clean `fceec1b` build was deployed to `C:\Videoprocessor\vp`; executable
and plugin hashes match their Release outputs. Rollback backups use timestamp
`20260729-101624`. VP started successfully as process 33748 with version
`v1.1.012-beta-ffmpeg-4.4.8-116-gfceec1b`. Live fullscreen madVR-to-Alpha
validation remains required.
