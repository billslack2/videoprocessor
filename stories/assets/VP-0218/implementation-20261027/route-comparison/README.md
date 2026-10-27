# VP-0218 route comparison probe

This is a standalone metadata characterization, not decoded movie pixels or a GPU/player test. It uses the existing `3840x2160` scope `(0,276)-(3840,1884)` and taller target `(0,68)-(3840,2092)` fixtures from `LookaheadOutwardProofTests.cpp`. Synthetic current-frame retention values model exactly the strict native-distributed and broad predicates exercised by policy tests. The source uses only APIs and flat state fields present in both reviewed beta and candidate headers.

## Build and run

VS2019 Professional 16.11 / v142, x64, optimized Release, `/O2 /MD /EHsc /DNOMINMAX /std:c++17`, `/LTCG`.

Baseline (beta `7394ba420999a8fa708718ee4ce4d87e7459369f`):

```powershell
cmd.exe /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cl /nologo /O2 /MD /EHsc /DNOMINMAX /std:c++17 /I"E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026\src\VideoProcessor-Lib" /I"E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026\src" /I"E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026\3rdparty\libplacebo\include" "E:\codex\diagnostics\vp-0218-implementation-20261027\route-comparison\route_probe.cpp" "E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026\x64\Release\VideoProcessor-Lib.lib" /link /LTCG /OUT:"E:\codex\diagnostics\vp-0218-implementation-20261027\route-comparison\baseline.exe" strmiids.lib ole32.lib oleaut32.lib uuid.lib winmm.lib bcrypt.lib Propsys.lib'
```

Candidate (`e211bdb929219ea6e60d2e9390b0cf90cbabef8a`): same command with the `vp-0218-adaptive-ar-20261027` include root and `x64\Release\VideoProcessor-Lib.lib`, outputting `candidate.exe`. Execute both binaries directly from PowerShell.

## Results

| Scenario | Beta | Candidate |
| --- | --- | --- |
| Live native → both → native, accepted sequences 100–102 | votes 1, 1, 1; no authority | votes 1, 2, 3; authority at 102 |
| Duplicate repeat at sequence 101 | remains 1 | remains 2 |
| Duplicate repeat at sequence 102 | remains 1 | remains 3; no added vote |
| Three-frame native-only queued window | invalid | valid; effective/first sequence 100 |
| Alternating native-only → broad-only → native-only queued window | invalid | invalid |

Exact output is in `results.txt`.

## Pinned inputs

- Beta Release library SHA-256: `E3BD5E2E14D8A1B3C833C1E4AC9B7451CB7790E6E20EA6C3650D3F736CEFB15C`
- Candidate solution Release library SHA-256: `2467F6DBF66C1BEF83F22EDADA62082D022D3F8B11F4D5A33A0EF1B537412B0B`
- Baseline executable SHA-256: `6CC9E56939A4BDCA02F7E4CC3D6EBA11FCC09A1BA52DFED8F07B4E3960134C73`
- Candidate executable SHA-256: `156F2807A17E520E48A6B3FE941EDEEBC9E67F45D881A80A3117EC70D2505C8B`

This probe does not establish behavior on actual movie pixels, rendering, GPU scheduling, subtitles, or every owner state. Focused tests and full Release build remain the source of qualification for those behaviors.
