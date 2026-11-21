#pragma once

// Return the same luminance for colored side panels and neutral raised mattes.
// The grayscale moving skyline, lighthouse and waves extend to the true edges.
static bool RunNarrowAspectSequences(double fps, uint64_t generation) {
    constexpr int width = 896, height = 504;
    enum class Sides { Black, Raised, Colored, LeftColored, RightColored, Alternating, Pulse132, InteriorLinesBlack, InteriorLinesRaised };
    struct Shot { const char* name; int left, right; Sides sides; };
    const Shot shots[] = {
        {"lighthouse-black-1.1905", 148, 748, Sides::Black},
        {"lighthouse-raised-1.1905", 148, 748, Sides::Raised},
        {"colored-panels-1.1905", 148, 748, Sides::Colored},
        {"artwork-left-matte-right", 148, 748, Sides::LeftColored},
        {"neutral-matte-1.3214", 114, 780, Sides::Raised},
        {"colored-panels-1.3214", 114, 780, Sides::Colored},
        {"isolated-1.3214-in-full", 114, 780, Sides::Pulse132},
        {"matte-left-artwork-right", 114, 780, Sides::RightColored},
        {"neutral-matte-4x3", 112, 784, Sides::Raised},
        {"colored-panels-4x3", 112, 784, Sides::Colored},
        {"alternating-one-frame-crops", 0, width, Sides::Alternating},
        {"sustained-1.1905-after-cuts", 148, 748, Sides::Raised},
        // The former 1.19 picture edges now lie inside a wider colored picture.
        {"black-bars-interior-lines", 88, 808, Sides::InteriorLinesBlack},
        {"raised-bars-interior-lines", 88, 808, Sides::InteriorLinesRaised}
    };

    auto pixels = std::make_shared<std::vector<uint16_t>>(width * height * 3 / 2);
    AnalysisLumaSource source;
    source.width = width;
    source.height = height;
    source.rowBytes = source.chromaRowBytes = width * 2;
    source.range = AnalysisLumaRange::Limited;
    source.generation = generation;
    source.data = reinterpret_cast<const uint8_t*>(pixels->data());
    source.dataBytes = pixels->size() * sizeof(uint16_t);
    source.backingOwner = pixels;

    ActivePictureFrameIdentity id{};
    id.transportGeneration = generation;
    id.sourceFormatGeneration = 1;
    id.analysisDomainKey = 42;
    id.rendererGeneration = 1;
    ActivePictureTransitionModel model;
    const auto matches = [&](const ActivePictureBounds& bounds, int left, int right) {
        return bounds.left == left && bounds.right == right && bounds.top == 0 &&
            bounds.bottom == height && bounds.rasterWidth == width && bounds.rasterHeight == height &&
            std::abs(bounds.aspectRatio - double(right - left) / height) < 1e-9;
    };

    bool passed = true;
    for (const auto& shot : shots) {
        const bool interiorLines = shot.sides == Sides::InteriorLinesBlack || shot.sides == Sides::InteriorLinesRaised;
        const bool alternate = shot.sides == Sides::Alternating;
        const bool pulse = shot.sides == Sides::Pulse132;
        const bool oneSided = shot.sides == Sides::LeftColored || shot.sides == Sides::RightColored;
        const bool keepFull = shot.sides == Sides::Colored || alternate || pulse;
        const auto preservesOneSidedPicture = [&](const ActivePictureBounds& bounds) {
            const int pictureLeft = shot.sides == Sides::LeftColored ? 0 : shot.left;
            const int pictureRight = shot.sides == Sides::RightColored ? width : shot.right;
            return bounds.rasterWidth == width && bounds.rasterHeight == height &&
                bounds.top == 0 && bounds.bottom == height &&
                bounds.left >= 0 && bounds.left <= pictureLeft &&
                bounds.right >= pictureRight && bounds.right <= width;
        };
        int firstStable = -1, lostStable = 0, wrongMeasured = 0, unavailable = 0;
        constexpr int frames = 32;
        for (int frame = 0; frame < frames; ++frame) {
            ++id.acceptedSequence;
            // Both transient rectangles differ from the established full picture
            // and from each other, so consecutive source frames never corroborate one.
            const int left = alternate ? (frame % 2 ? 112 : 148) : shot.left;
            const int right = alternate ? width - left : shot.right;
            const bool colored = shot.sides == Sides::Colored || (pulse && frame % 8 != 4);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const bool side = x < left || x >= right;
                    const int grain = (3*x + 5*y + 7*frame) % 9 - 4;
                    int luma = 0;
                    if (side) {
                        luma = shot.sides == Sides::Black || shot.sides == Sides::InteriorLinesBlack ? 64 : 144 + grain;
                    } else {
                        const int horizon = 255 + std::abs((x + frame) % 128 - 64) / 8;
                        const bool tower = y > 100 && y < horizon && std::abs(x - 448) < 10 + y / 14;
                        if (tower)
                            luma = y < 123 ? 800 : 115 + y / 5;
                        else if (y < horizon)
                            luma = 500 - y / 2 + std::abs((x + frame * 3) % 192 - 96);
                        else
                            luma = 240 + std::abs((x / 3 + y + frame * 2) % 72 - 36) * 3;
                        luma += grain;
                    }
                    if (interiorLines && !side) {
                        // Textured, colored picture starts at the true boundary.
                        // The stronger pair sits still, vanishes, then moves.
                        const int depth = std::min(x, width-1-x);
                        const int inner = frame < 16 ? 148 : 140 + 2*(frame % 9);
                        const int detail = (x/6 + y/5 + frame*3) % 71;
                        luma = 196 + (y/4)%79 + detail;
                        if (frame < 12 || frame >= 16) {
                            if (depth >= inner) luma += 290;
                            if (depth >= inner && depth < inner+3) luma = 860;
                        }
                    }
                    (*pixels)[y*width + x] = static_cast<uint16_t>(luma << 6);
                    if ((x % 2 == 0) && (y % 2 == 0)) {
                        int u = 512, v = 512;
                        const bool artwork = colored || (shot.sides == Sides::LeftColored && x < left) ||
                            (shot.sides == Sides::RightColored && x >= right);
                        if (interiorLines && !side) {
                            u = 580 + (x/6 + y/3 + frame)%106;
                            v = 420 - (y/7 + frame)%81;
                        }
                        if (artwork && side) {
                            // Broad blue/red blocks travel across the side artwork.
                            const bool blue = (x / 48 + (y + 2*frame) / 64) % 2 == 0;
                            u = blue ? 810 : 484;
                            v = blue ? 490 : 680;
                        }
                        const int uv = width*height + (y/2)*width + x;
                        (*pixels)[uv] = static_cast<uint16_t>(u << 6);
                        (*pixels)[uv + 1] = static_cast<uint16_t>(v << 6);
                    }
                }
            }
            const auto evidence = MeasureActiveTrial(source, id);
            const int rawLeft = colored || shot.sides == Sides::LeftColored ? 0 : left;
            const int rawRight = colored || shot.sides == Sides::RightColored ? width : right;
            const auto rawClass = colored ? ActivePictureClassification::FULL_RASTER_TRUSTED :
                ActivePictureClassification::BAR_CROP_TRUSTED;
            // An unpaired side may remain unknown or retain neutral pixels.
            // Once a rectangle gains authority it must retain every picture column.
            const bool rawCorrect = oneSided ? preservesOneSidedPicture(evidence.trustedBounds) :
                evidence.classification == rawClass && matches(evidence.proposedBounds, rawLeft, rawRight) &&
                    matches(evidence.trustedBounds, rawLeft, rawRight);
            if (!evidence.available || evidence.classification == ActivePictureClassification::PROVISIONAL) {
                ++unavailable;
            } else if (!rawCorrect) {
                if (wrongMeasured++ == 0)
                    std::printf("  %s frame=%d measured=(%d,%d,%d,%d) expected=(%d,0,%d,%d) reason=%s\n",
                        shot.name, frame, evidence.trustedBounds.left, evidence.trustedBounds.top,
                        evidence.trustedBounds.right, evidence.trustedBounds.bottom, rawLeft, rawRight, height,
                        evidence.reason.c_str());
            }
            const auto decision = model.Observe(MakeActivePictureObservation(evidence, id.acceptedSequence, fps));
            const bool stableCorrect = oneSided ? preservesOneSidedPicture(decision.stableBounds) :
                matches(decision.stableBounds, keepFull ? 0 : shot.left, keepFull ? width : shot.right);
            if (decision.stable && stableCorrect) {
                if (firstStable < 0) firstStable = frame;
            } else if (firstStable >= 0 || alternate || pulse || oneSided) {
                ++lostStable;
            }
        }
        const bool ok = firstStable >= 0 && firstStable < 8 && lostStable == 0 &&
            wrongMeasured == 0 && (oneSided || unavailable < frames / 4);
        std::printf("%s %.3fHz %-28s stable_frame=%d wrong_after_lock=%d wrong_measured=%d unavailable=%d\n",
            ok ? "PASS" : "FAIL", fps, shot.name, firstStable, lostStable, wrongMeasured, unavailable);
        passed = passed && ok;
    }
    return passed;
}


