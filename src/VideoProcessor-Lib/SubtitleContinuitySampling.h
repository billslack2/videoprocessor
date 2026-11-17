#pragma once
#include <SubtitleBoxLookahead.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

// Worker-only comparison evidence. The original detector grid/ownership remains
// authoritative for extraction, cleanup and bar proof. This function never
// stores source pixels, interpolates a missing row, or acquires new components.
inline std::shared_ptr<const SubtitleInkSnapshot> BuildCanonicalSubtitleContinuityInk(
    const SubtitleBoxObservation& observation,const AnalysisLumaSource& source,
    size_t maxSamples=8192)
{
    const auto original=observation.ink;
    if(!observation.analyzed || observation.discontinuity || observation.pendingRefresh || observation.analysisRefresh ||
        !source.IsValid() || !original || !original->rawEvidenceComplete || !original->paletteValid ||
        observation.width!=source.width || observation.height!=source.height ||
        observation.pictureTop<0 || observation.pictureBottom>source.height ||
        observation.pictureTop>=observation.pictureBottom)
        return {};
    const auto& old=*original;
    if(old.width<1 || old.height<1 || old.width>960 || old.height>540 || old.step<1 || old.step>8 ||
        old.width!=(source.width+old.step-1)/old.step ||
        old.height!=(source.height+old.step-1)/old.step ||
        old.sourceRows.size()!=size_t(old.height) ||
        old.inkFloor<0 || old.inkFloor>1023 || old.blackLimit<0 || old.blackLimit>=old.inkFloor ||
        old.inkU<0 || old.inkU>1023 || old.inkV<0 || old.inkV>1023)
        return {};
    const size_t area=size_t(old.width)*old.height,words=(area+63)/64;
    if(old.rawInk.size()!=words || old.ownedInk.size()!=words || old.blackBacking.size()!=words)
        return {};
    for(size_t i=0;i<words;++i)
        if(old.ownedInk[i]&~old.rawInk[i])return {};
    if(area%64) {
        const uint64_t valid=(uint64_t{1}<<(area%64))-1;
        if((old.rawInk.back()|old.ownedInk.back()|old.blackBacking.back())&~valid)return {};
    }
    std::array<int,2> changed{};size_t changedCount=0;
    const int topIndex=(observation.pictureTop+old.step-1)/old.step-1;
    const int bottomIndex=(std::min)(old.height-1,(observation.pictureBottom+old.step-1)/old.step);
    for(int y=0;y<old.height;++y) {
        const int actual=old.sourceRows[size_t(y)],canonical=y*old.step;
        if(actual<0 || actual>=source.height || (y && actual<=old.sourceRows[size_t(y-1)]))return {};
        if(actual==canonical)continue;
        // Only the detector's two explicit edge-row substitutions qualify.
        // Arbitrary/nonlocal grids cannot be relabeled as uniform evidence.
        const bool top=observation.pictureTop>0 && y==topIndex && actual==observation.pictureTop-1;
        const bool bottom=observation.pictureBottom<source.height && y==bottomIndex && actual==observation.pictureBottom;
        if((!top && !bottom) || std::abs(actual-canonical)>=old.step || changedCount==changed.size())return {};
        changed[changedCount++]=y;
    }
    if(!changedCount)return original;
    if(!maxSamples || changedCount*size_t(old.width)>maxSamples)return {};

    auto fresh=std::make_shared<SubtitleInkSnapshot>(old);
    size_t samples=0;
    const auto read=[&](int x,int y,AnalysisLumaSample& value) {
        if(samples>=maxSamples)return false;
        ++samples;return source.Sample(x,y,value);
    };
    const auto bright=[&](const AnalysisLumaSample& value) {
        return value.luma>=old.inkFloor &&
            std::abs(int(value.chromaU)-old.inkU)<=80 && std::abs(int(value.chromaV)-old.inkV)<=80;
    };
    const auto write=[](std::vector<uint64_t>& plane,size_t pixel,bool value) {
        const uint64_t bit=uint64_t{1}<<(pixel%64);
        plane[pixel/64]=(plane[pixel/64]&~bit)|(value?bit:0);
    };
    const auto insideCurrentLine=[&](int x,int y) {
        if(!observation.text.detected || observation.text.lineCount<1 || observation.text.lineCount>3)return false;
        for(int line=0;line<observation.text.lineCount;++line) {
            const auto& box=observation.text.lineBounds[line];
            if(box.Valid() && x>=box.left && x<box.right && y>=box.top && y<box.bottom)return true;
        }
        return false;
    };
    for(size_t i=0;i<changedCount;++i) {
        const int row=changed[i],canonical=row*old.step,actual=old.sourceRows[size_t(row)];
        fresh->sourceRows[size_t(row)]=canonical;
        for(int x=0;x<old.width;++x) {
            AnalysisLumaSample pixel;
            const int sx=x*old.step;
            if(!read(sx,canonical,pixel))return {};
            const size_t index=size_t(row)*old.width+x;
            const bool raw=bright(pixel);
            write(fresh->rawInk,index,raw);
            write(fresh->blackBacking,index,pixel.luma<=old.blackLimit);
            bool owned=raw && old.Get(x,row,true) && insideCurrentLine(sx,canonical);
            if(owned) {
                // Establish a real CURRENT connection to this detector-owned
                // sample, not ownership inferred from a line rectangle. Include
                // the original endpoint; never interpolate over an unread gap.
                const int direction=actual>canonical?1:-1;
                for(int sy=canonical+direction;;sy+=direction) {
                    AnalysisLumaSample path;
                    if(!read(sx,sy,path))return {};
                    if(!bright(path)){owned=false;break;}
                    if(sy==actual)break;
                }
            }
            write(fresh->ownedInk,index,owned);
        }
    }

    // A dot/dash/component confined to a substituted row must not silently
    // vanish from acquisition ownership. Flood only the <=2 affected grid
    // rows; a connection into any unchanged owned row anchors it elsewhere.
    // Native reads are already finished; this bounded mask walk has <=1920
    // cells and at most eight neighbour checks per cell.
    std::vector<uint8_t> visited(changedCount*size_t(old.width),0);
    std::vector<size_t> pending;pending.reserve(visited.size());
    const auto changedSlot=[&](int row) {
        for(size_t i=0;i<changedCount;++i)if(changed[i]==row)return int(i);
        return -1;
    };
    for(size_t slot=0;slot<changedCount;++slot)for(int x=0;x<old.width;++x) {
        const size_t start=slot*size_t(old.width)+x;
        if(visited[start] || !old.Get(x,changed[slot],true))continue;
        bool anchoredOutside=false,allProjected=true;
        pending.clear();pending.push_back(start);visited[start]=1;
        for(size_t at=0;at<pending.size();++at) {
            const int cx=int(pending[at]%size_t(old.width));
            const int cy=changed[pending[at]/size_t(old.width)];
            allProjected &= fresh->Get(cx,cy,true);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                if(!dx && !dy)continue;
                const int nx=cx+dx,ny=cy+dy;
                if(nx<0 || nx>=old.width || !old.Get(nx,ny,true))continue;
                const int neighbourSlot=changedSlot(ny);
                if(neighbourSlot<0){anchoredOutside=true;continue;}
                const size_t next=size_t(neighbourSlot)*old.width+nx;
                if(!visited[next]){visited[next]=1;pending.push_back(next);}
            }
        }
        if(!anchoredOutside && !allProjected)return {};
    }
    return fresh;
}

inline void AttachCanonicalSubtitleContinuityInk(SubtitleBoxObservation& observation,
    const AnalysisLumaSource& source,size_t maxSamples=8192) {
    observation.continuityEvidence.reset();
    const auto ink=BuildCanonicalSubtitleContinuityInk(observation,source,maxSamples);
    if(!ink)return;
    auto evidence=std::make_shared<SubtitleContinuityEvidence>();
    evidence->identity=observation.identity;evidence->policyGeneration=observation.policyGeneration;
    evidence->continuityGeneration=observation.continuityGeneration;
    evidence->detector=observation.ink;evidence->comparison=ink;
    observation.continuityEvidence=std::move(evidence);
}
