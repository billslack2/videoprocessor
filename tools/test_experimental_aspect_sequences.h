#pragma once

// Each step supplies a fresh 10-bit image to the production worker entry point.
// Keep the transition model alive across cuts, including cuts back to old bounds.
static bool RunTemporalAspectSequences(double fps, uint64_t generation) {
    constexpr int w=640, h=360, frames=100;
    enum Scene { Pillars, Layered, Scope, Full, Window, Broadcast };
    struct Segment {const char* name; Scene scene; int left,top;};
    const Segment segments[]={
        {"moving-interior-lines",Pillars,44,0}, {"layered-side-mattes",Layered,44,0},
        {"scope-cue-and-volume",Scope,0,46}, {"expanded-full",Full,0,0},
        {"return-to-scope",Scope,0,46}, {"windowbox",Window,44,46},
        {"broadcast-side-artwork",Broadcast,0,0}, {"pillarbox-after-reset",Pillars,44,0}
    };
    ActivePictureTransitionModel model;
    ActivePictureFrameIdentity id{};
    id.transportGeneration=generation; id.sourceFormatGeneration=1;
    id.analysisDomainKey=42; id.rendererGeneration=1;
    auto pixels=std::make_shared<std::vector<uint16_t>>(w*h*3/2,512<<6);
    AnalysisLumaSource source;
    source.width=w; source.height=h; source.rowBytes=source.chromaRowBytes=w*2;
    source.range=AnalysisLumaRange::Limited; source.generation=generation;
    source.data=reinterpret_cast<const uint8_t*>(pixels->data());
    source.dataBytes=pixels->size()*2; source.backingOwner=pixels;
    bool passed=true;
    for(const auto& seg:segments){
        if(seg.scene==Pillars && id.acceptedSequence!=0){model.Reset(); ++id.viewportGeneration;}
        int first=-1,wrongAfterLock=0,wrongRaw=0,unknown=0;
        auto matches=[&](const ActivePictureBounds& b){
            return b.left==seg.left && b.top==seg.top && b.right==w-seg.left && b.bottom==h-seg.top;
        };
        for(int frame=0;frame<frames;++frame){
            ++id.acceptedSequence;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x){
                const int dx=std::min(x,w-1-x),dy=std::min(y,h-1-y);
                int value=200+x/5+y/4+(frame%17);
                if(dx<seg.left || dy<seg.top) value=64;
                if(seg.scene==Layered && dx>=20 && dx<44) value=84;
                if((seg.scene==Pillars || seg.scene==Layered) && dx>=44 && frame%32<20){
                    value=dx<74+frame%12 ? 144+(y/5)%88 : 620;
                }
                if(seg.scene==Broadcast && dx<180) value=130+y/4+(dx/12+frame/5)%45;
                if(seg.scene==Scope && frame%40>=8 && frame%40<32 && y>h-34 && y<h-19 &&
                   x>180 && x<410+(frame/12)%3*18 && (x/4)%3) value=820;
                if(seg.scene==Scope && frame%48>=20 && frame%48<30 && x>500 && x<580 && y>12 && y<28) value=720;
                (*pixels)[y*w+x]=static_cast<uint16_t>(value<<6);
            }
            // Simulate a missing optional result during established scope.
            const bool missing=seg.scene==Scope && frame>=55 && frame<60;
            const auto e=missing?ActivePictureEvidence{}:MeasureActiveTrial(source,id);
            if(!missing){
                if(!e.available)++unknown;
                else if(!matches(e.trustedBounds))++wrongRaw;
            }
            auto d=model.Observe(MakeActivePictureObservation(e,id.acceptedSequence,fps));
            if(frame==frames-1 && (!matches(e.trustedBounds) || !matches(d.stableBounds))){
                const auto& m=e.trustedBounds; const auto& b=d.stableBounds;
                std::printf("  %s end measured=(%d,%d,%d,%d) stable=%d bounds=(%d,%d,%d,%d)\n",
                    seg.name,m.left,m.top,m.right,m.bottom,int(d.stable),b.left,b.top,b.right,b.bottom);
            }
            if(d.stable && matches(d.stableBounds)){if(first<0)first=frame;}
            else if(first>=0)++wrongAfterLock;
        }
        const bool ok=first>=0 && first<frames/2 && wrongAfterLock==0 && wrongRaw==0;
        std::printf("%s %.3fHz %-26s lock_frame=%d unstable_after_lock=%d wrong_measured=%d unavailable=%d\n",
            ok?"PASS":"FAIL",fps,seg.name,first,wrongAfterLock,wrongRaw,unknown);
        passed=passed && ok;
    }
    return passed;
}
