# Fox-to-Alien opening retention review

Running PID 33108, launched 11:21:18. Host SHA256 AC82E6218ACFF520A879FFBA2D9C3569C79DE20FF2D7A2106E3076CC33EB3C33; renderer 57E819009C2748F66207E0CFB3E287FC47A5EC06D626D0F3560CE32F1B015203. No deployment or configuration change in this review. Shadow diagnostics have policy_effect=none.

Renderer instance {391336C1-34B6-4608-9E4A-D820A32EC1B9}:
- 13:25:01 sequence 277: applied established scope contract 0,284-3840,1876 (fill presentation 50,284-3790,1876).
- 13:25:06 sequence 404: released to full raster. Broad excluded bands safe, no bounded outward visible witness, provisional proposal 972,660-3840,1880, only outward excursion bottom +4. Inward top/left geometry is very different. Inspection expired, scene event ended near-black retention, non-contained proposal prevents retention. No subtitle shift.
- 13:25:51 sequence 1496: scope restored, recovery duration 45547ms.
- 13:26:21 sequence 2200: distinct release with unsafe excluded bands and outward witness reaching source top y=0; consistent with user's confirmation that showing OSD triggers release, but log does not semantically identify OSD.
- 13:26:45 sequence 2782 scope restored; 13:26:58 sequence 3084 released again; 13:27:07 sequence 3301 scope restored.

Code finding: ActivePictureEvidence.cpp IsVerticalSamplingProposal requires absolute top AND bottom changes <= one scan step (4px at 2160p). Therefore the 13:25:06 partial dark-picture proposal fails sampling-equivalence even though its only OUTWARD excursion is 4px. Broad-band safety alone does not prove disputed rows safe; narrow-strip sampling was not run here (sampling_equivalent=0). Existing expansion-strip p90=112 suggests real near-edge pixels, so do not assert simple tolerance relaxation is proven safe. This is a targeted retention test candidate, separate from acquiring new scope using motion evidence. No behavioral changes made.

User replay initially kept scope, then confirmed OSD causes release. Do not attribute the earlier 13:25:06 event to OSD without matched evidence. Preserve current guards and distinguish overlay handling from ambiguous retention.