static bool RunDimPictureReacquisition(double fps, uint64_t generation) {
    constexpr int width=3840, height=2160;
    std::vector<uint16_t> pixels(size_t(width)*height*2,uint16_t(512<<6));
    std::vector<uint16_t> row(width);
    AnalysisLumaSource source;
    source.width=width;source.height=height;
    source.rowBytes=source.chromaRowBytes=width*2;
    source.generation=generation;source.range=AnalysisLumaRange::Limited;
    source.data=reinterpret_cast<const uint8_t*>(pixels.data());
    ActivePictureFrameIdentity id{};
    id.transportGeneration=generation;id.rendererGeneration=1;
    id.sourceFormatGeneration=1;id.analysisDomainKey=43;
    bool passed=true;
    for(int shot=0;shot<3;++shot) {
        // Warm a conflicting 400-row picture, cut to the dim picture without
        // a reset, then reacquire the same dim pixels on a new format/domain.
        const bool changedFormat=shot==2;
        if(changedFormat) {++id.sourceFormatGeneration;++id.analysisDomainKey;}
        source.format=changedFormat?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010;
        source.dataBytes=size_t(width)*height*(changedFormat?4:3);
        const int outer=shot==0?400:68;
        for(int x=0;x<width;++x) row[x]=uint16_t((68+32*x/(width-1))<<6);
        for(int y=0;y<height;++y) {
            auto dst=pixels.begin()+size_t(y)*width;
            const int depth=(std::min)(y,height-1-y);
            if(depth<outer) std::fill_n(dst,width,uint16_t(64<<6));
            else if(depth<400) std::copy(row.begin(),row.end(),dst);
            else std::fill_n(dst,width,uint16_t(300<<6));
            if(shot>0 && y>=2110 && y<2142) std::fill(dst+1600,dst+2240,uint16_t(700<<6));
        }
        int available=0,wrong=0;
        for(int frame=0;frame<6;++frame) {
            ++id.acceptedSequence;++id.sourceFrameNumber;
            id.captureTimestamp=uint64_t(id.acceptedSequence*10000000.0/fps);
            const auto evidence=MeasureActiveTrial(source,id);
            if(!evidence.available) continue; // The bounded worker can miss an offered deadline.
            ++available;
            const auto& b=evidence.trustedBounds;
            if(evidence.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
                b.left!=0 || b.right!=width || b.top!=outer || b.bottom!=height-outer) ++wrong;
        }
        const bool ok=available>0 && wrong==0;
        std::printf("%s %.3fHz dim picture shot=%d correct=%d unavailable=%d wrong=%d\n",
            ok?"PASS":"FAIL",fps,shot,available-wrong,6-available,wrong);
        passed=passed && ok;
    }
    return passed;
}

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
    const bool narrowPassed = RunNarrowAspectSequences(fps, generation);
    const bool dimPassed = RunDimPictureReacquisition(fps, generation + 100);
    return passed && narrowPassed && dimPassed;
}
