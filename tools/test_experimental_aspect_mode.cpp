#include "../src/VideoProcessor-Lib/vprenderer/ExperimentalAspectMode.h"
#include <cstdio>
using namespace ExperimentalAspect;
int main(){
    int failures=0;
    auto check=[&](bool ok,const char* text){std::printf("%s %s\n",ok?"PASS":"FAIL",text);if(!ok)++failures;};
    auto expect=[](const char* value, TrialMode expected) {
        return SetEnvironmentVariableA("VP_AR_EXPERIMENT",value) &&
            ReadTrialMode().mode==expected;
    };
    SetEnvironmentVariableA("VP_AR_EXPERIMENT",nullptr);
    check(ReadTrialMode().mode==TrialMode::Active,"direct launch activates detector");
    check(expect("off",TrialMode::Off),"explicit control");
    check(expect("shadow",TrialMode::Shadow),"explicit shadow comparison");
    check(expect("active",TrialMode::Active),"explicit active");
    check(expect("invalid",TrialMode::Active),"invalid override keeps production default");
    check(expect("this-environment-value-is-too-long-for-buffer",TrialMode::Active),"oversized override keeps production default");
    return failures?1:0;
}
