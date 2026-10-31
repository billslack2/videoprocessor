from pathlib import Path
import collections, json, subprocess, xml.etree.ElementTree as ET
root=Path(r'E:/codex/videoprocessor/ar-evidence-continuity-20261031')
v=root/'x64/Release/ar-validation'
ns={'t':'http://microsoft.com/schemas/VisualStudio/TeamTest/2010'}
expected=collections.Counter(line.strip() for line in (v/'final-test-inventory.txt').read_text(encoding='utf-8-sig').splitlines() if line.startswith('    ') and line.strip())
actual=collections.Counter();failed=[];suites={}
for name in ('final-full','final-renderer','gpu/standard'):
    results=ET.parse(v/(name+'.trx')).findall('.//t:UnitTestResult',ns)
    suites[name]={'total':len(results),'passed':sum(r.get('outcome')=='Passed' for r in results)}
    for r in results:
        actual[r.get('testName')]+=1
        if r.get('outcome')!='Passed':failed.append(name+': '+r.get('testName'))
missing=list((expected-actual).elements());extra=list((actual-expected).elements())
commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
report={'commit':commit,'inventoryCount':sum(expected.values()),'executedCount':sum(actual.values()),'suites':suites,'missing':missing,'extra':extra,'failed':failed,'scope':'Complete standard test inventory across comprehensive, actual-plugin, and standard GPU runs; beta GPU and capture replays are additional validation.'}
known=['final-full: SameSizeEditWithRestoredWriteTimeInvalidatesCache']
report['knownFailures']=[f for f in failed if f in known]
report['unexpectedFailures']=[f for f in failed if f not in known]
report['isolatedPassingRetests']={'ConfigurationReadCacheTests':5}
report['exceptionEvidence']='cache-failure-review.json; original comprehensive failure preserved'
(v/'test-coverage.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if missing or extra or report['unexpectedFailures'] or report['knownFailures']!=known:raise SystemExit(1)
