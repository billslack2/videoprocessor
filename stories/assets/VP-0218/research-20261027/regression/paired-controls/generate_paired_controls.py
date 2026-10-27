from pathlib import Path
import json,hashlib
import numpy as np
profile=Path(r'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile\boundary_profile_experiment.py')
src=profile.read_text(encoding='utf-8'); pre=src.split('# Use each unique native payload once',1)[0]; ns={};exec(compile(pre,str(profile),'exec'),ns)
out=Path(__file__).resolve().parent
kinds=('gradient_matte','authored_ceiling_identical_pixels','fine_caption','colored_object','broad_low_contrast','gradient_noise')
manifest=[]
for kind in kinds:
 P=ns['synthetic'](kind); profile_result=ns['analyze'](P,kind)
 Y,U,V=(P[k].astype(np.uint16) for k in ('y','u','v'))
 H,W=Y.shape
 uv=np.empty((H,W//2,2),np.uint16);uv[:,:,0]=U[:,::2];uv[:,:,1]=V[:,::2]
 y_bytes=(Y<<6).astype('<u2').tobytes()
 uv_bytes=(uv<<6).astype('<u2').tobytes()
 raw=y_bytes+uv_bytes # P210 stores the full Y plane followed by the full interleaved UV plane.
 # Independently decode the serialized planes and assert equality with every source plane.
 raw16=np.frombuffer(raw,dtype='<u2'); y2=(raw16[:H*W].reshape(H,W)>>6).astype(np.int16); uv2=(raw16[H*W:].reshape(H,W//2,2)>>6).astype(np.int16)
 u2=np.repeat(uv2[:,:,0],2,axis=1);v2=np.repeat(uv2[:,:,1],2,axis=1)
 assert np.array_equal(y2,P['y']) and np.array_equal(u2,P['u']) and np.array_equal(v2,P['v']),f'Python P210 roundtrip failed for {kind}'
 p=out/(kind+'.p210');p.write_bytes(raw)
 expected=np.concatenate((P['y'].astype('<i2').reshape(-1),P['u'].astype('<i2').reshape(-1),P['v'].astype('<i2').reshape(-1))).astype('<i2').tobytes()
 ep=out/(kind+'.expected');ep.write_bytes(expected)
 manifest.append({'kind':kind,'file':p.name,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'expected_file':ep.name,'expected_sha256':hashlib.sha256(expected).hexdigest(),'python_roundtrip':'all Y/U/V samples exact','profile_edge_pass':profile_result['edge_pass'],'profile_route_pass':profile_result['route_pass'],'profile_top':profile_result['edges']['top'],'profile_bottom':profile_result['edges']['bottom'],'profile_top_exterior':profile_result['exteriors']['top'],'profile_bottom_exterior':profile_result['exteriors']['bottom']})
(out/'manifest.json').write_text(json.dumps({'width':1280,'height':720,'rowBytes':2560,'chromaRowBytes':2560,'layout':'P210 Y plane then interleaved UV plane, high 10 bits in 16-bit lanes','controls':manifest},indent=2))
print(json.dumps({'count':len(manifest),'layout':'planar Y followed by interleaved UV','python_roundtrip':'all Y/U/V samples exact','out':str(out),'sha256':[x['sha256'] for x in manifest]},indent=2))
