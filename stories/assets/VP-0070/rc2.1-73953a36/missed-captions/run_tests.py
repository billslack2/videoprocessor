from pathlib import Path
import subprocess,json,hashlib
p=Path(__file__).resolve().parent
from PIL import Image
rows=[]
for label,w,h in [('stabilizers',2008,1112),('pitch',2005,1121)]:
 (p/(label+'.bgra')).write_bytes(Image.open(p/(label+'.png')).convert('RGB').tobytes('raw','BGRX'))
 result=subprocess.run([str(p/'bin/IndependentSubtitleReplay.exe'),str(p/(label+'.bgra')),str(w),str(h),'140','978'],capture_output=True,text=True,check=True)
 assert result.stdout.count('detected=1')==2 and 'limit=1' not in result.stdout
 rows.append({'fixture':label,'width':w,'height':h,'picture_top':140,'picture_bottom':978,'png_sha256':hashlib.sha256((p/(label+'.png')).read_bytes()).hexdigest(),'output':result.stdout})
report={'source_commit':'73953a364058b1cf6e5875dfc62e32df4e3bdcd5','cases':rows,'context_correction':'User used 16:9 for screenshots; logs confirm 1.77778 and automatic crop off. No normal-scope regression established.','result':'Both screenshots detect at measured boundary and one-pixel inward perturbation. Runtime logs instead report analysis_reason=not-analyzed and bar_reason=shared-classic-authority-unavailable.','limitations':'Rendered screenshot SDR replay at screenshot resolution; supplied picture bounds. Does not reproduce HDMI framing-state history and is not a full pipeline regression pass.'}
(p/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print('4/4 detector replay cases passed')
