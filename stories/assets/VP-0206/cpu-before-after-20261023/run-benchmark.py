import array,ctypes,csv,datetime,hashlib,json,math,os,pathlib,platform,statistics,time
ROOT=pathlib.Path(__file__).resolve().parent
BEFORE=pathlib.Path(r'E:\codex\videoprocessor\vp-0205-deploy-20261023')
AFTER=pathlib.Path(r'E:\codex\videoprocessor\vp-0206-deploy-20261023')
CAPTURE=pathlib.Path(r'C:\Videoprocessor\vp\logs\analysis-20261023-122213-68268-1\frame-000001.bin')
EXPECTED_BEFORE='EB712353D5993799A582AD7443325A084D76B8F4BAED6BE1AA422AB029636689'
EXPECTED_AFTER='2211F1553C2BD85D2F9C168245993BB4CE1EF927D13DF179C3EBDA6380B39AF5'
class Config(ctypes.Structure):
 _fields_=[(n,ctypes.c_int32) for n in ('width','height','format','encoding','colorspace','range','top','bottom')]+[(n,ctypes.c_uint64) for n in ('rowBytes','chromaRowBytes')]
class Outcome(ctypes.Structure):
 _fields_=[('wallMs',ctypes.c_double),('cycles',ctypes.c_uint64)]+[(n,ctypes.c_uint64) for n in ('rawLuma','rawChroma','globalDarkLuma','retentionLuma','retentionChroma')]+[(n,ctypes.c_int32) for n in ('classification','horizontalState','verticalState')]+[('bounds',ctypes.c_int32*4)]+[(n,ctypes.c_int32) for n in ('calibrationMode','effectiveFloor','effectiveCutoff','retentionValid','retentionSafe')]+[('visible',ctypes.c_int32*4)]
assert ctypes.sizeof(Config)==48 and ctypes.sizeof(Outcome)==120
MODES=('raw','retention_precomputed','raw_global_darkness_retention_bundle')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def load(name):
 dll=ctypes.CDLL(str(ROOT/f'bin-{name}'/f'vp-cpu-{name}.dll'))
 dll.BenchCreate.argtypes=[ctypes.POINTER(ctypes.c_uint8),ctypes.c_uint64,ctypes.POINTER(Config)];dll.BenchCreate.restype=ctypes.c_void_p
 dll.BenchDestroy.argtypes=[ctypes.c_void_p];dll.BenchMeasure.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.POINTER(Outcome)];dll.BenchMeasure.restype=ctypes.c_int
 return dll

