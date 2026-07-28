# VP-0032: Alpha renderer field-aware GPU deinterlacing

## Status

Backlog — low priority. This matters for interlaced broadcast and legacy inputs,
but not for the progressive HDMI/movie paths that currently dominate VP use.

## User story

As an Alpha renderer user with interlaced input, I want VP to recognize field
structure and use libplacebo's GPU deinterlacing so 1080i/576i/480i material is
presented cleanly at the intended field-derived cadence rather than being
treated as progressive or woven incorrectly.

## Libplacebo capability and boundary

The bundled libplacebo build provides weave, bob, YADIF, and BWDIF. YADIF and
BWDIF require correctly ordered previous/current/next field references.
Libplacebo performs the pixel processing; VP must supply accurate field order,
timestamps, frame lifetime, and presentation pacing.

## Scope

- Alpha/libplacebo renderer only.
- Accept interlace metadata from VP capture/media negotiation and retain it
  through queueing and renderer resets.
- Provide a safe policy such as `AUTO`, `OFF`, `BOB`, `YADIF`, and `BWDIF`,
  defaulting to unchanged progressive behavior unless interlaced input is
  positively identified.
- Present field-derived output at the correct cadence, for example 29.97i to
  59.94 Hz where the display path supports it.

Do not change the established renderer, invent interlace metadata when capture
does not provide it, or add motion-compensated frame interpolation.

## Required implementation work

1. Trace capture format/field-order metadata into the Alpha frame submission
   path. Define explicit behavior for unknown, progressive, top-field-first,
   bottom-field-first, and mixed/discontinuous streams.
2. Retain coherent previous/current/next frames while respecting VP's source
   buffer ownership, low-latency queue policy, reset generation, and GPU
   resource lifetime.
3. Populate libplacebo's field/deinterlacing inputs and choose a safe default
   algorithm. Never request a temporal algorithm without valid references.
4. Couple field timestamps to actual display cadence and refresh switching.
   Avoid duplicate/drop loops when changing from interlaced to progressive or
   when display-rate switching fails.
5. Add OSD/log diagnostics for input field state/order, selected algorithm,
   reference availability, derived output rate, fallback reason, and resets.
6. Add configuration/help only after the automatic safe path is demonstrated.

## Verification

- Test known TFF and BFF 1080i/29.97, 1080i/25, 576i, and progressive control
  samples with motion, diagonal detail, scrolling text, and hard cuts.
- Verify BWDIF/YADIF use only valid temporal references, Bob works without
  references, and unknown/progressive input remains unmodified.
- Confirm field cadence, refresh-rate selection, renderer resets, color/HDR
  metadata, queues, latency, and source-buffer release remain stable.
- Compare against the established renderer and a trusted reference for visible
  combing, motion artifacts, and cadence correctness.

## Acceptance criteria

- Alpha never deinterlaces progressive input by mistake.
- Correctly signaled interlaced input is rendered with a documented selected
  method and correct field-derived cadence.
- Missing references or uncertain metadata safely fall back without crashes,
  queue starvation, or presentation loops.
- Existing progressive Alpha playback is unchanged when the feature is off or
  unavailable.
