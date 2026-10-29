# Beta integration, 2026-10-29

User authorized merging the fullscreen focus/stats correction, DV/capture updates,
and madVR NLS. Core RGB8/R210 DV support was already merged as PR #225. PR #226
merged the BGRA capability fix and native P210 capture; PR #227 merged bounded
fullscreen focus retries and disabled only the new CPU/wait statistics; PR #200
merged madVR-owned NLS source geometry.

The madVR integration is based on beta 7c2735f0, after both capture and focus/stats
merges. Its original seven-file feature delta merged cleanly. Clean source
98b95e25 passed the full x64 Release solution build and all 277 selected tests in
4.93 minutes, including actual GPU subtitle/NLS composition, DirectShow contracts,
DV/capture/native P210, focus cancellation and preservation of the older stats.
See integration-results.trx and validation.json for exact identities and results.

Focus integration 8b9095f3 has an application tree identical to the previously
built/deployed 20ec2ad8; it was rebuilt and all 30 focused checks passed again.
The prior live focus run acquired actual foreground and keyboard focus ten times,
then stopped. User reported it working; projector testing remained their follow-up.
The superseded broad stats-disable commit d4ed8464 was excluded.

No deployment, installer or public release was performed by this merge operation.
Live madVR acceptance (mixed aspects, detection disabled, dynamic-DAR rejection
and rebuild) remains qualification work, rather than a result of automated tests.
The native P210 story also retains its broader card/driver and performance limits.
An independently reported sixty-Hz delivery issue is under investigation; no
performance correction is included in these merges. Existing VP-0189 and VP-0220
review states and unrelated drafts are unchanged.
