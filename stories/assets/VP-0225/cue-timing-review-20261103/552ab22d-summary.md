# VP-0225 evidence snapshot: 552ab22d

Source: `552ab22dfad780b31b3f7a4754e9162ef052c3cd` (clean; not deployed). Tracker status remains **Review**. Last deployed build remains `53058498`.

- Native lane: 1,082/1,082 pass (`20261103-100504-13e17543`). This is not full qualification.
- Focused Native set: 11/11 pass (`20261103-100221-77319040`). This is not full qualification.
- Actual Standard/Beta long-pair run: failed (`20261103-100307-2f2b4620`). Standard source 15 successfully consumed the exact-current proof and composed the correct crop (flags `16777315`, cue/certificate source 15). Worker-proof maximum was 13.690 ms Standard / 12.997 ms Beta; receipt binding was 0.073 / 0.068 ms. On source 16, raw detection still found three lines, but cleanup plus normal semantic cue handoff returned `glyph-proof-lost-or-changed`; composition dropped on the submitted source. This remains a product failure, not an oracle waiver.
- Next candidate `dbfbb200` adds an output-only adapter; focused Native validation was pending when this snapshot was recorded.

Copied receipts were byte-verified against their source files by SHA-256:

| Receipt | SHA-256 |
| --- | --- |
| `552ab22d-focused-11-iteration.json` | `78D1E95740B7C1E1DE4F004B3CE315700CAE5BFD517E3137593C80AC00894A6E` |
| `552ab22d-focused-11-standard.trx` | `034DF8336AD3582618DDFB02EC1C7A91C18F4C7806918A4E8EED6A8C83111C75` |
| `552ab22d-native-1082-iteration.json` | `89D1365F0E9F34EAF338065986D1210B62447D4E4D7B8BBAE59D286C8D855398` |
| `552ab22d-native-1082-standard.trx` | `BA074ADBADCB65CDA95394B6C958582F039ED4C6635C31CD0FB027FF56CD1137` |
| `552ab22d-longpair-failed.json` | `9B171534BE30F78FD0315927AB2A990C2C7E10759820DA26BC94F4627E92FE59` |
| `552ab22d-longpair-standard.trx` | `158C76FE03CE2EF9F150F9CB908FF176F41FB6D1ED4D3766D613BBB4FB3E045C` |