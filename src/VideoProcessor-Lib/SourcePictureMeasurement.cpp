#include "pch.h"
#include "SourcePictureMeasurement.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace {
constexpr unsigned kEdgeProbes=128;
// Fixed-size source-coordinate overview, not an averaged/resized picture.
constexpr int kOverviewWidth=480,kOverviewHeight=270;
constexpr size_t kMaximumComponents=512;
struct TopologyLimit {};
struct SourceReadFailure {};
struct Reader {
    const AnalysisLumaSource& source;
    const AnalysisEvaluationBudget* budget;
    ValidatedAnalysisLumaSampler sampler;
    size_t samples=0;
    Reader(const AnalysisLumaSource& value,const AnalysisEvaluationBudget* limit)
        : source(value),budget(limit),sampler(value) {}
    AnalysisLumaSample At(int x,int y) {
        if((samples++&255)==0)CheckAnalysisEvaluationBudget(budget);
        AnalysisLumaSample value;
        if(!sampler.Sample(x,y,value))throw SourceReadFailure{};
        return value;
    }
};
int Difference(const AnalysisLumaSample& a,const AnalysisLumaSample& b) {
    // Chroma footprints cover pairs of luma pixels. They describe content,
    // but cannot move a native luma boundary by one pixel.
    return std::abs(int(a.luma)-b.luma);
}
bool Foreground(const AnalysisLumaSample& a,int floor) {
    return int(a.luma)>floor+12 || std::abs(int(a.chromaU)-512)>20 || std::abs(int(a.chromaV)-512)>20;
}
unsigned Regions(const std::array<unsigned,4>& counts) {
    unsigned n=0;for(auto c:counts)if(c)n++;return n;
}
struct OccupiedRun {
    int row,left,right;
    size_t parent;
    uint8_t rank=0;
};
size_t RunRoot(std::vector<OccupiedRun>& runs,size_t index) {
    size_t root=index;
    while(runs[root].parent!=root)root=runs[root].parent;
    while(runs[index].parent!=index) {
        const auto next=runs[index].parent;runs[index].parent=root;index=next;
    }
    return root;
}
void JoinRuns(std::vector<OccupiedRun>& runs,size_t a,size_t b) {
    a=RunRoot(runs,a);b=RunRoot(runs,b);if(a==b)return;
    if(runs[a].rank<runs[b].rank)std::swap(a,b);
    runs[b].parent=a;if(runs[a].rank==runs[b].rank)++runs[a].rank;
}
// Adjacent rows connect when their occupied spans overlap or touch diagonally.
// A row has disjoint sorted runs, so the overlap walk is linear in run count;
// no per-pixel neighbor queue or pairwise component search is needed.
template<class Occupied>
std::vector<OccupiedRun> BuildRuns(int left,int top,int right,int bottom,Occupied occupied,
    const AnalysisEvaluationBudget* budget=nullptr) {
    std::vector<OccupiedRun> runs;
    runs.reserve(size_t(std::min(4096,std::max(0,right-left)*std::max(0,bottom-top))));
    size_t previousFirst=0,previousEnd=0;
    for(int y=top;y<bottom;++y) {
        CheckAnalysisEvaluationBudget(budget);
        const size_t currentFirst=runs.size();int start=-1;
        for(int x=left;x<=right;++x) {
            const bool present=x<right && occupied(x,y);
            if(present && start<0)start=x;
            if(!present && start>=0) {
                const size_t index=runs.size();runs.push_back({y,start,x,index,0});start=-1;
            }
        }
        size_t previous=previousFirst;
        for(size_t current=currentFirst;current<runs.size();++current) {
            if((current&255)==0)CheckAnalysisEvaluationBudget(budget);
            while(previous<previousEnd && runs[previous].right<runs[current].left)++previous;
            for(size_t prior=previous;prior<previousEnd && runs[prior].left<=runs[current].right;++prior)
                JoinRuns(runs,current,prior);
        }
        previousFirst=currentFirst;previousEnd=runs.size();
    }
    return runs;
}
void Components(Reader& reader,int floor,SourcePictureObservation& out) {
    const int sx=std::max(1,(out.width+kOverviewWidth-1)/kOverviewWidth);
    const int sy=std::max(1,(out.height+kOverviewHeight-1)/kOverviewHeight);
    out.overviewStepX=sx;out.overviewStepY=sy;out.denseCoverage=sx==1&&sy==1;
    const int w=(out.width+sx-1)/sx,h=(out.height+sy-1)/sy;
    out.overviewWidth=w;out.overviewHeight=h;
    out.overviewLabels.assign(size_t(w)*h,0);
    auto runs=BuildRuns(0,0,w,h,[&](int x,int y) {
        return Foreground(reader.At(std::min(out.width-1,x*sx+sx/2),
            std::min(out.height-1,y*sy+sy/2)),floor);
    },reader.budget);
    std::vector<uint16_t> labels(runs.size());
    for(size_t i=0;i<runs.size();++i) {
        if((i&255)==0)CheckAnalysisEvaluationBudget(reader.budget);
        const auto root=RunRoot(runs,i);auto& label=labels[root];const auto& run=runs[i];
        if(!label) {
            if(out.components.size()>=kMaximumComponents)throw TopologyLimit{};
            label=uint16_t(out.components.size()+1);
            out.components.push_back({run.left*sx,run.row*sy,std::min(out.width,run.right*sx),
                std::min(out.height,(run.row+1)*sy),0});
        }
        auto& component=out.components[label-1];
        component.left=std::min(component.left,run.left*sx);component.top=std::min(component.top,run.row*sy);
        component.right=std::max(component.right,std::min(out.width,run.right*sx));
        component.bottom=std::max(component.bottom,std::min(out.height,(run.row+1)*sy));
        component.samples+=unsigned(run.right-run.left);
        std::fill(out.overviewLabels.begin()+size_t(run.row)*w+run.left,
            out.overviewLabels.begin()+size_t(run.row)*w+run.right,label);
    }
    out.topologyComplete=true;
}
void EdgeCandidates(Reader& reader,SourcePictureEdge edge,int floor,SourcePictureObservation& out) {
    const bool vertical=edge==SourcePictureEdge::Top||edge==SourcePictureEdge::Bottom;
    const bool leading=edge==SourcePictureEdge::Top||edge==SourcePictureEdge::Left;
    const int depth=vertical?out.height:out.width,along=vertical?out.width:out.height;
    const int step=std::max(2,depth/540),sustain=std::max(6,depth/270);
    std::map<int,SourcePictureEdgeCandidate> candidates;
    for(unsigned i=0;i<kEdgeProbes;++i) {
        const int position=int((uint64_t(2*i+1)*along)/(2*kEdgeProbes));
        auto read=[&](int offset) {const int p=leading?offset:depth-1-offset;return reader.At(vertical?position:p,vertical?p:position);};
        const auto border=read(0);
        auto add=[&](int offset,bool strong) {
            const int coordinate=leading?offset:depth-offset;
            auto& candidate=candidates[coordinate];candidate.coordinate=coordinate;
            candidate.minimum=coordinate;candidate.maximum=coordinate;
            ++candidate.support;++candidate.regionSupport[i/32];candidate.coverage=kEdgeProbes;
            candidate.strong=candidate.strong||strong;
        };
        if(Foreground(border,floor) && Foreground(read(sustain),floor))add(0,true);
        // Keep every sustained matte-to-picture departure, including a scene
        // behind an earlier detached graphic. A drift from the perimeter is
        // insufficient: the local boundary itself must be abrupt.
        int lastHit=-sustain;
        int previousDelta=0;
        for(int coarse=step;coarse<depth/2;coarse+=step) {
            const int delta=Difference(read(coarse),border);
            const bool possible=delta>=6 && previousDelta<16;previousDelta=delta;
            if(!possible)continue;
            const int start=std::max(1,coarse-step+1);
            for(int at=start;at<=coarse;++at) {
                if(at<=lastHit+sustain)continue;
                const auto first=read(at),outside=read(at-1);
                const int halo=Difference(outside,border),jump=int(first.luma)-int(outside.luma);
                if(halo>12 || jump<6 || (halo>5 && jump<std::max(16,4*halo+4)))continue;
                const auto second=read(at+1),third=read(at+2),inside=read(at+sustain);
                if(Difference(second,border)<6||Difference(third,border)<6||Difference(inside,border)<6)continue;
                if(Difference(first,border)<16 && Difference(second,border)>=16)continue;
                add(at,Difference(first,border)>=16);lastHit=at;
            }
        }
    }
    auto& result=out.edges[unsigned(edge)];
    for(auto& entry:candidates)result.push_back(entry.second);
    std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b){return leading?a.coordinate<b.coordinate:a.coordinate>b.coordinate;});
}
void ClassifyGlyphRows(SourcePictureObservation& out,const AnalysisEvaluationBudget* budget) {
    for(auto& component:out.components) {
        CheckAnalysisEvaluationBudget(budget);
        const int h=component.bottom-component.top,w=component.right-component.left;
        if(h>out.height/12 || w>2*h || w<2 || h<3)continue;
        unsigned peers=0;
        for(const auto& other:out.components) {
            const int oh=other.bottom-other.top,ow=other.right-other.left;
            if(ow>2*oh || std::abs(other.top-component.top)>std::max(2,h/4) || std::abs(oh-h)>std::max(2,h/3))continue;
            ++peers;
        }
        component.glyphLike=peers>=6;
    }
}
void MarkGlyphEdges(SourcePictureObservation& out,const AnalysisEvaluationBudget* budget) {
    for(unsigned side:{1u,3u})for(auto& edge:out.edges[side]) {
        CheckAnalysisEvaluationBudget(budget);
        unsigned glyphVotes=0;
        for(unsigned i=0;i<kEdgeProbes;++i) {
            const int x=int(uint64_t(2*i+1)*out.width/(2*kEdgeProbes));
            for(const auto& component:out.components) {
                const int position=side==1?component.top:component.bottom;
                if(component.glyphLike && x>=component.left && x<component.right &&
                    std::abs(position-edge.coordinate)<=out.overviewStepY) {++glyphVotes;break;}
            }
        }
        edge.overlayLike=glyphVotes*2>=edge.support && glyphVotes>=8;
    }
}
const SourcePictureEdgeCandidate* Select(const std::vector<SourcePictureEdgeCandidate>& candidates,int terminal) {
    for(const auto& candidate:candidates) {
        unsigned regions=0;for(auto n:candidate.regionSupport)if(n>=2)++regions;
        const bool extent=candidate.coordinate==terminal && candidate.support>=kEdgeProbes/2 && regions>=2;
        // A center caption/backing can occlude both middle quarters of a real
        // border. Broad agreement at the SAME coordinate in opposite outer
        // quarters is independently sufficient; two narrow edge widgets are
        // not. Retain the existing total-support requirement and all topology
        // reconciliation instead of inferring an edge from caption geometry.
        const bool pairedOuter=candidate.regionSupport[0]>=12 && candidate.regionSupport[3]>=12;
        if(!candidate.overlayLike && (extent ||
            (candidate.support>=kEdgeProbes/4 && (regions>=3 || pairedOuter))))return &candidate;
    }
    return nullptr;
}
}
namespace {
bool UsableCell(const SourcePictureObservation& observation,int index) {
    const auto label=observation.overviewLabels[size_t(index)];
    return label && label<=observation.components.size() && !observation.components[label-1].glyphLike;
}
// Independent positive layout evidence: at least two substantial occupied panel
// bands with a shared horizontal scaffold. A subtitle, logo or one continuous
// movie cannot supply this body structure merely by contradicting a crop.
bool PanelScaffold(const SourcePictureObservation& observation,const ActivePictureBounds& candidate,const AnalysisEvaluationBudget* budget) {
    struct Band {int first,last,left,right;};
    std::vector<Band> bands;
    const int w=observation.overviewWidth,h=observation.overviewHeight;
    const int sx=observation.overviewStepX,sy=observation.overviewStepY;
    bool active=false;Band current{};
    const auto finish=[&]() {
        if(active && (current.last-current.first)*sy>=std::max(8,observation.height/12))bands.push_back(current);
        active=false;
    };
    for(int y=0;y<h;++y) {
        CheckAnalysisEvaluationBudget(budget);
        const int nativeY=std::min(observation.height-1,y*sy+sy/2);
        unsigned count=0;int left=w,right=0;
        if(nativeY>=candidate.top && nativeY<candidate.bottom)for(int x=0;x<w;++x) {
            const int nativeX=std::min(observation.width-1,x*sx+sx/2);
            if(nativeX<candidate.left || nativeX>=candidate.right || !UsableCell(observation,y*w+x))continue;
            ++count;left=std::min(left,x);right=std::max(right,x+1);
        }
        const bool broad=count>=unsigned(std::max(4,w/4)) && right-left>=w/4;
        if(!broad){finish();continue;}
        if(!active){current={y,y+1,left,right};active=true;}
        else {current.last=y+1;current.left=std::min(current.left,left);current.right=std::max(current.right,right);}
    }
    finish();
    for(size_t i=1;i<bands.size();++i) {
        const auto& previous=bands[i-1];const auto& next=bands[i];
        const int shared=std::min(previous.right,next.right)-std::max(previous.left,next.left);
        const int narrower=std::min(previous.right-previous.left,next.right-next.left);
        if(next.first-previous.last>=2 && shared*4>=narrower*3)return true;
    }
    return false;
}
}
SourcePictureCandidateAssessment ReconcileSourcePictureCandidate(const SourcePictureObservation& observation,const ActivePictureBounds& candidate,const AnalysisEvaluationBudget* budget) {
    SourcePictureCandidateAssessment out;
    const int w=observation.overviewWidth,h=observation.overviewHeight;
    if(!observation.complete || !observation.topologyComplete || w<=0 || h<=0 ||
        observation.overviewLabels.size()!=size_t(w)*h ||
        candidate.rasterWidth!=observation.width || candidate.rasterHeight!=observation.height ||
        candidate.left<0 || candidate.top<0 || candidate.right>observation.width || candidate.bottom>observation.height ||
        candidate.left>=candidate.right || candidate.top>=candidate.bottom)return out;
    out.evaluated=true;
    const int sx=observation.overviewStepX,sy=observation.overviewStepY;
    if(sx<=0 || sy<=0)return {};
    const auto boundaryCell=[](int coordinate,int count,int step,int extent) {
        int low=0,high=count;
        while(low<high) {
            const int middle=low+(high-low)/2;
            if(std::min(extent-1,middle*step+step/2)<coordinate)low=middle+1;
            else high=middle;
        }
        return low;
    };
    bool headerSupported=false;
    for(unsigned side=0;side<4;++side) {
        CheckAnalysisEvaluationBudget(budget);
        int clipLeft=0,clipTop=0,clipRight=w,clipBottom=h;
        if(side==0)clipRight=boundaryCell(candidate.left,w,sx,observation.width);
        if(side==1)clipBottom=boundaryCell(candidate.top,h,sy,observation.height);
        if(side==2)clipLeft=boundaryCell(candidate.right,w,sx,observation.width);
        if(side==3)clipTop=boundaryCell(candidate.bottom,h,sy,observation.height);
        if(clipLeft>=clipRight || clipTop>=clipBottom)continue;
        auto runs=BuildRuns(clipLeft,clipTop,clipRight,clipBottom,[&](int x,int y) {
            return UsableCell(observation,y*w+x);
        },budget);
        struct Extent {int left,top,right=0,bottom=0;unsigned cells=0;};
        std::vector<Extent> extents(runs.size(),{w,h,0,0,0});
        for(size_t i=0;i<runs.size();++i) {
            if((i&255)==0)CheckAnalysisEvaluationBudget(budget);
            auto& extent=extents[RunRoot(runs,i)];const auto& run=runs[i];
            extent.left=std::min(extent.left,run.left);extent.top=std::min(extent.top,run.row);
            extent.right=std::max(extent.right,run.right);extent.bottom=std::max(extent.bottom,run.row+1);
            extent.cells+=unsigned(run.right-run.left);
        }
        unsigned count=0;std::array<unsigned,4> regions{};
        for(const auto& extent:extents) {
            // Clipping precedes topology, so a connector cannot hide header.
            if(extent.cells<4 || (extent.bottom-extent.top)*sy<std::max(4,observation.height/40) ||
                (extent.right-extent.left)*sx<4)continue;
            ++count;++out.exteriorComponents;
            const bool horizontal=side==1||side==3;
            const int low=horizontal?extent.left*sx:extent.top*sy,high=horizontal?extent.right*sx:extent.bottom*sy;
            const int along=horizontal?observation.width:observation.height;
            for(unsigned quarter=0;quarter<4;++quarter)
                if(low<int((quarter+1)*along/4) && high>int(quarter*along/4))++regions[quarter];
        }
        out.exteriorRegions=std::max(out.exteriorRegions,Regions(regions));
        const bool distributed=count>=3 && Regions(regions)>=3;
        out.contradicted=out.contradicted || distributed;
        headerSupported=headerSupported || (side==1 && distributed);
    }
    out.canvasScaffoldSupported=headerSupported && PanelScaffold(observation,candidate,budget);
    out.reason=out.canvasScaffoldSupported?"composite-layout-scaffold":out.contradicted?
        "distributed-exterior-components":"no-distributed-exterior-contradiction";
    return out;
}
SourcePictureObservation MeasureSourcePicture(const AnalysisLumaSource& source,const AnalysisEvaluationBudget* budget) {
    SourcePictureObservation out;out.sourceGeneration=source.generation;out.width=source.width;out.height=source.height;
    if(!source.IsValid())return out;
    Reader reader{source,budget};
    try {
        CheckAnalysisEvaluationBudget(budget);
        std::vector<int> perimeter;perimeter.reserve(256);
        for(int i=0;i<64;++i) {
            const int x=(2*i+1)*source.width/128,y=(2*i+1)*source.height/128;
            perimeter.push_back(reader.At(x,0).luma);perimeter.push_back(reader.At(x,source.height-1).luma);
            perimeter.push_back(reader.At(0,y).luma);perimeter.push_back(reader.At(source.width-1,y).luma);
        }
        std::sort(perimeter.begin(),perimeter.end());
        int floor=perimeter[perimeter.size()/20];
        // Bright picture covering every raster edge is not a bright matte.
        if(floor>104)floor=source.range==AnalysisLumaRange::Full?0:64;
        auto& reference=out.blackReference;
        reference.evaluated=true;reference.floor=reference.observedFloor=floor;
        reference.threshold=reference.observedThreshold=std::min(104,floor+24);
        reference.mode=source.range!=AnalysisLumaRange::Full?ActivePictureBlackReferenceMode::LEGACY:
            floor?ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED:ActivePictureBlackReferenceMode::FULL_NOMINAL;
        // No qualified edge/axis follows from a perimeter percentile alone.
        Components(reader,floor,out);
        ClassifyGlyphRows(out,budget);
        for(auto edge:{SourcePictureEdge::Left,SourcePictureEdge::Top,SourcePictureEdge::Right,SourcePictureEdge::Bottom})EdgeCandidates(reader,edge,floor,out);
        MarkGlyphEdges(out,budget);
        out.complete=true;out.outcome=SourcePictureOutcome::Partial;out.reason="insufficient-distributed-edge-support";
        const auto* left=Select(out.edges[0],0);const auto* top=Select(out.edges[1],0);
        const auto* right=Select(out.edges[2],out.width);const auto* bottom=Select(out.edges[3],out.height);
        if(left&&top&&right&&bottom&&left->coordinate<right->coordinate&&top->coordinate<bottom->coordinate) {
            ActivePictureBounds bounds;bounds.rasterWidth=source.width;bounds.rasterHeight=source.height;
            bounds.left=left->coordinate;bounds.top=top->coordinate;bounds.right=right->coordinate;bounds.bottom=bottom->coordinate;
            bounds.aspectRatio=double(bounds.right-bounds.left)/(bounds.bottom-bounds.top);
            const auto reconciliation=ReconcileSourcePictureCandidate(out,bounds,budget);
            if(reconciliation.contradicted) {
                out.canvasSupported=reconciliation.canvasScaffoldSupported;
                out.blankMarginsUnresolved=out.canvasSupported;
                out.outcome=out.canvasSupported?SourcePictureOutcome::CanvasEstimate:SourcePictureOutcome::Partial;
                out.reason=out.canvasSupported?"composite-canvas-with-unresolved-blank-margins":"candidate-contradicted-canvas-unproven";
                for(auto& side:out.edges)for(auto& candidate:side)candidate.contradicted=true;
            } else {
                out.measuredBounds=bounds;out.rectangleAvailable=true;out.outcome=SourcePictureOutcome::Measured;out.reason="distributed-source-boundaries";
            }
        }
    } catch(const TopologyLimit&) {
        out.complete=false;out.topologyComplete=false;out.rectangleAvailable=false;out.canvasSupported=false;
        out.outcome=SourcePictureOutcome::Partial;out.reason="bounded-topology-component-limit";
    } catch(const SourceReadFailure&) {
        out.complete=false;out.rectangleAvailable=false;out.canvasSupported=false;
        out.outcome=SourcePictureOutcome::NotMeasured;out.reason="measurement-source-unavailable";
    } catch(const AnalysisEvaluationCancelled&) {
        // Partial work never masquerades as a complete measurement.
        out.complete=false;out.rectangleAvailable=false;out.outcome=SourcePictureOutcome::NotMeasured;out.reason="measurement-budget-exhausted";
    }
    out.samples=reader.samples;return out;
}
