from pathlib import Path
p=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\synthetic\synthetic-beta-probe.cpp')
s=p.read_text();old='<<raw.reason<<"\\\",\"<<ActivePictureBlackReferenceModeName';new='<<raw.reason<<"\\\",\\\""<<ActivePictureBlackReferenceModeName';print('old?',old in s);s=s.replace(old,new);p.write_text(s)
