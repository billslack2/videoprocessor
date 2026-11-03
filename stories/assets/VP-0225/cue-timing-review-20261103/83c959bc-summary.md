# VP-0225 evidence snapshot: 83c959bc

Source: `83c959bc5ed169ee6338f81e2c3d895ff3c059fb` (clean; not deployed). Tracker status remains **Review**. Last deployment remains `53058498`.

- The latest full Native run remains cb8 `20261103-114404-09062067`: 1,083/1,083 pass. No newer full Native result is claimed.
- Focused Native checks passed 10/10 on `f78586d15eb213b51a14550374f0b92402df0772` (`20261103-120628-31eb2c6a`) and 11/11 on this source (`20261103-122315-d90adf18`). Qualification remains false.
- The earlier actual mode-acquisition run on `a1741c195438810b59f7a082330bff21f84b4dd0` (`20261103-120903-f7659f78`) exposed stale Classic-template selection suppressing Generated full scans. Standard ran cold for 180 seconds without a first composed output. Beta eventually composed, then failed the held/full-scan oracle. The `83c959bc` candidate fixes mode selection and labels current full scans.
- Paired 4K/60 actual-renderer run `20261103-122541-116eae38` showed improved mode acquisition/composition and grouping, but both Standard and Beta still failed the live budget assertions. Only the first 59.94 Hz input-rate case ran; the other three rates were not reached. The test restored the 4K display to 24 Hz afterward.
- Standard: 301 sources, 298 proofs, 54 grouped frames; maximum frame time 12.9055 ms; proof p95/max 14.9399/22.5481 ms.
- Beta: 301 sources, 296 proofs, 55 grouped frames; maximum frame time 19.4062 ms; proof p95/max 15.7625/21.1673 ms.
- This is partial runtime evidence with a failed budget result, not qualification or a renderer pass. No deployment beyond `53058498` is claimed.

Receipts and hashes:

| Receipt | SHA-256 |
| --- | --- |
| `f78586d1-focused-10-iteration.json` | `A95482027A33BBDFA9ABA183BA08597089606BDF3358EB147FAEC962DFE52CBB` |
| `f78586d1-focused-10-standard.trx` | `1444D4CA1ACEEC245E95DD9EDCF801672CC9F722C0C46F949C7B8B0CA5B5EA64` |
| `a1741c19-mode-acquisition-failed.json` | `A7C40B8E225671E0AD8AA7B9C2BBBC319A18865C321E0B99810934F5187FE7E2` |
| `a1741c19-mode-acquisition-standard.trx.zip` | `7918B16EC1F6B5B2B22173A67059631D223E67909701BDDAB979C988546C1364` |
| Embedded original `standard.trx` SHA-256 | `C4646EDAD9B9F470E8D3B257A6D12A316D01110D0C0A8DE5C69DC29A90AA8E81` |
| `83c959bc-focused-11-iteration.json` | `E2AD061868EE4C722543CFD960BC636ECFB58E6B45B8E127CC2214B2607CB293` |
| `83c959bc-focused-11-standard.trx` | `ABC02B8DA6A1D3856744D9D6429174DFD36AAB4A13E7CE54D20F72796DF9C743` |
| `83c959bc-4k60-pair-failed.json` | `825B002EFD778C479FD68932D9428E050795DBF58E7D9DB6B1F41F12940E3B9A` |
| `83c959bc-4k60-pair-standard.trx` | `8CC81E8DC078BBBE0C8D7D7E99D26419C4CE30323690B7CC55E4656F2FD64095` |

The unusually large a174 TRX is stored as a ZIP to keep the tracker checkout compact; its embedded original file hash was checked after extraction.
