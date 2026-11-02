# Classic translation during a pending replacement target

The 2026-11-02 3 Body Problem reproduction used Classic, selected by the active
renderer-ready action. Its rendered screenshots and source-bound output records
are preserved outside the repository under
E:\codex\videoprocessor\evidence\three-body-repro-20261102-0943.

At S1184 / O2780-O2782, presentation briefly changes from translation to outward
fit, then returns to translation at S1185 / O2783, 52 ms later. Accepted native
bounds stay [0,254,3840,1906]. Caption bounds [1630,1862,2210,1978] and raw-visible
bounds [0,254,3840,1983] are unchanged on the surrounding sources. The current
translation proposal [0,362,3840,2014] covers those outside-native pixels.

The verbose source-crop record identifies dense_scan=skipped and
translation_confirm=1/3 on S1184. Adjacent scheduled dense scans can independently
qualify caption-controller lifetime; this source cannot borrow their proof. The
existing submitted-translation continuation path also unconditionally rejects a
pending replacement target. With neither route available, current-excluded-band
recovery widens the presentation despite the current controller pose covering the
visible extent. This is a presentation ownership conflict, not evidence that the
measured movie should become 16:9.

The repair lets an already submitted Classic translation continue while its
replacement target is pending. That exception requires current band inspection
and either clean excluded bands or an exact current raw-visible witness. Existing
checks still require the accepted native contract, matching actual submitted
translation, current controller pose, source/context identities and outside-native
pixel coverage. Competing fit/picture owners and uncovered pixels still veto it.
The pending target gets no acquisition permission, confirmation vote or renewed
caption lease. Detection and configured fitting are unchanged.

The regression uses captured geometry and reconstructed controller state, with
mirrored top-caption coverage and preservation controls. It is not a serialized
replay of every runtime input. Before the repair, the continuation assertion fails
and the safety controls pass. Required native methods now include both new tests;
full x64 Release qualification still requires both actual renderer dependencies.

No additional native-pixel capture is needed to test this policy hypothesis.
Post-repair acceptance should replay the same subtitle handoff with the timecode
and incident logging enabled, and compare pending-target/scan state with actual
submitted geometry. A genuine new occupied extent must still widen immediately.
Then separately verify the requested Reconstruction profile in the output log;
the existing capture does not establish Reconstruction behavior. Playback controls
and rating overlays in the other images remain separate visibility cases, and
exact accepted-versus-current native boundary refinement remains open.