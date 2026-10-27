from pathlib import Path
p=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\native-decode-probe.cpp');s=p.read_text()
s=s.replace('reason,global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid','reason,black_reference_mode,black_observed_floor,black_observed_threshold,black_floor,black_threshold,global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid')
s=s.replace('<<"\\\",\\\""<<e.reason<<"\\\",\"<<g.nearBlack', '<<"\\\",\\\""<<e.reason<<"\\\",\\\""<<ActivePictureBlackReferenceModeName(e.blackReference.mode)<<"\\\",\"<<e.blackReference.observedFloor<<\',\'<<e.blackReference.observedThreshold<<\',\'<<e.blackReference.floor<<\',\'<<e.blackReference.threshold<<\',\'<<g.nearBlack')
p.write_text(s)
print('\n'.join(x for x in s.splitlines() if 'black_reference_mode' in x or 'ActivePictureBlackReferenceModeName' in x))
