#include "../src/VideoProcessor-Lib/vprenderer/ExperimentalAspectMode.h"
#include <cstdio>
#include <string>
using namespace ExperimentalAspect;
int main(){
    int failures=0;
    auto check=[&](bool ok,const char* text){std::printf("%s %s\n",ok?"PASS":"FAIL",text);if(!ok)++failures;};
    check(ResolveTrialMode(nullptr,nullptr).mode==TrialMode::Off,"ordinary installation is off");
    check(ResolveTrialMode(nullptr,"active\r\n").mode==TrialMode::Active,"trial marker persists active mode");
    check(ResolveTrialMode("active","off").mode==TrialMode::Active,"explicit active overrides marker");
    check(ResolveTrialMode("off","active").mode==TrialMode::Off,"explicit control overrides active marker");
    check(ResolveTrialMode("shadow","active").mode==TrialMode::Shadow,"explicit shadow overrides active marker");
    check(ResolveTrialMode("invalid","active").mode==TrialMode::Off,"invalid environment fails closed");
    check(ResolveTrialMode(nullptr,"invalid").mode==TrialMode::Off,"invalid marker fails closed");
    check(ResolveTrialMode("","active").mode==TrialMode::Active,"empty environment uses trial marker");
    wchar_t host[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,host,MAX_PATH);
    if(!length||length>=MAX_PATH)return 2;
    std::wstring marker(host,length);marker.resize(marker.find_last_of(L"\\/")+1);marker+=L"vp-ar-trial.mode";
    HANDLE file=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){std::puts("FAIL mode test requires no pre-existing marker beside its executable");return 2;}
    const char value[]="active\n";DWORD written=0;
    const bool wrote=WriteFile(file,value,sizeof(value)-1,&written,nullptr)&&written==sizeof(value)-1;
    CloseHandle(file);
    SetEnvironmentVariableA("VP_AR_EXPERIMENT",nullptr);
    const auto fromMarker=ReadTrialMode();
    check(wrote&&fromMarker.mode==TrialMode::Active&&std::strcmp(fromMarker.source,"trial-marker")==0,
        "startup resolves marker beside host executable");
    SetEnvironmentVariableA("VP_AR_EXPERIMENT","off");
    check(ReadTrialMode().mode==TrialMode::Off,"control launch disables real marker");
    SetEnvironmentVariableA("VP_AR_EXPERIMENT",nullptr);
    check(ReadTrialMode().mode==TrialMode::Active,"ordinary restart resolution preserves trial mode");
    const bool removed=DeleteFileW(marker.c_str())!=FALSE;
    check(removed&&ReadTrialMode().mode==TrialMode::Off,"without marker startup returns to off");
    return failures?1:0;
}
