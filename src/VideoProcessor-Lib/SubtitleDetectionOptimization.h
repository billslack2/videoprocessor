#pragma once
#include <string>
inline bool ParseSubtitleDetectionOptimization(const std::string& value,int& mode) {
    if(value=="baseline"){mode=0;return true;}
    if(value=="buffers"){mode=1;return true;}
    if(value=="shared_samples"){mode=2;return true;}
    return false;
}
inline const char* SubtitleDetectionOptimizationName(int mode) {
    return mode==2?"shared_samples":mode==1?"buffers":"baseline";
}
