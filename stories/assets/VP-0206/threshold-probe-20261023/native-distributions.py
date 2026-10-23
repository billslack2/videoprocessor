import json,math,sys,statistics,pathlib,array,hashlib,re
capture=pathlib.Path(r'C:\Videoprocessor\vp\logs\analysis-20261023-122213-68268-1')
raw=(capture/'frame-000001.bin').read_bytes()
meta=(capture/'frame-000001.txt').read_text().splitlines()
w,h,stride,uvstride,fmt,enc,color,top,bottom,script,rng=map(int,meta[0].split())
assert (fmt,w,h,stride,uvstride)==(4,3840,2160,7680,7680)
lanes=array.array('H');lanes.frombytes(raw)
if sys.byteorder!='little':lanes.byteswap()
ystride=stride//2;cstride=uvstride//2;uvoffset=ystride*h
# Matches production AnalysisLumaSource::Sample decoded-DV contract: upper10bits of LE16 lanes, no rounding.
def sample(x,y):
 p=uvoffset+y*cstride+(x//2)*2
 return lanes[y*ystride+x]>>6,lanes[p]>>6,lanes[p+1]>>6

def quant(a,q):
 a=sorted(a);return a[int(q*(len(a)-1))]
def channel(a):
 return dict(min=min(a),p10=quant(a,.1),p50=quant(a,.5),p90=quant(a,.9),max=max(a),p90_minus_p10=quant(a,.9)-quant(a,.1),standard_deviation=statistics.pstdev(a))
def describe(coords):
 v=[sample(*xy) for xy in coords];ys,us,vs=zip(*v);ym,um,vm=quant(ys,.5),quant(us,.5),quant(vs,.5)
 return dict(samples=len(v),Y=channel(ys),U=channel(us),V=channel(vs),neutral_chroma_fraction=sum(abs(u-512)<=32 and abs(vv-512)<=32 for _,u,vv in v)/len(v),max_chroma_deviation_from_neutral=max(max(abs(u-512),abs(vv-512)) for _,u,vv in v),fraction_within_4_codes_of_own_median_yuv=sum(abs(yy-ym)<=4 and abs(u-um)<=4 and abs(vv-vm)<=4 for yy,u,vv in v)/len(v),luma_fraction_at_or_below={str(t):sum(y<=t for y in ys)/len(ys) for t in (24,32,48,85)})
xs=[(2*i+1)*w//128 for i in range(64)];ys=[(2*i+1)*h//128 for i in range(64)]
perimeter={'top':[(x,0) for x in xs],'bottom':[(x,h-1) for x in xs],'left':[(0,y) for y in ys],'right':[(w-1,y) for y in ys]}
lines48x=[(2*i+1)*w//96 for i in range(48)];lines48y=[(2*i+1)*h//96 for i in range(48)]
leftdepth=[(2*d+1)*936//12 for d in range(6)];topdepth=[(2*d+1)*274//12 for d in range(6)]
regions={'proposed_left_six_by_48':[(x,y) for x in leftdepth for y in lines48y], 'retained_top_six_by_48':[(x,y) for y in topdepth for x in lines48x], 'retained_bottom_six_by_48':[(x,h-1-y) for y in topdepth for x in lines48x]}
result={'source':str(capture/'frame-000001.bin'),'source_sha256':hashlib.sha256(raw).hexdigest(),'sampler':'Production decoded-DV LE16 >>6 exact code formula; metadata format4/range2. Lower-index percentile identical to production.', 'perimeter_64_per_edge':{k:describe(v) for k,v in perimeter.items()},'combined_perimeter_256':describe(sum(perimeter.values(),[])),'candidate_and_retained_band_grids':{k:describe(v) for k,v in regions.items()},'proposed_left_depth_columns':[],'retained_top_depth_rows':[],'retained_bottom_depth_rows':[]}
for x in leftdepth:result['proposed_left_depth_columns'].append(dict(x=x,**describe([(x,y) for y in lines48y])))
for y in topdepth:result['retained_top_depth_rows'].append(dict(y=y,**describe([(x,y) for x in lines48x])))
for dy in topdepth:
 y=h-1-dy;result['retained_bottom_depth_rows'].append(dict(y=y,**describe([(x,y) for x in lines48x])))
# Metadata-only source-log extracts: preserve actual fields and domain transitions, never invent earlier native pixels.
source_log=pathlib.Path(__file__).parents[1]/'vp-snapshot.log'
lines=source_log.read_text(encoding='utf-8-sig',errors='replace').splitlines();extract=[]
for n,line in enumerate(lines,1):
 if 'Alpha analysis contract:' in line or (('Alpha crop candidate levels:' in line or 'Alpha crop black levels:' in line) and ('classification=2' in line or 'observation_class=2' in line)):
  if ('12:13:5' in line or '12:14:' in line or 'Alpha analysis contract:' in line):extract.append(dict(line=n,text=line))
result['earlier_scope_log_extracts']=extract
out=pathlib.Path(__file__).with_name('native-distributions.json');out.write_text(json.dumps(result,indent=2)+'\n')
for k,v in result['perimeter_64_per_edge'].items():print(k,json.dumps(v))
for k,v in result['candidate_and_retained_band_grids'].items():print(k,json.dumps(v))
print('combined_perimeter',json.dumps(result['combined_perimeter_256']))
print('earlier_scope_rows',len(extract))