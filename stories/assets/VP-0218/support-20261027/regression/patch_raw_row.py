from pathlib import Path
p=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\native-decode-probe.cpp')
lines=p.read_text().splitlines()
for i,l in enumerate(lines):
 if l.strip().startswith('o<<"\\\""<<pixelPath.filename'):
  lines[i]=' o<<"\\\""<<pixelPath.filename().string()<<"\\\","<<w<<\',\'<<hh<<\',\'<<bytes.size()<<\',\'<<fmt<<\',\'<<enc<<\',\'<<color<<\',\'<<hasRange<<\',\'<<int(s.range)<<\',\'<<plane<<\',\'<<cls(e.classification)<<\',\'<<e.proposedBounds.left<<\',\'<<e.proposedBounds.top<<\',\'<<e.proposedBounds.right<<\',\'<<e.proposedBounds.bottom<<\',\'<<e.trustedBounds.left<<\',\'<<e.trustedBounds.top<<\',\'<<e.trustedBounds.right<<\',\'<<e.trustedBounds.bottom<<\',\'<<int(e.trustedBounds.trustedBarAxes)<<",\\\""<<e.reason<<"\\\",\\\""<<ActivePictureBlackReferenceModeName(e.blackReference.mode)<<"\\\","<<e.blackReference.observedFloor<<\',\'<<e.blackReference.observedThreshold<<\',\'<<e.blackReference.floor<<\',\'<<e.blackReference.threshold<<\',\'<<g.nearBlack<<\',\'<<g.lumaCutoff<<\',\'<<d.distributedDarkPicture<<\',\'<<retention.analysisValid<<\',\'<<retention.presentationValid<<\',\'<<retention.currentlyPixelSafe<<\',\'<<retention.CanRetainPresentation()<<\',\'<<retention.outwardVisibleBoundsAvailable<<\',\'<<retention.outwardVisibleBounds.top<<\',\'<<retention.outwardVisibleBounds.bottom<<",\\\""<<retention.reason<<"\\\"\\n";'
  break
else: raise SystemExit('output row not found')
p.write_text('\n'.join(lines)+'\n')
print(lines[i])
