# VP-0225 user-authorized live trial: d8ae4175

- Deployed source: `d8ae41757d186b26dab08276ef334a34325f6cc5`, clean x64 Release, at 2026-11-03 12:44. Deployment receipt records status `UserAuthorizedLiveTrial`, not full qualification. Process PID 57168 responded at startup.
- The deployment left `VideoProcessor.cfg` and `VideoProcessor.state` unchanged; no profile changes were made. Backup: `C:\Videoprocessor\vp\backups\trial-d8ae4175-20261103-124413`.
- The latest recorded Native run passed 1,083/1,083 on source `b6710bf556cd40d06b0ef469684410ee01ebaa13` (`20261103-123403-c3101484`). This is separate Native evidence; it does not qualify the deployed d8 build.
- The d8 Standard/Beta actual-renderer run (`20261103-123652-71c45a17`) failed both worker-proof timing assertions. It tested only the first 59.94 Hz source-rate case; the other three rates were not run. Each renderer processed 302 distinct sources and returned 299 worker receipts. Standard grouped 56 scans (~11.2/s); Beta grouped 55 (~11.0/s). Main-path maximums were 14.7982 ms (Standard) and 12.5986 ms (Beta), below the 16.683 ms source period. Worker proof maxima were 20.6064 ms and 22.9182 ms, above that period; this is the remaining reported live-budget failure.
- The trial is not runtime-qualified. Story state remains **Review** pending user acceptance; follow-up profiling edits remain unbuilt.

| Receipt | SHA-256 |
| --- | --- |
| `deployment.json` | `FF67808DF38004157B8A487235FEEF4B2B3BBCFA60435FCDA8E7DCC1D5C539AE` |
| `b6710bf-native-1083-standard.trx` | `4AD543AD190091F6B2C4E4074B8856C75641D06D0A2C6E24ED81692F0F6F9669` |
| `b6710bf-native-1083-iteration.json` | `3B165D97269B72EBC17FE34B66E86DBA4BFFA7A7EF12EA6C71F268D606D80ACA` |
| `d8ae4175-actual-pair-failed-standard.trx` | `5F1F71C7357632B418260135755D346DC70A0A2638600199A625CC41DF86A70C` |
| `d8ae4175-actual-pair-failed.json` | `5D00E62EABB3C02E92C19C014523A7393E104245F09EAA59A5525A3B5AB5D71C` |
