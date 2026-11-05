#pragma once
#include <cstdint>
// User-authorized presentation-only hold during a requested subtitle update.
// Stores no pixels, source evidence, receipt, timer, queue or observation history.
struct SubtitleConfigurationFrameContext {
    uint64_t generation=0,format=0,domain=0,viewport=0;
    int width=0,height=0;
    bool Valid() const {return generation && format && domain && width>0 && height>0;}
    bool operator==(const SubtitleConfigurationFrameContext& b) const {
        return generation==b.generation && format==b.format && domain==b.domain &&
            viewport==b.viewport && width==b.width && height==b.height;
    }
};
template<class Mapping> class SubtitleConfigurationFraming {
    SubtitleConfigurationFrameContext submittedContext{},pendingContext{};
    Mapping submitted{},pending{};
    bool haveSubmitted=false,armed=false;
public:
    void Submitted(const SubtitleConfigurationFrameContext& c,const Mapping& m) {
        if(!c.Valid())return;submittedContext=c;submitted=m;haveSubmitted=true;
    }
    void Request(bool subtitleOnly,bool advancedRequested) {
        armed=subtitleOnly && advancedRequested && haveSubmitted;
        if(armed){pendingContext=submittedContext;pending=submitted;}
    }
    void Reset(){armed=false;haveSubmitted=false;}
    bool Armed() const{return armed;}
    const Mapping* Select(const SubtitleConfigurationFrameContext& c,bool resync,
        bool requestedReady,bool confirmedPictureChanged) {
        if(resync || !c.Valid() || (haveSubmitted && !(c==submittedContext))) {
            Reset();return nullptr;
        }
        if(!armed)return nullptr;
        if(!(c==pendingContext) || confirmedPictureChanged) {
            Reset();return nullptr;
        }
        if(requestedReady){armed=false;return nullptr;}
        return &pending;
    }
    const Mapping* Pending() const{return armed?&pending:nullptr;}
};
