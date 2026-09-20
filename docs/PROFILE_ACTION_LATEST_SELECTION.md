# Preserve screen intent while profile actions run

The September 20 log at 11:18:03-11:18:09 shows a running 16:9 action,
a manual F2 Scope selection, suppression of the Scope actions, then set_hdr
reading the stale 169 marker and selecting Rec709 169 Med (113 versus 117 nits).
The broad suppression dates to August 30, commit 09c304f8.

Start from current remote beta a6914dc6, which includes the deployed crop work.
Keep existing serialized process completion and latest-profile-batch cancellation.
Classify Rendering-only manual shortcut feedback at the originating selection,
before scheduling its emitted actions. This preserves protection for generic
state.committed callbacks too. Screen/color/zoom/queue and mixed selections remain
eligible; they replace pending old batches and execute after the running script.
Explicit cycling retains its existing behavior. This is a narrow compatibility
policy: a manual Rendering-only shortcut during a running script still has its
actions suppressed, even if physically pressed by a user. No claim is made to
identify arbitrary injected keys or prevent every possible user-script cycle.

Pending-action tokens now increase across successful claims and cancellation.
An older sleeping worker cannot claim a new invocation that reused its token.
Replacement diagnostics use actual pending membership, not the numeric token.

RED: three of four tests fail on the original guard/coalescer; Rendering feedback
control passes. First green full Release suite passes 1380/1380. Final source adds
selection-level classification plus generic callback/mixed-selection regression;
its clean committed build/test and deployment receipt are kept under
C:\Users\bslac\Documents\ChatGPT\Done\profile-action-latest-20260920.
No external scripts or user configuration edits are needed. Live validation is
F3 then F2 while the delayed 16:9 action is running, ending with Scope screen,
Scope Rendering calibration, and the external 235 marker aligned.
