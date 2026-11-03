# VP-0225 evidence snapshot: cb8a95a6

Source: `cb8a95a627f708cd260f16bd52b2426e4892ded7` (clean; not deployed). Tracker status remains **Review**. Last deployed build remains `53058498`.

- Native lane: 1,083/1,083 pass (`20261103-114404-09062067`). Qualification is false.
- Focused Native set: 12/12 pass on source `ad55ecc331f32b76b0a0692e41979a38ee6c6dd4` (`20261103-113323-8e00392c`). Qualification is false.
- Paired Standard/Beta actual-renderer run: both `StaticCueWorkerFitsLiveFrameBudget` cases failed the grouping budget (`20261103-114547-f555e12d`). The run used a 3840x2160 60 Hz presentation target, with 59.94 Hz source cadence. The desktop display was restored to 24 Hz after the run. Only the first source-rate case, 59.94 Hz, was reached; 23.976, 24 and 60 Hz source cases were not run.
- Beta: 302 sources, 299 proofs, 304 grouped frames; maximum frame time 12.2639 ms; proof p95/max 14.8452/23.0543 ms; receipt-bind p95 0.0285 ms.
- Standard: 301 sources, 298 proofs, 303 grouped frames; maximum frame time 18.678 ms; proof p95/max 18.3424/23.7402 ms; receipt-bind p95 0.0247 ms.
- These are partial runtime measurements with a failed budget assertion, not a renderer pass or qualification. No new deployment is claimed.

Copied receipts were byte-verified against their source files by SHA-256:

| Receipt | SHA-256 |
| --- | --- |
| `ad55ecc3-focused-12-iteration.json` | `F96F9028329BA900CE5531F8AB0B0D2356E5BCD0730BFFDFF6CA0815B087BAA7` |
| `ad55ecc3-focused-12-standard.trx` | `E74A963AF20088CC612BDBF4BFED1E4950F0D295F60D3FEDD9831FCDD90AE832` |
| `cb8a95a6-native-1083-iteration.json` | `8FA19B68FF9B7273799AF0B80AFB047BA9B22AE6E5DC920EDA21DED3A2160020` |
| `cb8a95a6-native-1083-standard.trx` | `68964D80BCF46D061EF026A9E487F26010219A2DE80BA301A9FF231306AF314A` |
| `cb8a95a6-4k60-pair-failed.json` | `3755E55E5E3748F151AF1145C7672A0D60307AB8C3A0E435DDE9182F16FEB0D4` |
| `cb8a95a6-4k60-pair-standard.trx` | `EDDCD63E5D2C094ECA965539653E26C093D8C8078AA1E730822E0E4FCD8631A3` |
