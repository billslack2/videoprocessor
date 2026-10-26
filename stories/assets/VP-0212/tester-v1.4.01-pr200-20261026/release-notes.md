Tester build based on v1.4.01-beta, incorporating PR #200 (madVR NLS source geometry).

Source branch: codex/v1.4.01-beta-pr200-20261026
Base: c71b44593cad5ba5dd8bd7ec369f0d3a5965ff72
Source: 404ea6a34a7ca2a80ddf3ed338ec2e5c9cf6a65f
Feature: a5769e53a89c4684104feb90cfedd2de06eff55e (backported as cb79824f)
Packaging fix: validates all five recorded build binaries. No newer beta renderer changes are included.

Choose the Setup EXE for installation or the self-contained x64 Portable ZIP. Close VP and Config (including its tray icon) before upgrading. Back up your settings before testing an older base. For an existing portable folder, preserve VideoProcessor.cfg. For a fresh folder only, copy VideoProcessor.cfg.example to VideoProcessor.cfg. No separate runtime installation is required.

madVR must detect and report the intended black-bar crop. Missing or invalid crop feedback suspends NLS with native output. Test matching 2.35/2.39 scope passthrough, 16:9 to scope, 4:3 safe fit, asymmetric bars, mixed-aspect transitions, disabled bar detection, and feedback loss/reacquisition. Report framing, oscillation, and logs showing geometry=madvr-videoCropRect.

This is an unsigned tester prerelease for Windows 10/11 x64. Live madVR playback, clean Windows without Visual Studio, and real installer lifecycle/Config Apply/OK/reopen acceptance remain unverified. It does not change the normal updater feed.

Validation: clean x64 Release solution rebuild; all 2,400 native tests passed (11.53 minutes); installer support/identity, runtime packaging and portable configuration contract/archive checks passed. SHA-256 and build receipts retained internally.
