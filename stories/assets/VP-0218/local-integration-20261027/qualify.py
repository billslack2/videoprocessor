from pathlib import Path
import hashlib,json,subprocess,xml.etree.ElementTree as ET
root=Path(r'E:\codex\videoprocessor\vp-0218-local-integration-20261027');out=Path(r'E:\codex\diagnostics\vp-0218-local-integration-20261027');expected='f4219a7aa679c57f1f6d637cb08406b9c2b4b97b'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest().upper()
def read(path):
 tree=ET.parse(path).getroot();defs={u.attrib['id']:u.find('{*}TestMethod').attrib['name'] for u in tree.findall('./{*}TestDefinitions/{*}UnitTest')};results={}
 for item in tree.findall('./{*}Results/{*}UnitTestResult'):
  name=defs[item.attrib['testId']];assert name not in results,name;results[name]=item.attrib['outcome']
 counters=tree.find('./{*}ResultSummary/{*}Counters').attrib
 assert len(results)==int(counters['total']),str(path)
 return results,counters
build=json.loads((out/'build-result.json').read_text(encoding='utf-8-sig'));run=json.loads((out/'native-test-result.json').read_text(encoding='utf-8-sig'));inv=json.loads((out/'native-test-invocation.json').read_text(encoding='utf-8-sig'));cfg=json.loads((out/'config-test-result.json').read_text(encoding='utf-8-sig'))
assert build['exitCode']==run['exitCode']==cfg['exitCode']==0
assert build['sourceCommit']==run['sourceCommit']==cfg['sourceCommit']==inv['sourceCommit']==expected
assert build['configuration']=='Release' and build['architecture']=='x64'
assert not any('TestCaseFilter' in a for a in inv['arguments'])
assert sha(root/'x64/Release/VideoProcessor-Test.dll')==run['testAssemblySha256']==inv['testAssemblySha256']
assert sha(root/'x64/Release/VideoProcessorConfigTests.exe')==cfg['testExecutableSha256']
current,counters=read(out/'combined-native-full.trx');assert all(x=='Passed' for x in current.values());assert int(counters['passed'])==len(current)
prior={};paths=[]
for name in ['core-full.trx','gpu-full.trx','final-repairs.trx']:
 p=Path(r'E:\codex\diagnostics\vp-0218-implementation-20261027\candidate')/name;r,_=read(p);prior.update(r);paths.append({'path':str(p),'sha256':sha(p)})
missing=sorted(set(prior)-set(current));assert not missing,missing
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()==expected
assert not subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()
report={'status':'passed','sourceCommit':expected,'localBase':'f22e22c4f0dcd9be87c26189fc18a83a798c9a26','total':len(current),'passed':len(current),'counters':counters,'trxSha256':sha(out/'combined-native-full.trx'),'testAssemblySha256':run['testAssemblySha256'],'priorUniqueTests':len(prior),'missingPriorTests':missing,'newTests':sorted(set(current)-set(prior)),'priorRuns':paths,'build':build,'configTest':cfg,'limitations':['Recorded-input test bodies without configured inputs do not establish native capture replay.','Functional tests are not isolated performance measurements or moving-scene visual acceptance.']}
(out/'qualification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'total':len(current),'prior':len(prior),'missing':missing,'new':report['newTests']},indent=2))
