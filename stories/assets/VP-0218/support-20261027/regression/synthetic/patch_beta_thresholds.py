from pathlib import Path
p=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\synthetic\synthetic-beta-probe.cpp');lines=p.read_text().splitlines()
for i,l in enumerate(lines):
 if 'out<<"kind,sampler_all_pixels_exact' in l:
  lines[i]=l.replace('reason,global_nearblack','reason,black_reference_mode,black_floor,black_threshold,global_nearblack')
 if l.strip().startswith('out<<"\\\""<<cnames[idx++]'):
  lines[i]=l.replace('<<raw.reason<<"\\\","<<global.nearBlack', '<<raw.reason<<"\\\","<<ActivePictureBlackReferenceModeName(raw.blackReference.mode)<<"\\\","<<raw.blackReference.floor<<\',\'<<raw.blackReference.threshold<<\',\'<<global.nearBlack')
p.write_text('\n'.join(lines)+'\n')
