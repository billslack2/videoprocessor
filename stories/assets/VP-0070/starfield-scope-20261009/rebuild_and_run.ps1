$ErrorActionPreference='Stop'
$replayRoot=$PSScriptRoot
foreach($variant in @('baseline','candidate')) {
    Copy-Item -LiteralPath "$replayRoot\recovery-$variant.h" -Destination "$replayRoot\recovery-under-test.h"
    & 'C:/Program Files/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/MSBuild.exe' "$replayRoot\replay.vcxproj" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:q /nologo
    if($LASTEXITCODE -ne 0) { throw "Replay build failed: $variant" }
    Copy-Item -LiteralPath "$replayRoot\bin\IndependentSubtitleReplay.exe" -Destination "$replayRoot\bin\IndependentSubtitleReplay-$variant.exe"
}
python "$replayRoot\run_corpus.py"
if($LASTEXITCODE -ne 0) {throw 'Corpus replay failed'}
python "$replayRoot\summarize_corpus.py"
if($LASTEXITCODE -ne 0) {throw 'Corpus assertions failed'}
