#pragma once
#include <Windows.h>
#include <string>

namespace ExperimentalAspect {
enum class TrialMode { Off, Shadow, Active };
struct TrialModeSelection { TrialMode mode=TrialMode::Active; const char* source="default"; };
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
// Diagnostics may explicitly select the control or shadow comparison.
inline TrialModeSelection ReadTrialMode() {
    char environment[32]{};
    const DWORD count=GetEnvironmentVariableA("VP_AR_EXPERIMENT",environment,sizeof(environment));
    if (!count) return {};
    TrialMode mode;
    if (count < sizeof(environment) && ParseTrialMode(environment,mode))
        return {mode,"environment"};
    return {TrialMode::Active,"invalid-environment"};
}
}
