import argparse, hashlib, json, re, subprocess, xml.etree.ElementTree as ET
from datetime import datetime
from pathlib import Path

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read_trx(path):
    path=Path(path)
    root=ET.parse(path).getroot()
    definitions={u.attrib['id']: u.find('{*}TestMethod').attrib['name'] for u in root.findall('./{*}TestDefinitions/{*}UnitTest')}
    counters=root.find('./{*}ResultSummary/{*}Counters').attrib
    times=root.find('./{*}Times').attrib
    results={}
    for item in root.findall('./{*}Results/{*}UnitTestResult'):
        name=definitions[item.attrib['testId']]
        if name in results: raise RuntimeError('Duplicate test identity: '+name)
        error=item.find('./{*}Output/{*}ErrorInfo/{*}Message')
        results[name]={'outcome':item.attrib['outcome'],'sourceRun':str(path),'error':error.text if error is not None else None}
    if len(results)!=int(counters['total']): raise RuntimeError('Incomplete TRX inventory: '+str(path))
    return {'path':str(path),'sha256':sha(path),'counters':dict(counters),'times':dict(times)},results

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--source-root',required=True)
    ap.add_argument('--expected-commit',required=True)
    ap.add_argument('--tested-commit',required=True)
    ap.add_argument('--baseline',required=True)
    ap.add_argument('--expected-total',type=int,required=True)
    ap.add_argument('--runs',nargs='+',required=True)
    ap.add_argument('--output',required=True)
    a=ap.parse_args()
    def git(*args): return subprocess.check_output(['git','-C',a.source_root,*args],text=True).strip()
    if git('rev-parse','HEAD')!=a.expected_commit or git('status','--porcelain'):
        raise RuntimeError('Final source is not the expected clean commit')
    changes=git('diff','--name-only',a.tested_commit,a.expected_commit).splitlines()
    allowed={'src/VideoProcessor-Test/FitLookaheadDiagnosticTests.cpp','src/VideoProcessor-Test/LibplaceboLutParserTests.cpp'}
    if set(changes)-allowed: raise RuntimeError('Production source changed after broad tests: '+str(changes))
    baseline,baseline_results=read_trx(a.baseline)
    runs=[]; effective={}; superseded=[]
    if len(a.runs)!=3: raise RuntimeError('Exactly two broad runs and one final repair run are required')
    seen_hashes=set()
    for index,path in enumerate(a.runs):
        record,results=read_trx(path)
        if Path(path).resolve()==Path(a.baseline).resolve() or record['sha256']==baseline['sha256']:
            raise RuntimeError('Baseline TRX cannot serve as candidate evidence')
        if record['sha256'] in seen_hashes: raise RuntimeError('Repeated TRX cannot serve as a retest')
        seen_hashes.add(record['sha256'])
        expected_source=a.tested_commit if index<2 else a.expected_commit
        expected_name=('core-full.trx','gpu-full.trx','final-repairs.trx')[index]
        if Path(path).name!=expected_name: raise RuntimeError('Unexpected run/order: '+str(path))
        provenance_path=Path(str(path)+'.input.json')
        provenance=json.loads(provenance_path.read_text(encoding='utf-8-sig'))
        build_path=Path(provenance['buildEvidence'])
        if sha(build_path).lower()!=provenance['buildEvidenceSha256'].lower(): raise RuntimeError('Build evidence hash mismatch')
        build=json.loads(build_path.read_text(encoding='utf-8-sig'))
        if provenance['sourceCommit']!=expected_source or build['sourceCommit']!=expected_source:
            raise RuntimeError('Run/build source identity mismatch')
        if build['configuration']!='Release' or build['architecture']!='x64' or build['exitCode']!=0:
            raise RuntimeError('Missing successful x64 Release build evidence')
        if provenance['testAssemblySha256'].lower()!=build['testAssemblySha256'].lower():
            raise RuntimeError('Test assembly hash differs from build evidence')
        if sha(build['buildRecord']).lower()!=build['buildRecordSha256'].lower(): raise RuntimeError('Build record hash changed')
        build_record=json.loads(Path(build['buildRecord']).read_text(encoding='utf-8-sig'))
        if build_record['sourceCommit']!=expected_source or build_record['exitCode']!=0 or build_record['configuration']!='Release' or build_record['architecture']!='x64':
            raise RuntimeError('Original build record disagrees')
        expected_dll=Path(a.source_root)/'x64'/'Release'/'VideoProcessor-Test.dll'
        if Path(provenance['testAssembly']).resolve()!=expected_dll.resolve(): raise RuntimeError('Unexpected test assembly path')
        def stamp(value): return datetime.fromisoformat(re.sub(r'(\.\d{6})\d+', r'\1', value.replace('Z','+00:00')))
        if stamp(record['times']['start'])<stamp(build['completedUtc']): raise RuntimeError('Test run predates its build')
        if 'testAssemblySha256' in build_record and build_record['testAssemblySha256'].lower()!=provenance['testAssemblySha256'].lower():
            raise RuntimeError('Original build record test assembly mismatch')
        if index==2:
            prepared=stamp(provenance['preparedUtc'])
            if prepared<stamp(build['completedUtc']) or stamp(record['times']['start'])<prepared:
                raise RuntimeError('Final retest predates its prepared invocation')
            run_receipt_path=Path(str(path)+'.result.json')
            run_receipt=json.loads(run_receipt_path.read_text(encoding='utf-8-sig'))
            if (run_receipt['runId']!=provenance['runId'] or run_receipt['sourceCommit']!=expected_source or
                run_receipt['exitCode']!=0 or run_receipt['testAssemblySha256'].lower()!=provenance['testAssemblySha256'].lower() or
                stamp(run_receipt['completedUtc'])<stamp(record['times']['finish'])):
                raise RuntimeError('Final retest did not complete successfully with the prepared source/assembly')
            record['executionReceipt']={'path':str(run_receipt_path),'sha256':sha(run_receipt_path),**run_receipt}
            if sha(expected_dll).lower()!=provenance['testAssemblySha256'].lower(): raise RuntimeError('Final test assembly changed')
            allowed_retests=('Tests::FitLookaheadDiagnosticTests::',
                'VideoProcessorTest::LibplaceboLutParserTests::BlackPanelCoversLastPictureRowWithSquareLowerCorners',
                'VideoProcessorTest::LibplaceboLutParserTests::MeasuredCardEdgesRestoreOutsideSamplingRectangleAtEveryDisplayPadding')
            if any(not (name.startswith(allowed_retests[0]) or name in allowed_retests[1:]) for name in results):
                raise RuntimeError('Unexpected overlay outside corrected test cases')
        record['inputProvenance']={'path':str(provenance_path),'sha256':sha(provenance_path),**provenance}
        runs.append(record)
        for name,result in results.items():
            if name in effective and effective[name]['outcome']!='Passed':
                superseded.append({'name':name,'original':effective[name],'retest':result})
            effective[name]=result
    missing=sorted(set(baseline_results)-set(effective))
    failures=[{'name':name,**result} for name,result in effective.items() if result['outcome']!='Passed']
    complete=len(effective)==a.expected_total and not missing and not failures
    report={'sourceCommit':a.expected_commit,'broadTestSourceCommit':a.tested_commit,'productionSourceUnchangedSinceBroadTests':True,
        'testOnlyChangesAfterBroadTests':changes,'baseline':baseline,'baselineFailures':[{'name':n,**r} for n,r in baseline_results.items() if r['outcome']!='Passed'],'runsInEffectiveOrder':runs,'uniqueTests':len(effective),
        'expectedTotal':a.expected_total,'missingBaselineTests':missing,'newTests':sorted(set(effective)-set(baseline_results)),
        'passed':sum(r['outcome']=='Passed' for r in effective.values()),'failures':failures,
        'originalFailedResultsPreserved':superseded,'status':'passed' if complete else 'not-qualified',
        'limitations':['Optional recorded-input test bodies may return without exercising unavailable captures; a Passed result does not establish physical playback acceptance.','Concurrent GPU timings are not isolated performance measurements.']}
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':report['status'],'uniqueTests':len(effective),'passed':report['passed'],'failed':len(failures),'missingBaselineTests':len(missing)}))
    if not complete: raise SystemExit(1)

if __name__=='__main__': main()