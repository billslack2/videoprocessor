#pragma once
#include <SubtitleCutPaste.h>
#include <cmath>
#include <new>

// A single deliberately unanalysed input between strong detector scans may
// reuse established placement only after sampling its CURRENT source footprint.
struct SubtitleCadenceProof {
    bool valid=false;
    const char* reason="no-cadence-seed";
    unsigned samples=0;
    SubtitleBoxPreview preview;
    SubtitleCueContinuation continuation;
    SubtitleCutPasteGeometry geometry;
};

class SubtitleCadenceGuard {
public:
    using Result=SubtitleCadenceProof;
    // Native pixels, counted once across the exact extraction/erasure union.
    static constexpr unsigned SampleBudget=1048576;
    void Reset() { m_valid=false;m_used=false;ClearInPlace(*m_reference);m_source={};m_baseline.reset();m_detectorReference.reset();
        m_native.clear();m_currentPixels.clear();m_index.clear();m_components.clear();ClearInPlace(*m_cached); }
    bool HasReference() const {return m_valid;}
    const char* DecisionReason() const {return m_reason;}
    unsigned SampleCount() const {return m_samples;}
    bool RememberAccepted(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,const ActivePictureBounds& base,
        const SubtitleCutPasteGeometry& geometry,double frameMs=1000.0/60.0,int holdMs=250,int backgroundMode=3) {
        m_samples=0;
        const auto fail=[&](const char* reason){m_valid=false;m_used=false;m_reason=reason;return false;};
        const auto& c=preview.current;
        if(!preview.available || !c.analyzed || c.analysisStride!=2 || c.cadenceSkipped ||
            c.discontinuity || c.pendingRefresh || c.analysisRefresh || c.text.held ||
            !c.text.detected || c.text.confirmationPending || c.text.workLimit || !accepted.detected || accepted.held ||
            accepted.confirmationPending || accepted.workLimit || accepted.lineCount<1 ||
            accepted.lineCount>3 || !geometry.valid || !ValidSource(c,source) ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) ||
            !SameBase(c.sharedPicture.bounds,base) || !c.ink || !ValidGrid(*c.ink,source))
            return fail("cadence-seed-incomplete");
        if(!std::isfinite(frameMs) || frameMs<=0 || frameMs>(std::max)(0,(std::min)(250,holdMs)))
            return fail("cadence-lease-expired");
        if(geometry.generatedCleanupRowCount>geometry.generatedCleanupRows.size())
            return fail("cadence-cleanup-unbounded");
        // Only generated black/reconstruction modes erase bar content this way.
        if(backgroundMode!=3 && backgroundMode!=5)return fail("cadence-unsupported-background");
        if(CanRearm(preview,accepted,source,base,geometry)) {
            // Only a fresh independent positive scan advances this identity.
            // Keep the ORIGINAL native baseline: fringe drift cannot accumulate.
            *m_reference=preview;m_reference->current.text=accepted;m_reference->text=accepted;
            m_reference->current.ink=m_baseline;m_source=source;m_source.backingOwner.reset();
            m_valid=true;m_used=false;ClearInPlace(*m_cached);m_reason="cadence-seed-rearmed";return true;
        }
        Reset();m_samples=0;
        m_capture=geometry.glyphLines;m_cleanup={};m_roi={};
        const auto merge=[&](SubtitleBoxRect& into,const SubtitleBoxRect& r) {
            if(!r.Valid())return true;
            if(!InRaster(r,c.width,c.height))return false;
            if(!into.Valid())into=r;
            else into={(std::min)(into.left,r.left),(std::min)(into.top,r.top),
                (std::max)(into.right,r.right),(std::max)(into.bottom,r.bottom)};
            return true;
        };
        if(!InRaster(geometry.content,c.width,c.height) || geometry.pictureTop<0 ||
            geometry.pictureBottom>c.height || geometry.pictureTop>=geometry.pictureBottom)
            return fail("cadence-invalid-footprint");
        if(geometry.pictureCapture.Valid() && !InRaster(geometry.pictureCapture,c.width,c.height))
            return fail("cadence-invalid-picture-capture");
        unsigned captures=0;
        for(const auto& capture:m_capture)if(capture.Valid()) {
            if(!merge(m_roi,capture))return fail("cadence-capture-incomplete");
            bool corresponds=false;
            for(int line=0;line<accepted.lineCount;++line) {
                const auto& r=accepted.lineBounds[line];
                corresponds|=(std::min)(r.right,capture.right)>(std::max)(r.left,capture.left) &&
                    (std::min)(r.bottom,capture.bottom)>(std::max)(r.top,capture.top);
            }
            if(!corresponds || ++captures>unsigned(accepted.lineCount))return fail("cadence-unowned-capture");
        }
        if(!captures)return fail("cadence-capture-incomplete");
        for(int line=0;line<accepted.lineCount;++line) {
            if(!InRaster(accepted.lineBounds[line],c.width,c.height))return fail("cadence-capture-incomplete");
            bool currentCovers=false;
            for(int row=0;row<c.text.lineCount && row<3;++row)
                currentCovers|=SubtitleBoxLookahead::SameLine(c.text.lineBounds[row],accepted.lineBounds[line],
                    (std::max)(c.ink->step,c.height/180));
            if(!currentCovers)return fail("cadence-current-glyph-incomplete");
        }
        if(!Contains(m_roi,geometry.content))return fail("cadence-content-unverified");
        // A measured sourcePanel is not an erase mask. In particular the live
        // corners beside rounded cleanup rows must never veto continuation.
        std::vector<SubtitleBoxRect> erase;
        if(geometry.generatedCleanupRowCount) {
            for(unsigned n=0;n<geometry.generatedCleanupRowCount;++n) {
                const auto& r=geometry.generatedCleanupRows[n];
                if(!InRaster(r,c.width,c.height) || r.bottom!=r.top+1)
                    return fail("cadence-invalid-footprint");
                erase.push_back(r);
            }
        } else if(geometry.generatedCleanup.Valid())erase.push_back(geometry.generatedCleanup);
        auto topBar=geometry.content;topBar.bottom=(std::min)(topBar.bottom,geometry.pictureTop);
        auto bottomBar=geometry.content;bottomBar.top=(std::max)(bottomBar.top,geometry.pictureBottom);
        if(topBar.Valid())erase.push_back(topBar);
        if(bottomBar.Valid())erase.push_back(bottomBar);
        for(const auto& r:erase)if(!merge(m_cleanup,r) || !merge(m_roi,r))
            return fail("cadence-invalid-footprint");
        const size_t area=size_t(m_roi.right-m_roi.left)*(m_roi.bottom-m_roi.top);
        if(area>size_t(SampleBudget)*8)return fail("cadence-footprint-index-budget");
        m_index.assign(area,-1);
        // Merge intervals per native row; no hull gaps or duplicate reads.
        for(int y=m_roi.top;y<m_roi.bottom;++y) {
            std::vector<std::pair<int,int>> spans,eraseSpans;
            std::array<SubtitleBoxRect,3> rowCaptures{};
            for(size_t line=0;line<m_capture.size();++line) {
                const auto& r=m_capture[line];
                if(!r.Valid() || y<r.top || y>=r.bottom || y<geometry.content.top || y>=geometry.content.bottom)continue;
                int left=(std::max)(r.left,geometry.content.left),right=(std::min)(r.right,geometry.content.right);
                if(y>=geometry.pictureTop && y<geometry.pictureBottom) {
                    const auto& card=geometry.pictureCapture;
                    // The shader accepts the measured card and a connected
                    // 1-3-row disputed-edge collar within its horizontal span.
                    int top=card.top,bottom=card.bottom;
                    if(top>geometry.pictureTop && top-geometry.pictureTop<=3)top=geometry.pictureTop;
                    if(bottom<geometry.pictureBottom && geometry.pictureBottom-bottom<=3)bottom=geometry.pictureBottom;
                    if(!card.Valid() || y<top || y>=bottom)continue;
                    left=(std::max)(left,card.left);right=(std::min)(right,card.right);
                }
                if(left>=right)continue;
                rowCaptures[line]={left,y,right,y+1};spans.push_back({left,right});
            }
            for(const auto& r:erase)if(y>=r.top && y<r.bottom) {
                spans.push_back({r.left,r.right});eraseSpans.push_back({r.left,r.right});
            }
            std::sort(spans.begin(),spans.end());
            int end=-1;
            for(const auto& span:spans) {
                const int left=(std::max)(end,span.first);end=(std::max)(end,span.second);
                for(int x=left;x<span.second;++x) {
                    if(m_samples>=SampleBudget)return fail("cadence-sample-budget-or-source");
                    NativePixel n;n.x=x;n.y=y;
                    for(int line=0;line<3;++line)if(Inside(rowCaptures[line],x,y))n.line=line;
                    for(const auto& r:eraseSpans)if(x>=r.first && x<r.second)n.erase=true;
                    if(!SampleValidated(source,x,y,n.pixel))return fail("cadence-cleanup-source-unavailable");
                    ++m_samples;n.ink=Ink(n.pixel,*c.ink);n.black=Black(n.pixel,*c.ink);
                    m_index[size_t(y-m_roi.top)*(m_roi.right-m_roi.left)+x-m_roi.left]=int(m_native.size());
                    m_native.push_back(n);
                }
            }
        }
        *m_reference=preview;m_reference->current.text=accepted;m_reference->text=accepted;
        m_geometry=geometry;m_base=base;m_source=source;m_source.backingOwner.reset();
        // Detector raw bits also require local picture contrast; native palette
        // classification deliberately has different semantics. Preserve the
        // immutable acquisition detector snapshot for detector-to-detector
        // rearm checks, never for skipped-source native pixel authority.
        m_detectorReference=c.ink;
        m_baseline=Snapshot(*c.ink,m_native);
        for(int line=0;line<accepted.lineCount;++line) {
            unsigned owned=0;
            const auto& bounds=accepted.lineBounds[line];
            for(int y=0;y<c.ink->height;++y)for(int x=(bounds.left+c.ink->step-1)/c.ink->step;
                x<c.ink->width && x*c.ink->step<bounds.right;++x)
                if(Inside(bounds,x*c.ink->step,c.ink->sourceRows[y]) && c.ink->Get(x,y,true)) {
                    const int at=Index(x*c.ink->step,c.ink->sourceRows[y]);
                    if(at<0 || m_native[at].line<0)return fail("cadence-owned-glyph-outside-capture");
                    if(m_native[at].ink)++owned;
                }
            if(owned<4)return fail("cadence-no-owned-glyphs");
        }
        BuildComponents(*c.ink);
        m_reference->current.ink=m_baseline;m_valid=true;m_reason="cadence-seeded";return true;
    }
    SubtitleCadenceProof Prepare(const SubtitleBoxPreview& key,const AnalysisLumaSource& source,
        const ActivePictureBounds& base,double frameMs,int holdMs) {
        return PrepareReference(key,source,base,frameMs,holdMs);
    }
    // Stable until the next guard operation. Production copies directly into
    // its persistent heap result, avoiding a 64KB by-value return temporary.
    const SubtitleCadenceProof& PrepareReference(const SubtitleBoxPreview& key,const AnalysisLumaSource& source,
        const ActivePictureBounds& base,double frameMs,int holdMs) {
        ClearInPlace(*m_work);auto& out=*m_work;m_samples=0;
        // Returning the heap workspace by reference from this local helper
        // avoids a second large by-value result in the caller's stack frame.
        const auto fail=[&](const char* reason)->SubtitleCadenceProof& {
            m_valid=false;m_reason=reason;out.reason=reason;out.samples=m_samples;return out;
        };
        if(!m_valid)return fail("no-cadence-seed");
        const auto& c=key.current;const auto& old=m_reference->current;
        if(!c.cadenceSkipped || c.analysisStride!=2 || c.analyzed || c.discontinuity ||
            c.pendingRefresh || c.analysisRefresh || c.text.detected || c.text.workLimit ||
            !ValidSource(c,source) || !SameContext(c,old) || !SameBase(base,m_base) ||
            key.policyGeneration!=m_reference->policyGeneration ||
            key.continuityGeneration!=m_reference->continuityGeneration ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) ||
            !SameBase(c.sharedPicture.bounds,base) || source.format!=m_source.format ||
            source.encoding!=m_source.encoding || source.range!=m_source.range ||
            source.colorspace!=m_source.colorspace || source.generation!=m_source.generation ||
            source.rgbBitAlignment!=m_source.rgbBitAlignment)
            return fail("cadence-context-changed");
        if(m_used) {
            if(SameAnalysisFrameIdentity(c.identity,m_cached->continuation.identity))return *m_cached;
            return fail("cadence-needs-new-independent-scan");
        }
        if(c.identity.acceptedSequence!=old.identity.acceptedSequence+1 ||
            c.identity.sourceFrameNumber<=old.identity.sourceFrameNumber ||
            c.identity.captureTimestamp<=old.identity.captureTimestamp)
            return fail("cadence-source-gap");
        if(!std::isfinite(frameMs) || frameMs<=0 || frameMs>(std::max)(0,(std::min)(250,holdMs)))
            return fail("cadence-lease-expired");
        // Current samples need no copy of immutable component/coordinate data.
        // Reuse the compact buffer across source packets instead of allocating
        // and copying the full native proof map on every skipped input.
        m_currentPixels.resize(m_native.size());
        for(size_t i=0;i<m_native.size();++i) {
            auto& pixel=m_currentPixels[i];const auto& location=m_native[i];
            if(m_samples>=SampleBudget || !SampleValidated(source,location.x,location.y,pixel.pixel))
                return fail("cadence-sample-budget-or-source");
            ++m_samples;pixel.ink=Ink(pixel.pixel,*m_baseline);pixel.black=Black(pixel.pixel,*m_baseline);
        }
        const auto& currentPixels=m_currentPixels;
        std::vector<unsigned> changed(m_components.size(),0);
        std::array<unsigned,3> lineChanges{};
        for(size_t i=0;i<m_native.size();++i) {
            const auto& oldPixel=m_native[i];const auto& now=currentPixels[i];
            bool fringe=false;
            if(oldPixel.line>=0 && oldPixel.ink!=now.ink) {
                const int owner=oldPixel.fringeComponent;
                if(owner<0 || !NearFloor(oldPixel.pixel,*m_baseline) || !NearFloor(now.pixel,*m_baseline))
                    return fail("cadence-current-glyph-changed");
                const auto& component=m_components[owner];
                if(component.area<16 || component.cores<4 ||
                    ++changed[owner]>(std::min)(16u,(std::max)(2u,component.area/64)) ||
                    ++lineChanges[oldPixel.line]>32)return fail("cadence-current-glyph-changed");
                bool survivingCore=false;
                for(const auto& d:Neighbours()) {
                    const int at=Index(oldPixel.x+d.first,oldPixel.y+d.second);
                    if(at>=0 && m_native[at].component==owner &&
                        m_native[at].pixel.luma>=m_baseline->inkFloor+16 &&
                        currentPixels[at].ink && currentPixels[at].pixel.luma>=m_baseline->inkFloor+16)
                        survivingCore=true;
                }
                if(!survivingCore)return fail("cadence-current-glyph-changed");
                fringe=true;
            }
            // Never tolerate a disappearing strong core, even if another edge
            // happens to gain the same number of pixels on this frame.
            if(oldPixel.core && (!now.ink || now.pixel.luma<m_baseline->inkFloor+16))
                return fail("cadence-current-glyph-changed");
            if(oldPixel.erase && !fringe) {
                if(oldPixel.black && !now.black)return fail("cadence-current-cleanup-changed");
                const bool owned=oldPixel.component>=0 && m_components[oldPixel.component].owned;
                if(!oldPixel.black && !owned &&
                    (std::abs(int(now.pixel.luma)-oldPixel.pixel.luma)>8 ||
                     std::abs(int(now.pixel.chromaU)-oldPixel.pixel.chromaU)>8 ||
                     std::abs(int(now.pixel.chromaV)-oldPixel.pixel.chromaV)>8))
                    return fail("cadence-current-cleanup-changed");
            }
        }
        auto fresh=Snapshot(*m_baseline,currentPixels);
        for(size_t i=0;i<fresh->ownedInk.size();++i)
            fresh->ownedInk[i]=m_baseline->ownedInk[i]&fresh->rawInk[i];
        out.valid=true;out.reason="cadence-current-pixels";out.samples=m_samples;out.geometry=m_geometry;
        out.preview=*m_reference;out.preview.current.identity=c.identity;
        out.preview.current.sharedPicture=c.sharedPicture;out.preview.current.text.held=true;
        out.preview.current.pendingRefresh=true;out.preview.current.analysisRefresh=false;
        out.preview.current.cadenceSkipped=true;out.preview.current.ink=fresh;
        out.preview.current.independentBarAnalysis=false;out.preview.current.barAuthority=false;
        out.preview.current.barEvidence={};out.preview.current.subtitleArProof={};
        out.preview.current.subtitleArDarkness={};out.preview.current.barStripClearance={};
        out.preview.current.barTrackingAuthority=false;out.preview.current.currentAnchorUsesTrackedEdge=false;
        out.preview.current.hasBarTrackingReference=false;out.preview.current.incomingBarTrackingReference={};
        out.preview.text=out.preview.current.text;out.preview.futureAvailable=false;
        for(auto& observation:out.preview.following)ClearInPlace(observation);out.preview.followingCount=0;out.preview.matchingFrames=1;
        out.preview.currentLinesConfirmed=false;out.preview.newScanMs=0;out.preview.newScanFrames=0;
        auto& proof=out.continuation;proof.valid=true;
        proof.identity=c.identity;proof.previousIdentity=old.identity;proof.templateIdentity=old.identity;
        proof.policyGeneration=key.policyGeneration;proof.continuityGeneration=key.continuityGeneration;
        proof.physicalBase=base;proof.pictureTop=old.pictureTop;proof.pictureBottom=old.pictureBottom;
        proof.accepted=old.text;proof.accepted.held=true;proof.ink=fresh;
        proof.sampledBounds=m_roi;
        // Exact row spans authorize only the cached geometry. A hull would
        // falsely certify un-erased rounded corners to other continuation paths.
        proof.backingVerifiedBounds={};
        proof.samples=m_samples;proof.elapsedMs=frameMs;proof.reason=out.reason;
        m_used=true;m_reason=out.reason;*m_cached=out;return out;
    }
