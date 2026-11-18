#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace ExperimentalAspect {
enum class TrialMode { Off, Shadow, Active };
struct TrialModeSelection { TrialMode mode=TrialMode::Off; const char* source="default"; };
inline const char* TrialModeName(TrialMode mode) {
    return mode==TrialMode::Active?"active":mode==TrialMode::Shadow?"shadow":"off";
}
inline bool ParseTrialMode(const char* value, TrialMode& mode) {
    if(!value)return false;
    std::string token(value);
    const auto begin=token.find_first_not_of(" \t\r\n");
    if(begin==std::string::npos)return false;
    const auto end=token.find_last_not_of(" \t\r\n");
    token=token.substr(begin,end-begin+1);
    if(token=="active")mode=TrialMode::Active;
    else if(token=="shadow")mode=TrialMode::Shadow;
    else if(token=="off")mode=TrialMode::Off;
    else return false;
    return true;
}
inline TrialModeSelection ResolveTrialMode(const char* environment, const char* marker) {
    TrialMode mode=TrialMode::Off;
    if(environment&&*environment) {
        if(ParseTrialMode(environment,mode))return {mode,"environment"};
        return {TrialMode::Off,"invalid-environment"};
    }
    if(marker&&*marker) {
        if(ParseTrialMode(marker,mode))return {mode,"trial-marker"};
        return {TrialMode::Off,"invalid-trial-marker"};
    }
    return {};
}
// Startup-only opt-in, rooted beside the host EXE rather than the working
// directory. Normal installations have no marker and remain disabled.
inline TrialModeSelection ReadTrialMode() {
    char environment[32]{};
    const DWORD count=GetEnvironmentVariableA("VP_AR_EXPERIMENT",environment,sizeof(environment));
    if(count>=sizeof(environment))return {TrialMode::Off,"invalid-environment"};
    if(count)return ResolveTrialMode(environment,nullptr);
    wchar_t host[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,host,MAX_PATH);
    if(!length||length>=MAX_PATH)return {};
    std::wstring path(host,length);
    const auto slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return {};
    path.resize(slash+1);path+=L"vp-ar-trial.mode";
    FILE* file=nullptr;
    if(_wfopen_s(&file,path.c_str(),L"rb")||!file)return {};
    char marker[32]{};
    const size_t bytes=std::fread(marker,1,sizeof(marker)-1,file);
    const bool invalid=std::ferror(file)||std::fgetc(file)!=EOF;
    std::fclose(file);
    if(invalid||std::memchr(marker,0,bytes))return {TrialMode::Off,"invalid-trial-marker"};
    return ResolveTrialMode(nullptr,marker);
}
}
