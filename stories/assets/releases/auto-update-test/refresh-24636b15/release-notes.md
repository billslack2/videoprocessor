# Auto-update test refresh

Refresh of the existing auto-update-test release, including the current Config Updates page, sidebar placement, and helper connection fixes. A no-op commit provides a new build identity.

Channel: beta. Both full VP + Config and Config-only installers are included. Setup and portable installations select their existing package flavor automatically. This is a GitHub pre-release and is not Latest.

Tests were explicitly skipped for this refresh at the user's request. x64 Release compilation, package inventory/hash validation, and signed update metadata generation are recorded separately; no test pass is claimed for this release.