def synthetic(kind,w,h,top,bottom):
 if kind.startswith('bars_'):floor=int(kind.split('_')[1])
 else:floor=0
 picture=[]
 for phase in range(7):picture.append(array.array('H',[(240+((x//32+phase)%7)*24)<<6 for x in range(w)]).tobytes())
 quiet=array.array('H',[floor<<6])*w
 noisy=[array.array('H',[(64+((x*17+y*11)%13)-6)<<6 for x in range(w)]).tobytes() for y in range(13)] if kind=='noisy_bars_64' else None
 rows=[]
 for y in range(h):
  if kind=='dark_gradient':rows.append(array.array('H',[(61+26*y//(h-1) if x<w//4 else 300)<<6 for x in range(w)]).tobytes())
  elif y<top or y>=bottom:rows.append(noisy[y%13] if noisy else quiet.tobytes())
  else:rows.append(picture[(y//32)%7])
 return b''.join(rows)+(array.array('H',[512<<6])*(w*h)).tobytes()

def outcome_dict(o):
 return dict(classification=o.classification,horizontal_state=o.horizontalState,vertical_state=o.verticalState,bounds=list(o.bounds),raw_luma_reads=o.rawLuma,raw_chroma_reads=o.rawChroma,global_darkness_luma_reads=o.globalDarkLuma,retention_additional_luma_reads=o.retentionLuma,retention_additional_chroma_reads=o.retentionChroma,calibration_mode=o.calibrationMode,effective_floor=o.effectiveFloor,effective_cutoff=o.effectiveCutoff,retention_valid=o.retentionValid,retention_safe=o.retentionSafe,visible=list(o.visible))
def percentile(values,p):return sorted(values)[max(0,math.ceil(len(values)*p)-1)]
def describe(values):return dict(n=len(values),median=statistics.median(values),p95=percentile(values,.95),maximum=max(values),minimum=min(values),mean=statistics.mean(values))
def filetime_cpu():
 k=ctypes.WinDLL('kernel32',use_last_error=True);k.GetCurrentThread.restype=ctypes.c_void_p;k.GetThreadTimes.argtypes=[ctypes.c_void_p]+[ctypes.POINTER(ctypes.c_uint64)]*4
 vals=[ctypes.c_uint64() for _ in range(4)];assert k.GetThreadTimes(k.GetCurrentThread(),*[ctypes.byref(v) for v in vals]);return (vals[2].value+vals[3].value)/10000

before_hash=sha(BEFORE/'x64/Release/VideoProcessor-Lib.lib');assert before_hash==EXPECTED_BEFORE
# Coordinator is invoked only after the parent confirms the exact after Release build completed.
after_hash=sha(AFTER/'x64/Release/VideoProcessor-Lib.lib');assert after_hash==EXPECTED_AFTER
meta=list(map(int,CAPTURE.with_suffix('.txt').read_text().splitlines()[0].split()))
w,h,pitch,uvpitch,fmt,enc,color,top,bottom,script,sample_range=meta
assert (fmt,w,h,pitch,uvpitch,sample_range)==(4,3840,2160,7680,7680,2)
config=Config(w,h,fmt,enc,color,sample_range,top,bottom,pitch,uvpitch)
fixtures=[('captured_paused',CAPTURE.read_bytes())]+[(name,synthetic(name,w,h,top,bottom)) for name in ('bars_0','bars_40','bars_64','noisy_bars_64','dark_gradient')]
libs={name:load(name) for name in ('baseline','after')};contexts={};fixture_meta=[]
try:
 for name,data in fixtures:
  view=(ctypes.c_uint8*len(data)).from_buffer_copy(data);fixture_meta.append(dict(name=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
  for variant,dll in libs.items():
   context=dll.BenchCreate(view,len(data),ctypes.byref(config));assert context,('create failed',name,variant);contexts[name,variant]=context
 def measure(name,variant,mode):
  out=Outcome();assert libs[variant].BenchMeasure(contexts[name,variant],mode,ctypes.byref(out));assert out.wallMs>=0 and out.cycles>0;return out
 # All fixtures and variants are prepared and warmed before any retained sample.
 for trial in range(20):
  for i in range(len(fixtures)):
   name=fixtures[(i+trial)%len(fixtures)][0]
   for m in range(3):
    mode=(m+trial)%3
    for variant in (('baseline','after') if (i+trial+mode)%2==0 else ('after','baseline')):measure(name,variant,mode)
 rows=[];outcomes={};start_cpu=filetime_cpu();start=time.perf_counter();started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()
 for trial in range(101):
  for i in range(len(fixtures)):
   name=fixtures[(i+trial)%len(fixtures)][0]
   for m in range(3):
    mode=(m+trial)%3
    for order,variant in enumerate(('baseline','after') if (i+trial+mode)%2==0 else ('after','baseline')):
     out=measure(name,variant,mode);o=outcome_dict(out);o['actual_new_luma_reads']=out.rawLuma if mode==0 else out.retentionLuma if mode==1 else out.rawLuma+out.globalDarkLuma+out.retentionLuma;o['actual_new_chroma_reads']=out.rawChroma if mode==0 else out.retentionChroma if mode==1 else out.rawChroma+out.retentionChroma;outcomes.setdefault((name,variant,mode),set()).add(json.dumps(o,sort_keys=True))
     rows.append(dict(trial=trial,fixture=name,variant=variant,method=MODES[mode],pair_order=order,wall_ms=out.wallMs,thread_cycles=out.cycles,**{k:v for k,v in o.items() if not isinstance(v,list)}))
 elapsed=time.perf_counter()-start;cpu_ms=filetime_cpu()-start_cpu
 # Formatting and files happen after the complete timed run.
 with (ROOT/'samples.csv').open('w',newline='') as f:
  writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
 summary=[]
 for name,_ in fixtures:
  for mode in range(3):
   entry=dict(fixture=name,method=MODES[mode])
   for variant in ('baseline','after'):
    selected=[r for r in rows if r['fixture']==name and r['variant']==variant and r['method']==MODES[mode]]
    entry[variant]=dict(wall_ms=describe([r['wall_ms'] for r in selected]),thread_cycles=describe([r['thread_cycles'] for r in selected]),outcomes=[json.loads(v) for v in sorted(outcomes[name,variant,mode])])
   entry['median_wall_ratio_after_before']=entry['after']['wall_ms']['median']/entry['baseline']['wall_ms']['median']
   entry['median_cycle_ratio_after_before']=entry['after']['thread_cycles']['median']/entry['baseline']['thread_cycles']['median']
   summary.append(entry)
 result=dict(schema=1,started_utc=started_utc,baseline_source='24e0dd10430e5140c06c1fa1bd106c9bbde61ef7',after_source='4d1d62d0a664ba97137ba89497888bc8fad483a0',baseline_library_sha256=before_hash,after_library_sha256=after_hash,host=dict(platform=platform.platform(),processor=os.environ.get('PROCESSOR_IDENTIFIER',''),logical_cpus=os.cpu_count()),capture=str(CAPTURE),fixtures=fixture_meta,warmups_per_fixture_method_variant=20,measured_trials_per_fixture_method_variant=101,rows=len(rows),coordinator_measurement_wall_seconds=elapsed,coordinator_thread_cpu_ms=cpu_ms,metrics='QPC wall milliseconds inside native wrapper and QueryThreadCycleTime cycle deltas. Cycle measurement includes the two QPC reads; IO/preparation/CSV formatting excluded.',rotation='Every trial rotates fixture/method order; immediate pairs alternate baseline-first/after-first. Both DLLs loaded together on one calling thread.',interpretation='Offline CPU helper workload; no live A/B, frame jitter, Present, GPU or displayed-adoption measurement. Corrected classification can change work and sample counts; review outcomes alongside timing. Maxima include any host scheduling interference. No timing assertion.',summary=summary,files={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'bench-wire.h',ROOT/'bench-native.cpp',ROOT/'bench-native.vcxproj',ROOT/'bin-baseline/vp-cpu-baseline.dll',ROOT/'bin-after/vp-cpu-after.dll')})
 (ROOT/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
 for e in summary:print(e['fixture'],e['method'],'baseline',e['baseline']['wall_ms']['median'],'after',e['after']['wall_ms']['median'],'ratio',e['median_wall_ratio_after_before'])
 print('measurement_wall_seconds',elapsed,'coordinator_thread_cpu_ms',cpu_ms,'rows',len(rows))
finally:
 for (name,variant),context in contexts.items():libs[variant].BenchDestroy(context)