private:
    template<class T> static void ClearInPlace(T& value) {
        // Direct placement construction releases old shared ownership and
        // restores all defaults without a T{} assignment temporary. The same
        // allocation survives every source packet, including failure/reset.
        value.~T();::new(static_cast<void*>(&value)) T{};
    }
    static bool Inside(const SubtitleBoxRect& r,int x,int y){return r.Valid() && x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
    static bool Contains(const SubtitleBoxRect& a,const SubtitleBoxRect& b){return SubtitleCueContinuation::Contains(a,b);}
    static bool InRaster(const SubtitleBoxRect& r,int w,int h){return r.Valid() && r.left>=0 && r.top>=0 && r.right<=w && r.bottom<=h;}
    static bool SameBase(const ActivePictureBounds& a,const ActivePictureBounds& b){return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom && a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;}
    static bool ValidSource(const SubtitleBoxObservation& o,const AnalysisLumaSource& s){return s.IsValid() && s.width==o.width && s.height==o.height && s.generation==o.identity.transportGeneration;}
    static bool SameContext(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        return a.identity.transportGeneration==b.identity.transportGeneration &&
            a.identity.sourceFormatGeneration==b.identity.sourceFormatGeneration &&
            a.identity.analysisDomainKey==b.identity.analysisDomainKey &&
            a.identity.viewportGeneration==b.identity.viewportGeneration &&
            a.identity.rendererGeneration==b.identity.rendererGeneration &&
            a.width==b.width && a.height==b.height && a.scriptHint==b.scriptHint &&
            a.optimizationMode==b.optimizationMode && a.nearBarDistance==b.nearBarDistance &&
            a.policyGeneration==b.policyGeneration && a.continuityGeneration==b.continuityGeneration;
    }
    static bool ValidGrid(const SubtitleInkSnapshot& p,const AnalysisLumaSource& source) {
        const size_t words=(size_t(p.width)*p.height+63)/64;
        return p.paletteValid && p.rawEvidenceComplete && p.step>0 && p.step<=8 &&
            p.width>0 && p.height>0 && p.width<=960 && p.height<=540 &&
            p.width==(source.width+p.step-1)/p.step && p.height==(source.height+p.step-1)/p.step &&
            p.sourceRows.size()==size_t(p.height) && p.rawInk.size()==words &&
            p.ownedInk.size()==words && p.blackBacking.size()==words &&
            p.inkFloor>=0 && p.inkFloor<=1023 && p.blackLimit>=0 && p.blackLimit<=1023 &&
            std::is_sorted(p.sourceRows.begin(),p.sourceRows.end()) &&
            std::adjacent_find(p.sourceRows.begin(),p.sourceRows.end())==p.sourceRows.end() &&
            p.sourceRows.front()>=0 && p.sourceRows.back()<source.height;
    }
    // Call only after ValidSource() validated the complete allocation and the
    // immutable footprint proved x/y in raster. Match Sample() exactly: decoded
    // DV and P010/P210 all expose the high ten bits, never rounded 12-bit codes.
    // Byte loads preserve padded/odd strides and unaligned frame backing.
    static uint16_t ReadPlanar10(const uint8_t* p) {
        return uint16_t((uint16_t(p[0])|(uint16_t(p[1])<<8))>>6);
    }
    static bool SampleValidated(const AnalysisLumaSource& source,int x,int y,AnalysisLumaSample& pixel) {
        if(source.format!=AnalysisLumaFormat::P010 && source.format!=AnalysisLumaFormat::P210 &&
            source.format!=AnalysisLumaFormat::DecodedDolbyVision422)return source.Sample(x,y,pixel);
        const auto* luma=source.data+size_t(y)*source.rowBytes+size_t(x)*2;
        const size_t row=source.format==AnalysisLumaFormat::P010?size_t(y/2):size_t(y);
        const auto* chroma=source.data+source.rowBytes*size_t(source.height)+row*source.chromaRowBytes+size_t(x/2)*4;
        pixel.luma=ReadPlanar10(luma);pixel.chromaU=ReadPlanar10(chroma);pixel.chromaV=ReadPlanar10(chroma+2);
        return true;
    }
    static bool SameRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static bool SameGeometry(const SubtitleCutPasteGeometry& a,const SubtitleCutPasteGeometry& b) {
        if(!SameRect(a.source,b.source) || !SameRect(a.destination,b.destination) ||
            !SameRect(a.content,b.content) || !SameRect(a.generatedCleanup,b.generatedCleanup) ||
            !SameRect(a.pictureCapture,b.pictureCapture) ||
            !SameRect(a.generatedCleanupExclusion,b.generatedCleanupExclusion) ||
            !SameRect(a.placementLimits,b.placementLimits) || !SameRect(a.layoutContent,b.layoutContent) ||
            !SameRect(a.layoutCoverage,b.layoutCoverage) || !SameRect(a.cleanupLimit,b.cleanupLimit) ||
            a.generatedCleanupRowCount!=b.generatedCleanupRowCount ||
            a.glyphScale!=b.glyphScale || a.glyphTranslateX!=b.glyphTranslateX || a.glyphTranslateY!=b.glyphTranslateY ||
            a.centeredGlyphMapping!=b.centeredGlyphMapping || a.roundedCorners!=b.roundedCorners ||
            a.extendToBar!=b.extendToBar || a.pictureIntrusion!=b.pictureIntrusion ||
            a.backgroundGuarded!=b.backgroundGuarded || a.valid!=b.valid || a.fromTopBar!=b.fromTopBar ||
            a.pictureTop!=b.pictureTop || a.pictureBottom!=b.pictureBottom ||
            a.displayPictureLeft!=b.displayPictureLeft || a.displayPictureRight!=b.displayPictureRight ||
            a.displayPictureTop!=b.displayPictureTop || a.displayPictureBottom!=b.displayPictureBottom)return false;
        for(size_t i=0;i<a.glyphLines.size();++i)if(!SameRect(a.glyphLines[i],b.glyphLines[i]))return false;
        for(unsigned i=0;i<a.generatedCleanupRowCount;++i)
            if(!SameRect(a.generatedCleanupRows[i],b.generatedCleanupRows[i]))return false;
        return true;
    }
    bool CanRearm(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,const ActivePictureBounds& base,const SubtitleCutPasteGeometry& geometry) const {
        if(!m_valid || !m_baseline || !m_detectorReference || !SameGeometry(geometry,m_geometry) || !SameBase(base,m_base))return false;
        const auto& c=preview.current;const auto& previous=m_reference->current;const auto& p=*c.ink;
        if(!SameContext(c,previous) || preview.policyGeneration!=m_reference->policyGeneration ||
            preview.continuityGeneration!=m_reference->continuityGeneration ||
            c.identity.acceptedSequence!=previous.identity.acceptedSequence+2 ||
            c.identity.sourceFrameNumber<=previous.identity.sourceFrameNumber ||
            c.identity.captureTimestamp<=previous.identity.captureTimestamp ||
            source.format!=m_source.format || source.encoding!=m_source.encoding || source.range!=m_source.range ||
            source.colorspace!=m_source.colorspace || source.generation!=m_source.generation ||
            source.rgbBitAlignment!=m_source.rgbBitAlignment ||
            accepted.cue!=previous.text.cue || accepted.lineCount!=previous.text.lineCount ||
            accepted.signature!=previous.text.signature || accepted.anchorSignature!=previous.text.anchorSignature ||
            accepted.lineSignatures!=previous.text.lineSignatures ||
            c.text.lineCount!=accepted.lineCount || c.text.signature!=accepted.signature ||
            p.width!=m_baseline->width || p.height!=m_baseline->height || p.step!=m_baseline->step ||
            p.sourceRows!=m_baseline->sourceRows || p.inkFloor!=m_baseline->inkFloor ||
            p.inkU!=m_baseline->inkU || p.inkV!=m_baseline->inkV || p.blackLimit!=m_baseline->blackLimit)return false;
        for(int line=0;line<accepted.lineCount;++line)
            if(!SameRect(accepted.lineBounds[line],previous.text.lineBounds[line]) ||
                !SameRect(c.text.lineBounds[line],accepted.lineBounds[line]))return false;
        // A fresh full detector scan must independently agree with acquisition
        // at every extraction grid sample. Cleanup changes are not silently
        // renewed: the next skipped source still compares every native erase
        // pixel to the immutable original baseline.
        for(int y=0;y<p.height;++y) {
            const int sy=p.sourceRows[y];if(sy<m_roi.top || sy>=m_roi.bottom)continue;
            for(int x=(m_roi.left+p.step-1)/p.step;x<p.width && x*p.step<m_roi.right;++x) {
                const int at=Index(x*p.step,sy);
                if(at>=0 && m_native[at].line>=0 &&
                    (p.Get(x,y)!=m_detectorReference->Get(x,y) || p.Get(x,y,true)!=m_detectorReference->Get(x,y,true)))return false;
            }
        }
        return true;
    }
    struct CurrentPixel {AnalysisLumaSample pixel;bool ink=false,black=false;};
    struct NativePixel {
        int x=0,y=0,line=-1,component=-1,fringeComponent=-1;
        AnalysisLumaSample pixel;
        bool erase=false,ink=false,black=false,core=false,exterior=false;
    };
    struct Component {unsigned area=0,cores=0;bool owned=false;};
    static const std::array<std::pair<int,int>,4>& Neighbours() {
        static const std::array<std::pair<int,int>,4> d{{{-1,0},{1,0},{0,-1},{0,1}}};return d;
    }
    int Index(int x,int y) const {
        if(!Inside(m_roi,x,y))return -1;
        return m_index[size_t(y-m_roi.top)*(m_roi.right-m_roi.left)+x-m_roi.left];
    }
    static bool Ink(const AnalysisLumaSample& p,const SubtitleInkSnapshot& palette) {
        return p.luma>=palette.inkFloor && std::abs(int(p.chromaU)-palette.inkU)<=80 &&
            std::abs(int(p.chromaV)-palette.inkV)<=80;
    }
    static bool Black(const AnalysisLumaSample& p,const SubtitleInkSnapshot& palette) {
        return p.luma<=palette.blackLimit && std::abs(int(p.chromaU)-512)<=48 &&
            std::abs(int(p.chromaV)-512)<=48;
    }
    static bool NearFloor(const AnalysisLumaSample& p,const SubtitleInkSnapshot& palette) {
        return std::abs(int(p.luma)-palette.inkFloor)<=16 &&
            std::abs(int(p.chromaU)-palette.inkU)<=64 && std::abs(int(p.chromaV)-palette.inkV)<=64;
    }
    bool GridOwned(const NativePixel& n,const SubtitleInkSnapshot& p) const {
        if(n.x%p.step)return false;
        const auto row=std::lower_bound(p.sourceRows.begin(),p.sourceRows.end(),n.y);
        return row!=p.sourceRows.end() && *row==n.y && p.Get(n.x/p.step,int(row-p.sourceRows.begin()),true);
    }
    void BuildComponents(const SubtitleInkSnapshot& p) {
        std::vector<int> queue;queue.reserve(m_native.size());
        for(size_t start=0;start<m_native.size();++start) {
            auto& first=m_native[start];
            if(first.line<0 || !first.ink || first.component>=0)continue;
            const int label=int(m_components.size());m_components.push_back({});
            first.component=label;queue.clear();queue.push_back(int(start));
            for(size_t at=0;at<queue.size();++at) {
                auto& n=m_native[queue[at]];auto& component=m_components[label];
                ++component.area;
                if(!component.owned)component.owned=GridOwned(n,p);
                for(const auto& d:Neighbours()) {
                    const int i=Index(n.x+d.first,n.y+d.second);
                    if(i>=0 && m_native[i].line==first.line && m_native[i].ink && m_native[i].component<0) {
                        m_native[i].component=label;queue.push_back(i);
                    }
                }
            }
        }
        for(auto& n:m_native) {
            if(n.component<0 || !m_components[n.component].owned || n.pixel.luma<p.inkFloor+16)continue;
            bool interior=true;
            for(const auto& d:Neighbours()) {
                const int i=Index(n.x+d.first,n.y+d.second);
                interior&=i>=0 && m_native[i].component==n.component;
            }
            if(interior){n.core=true;++m_components[n.component].cores;}
        }
        // Binary/strong captions cannot use threshold-fringe tolerance. Avoid
        // the exterior flood entirely unless it can authorize a real candidate.
        bool hasFringe=false;
        for(const auto& n:m_native)if(n.line>=0 && NearFloor(n.pixel,p)){hasFringe=true;break;}
        if(!hasFringe)return;
        // Flood only outside background, leaving enclosed glyph holes strict.
        queue.clear();
        for(size_t i=0;i<m_native.size();++i) {
            auto& n=m_native[i];if(n.line<0 || n.ink)continue;
            for(const auto& d:Neighbours()) {
                const int at=Index(n.x+d.first,n.y+d.second);
                if(at<0 || m_native[at].line!=n.line) {n.exterior=true;queue.push_back(int(i));break;}
            }
        }
        for(size_t at=0;at<queue.size();++at) {
            const auto n=m_native[queue[at]];
            for(const auto& d:Neighbours()) {
                const int i=Index(n.x+d.first,n.y+d.second);
                if(i>=0 && m_native[i].line==n.line && !m_native[i].ink && !m_native[i].exterior) {
                    m_native[i].exterior=true;queue.push_back(i);
                }
            }
        }
        for(auto& n:m_native) {
            if(n.line<0 || !NearFloor(n.pixel,p))continue;
            int owner=-1;unsigned inkNeighbours=0;bool outer=n.exterior,hasHole=false;
            for(const auto& d:Neighbours()) {
                const int i=Index(n.x+d.first,n.y+d.second);if(i<0)continue;
                const auto& neighbour=m_native[i];
                if(neighbour.line!=n.line)continue;
                if(neighbour.ink)++inkNeighbours;
                else {outer|=neighbour.exterior;hasHole|=!neighbour.exterior;}
                if(neighbour.component>=0 && m_components[neighbour.component].owned &&
                    neighbour.pixel.luma>=p.inkFloor+16) {
                    if(owner>=0 && owner!=neighbour.component){owner=-2;break;}
                    owner=neighbour.component;
                }
            }
            // A strong boundary pixel connects this one-pixel fringe to an
            // owned component with an independently surviving interior core.
            // Weak additions cannot bridge two strokes; inner hole boundaries
            // and tiny punctuation have no permissive fringe certificate.
            if(owner>=0 && outer && !hasHole && (n.ink || inkNeighbours<=1) &&
                (!n.ink || n.component==owner))n.fringeComponent=owner;
        }
    }
    template<class Pixel>
    std::shared_ptr<SubtitleInkSnapshot> Snapshot(const SubtitleInkSnapshot& p,
        const std::vector<Pixel>& pixels) const {
        auto out=std::make_shared<SubtitleInkSnapshot>(p);
        std::fill(out->rawInk.begin(),out->rawInk.end(),0);
        std::fill(out->blackBacking.begin(),out->blackBacking.end(),0);
        std::fill(out->ownedInk.begin(),out->ownedInk.end(),0);
        out->rawEvidenceComplete=false; // Footprint proof is not a detector scan.
        for(int y=0;y<p.height;++y) {
            const int sy=p.sourceRows[y];
            if(sy<m_roi.top || sy>=m_roi.bottom)continue;
            for(int x=(m_roi.left+p.step-1)/p.step;x<p.width && x*p.step<m_roi.right;++x) {
                const int at=Index(x*p.step,sy);if(at<0)continue;
                const auto& n=pixels[at];const size_t i=size_t(y)*p.width+x;
                const uint64_t bit=uint64_t(1)<<(i%64);
                if(n.ink)out->rawInk[i/64]|=bit;
                if(n.black)out->blackBacking[i/64]|=bit;
                if(n.ink && p.Get(x,y,true))out->ownedInk[i/64]|=bit;
            }
        }
        return out;
    }
    bool m_valid=false,m_used=false;const char* m_reason="no-cadence-seed";unsigned m_samples=0;
    // These aggregate records are 57-64KB each. Own reusable heap storage so
    // reset/default construction never creates their aggregate stack copies.
    std::unique_ptr<SubtitleBoxPreview> m_reference=std::make_unique<SubtitleBoxPreview>();
    std::unique_ptr<SubtitleCadenceProof> m_cached=std::make_unique<SubtitleCadenceProof>();
    std::unique_ptr<SubtitleCadenceProof> m_work=std::make_unique<SubtitleCadenceProof>();
    ActivePictureBounds m_base;AnalysisLumaSource m_source;
    SubtitleCutPasteGeometry m_geometry;SubtitleBoxRect m_roi,m_cleanup;
    std::array<SubtitleBoxRect,3> m_capture{};
    std::vector<NativePixel> m_native;
    std::vector<CurrentPixel> m_currentPixels;
    std::vector<int> m_index;
    std::vector<Component> m_components;
    std::shared_ptr<SubtitleInkSnapshot> m_baseline;
    std::shared_ptr<const SubtitleInkSnapshot> m_detectorReference;
};
