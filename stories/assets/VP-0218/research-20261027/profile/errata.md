Protocol/results corrections only; no detector thresholds or verdict changed.

1. The script hashed all 97 native capture payload entries and selected one payload for each unique SHA-256. There are four unique payloads; these happen to be files 1–4, with repetition counts 24/24/24/25. The original protocol sentence “original first four native capture payloads” should be read as “first payload for each unique hash after scanning the full manifest.”
2. In boundary-profile-results.json, the bottom `transition_outside_row` value stores `r=1856`, which is the inside row for the bottom scan. Its neighboring outside row is 1857. Paired coordinates are inside=1856/outside=1857. The separately recorded bottom crop coordinate is still 1865 and all support/outer-band measurements are unchanged.
