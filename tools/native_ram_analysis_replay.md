

### CPU analysis replay of a native RAM export

Build `native_ram_analysis_replay.vcxproj` as Release/x64 after a matching native
library build. The standalone program links the existing production
`OptionalAnalysisWorker` and `DetectPreparedSourcePictureBounded`; it does not
launch VideoProcessor, a renderer, or a GPU device.

```powershell
& $MSBuild tools/native_ram_analysis_replay.vcxproj /t:Build /m:1 /p:Configuration=Release /p:Platform=x64
python tools/export_native_ram_replay.py E:/captures/session --output E:/captures/export
python tools/replay_native_ram_analysis.py E:/captures/export/replay.json --executable artifacts/native-ram-analysis/build/NativeRamAnalysisReplay.exe --output E:/captures/analysis
python -m unittest discover -s tools -p test_native_ram_analysis.py -v
```

The driver independently revalidates the original ledger, files, identities and
continuity segmentation. Each exported segment starts a separate cold worker
process. It preserves all sixteen original identity fields, original indices,
unknown timestamp units, omissions and evictions. It neither fills gaps nor loops
or renumbers inputs. Source-format/context changes and declared discontinuities
are retained. The native consumer checks SHA-256 on the exact bytes it reads,
then derives geometry exclusively from those pixels; sidecar picture bounds and
semantic settings supply no authority.

`frames.jsonl` records current physical estimates, native evidence bounds and availability,
current verified sampling support, and any separately retained compatible
physical hypothesis. All sixteen input identity fields are retained; native and verified-support results
are checked against the native API’s eight-field analysis identity. Policy and
continuity metadata separate replay episodes, and supply no new evidence authority.
The ledger also records result reasons, cache reuse,
read/hash cost, queue cost and worker wall time. `receipt.json` pins the executable
and export, lists segment results, and retains gap/omission evidence. Record the
matching source/library build receipt alongside it; the executable hash alone
cannot establish which source was compiled.

This is **serial, unpaced CPU analysis**, not a live deadline or renderer test.
The normal production per-frame detector budget remains active. An unavailable
or budget-limited measurement is a recorded outcome, not an invented expected
aspect or a test failure. No accepted scene model, applied crop, subtitle
composition, HDMI progression or presentation is asserted. Offline admission
retries and completion waits never modify runtime scheduling. Input limits are
65,536 frames per segment and 128 MiB per native frame; file reads and hashing are
outside the worker cost. Run without competing CPU workloads when comparing cost.
