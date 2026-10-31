# VP-0223: Magewell USB capture research and implementation foundation

## Status

In Progress

2026-10-30: User requested ten Luna extra-high researchers and implementation if the evidence supports it, without USB hardware. Ten-agent research is being run in batches under the four-agent concurrency limit. Source branch `codex/magewell-usb-research-20261030`, worktree `E:\codex\videoprocessor\magewell-usb-research-20261030`, fresh beta `v1.4.02-beta` at `a8f62855c246019e3d9262f384e58c96d60f3147`.

## Objective

Establish the Magewell USB API/SDK and open-source path, implement a tested first increment, and identify the evidence needed to expose USB capture in the player.

## Scope and acceptance

- Inspect installed SDK/runtime 3.3.1.1596, official USB model specifications, and actual open-source implementations and licenses.
- Optional USB API binding must not make existing Pro Capture initialization depend on USB-only exports.
- Bound capability enumeration and callback frame memory; preserve callback pixels in owned storage and keep raw SDK timestamps separate from host time.
- Verify malformed frames, memory limits, overflow, ownership, concurrency, shutdown admission, and existing Magewell regressions with fakes in x64 Release.
- Supply a read-only USB device/capability diagnostic through the production runtime loader.
- Keep player USB discovery disabled until negotiated layout, timestamp semantics, callback teardown and actual signal/HDR behavior can be qualified. Hardware tests cannot be replaced by synthetic passing results.
- Do not alter device settings, deployed VP, active configuration or detector/fitting behavior.

## Research decision

The official generic `MWCreateVideoCapture` / `MWDestoryVideoCapture` path supports USB and wraps DirectShow. The existing Pro DMA/notification capture path does not. SDK callback timestamp units/epoch and teardown quiescence are not explicit in available documentation. An isolated foundation/probe increment is justified; full playback support is not yet qualified.

## Tracker audit

Fetched origin/main. Found 240 canonical records and 240 index rows, maximum root ID 0222, no duplicate or missing IDs. New ID 0223 is above both sources. Existing unrelated VP-0207 asset edit in the canonical checkout is preserved; this change is made in a clean E: tracker worktree.
