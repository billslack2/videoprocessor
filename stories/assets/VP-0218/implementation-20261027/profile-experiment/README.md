# VP-0218 bounded profile experiment

Run with `C:\Program Files\Python310\python.exe profile_experiment.py`. Inputs: the saved first-frame native Y/U/V planes in the local diagnostic archive. Output: `experiment.json`.

Before evaluating pixels, the script fixes a 16-cell horizontal model, samples every 8 columns, fits a constant plus linear depth term per cell from every fourth exterior depth while excluding the last 8 rows, and validates on every remaining sampled depth. Top and bottom boundaries are independently nominated by a 20-code positive luma step with 16-code chroma corroboration in at least 70% of samples in each of four quartiles. The candidate retains an 8-row uncertainty fringe. Fixed profile limits: Y heldout p95 <=4 and max <=8; U/V residual max <=8; at least 95% of samples within 16 codes of neutral chroma; no 4-connected component of at least 8 cells with Y residual >=12. These are experiment filters, not proposed production thresholds.

The ad nominates top transition row 300 and bottom row 1856 independently. Keeping eight rows visible yields candidate top 292 and bottom 1864. Bottom profile passes the frozen filter. Top fails clearly: heldout Y p95 17.41, max 90.21; maximum chroma residual 9.49; many large connected residual components. Neutrality passes at 100%. Thus even after retaining fringe, the captured top is not safely explained by this simple profile.

Synthetic gradient matte passes. A synthetic connected object fails through large local residuals. A steeper synthetic ceiling/floor fails. A low-slope authored ceiling with the same profile as the accepted gradient matte also passes: pixel-only profile fit cannot distinguish it from matte. These synthetic cases demonstrate a detector limit, not a semantic label for the captured ad.

No production files, configuration, player, or Git state were changed. The experiment does not qualify a shipping variable-profile route.
