from pathlib import Path
import sys, json, hashlib
import numpy as np
from scipy import ndimage
profile=Path(r'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile\boundary_profile_experiment.py')
src=profile.read_text(encoding='utf-8')
pre=src.split('# Use each unique native payload once',1)[0]
ns={}
exec(compile(pre,str(profile),'exec'),ns)
out=Path(__file__).resolve().parent
kinds=('gradient_matte','authored_ceiling_identical_pixels','fine_caption','colored_object','broad_low_contrast','gradient_noise')
manifest=[]
for kind in kinds:
    P=ns['synthetic'](kind); res=ns['analyze'](P,kind)
    Y,U,V=(P[k].astype(np.uint16) for k in ('y','u','v'))
    # P210 high 10-bit codes in 16-bit little-endian lanes; chroma is 4:2:2, one U/V pair per two luma pixels.
    uv=np.empty((Y.shape[0],Y.shape[1]//2,2),np.uint16)
    uv[:,:,0]=U[:,::2]; uv[:,:,1]=V[:,::2]
    raw=np.concatenate((Y<<6,uv.reshape(Y.shape[0],-1)<<6),axis=1).astype('<u2').tobytes()
    path=out/(kind+'.p210')
    path.write_bytes(raw)
    manifest.append({'kind':kind,'file':path.name,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'profile_edge_pass':res['edge_pass'],'profile_route_pass':res['route_pass'],'profile_top':res['edges']['top'],'profile_bottom':res['edges']['bottom'],'profile_top_exterior':res['exteriors']['top'],'profile_bottom_exterior':res['exteriors']['bottom']})
(out/'manifest.json').write_text(json.dumps({'width':1280,'height':720,'rowBytes':2560,'chromaRowBytes':2560,'layout':'P210 Y plane then interleaved UV pairs, high 10 bits in 16-bit lanes','controls':manifest},indent=2))
print(json.dumps({'count':len(manifest),'out':str(out),'sha256':[x['sha256'] for x in manifest]},indent=2))
