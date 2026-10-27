from pathlib import Path
p=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\native-decode-probe.cpp')
s=p.read_text()
s=s.replace('global_nearblack,global_cutoff,ar_distributed_dark\\n";', 'global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid,retention_presentation_valid,retention_pixel_safe,retention_can_retain,retention_outward_visible,retention_outward_top,retention_outward_bottom,retention_reason\\n";')
s=s.replace('auto e=ExtractActivePictureEvidence(s);auto g=EvaluateActivePictureGlobalNearBlack(s);auto d=EvaluateActivePictureArDarkness(s,e,g);', 'auto e=ExtractActivePictureEvidence(s);auto g=EvaluateActivePictureGlobalNearBlack(s);auto d=EvaluateActivePictureArDarkness(s,e,g);auto retention=EvaluateActivePicturePresentationRetention(s,e.trustedBounds,e,g,d);')
s=s.replace("<<d.distributedDarkPicture<<'\\n';", "<<d.distributedDarkPicture<<','<<retention.analysisValid<<','<<retention.presentationValid<<','<<retention.currentlyPixelSafe<<','<<retention.CanRetainPresentation()<<','<<retention.outwardVisibleBoundsAvailable<<','<<retention.outwardVisibleBounds.top<<','<<retention.outwardVisibleBounds.bottom<<\",\\\"\"<<retention.reason<<\"\\\"\\n\";")
p.write_text(s)
print('\n'.join(line for line in s.splitlines() if 'retention' in line))
