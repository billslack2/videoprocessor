#include "pch.h"
#include "SourcePictureMeasurement.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <iterator>

namespace {
constexpr unsigned kEdgeProbes=128;
// Fixed-size source-coordinate overview, not an averaged/resized picture.
constexpr int kOverviewWidth=480,kOverviewHeight=270;
constexpr size_t kMaximumComponents=512;
constexpr uint16_t kUnclassifiedForeground=0xffff;
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
    bool topologyComplete=true;
    auto labelRoot=[&](size_t index) {
        const auto root=RunRoot(runs,index);auto& label=labels[root];const auto& run=runs[index];
        if(!label) {
            if(out.components.size()>=kMaximumComponents) {
                label=kUnclassifiedForeground;topologyComplete=false;
            } else {
                label=uint16_t(out.components.size()+1);
                out.components.push_back({run.left*sx,run.row*sy,std::min(out.width,run.right*sx),
                    std::min(out.height,(run.row+1)*sy),0});
            }
        }
        return label;
    };
    // Protect current raster caption/contact classification from an interior
    // starfield consuming the bounded component budget in row-major order.
    for(unsigned pass=0;pass<2;++pass)for(size_t i=0;i<runs.size();++i) {
        if((i&255)==0)CheckAnalysisEvaluationBudget(reader.budget);
        const auto& run=runs[i];
        if(pass==0?(run.row==0 || run.row==h-1):(run.left==0 || run.right==w))labelRoot(i);
    }
    for(size_t i=0;i<runs.size();++i) {
        if((i&255)==0)CheckAnalysisEvaluationBudget(reader.budget);
        const auto label=labelRoot(i);const auto& run=runs[i];
        if(label!=kUnclassifiedForeground) {
            auto& component=out.components[label-1];
            component.left=std::min(component.left,run.left*sx);component.top=std::min(component.top,run.row*sy);
            component.right=std::max(component.right,std::min(out.width,run.right*sx));
            component.bottom=std::max(component.bottom,std::min(out.height,(run.row+1)*sy));
            component.samples+=unsigned(run.right-run.left);
        }
        std::fill(out.overviewLabels.begin()+size_t(run.row)*w+run.left,
            out.overviewLabels.begin()+size_t(run.row)*w+run.right,label);
    }
    out.topologyComplete=topologyComplete;
}
void MarkGlyphEdge(SourcePictureObservation& out,unsigned side,SourcePictureEdgeCandidate& edge,
    const AnalysisEvaluationBudget* budget);

void EdgeCandidates(Reader& reader,SourcePictureEdge edge,int floor,SourcePictureObservation& out,bool terminalOnly=false) {
    const bool vertical=edge==SourcePictureEdge::Top||edge==SourcePictureEdge::Bottom;
    const bool leading=edge==SourcePictureEdge::Top||edge==SourcePictureEdge::Left;
    const int depth=vertical?out.height:out.width,along=vertical?out.width:out.height;
    const int step=std::max(2,depth/540),sustain=std::max(6,depth/270);
    // A qualified raster contact is the first eligible outer candidate. Its
    // interior alternatives cannot change selection or topology reconciliation.
    // Apply the same glyph veto before short-circuiting; rejected/local terminal
    // evidence still takes the complete competing-boundary scan below.
    SourcePictureEdgeCandidate terminal;
    terminal.coordinate=terminal.minimum=terminal.maximum=leading?0:depth;
    terminal.coverage=kEdgeProbes;terminal.strong=true;
    for(unsigned ray=0;ray<kEdgeProbes;++ray) {
        const int position=int(uint64_t(2*ray+1)*along/(2*kEdgeProbes));
        auto read=[&](int offset) {
            const int coordinate=leading?offset:depth-1-offset;
            return reader.At(vertical?position:coordinate,vertical?coordinate:position);
        };
        if(Foreground(read(0),floor) && Foreground(read(sustain),floor)) {
            ++terminal.support;++terminal.regionSupport[ray/32];
            terminal.supportingRays[ray/64]|=uint64_t(1)<<(ray%64);
        }
    }
    if(terminalOnly) {out.edges[unsigned(edge)].push_back(terminal);return;}
    unsigned terminalRegions=0;
    for(auto count:terminal.regionSupport)if(count>=2)++terminalRegions;
    if(terminal.support>=kEdgeProbes/2 && terminalRegions>=2) {
        MarkGlyphEdge(out,unsigned(edge),terminal,reader.budget);
        if(!terminal.overlayLike) {out.edges[unsigned(edge)].push_back(terminal);return;}
    }
    struct Votes {
        SourcePictureEdgeCandidate edge;
        std::array<uint64_t,2> rays{};
    };
    const auto profile=[&](unsigned side,unsigned ray,int lo,int hi,int begin,int end,int readLo,int readHi,bool filtered) {
        if(!out.transitionProfilesComplete)return;
        if(out.transitionProfiles.size()>=8192) {
            out.transitionProfilesComplete=false;out.transitionProfiles.clear();return;
        }
        out.transitionProfiles.push_back({side,ray,lo,hi,begin,end,readLo,readHi,filtered});
    };
    std::map<int,Votes> candidates;
    // Exact immutable native samples, reused only within this ray. Bound memory
    // independently of source dimensions; unusual larger offsets still read
    // directly. All luma/chroma bits and source coordinates remain unchanged.
    const size_t cacheSize=size_t(std::min(8192,depth/2+sustain+8));
    std::vector<AnalysisLumaSample> raySamples(cacheSize);
    std::vector<uint8_t> raySampleValid(cacheSize);
    for(unsigned i=0;i<kEdgeProbes;++i) {
        std::fill(raySampleValid.begin(),raySampleValid.end(),uint8_t(0));
        const int position=int((uint64_t(2*i+1)*along)/(2*kEdgeProbes));
        unsigned logicalRequests=0;
        auto read=[&](int offset) {
            // Cache hits are still work inside the same cooperative deadline.
            if((logicalRequests++&255u)==0)CheckAnalysisEvaluationBudget(reader.budget);
            if(offset>=0 && size_t(offset)<cacheSize && raySampleValid[size_t(offset)])return raySamples[size_t(offset)];
            const int p=leading?offset:depth-1-offset;
            const auto value=reader.At(vertical?position:p,vertical?p:position);
            if(offset>=0 && size_t(offset)<cacheSize) {
                raySamples[size_t(offset)]=value;raySampleValid[size_t(offset)]=1;
            }
            return value;
        };
        const auto border=read(0);
        auto add=[&](int offset,bool strong,int uncertainty=0) {
            const int coordinate=leading?offset:depth-offset;
            const int minimum=leading?coordinate:coordinate-uncertainty;
            const int maximum=leading?coordinate+uncertainty:coordinate;
            // Each ray supports only its measured abrupt point or bounded
            // integrated interval. Count it once at every coordinate it can
            // actually establish, rather than splitting uncertain departures
            // by their first row and later borrowing neighboring ray counts.
            for(int supported=minimum;supported<=maximum;++supported) {
                auto& votes=candidates[supported];auto& candidate=votes.edge;
                if(!candidate.coverage) {candidate.minimum=minimum;candidate.maximum=maximum;}
                else {
                    candidate.minimum=std::max(candidate.minimum,minimum);
                    candidate.maximum=std::min(candidate.maximum,maximum);
                }
                candidate.coordinate=supported;candidate.coverage=kEdgeProbes;
                const uint64_t bit=uint64_t(1)<<(i%64);
                if(!(votes.rays[i/64]&bit)) {++candidate.support;++candidate.regionSupport[i/32];}
                votes.rays[i/64]|=bit;
                candidate.strong=candidate.strong||strong;
            }
        };
        if(Foreground(border,floor) && Foreground(read(sustain),floor))add(0,true);
        // Keep every sustained matte-to-picture departure, including a scene
        // behind an earlier detached graphic. Integrate only a bounded native
        // profile; a continuing drift from the perimeter is not a boundary.
        int lastHit=-sustain,lastExamined=-1;
        int previousDelta=0;
        for(int coarse=step;coarse<depth/2;coarse+=step) {
            const int delta=Difference(read(coarse),border);
            const bool possible=delta>=6 && previousDelta<16;previousDelta=delta;
            if(!possible)continue;
            // A quantized departure may start two rows before the first coarse
            // sample crosses the ordinary detection threshold.
            const int start=std::max(1,coarse-step-1);
            for(int at=start;at<=coarse;++at) {
                if(at<=lastHit+sustain || at<=lastExamined)continue;
                lastExamined=at;
                // Resolve compact first-picture support before fitting a broader filtered step.
                const auto first=read(at),outside=read(at-1);
                // A resolved short plateau is already visible picture support.
                // Never replace a faint first-picture collar with a stronger
                // interior step merely because the latter fits a wider window.
                const int near1=read(at+1).luma,near2=read(at+2).luma;
                const int nearLow=std::min({int(first.luma),near1,near2});
                const int nearHigh=std::max({int(first.luma),near1,near2});
                const int far0=read(at+sustain).luma,far1=read(at+sustain+2).luma,far2=read(at+sustain+4).luma;
                const int departure=int(first.luma)-int(outside.luma);
                const bool continuing=far0<=far1 && far1<=far2 && far2-far0>=8;
                if(Difference(outside,border)<=5 && departure>0 && nearHigh-nearLow<=std::max(8,departure) &&
                    near1>=first.luma && near2>=near1 && near2-int(outside.luma)>=6 &&
                    far0-int(border.luma)>=6 && !continuing) {
                    const int width=departure>=6?0:2;
                    profile(unsigned(edge),i,leading?at:depth-at-width,leading?at+width:depth-at,leading?at:depth-at-width,leading?at+width:depth-at,leading?std::max(0,at-3):depth-1-(at+sustain+4),leading?at+sustain+4:depth-1-std::max(0,at-3),false);
                    add(at,near2-int(border.luma)>=16,width);lastHit=at;continue;
                }
                const int pre0=read(std::max(0,at-3)).luma,pre1=read(std::max(0,at-2)).luma,pre2=outside.luma;
                const int post0=read(at+sustain).luma,post1=read(at+sustain+2).luma,post2=read(at+sustain+4).luma;
                auto median=[](int a,int b,int c){return a+b+c-std::min({a,b,c})-std::max({a,b,c});};
                const int low=median(pre0,pre1,pre2),high=median(post0,post1,post2),contrast=high-low;
                const int preSpread=std::max({pre0,pre1,pre2})-std::min({pre0,pre1,pre2});
                const int postSpread=std::max({post0,post1,post2})-std::min({post0,post1,post2});
                if(contrast<8 || std::abs(low-int(border.luma))>12 ||
                    preSpread>std::max(4,contrast/8) || postSpread>std::max(4,contrast/4))continue;
                const int noise=std::max(2,std::max(preSpread,postSpread)/2);
                const int half=low+contrast/2;
                // Require a bounded rise which finishes at the interior level,
                // not an arbitrary point on a continuing low-gradient ramp.
                if(int(first.luma)<=low || int(outside.luma)>low+contrast/4 ||
                    int(read(at+sustain-1).luma)<high-std::max(noise,contrast/4))continue;
                int crossing=-1,lo=-1,hi=-1;bool monotone=true;
                int previous=outside.luma;
                for(int k=0;k<sustain;++k) {
                    const int value=read(at+k).luma;
                    if(value+std::max(4,noise)<previous)monotone=false;
                    if(crossing<0 && value>=half)crossing=at+k;
                    if(value+noise>=half && previous-noise<half) {if(lo<0)lo=at+k;hi=at+k;}
                    previous=value;
                }
                if(!monotone || crossing<0 || lo<0 || hi-lo>2)continue;
                // Preserve exact single-row steps; only ambiguous samples
                // straddling the measured half-level carry positional width.
                profile(unsigned(edge),i,leading?lo:depth-hi,leading?hi:depth-lo,leading?at:depth-(at+sustain-1),leading?at+sustain-1:depth-at,leading?std::max(0,at-3):depth-1-(at+sustain+4),leading?at+sustain+4:depth-1-std::max(0,at-3),true);
                add(lo,contrast>=16,hi-lo);lastHit=hi;
            }
        }
    }
    auto& result=out.edges[unsigned(edge)];
    // Keep competing coordinates separate: every supporting ray must contain
    // the nominated coordinate in its own measured interval. A union here
    // would reintroduce unsupported votes and transitive edge chains.
    for(auto& entry:candidates) {
        auto& votes=entry.second;
        votes.edge.supportingRays=votes.rays;
        result.push_back(votes.edge);
    }
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
void MarkGlyphEdge(SourcePictureObservation& out,unsigned side,SourcePictureEdgeCandidate& edge,
    const AnalysisEvaluationBudget* budget) {
    if(side!=1 && side!=3)return;
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
void MarkGlyphEdges(SourcePictureObservation& out,const AnalysisEvaluationBudget* budget) {
    for(unsigned side:{1u,3u})for(auto& edge:out.edges[side])MarkGlyphEdge(out,side,edge,budget);
}
const SourcePictureEdgeCandidate* Select(const std::vector<SourcePictureEdgeCandidate>& candidates,int terminal) {
    for(const auto& candidate:candidates) {
        unsigned regions=0;for(auto n:candidate.regionSupport)if(n>=2)++regions;
        const bool extent=candidate.coordinate==terminal &&
            ((candidate.support>=kEdgeProbes/2 && regions>=2) || candidate.rasterContactProved);
        // A center caption/backing can occlude both middle quarters of a real
        // border. Broad agreement at the SAME coordinate in opposite outer
        // quarters is independently sufficient; two narrow edge widgets are
        // not. Retain the existing total-support requirement and all topology
        // reconciliation instead of inferring an edge from caption geometry.
        const bool pairedOuter=candidate.regionSupport[0]>=12 && candidate.regionSupport[3]>=12;
        if(!candidate.overlayLike && (extent || candidate.perpendicularInsetProved ||
            (candidate.support>=kEdgeProbes/4 && (regions>=3 || pairedOuter))))return &candidate;
    }
    return nullptr;
}
// Opposite native contacts on the same rays corroborate the full span even
// when a bright picture occupies under half the edge. A third independently
// selected raster contact anchors that span; glyph vetoes already apply.
void CorroborateOpposedRasterContacts(SourcePictureObservation& out) {
    const int terminals[4]={0,0,out.width,out.height};
    for(unsigned first:{0u,1u}) {
        const unsigned second=first+2,adjacent=first==0?1:0;
        SourcePictureEdgeCandidate* pair[2]={};
        for(unsigned i=0;i<2;++i)for(auto& edge:out.edges[i?second:first])
            if(edge.coordinate==terminals[i?second:first] && !edge.overlayLike && edge.strong) {pair[i]=&edge;break;}
        if(!pair[0] || !pair[1] || pair[0]->support<32 || pair[1]->support<32)continue;
        bool anchor=false;
        for(unsigned side:{adjacent,adjacent+2}) {
            const auto* edge=Select(out.edges[side],terminals[side]);
            anchor=anchor || (edge && edge->coordinate==terminals[side]);
        }
        if(!anchor)continue;
        unsigned shared=0;std::array<unsigned,4> regions{};
        for(unsigned ray=0;ray<kEdgeProbes;++ray)if(pair[0]->supportingRays[ray/64]&pair[1]->supportingRays[ray/64]&(uint64_t(1)<<(ray%64))) {
            ++shared;++regions[ray/32];
        }
        unsigned broad=0;for(auto count:regions)if(count>=8)++broad;
        if(shared<32 || broad<2)continue;
        pair[0]->rasterContactProved=pair[1]->rasterContactProved=true;
        out.rasterContact.opposed=true;
    }
}
// Darkness inside the movie can leave fewer than 32 probes on one inset.
// Keep the original votes. Two independently selected perpendicular edges
// must reach that same coordinate, with no supported pixels beyond it. This
// recovers a measured rectangle without mirroring an edge or guessing an AR.
void CorroboratePerpendicularInset(SourcePictureObservation& out,const AnalysisEvaluationBudget* budget) {
    const int terminals[4]={0,0,out.width,out.height};
    const SourcePictureEdgeCandidate* selected[4]={};int missing=-1;
    for(unsigned side=0;side<4;++side) {
        selected[side]=Select(out.edges[side],terminals[side]);
        if(!selected[side]) {if(missing>=0)return;missing=int(side);}
    }
    if(missing<0)return;
    auto& proof=out.perpendicularInset;proof.evaluated=true;proof.side=missing;
    const unsigned cross=(missing==0 || missing==2)?1:0;
    const int axis=(missing==0 || missing==2)?out.width:out.height;
    proof.probeSpacing=(axis+int(kEdgeProbes)-1)/int(kEdgeProbes);
    const bool leading=missing<2;
    SourcePictureEdgeCandidate* nominee=nullptr;
    std::array<int,2> nomineeNearest{{-1,-1}};std::array<unsigned,2> nomineeOutside{};
    for(auto& candidate:out.edges[unsigned(missing)]) {
        CheckAnalysisEvaluationBudget(budget);
        unsigned regions=0;for(auto count:candidate.regionSupport)if(count>=3)++regions;
        if(candidate.coordinate==terminals[missing] || !candidate.strong || candidate.overlayLike ||
            candidate.support<12 || regions<3 || candidate.maximum-candidate.minimum>2)continue;
        // The opposite boundary must leave a nonempty measured picture span.
        const auto* opposite=selected[(unsigned(missing)+2)%4];
        if(leading?candidate.maximum>=opposite->minimum:candidate.minimum<=opposite->maximum)continue;
        std::array<int,2> nearest{{-1,-1}};std::array<unsigned,2> outside{};
        bool reached=true;
        for(unsigned end=0;end<2;++end) {
            const auto& edge=*selected[cross+2*end];int distance=axis;
            for(unsigned ray=0;ray<kEdgeProbes;++ray) {
                if(!(edge.supportingRays[ray/64]&(uint64_t(1)<<(ray%64))))continue;
                const int position=int(uint64_t(2*ray+1)*axis/(2*kEdgeProbes));
                const bool exterior=leading?position<candidate.minimum:position>=candidate.maximum;
                if(exterior) {++outside[end];continue;}
                const int inward=leading?position-candidate.maximum:candidate.minimum-1-position;
                if(inward>=0 && inward<distance) {distance=inward;nearest[end]=position;}
            }
            reached=reached && !outside[end] && nearest[end]>=0 && distance<=proof.probeSpacing;
        }
        if(candidate.support>=proof.support) {
            proof.coordinate=candidate.coordinate;proof.minimum=candidate.minimum;proof.maximum=candidate.maximum;
            proof.support=candidate.support;proof.regions=regions;proof.nearest=nearest;proof.outside=outside;
        }
        if(!reached)continue;
        ++proof.eligibleCandidates;nominee=&candidate;nomineeNearest=nearest;nomineeOutside=outside;
    }
    // Two distinct compatible insets remain unresolved; do not manufacture a
    // precise physical rectangle by selecting a preferred standard ratio.
    proof.ambiguous=proof.eligibleCandidates>1;
    if(proof.eligibleCandidates!=1)return;
    proof.accepted=true;nominee->perpendicularInsetProved=true;
    proof.nearest=nomineeNearest;proof.outside=nomineeOutside;
    proof.coordinate=nominee->coordinate;proof.minimum=nominee->minimum;proof.maximum=nominee->maximum;
    proof.support=nominee->support;proof.regions=0;
    for(auto count:nominee->regionSupport)if(count>=3)++proof.regions;
}
// A sparse starfield need not illuminate half an edge. Require a coherent
// native patch touching the exact raster plus strong near-edge texture in
// three separated quarters, with the other three boundaries already proved.
// This is positive source content, never an empty-overview certificate.
bool CompleteSparseTerminal(Reader& reader,int floor,SourcePictureObservation& out) {
    if(reader.source.range==AnalysisLumaRange::Unknown)return false;
    const int terminals[4]={0,0,out.width,out.height};
    int missing=-1;
    for(unsigned side=0;side<4;++side) {
        const auto* selected=Select(out.edges[side],terminals[side]);
        if(!selected) {if(missing>=0)return false;missing=int(side);}
        else if(selected->coordinate!=terminals[side])return false;
    }
    if(missing<0)return false;
    auto& proof=out.rasterContact;proof.sparseEvaluated=true;proof.sparseSide=missing;
    const bool vertical=missing==1||missing==3,leading=missing<2;
    const int along=vertical?out.width:out.height,depth=vertical?out.height:out.width;
    const int alongStep=std::max(1,(along+1079)/1080),inwardStep=2;
    const int band=std::max(6,std::min(16,depth/240));
    auto read=[&](int position,int inward) {
        position=std::max(0,std::min(along-1,position));inward=std::max(0,std::min(depth-1,inward));
        const int coordinate=leading?inward:depth-1-inward;
        return int(reader.At(vertical?position:coordinate,vertical?coordinate:position).luma);
    };
    auto glyphAt=[&](int position,int inward) {
        const int coordinate=leading?inward:depth-1-inward;
        const int x=vertical?position:coordinate,y=vertical?coordinate:position;
        if(x<0 || y<0 || x>=out.width || y>=out.height || out.overviewStepX<=0 || out.overviewStepY<=0)return false;
        const int cellX=x/out.overviewStepX,cellY=y/out.overviewStepY;
        // A native stroke may occupy the unsampled half of its cell. Positive
        // glyph evidence in an adjacent cell retains that sampling uncertainty.
        for(int row=std::max(0,cellY-1);row<std::min(out.overviewHeight,cellY+2);++row)
            for(int column=std::max(0,cellX-1);column<std::min(out.overviewWidth,cellX+2);++column) {
                const auto label=out.overviewLabels[size_t(row)*out.overviewWidth+column];
                if(label && label<=out.components.size() && out.components[label-1].glyphLike)return true;
            }
        return false;
    };
    auto coherent=[&](int position,int inward) {
        unsigned support=0;int peak=0;
        for(int a=-1;a<=1;++a)for(int d=-1;d<=1;++d) {
            const int value=read(position+a,inward+d);
            support+=value>floor+24;peak=std::max(peak,value);
        }
        return support>=6 && peak>floor+48;
    };
    for(int position=alongStep/2;position<along;position+=alongStep) {
        const unsigned region=std::min(3u,unsigned(uint64_t(position)*4/along));
        for(int inward=inwardStep/2;inward<band;inward+=inwardStep) {
            const int value=read(position,inward);
            if(value>floor+48 && coherent(position,inward)) {
                if(glyphAt(position,inward))proof.glyphVeto=true;
                else ++proof.textureSupport[region];
            }
        }
        // Select clear candidates near the edge, then verify a 3x3 native
        // footprint at the exact terminal. No threshold is lowered to noise.
        if(read(position,0)>floor+48 && coherent(position,1)) {
            if(glyphAt(position,0))proof.glyphVeto=true;
            else {
                ++proof.contactPatches;proof.peakContrast=std::max(proof.peakContrast,double(read(position,0)-floor));
            }
        }
    }
    for(auto count:proof.textureSupport)if(count)++proof.textureRegions;
    if(!proof.contactPatches || proof.textureRegions<3)return false;
    // A small edge mark cannot borrow a nearby bright body behind a matte.
    // Check the same candidate strip for a distributed inward onset first.
    unsigned insetSupport=0;std::array<unsigned,4> insetRegions{};
    for(unsigned ray=0;ray<kEdgeProbes;++ray) {
        const int position=int(uint64_t(2*ray+1)*along/(2*kEdgeProbes));
        const int outer=read(position,0),inner=read(position,band*2);
        if(outer<=floor+12 && inner>floor+24 && inner-outer>24) {
            ++insetSupport;++insetRegions[ray/32];
        }
    }
    unsigned broad=0;for(auto count:insetRegions)if(count>=2)++broad;
    if(insetSupport>=32 && broad>=3) {proof.insetVeto=true;return false;}
    proof.sparseAccepted=true;
    SourcePictureEdgeCandidate terminal;
    terminal.coordinate=terminal.minimum=terminal.maximum=terminals[missing];
    terminal.strong=true;terminal.rasterContactProved=true;
    out.edges[unsigned(missing)].insert(out.edges[unsigned(missing)].begin(),terminal);
    return true;
}
// A weak raster contact can be measured against two independently bounded
// current bars. Only run after every competing inset has been scanned. The
// native ray lattice is preserved for downstream perpendicular support tests.
void CompleteRelativeMatteContacts(Reader& reader,int floor,SourcePictureObservation& out) {
    if(reader.source.range==AnalysisLumaRange::Unknown)return;
    const int terminals[4]={0,0,out.width,out.height};
    for(unsigned side=0;side<4;++side) {
        if(Select(out.edges[side],terminals[side]))continue;
        const bool horizontal=side==1 || side==3,leading=side<2;
        const unsigned cross=horizontal?0:1;
        const auto* first=Select(out.edges[cross],terminals[cross]);
        const auto* last=Select(out.edges[cross+2],terminals[cross+2]);
        const int along=horizontal?out.width:out.height,depth=horizontal?out.height:out.width;
        if(!first || !last || first->coordinate<8 || last->coordinate>along-8 ||
            last->coordinate-first->coordinate<32)continue;
        const int begin=first->coordinate,end=last->coordinate;
        auto& proof=out.relativeMatteContacts[side];proof.evaluated=true;
        const int sustain=std::max(2,depth/960);
        const int offsets[4]={0,1,sustain,sustain+1};
        auto read=[&](int position,int inward) {
            const int d=leading?inward:depth-1-inward;
            const auto pixel=reader.At(horizontal?position:d,horizontal?d:position);
            return std::array<double,3>{double(pixel.luma),double(pixel.chromaU),double(pixel.chromaV)};
        };
        std::array<double,3> means[2]{},low[2],high[2];
        for(unsigned bar=0;bar<2;++bar) {
            low[bar].fill(1024);high[bar].fill(0);
            for(int n=0;n<8;++n)for(int d:offsets) {
                const auto pixel=read(bar?along-1-n:n,d);
                for(unsigned c=0;c<3;++c) {
                    means[bar][c]+=pixel[c]/32;
                    low[bar][c]=std::min(low[bar][c],pixel[c]);
                    high[bar][c]=std::max(high[bar][c],pixel[c]);
                }
            }
        }
        bool matched=true;
        for(unsigned c=0;c<3;++c) {
            proof.reference[c]=(means[0][c]+means[1][c])/2;
            proof.noise[c]=std::max(high[0][c]-low[0][c],high[1][c]-low[1][c]);
            matched=matched && proof.noise[c]<=8 && std::abs(means[0][c]-means[1][c])<=4;
        }
        matched=matched && std::abs(proof.reference[0]-floor)<=std::max(4.0,proof.noise[0]+2);
        proof.referencesMatched=matched;if(!matched)continue;
        proof.lumaMargin=std::max(6.0,proof.noise[0]+2);
        proof.chromaMargin=std::max(6.0,std::max(proof.noise[1],proof.noise[2])+2);
        SourcePictureEdgeCandidate terminal;
        terminal.coordinate=terminal.minimum=terminal.maximum=terminals[side];
        std::array<unsigned,4> activeRegions{};
        std::array<std::vector<double>,3> values;
        for(unsigned ray=0;ray<kEdgeProbes;++ray) {
            const int position=int(uint64_t(2*ray+1)*along/(2*kEdgeProbes));
            if(position<begin || position>=end)continue;
            ++proof.coverage;std::array<std::array<double,12>,3> patch{};unsigned sample=0;
            unsigned coherent=0;
            for(int a=-1;a<=1;++a)for(int d:offsets) {
                const auto pixel=read(std::max(begin,std::min(end-1,position+a)),d);
                for(unsigned c=0;c<3;++c)patch[c][sample]=pixel[c];
                ++sample;
                const double dy=pixel[0]-proof.reference[0];
                coherent+=dy>=-proof.lumaMargin && (dy>=proof.lumaMargin ||
                    std::max(std::abs(pixel[1]-proof.reference[1]),std::abs(pixel[2]-proof.reference[2]))>=proof.chromaMargin);
            }
            // Preserve native quantization; averaging a four-code edge can
            // turn real texture into a fractional, apparently weaker signal.
            for(unsigned c=0;c<3;++c) {
                auto& channel=patch[c];std::sort(channel.begin(),channel.end());
                values[c].push_back((channel[5]+channel[6])/2);
            }
            if(coherent<9)continue;
            ++proof.support;++activeRegions[std::min(3,(position-begin)*4/(end-begin))];
            ++terminal.regionSupport[ray/32];
            terminal.supportingRays[ray/64]|=uint64_t(1)<<(ray%64);
        }
        bool textured=false;
        for(unsigned c=0;c<3;++c)if(!values[c].empty()) {
            auto& v=values[c];std::sort(v.begin(),v.end());
            proof.texture[c]=v[(v.size()-1)*9/10]-v[(v.size()-1)/10];
            textured=textured || proof.texture[c]>=std::max(4.0,2*proof.noise[c]+2);
        }
        for(auto count:activeRegions)if(count>=4)++proof.regions;
        if(proof.coverage<32 || proof.support*2<proof.coverage || proof.regions<3 || !textured)continue;
        proof.accepted=true;terminal.support=proof.support;terminal.coverage=proof.coverage;
        terminal.strong=true;terminal.rasterContactProved=true;
        out.edges[side].insert(out.edges[side].begin(),terminal);
    }
}
// A dark picture edge must not be erased by treating an unqualified picture
// percentile as matte. This bounded current-source route requires the other
// three measured raster contacts, no competing inset, and distributed early
// spatial change connected continuously to the same picture body.
// The trend score describes sampled agreement, not independent-probability
// confidence: neighboring source samples can be correlated.
bool CompleteDarkTerminal(Reader& reader,int floor,SourcePictureObservation& out) {
    if(reader.source.range==AnalysisLumaRange::Unknown)return false;
    const int terminals[4]={0,0,out.width,out.height};
    int missing=-1;
    for(unsigned side=0;side<4;++side) {
        const auto* selected=Select(out.edges[side],terminals[side]);
        if(!selected) {if(missing>=0)return false;missing=int(side);}
        else if(selected->coordinate!=terminals[side])return false;
    }
    if(missing<0)return false;
    const bool vertical=missing==1 || missing==3,leading=missing<2;
    const int depth=vertical?out.height:out.width,along=vertical?out.width:out.height;
    if(depth<96 || along<96)return false;
    const int nominal=reader.source.range==AnalysisLumaRange::Full?0:64;
    std::array<unsigned,4> support{};std::array<double,4> regionalTrend{};
    std::array<uint64_t,2> supportingRays{};
    double trendSum=0,trendSquares=0,earlyTrendSum=0,earlyTrendSquares=0,lateTrendSum=0;
    auto& proof=out.darkTerminal;proof.evaluated=true;proof.side=missing;
    for(unsigned ray=0;ray<kEdgeProbes;++ray) {
        const int position=int(uint64_t(2*ray+1)*along/(2*kEdgeProbes));
        auto read=[&](int offset) {
            const int coordinate=leading?offset:depth-1-offset;
            return reader.At(vertical?position:coordinate,vertical?coordinate:position);
        };
        // Integrate contiguous native samples: every-other-pixel probes can
        // lock to one HDMI quantization phase. Three bounded spatial scales
        // test a continuing dark picture trend, not one fragile luma delta.
        double outer=0,innerPatch=0,earlyPatch=0,latePatch=0;
        for(int offset=0;offset<16;++offset) {
            outer+=read(offset).luma;
            const double early=read(depth/24+offset).luma;
            earlyPatch+=early;
            const double late=read(depth/8+offset).luma;
            latePatch+=late;
            innerPatch+=early+read(depth/12+offset).luma+late;
        }
        const double earlyTrend=(earlyPatch-outer)/16;
        earlyTrendSum+=earlyTrend;earlyTrendSquares+=earlyTrend*earlyTrend;
        lateTrendSum+=(latePatch-outer)/16;
        const double trend=(innerPatch/3-outer)/16;
        trendSum+=trend;trendSquares+=trend*trend;regionalTrend[ray/32]+=trend;
        const auto border=read(0);auto previous=border;
        bool continuous=true;unsigned rises=0;
        for(int sample=1;sample<=4;++sample) {
            const auto current=read(sample*depth/8);
            const int difference=int(current.luma)-int(previous.luma);
            if(difference < -8)continuous=false;
            if(difference>=6)++rises;
            previous=current;
        }
        if(continuous && rises>=2 && int(previous.luma)-int(border.luma)>=16 &&
            Foreground(border,nominal) && Foreground(previous,floor)) {
            ++support[ray/32];supportingRays[ray/64]|=uint64_t(1)<<(ray%64);
        }
    }
    const double mean=trendSum/kEdgeProbes;
    const double variance=std::max(0.0,(trendSquares-trendSum*trendSum/kEdgeProbes)/(kEdgeProbes-1));
    const double earlyMean=earlyTrendSum/kEdgeProbes;
    const double earlyVariance=std::max(0.0,(earlyTrendSquares-earlyTrendSum*earlyTrendSum/kEdgeProbes)/(kEdgeProbes-1));
    proof.earlyMean=earlyMean;proof.integratedMean=mean;proof.lateMean=lateTrendSum/kEdgeProbes;
    proof.earlyScore=earlyVariance>0?std::min(1e6,earlyMean*earlyMean*kEdgeProbes/earlyVariance):(earlyMean>0?1e6:0);
    proof.integratedScore=variance>0?std::min(1e6,mean*mean*kEdgeProbes/variance):(mean>0?1e6:0);
    unsigned total=0,regions=0;
    for(unsigned i=0;i<4;++i){total+=support[i];if(support[i]>=12)++regions;proof.regionalMean[i]=regionalTrend[i]/32;}
    proof.supportingRays=total;proof.supportingRegions=regions;
    // The early patch remains independent evidence: later picture contrast
    // cannot manufacture contact through a noisy raised matte prefix. The
    // third patch is three times deeper; allow one 8-bit-equivalent code of
    // rounding, not an arbitrarily delayed rise hidden by spatial averaging.
    if(earlyMean<=0 || earlyMean*earlyMean*kEdgeProbes<=4*earlyVariance ||
        proof.lateMean>3*earlyMean+4 || mean<=0 || mean*mean*kEdgeProbes<=9*variance ||
        std::any_of(regionalTrend.begin(),regionalTrend.end(),[](double sum){return sum<=0;}) ||
        total<kEdgeProbes/2 || regions<3)return false;
    proof.accepted=true;
    SourcePictureEdgeCandidate terminal;
    terminal.coordinate=terminal.minimum=terminal.maximum=terminals[missing];
    terminal.support=total;terminal.coverage=kEdgeProbes;terminal.regionSupport=support;terminal.strong=true;
    terminal.supportingRays=supportingRays;
    out.edges[unsigned(missing)].insert(out.edges[unsigned(missing)].begin(),terminal);
    return true;
}
}
namespace {
// A rectangle cannot discard distributed native boundary support shared by
// both perpendicular edges. Overview foreground intentionally has a different
// support scale and may miss dark picture that these native profiles measured.
bool PerpendicularSupportOutside(const SourcePictureObservation& observation,const ActivePictureBounds& bounds) {
    const int coordinates[4]={bounds.left,bounds.top,bounds.right,bounds.bottom};
    const SourcePictureEdgeCandidate* edges[4]={};
    for(unsigned side=0;side<4;++side)for(const auto& edge:observation.edges[side]) {
        if(edge.coordinate==coordinates[side] && !edge.overlayLike) {edges[side]=&edge;break;}
    }
    for(unsigned side=0;side<4;++side) {
        const bool horizontal=side==0 || side==2;
        const unsigned first=horizontal?1:0,second=horizontal?3:2;
        if(!edges[first] || !edges[second])continue;
        const int extent=horizontal?observation.width:observation.height;
        const int low=side<2?0:coordinates[side],high=side<2?coordinates[side]:extent;
        if(low>=high)continue;
        unsigned count=0;std::array<unsigned,4> regions{};
        for(unsigned ray=0;ray<kEdgeProbes;++ray) {
            const uint64_t bit=uint64_t(1)<<(ray%64);
            if(!(edges[first]->supportingRays[ray/64]&edges[second]->supportingRays[ray/64]&bit))continue;
            const int position=int(uint64_t(2*ray+1)*extent/(2*kEdgeProbes));
            if(position<low || position>=high)continue;
            ++count;++regions[unsigned(uint64_t(position-low)*4/(high-low))];
        }
        unsigned distributed=0;for(auto region:regions)if(region>=2)++distributed;
        if(count>=kEdgeProbes/4 && distributed>=3)return true;
    }
    return false;
}
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
    const bool perpendicularContradiction=PerpendicularSupportOutside(observation,candidate);
    out.contradicted=out.contradicted || perpendicularContradiction;
    out.canvasScaffoldSupported=headerSupported && PanelScaffold(observation,candidate,budget);
    out.reason=out.canvasScaffoldSupported?"composite-layout-scaffold":out.contradicted?
        (perpendicularContradiction?"perpendicular-native-boundaries-outside-candidate":"distributed-exterior-components"):
        "no-distributed-exterior-contradiction";
    return out;
}
SourcePictureObservation MeasureSourcePicture(const AnalysisLumaSource& source,const AnalysisEvaluationBudget* budget) {
    SourcePictureObservation out;out.sourceGeneration=source.generation;out.width=source.width;out.height=source.height;
    if(!source.IsValid())return out;
    Reader reader{source,budget};
    try {
        CheckAnalysisEvaluationBudget(budget);
        std::vector<int> perimeter;perimeter.reserve(768);
        for(int i=0;i<64;++i) {
            const int x=(2*i+1)*source.width/128,y=(2*i+1)*source.height/128;
            // A single regularly spaced pixel can sample just the high phase
            // of encoded bar noise and misclassify the reference as picture.
            // Keep native coordinates, but cover neighboring encoding phases.
            for(int sample=0;sample<3;++sample) {
                // An L-shaped footprint covers both along-edge and inward
                // phase variation. Along-edge-only probes still alias a stripe
                // whose encoding phase is constant in that direction.
                const int along=sample==1?1:0,inward=sample==2?1:0;
                const int px=std::min(source.width-1,x+along),py=std::min(source.height-1,y+along);
                perimeter.push_back(reader.At(px,std::min(source.height-1,inward)).luma);
                perimeter.push_back(reader.At(px,std::max(0,source.height-1-inward)).luma);
                perimeter.push_back(reader.At(std::min(source.width-1,inward),py).luma);
                perimeter.push_back(reader.At(std::max(0,source.width-1-inward),py).luma);
            }
        }
        std::sort(perimeter.begin(),perimeter.end());
        int floor=perimeter[perimeter.size()/20];
        unsigned codeBits=0;for(int value:perimeter)codeBits|=unsigned(value);
        while(out.lumaQuantum<4 && !(codeBits&out.lumaQuantum))out.lumaQuantum*=2;
        // Allow one 8-bit-equivalent code in the canonical ten-bit domain.
        // RGB/matrix conversion may populate low bits even when source rounding
        // varies by four codes, so observed code spacing is diagnostic only.
        // In particular, 104 ->108 must not reset the reference to nominal 64.
        // Bright perimeter content still falls back to nominal source black.
        if(floor>108)floor=source.range==AnalysisLumaRange::Full?0:64;
        auto& reference=out.blackReference;
        reference.evaluated=true;reference.floor=reference.observedFloor=floor;
        reference.threshold=reference.observedThreshold=std::max(floor,std::min(104,floor+24));
        reference.mode=source.range!=AnalysisLumaRange::Full?ActivePictureBlackReferenceMode::LEGACY:
            floor?ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED:ActivePictureBlackReferenceMode::FULL_NOMINAL;
        // No qualified edge/axis follows from a perimeter percentile alone.
        // Native boundary evidence comes first. Interior object complexity
        // cannot prevent collecting independent current-source edge facts.
        for(auto edge:{SourcePictureEdge::Left,SourcePictureEdge::Top,SourcePictureEdge::Right,SourcePictureEdge::Bottom})EdgeCandidates(reader,edge,floor,out,true);
        Components(reader,floor,out);
        ClassifyGlyphRows(out,budget);
        MarkGlyphEdges(out,budget);
        CorroborateOpposedRasterContacts(out);
        // Gradient continuity still requires the complete competing inset
        // scan below; only direct coherent terminal content has a fast route.
        bool darkTerminalCompleted=false;
        const bool sparseTerminalCompleted=CompleteSparseTerminal(reader,floor,out);
        bool allRasterContacts=true;
        const int terminals[4]={0,0,out.width,out.height};
        for(unsigned side=0;side<4;++side) {
            const auto* selected=Select(out.edges[side],terminals[side]);
            allRasterContacts=allRasterContacts && selected && selected->coordinate==terminals[side];
        }
        if(!allRasterContacts) {
            // Any uncertain geometry takes the complete competing-boundary
            // route with glyph classification ready before its fast paths.
            for(unsigned side=0;side<4;++side) {
                out.edges[side].clear();EdgeCandidates(reader,SourcePictureEdge(side),floor,out);
            }
            MarkGlyphEdges(out,budget);
            CorroborateOpposedRasterContacts(out);
            CompleteRelativeMatteContacts(reader,floor,out);
            darkTerminalCompleted=CompleteDarkTerminal(reader,floor,out);
            CorroboratePerpendicularInset(out,budget);
        }
        out.complete=out.topologyComplete;out.outcome=SourcePictureOutcome::Partial;
        out.reason=out.topologyComplete?"insufficient-distributed-edge-support":"bounded-topology-component-limit";
        const auto* left=Select(out.edges[0],0);const auto* top=Select(out.edges[1],0);
        const auto* right=Select(out.edges[2],out.width);const auto* bottom=Select(out.edges[3],out.height);
        if(left&&top&&right&&bottom&&left->coordinate<right->coordinate&&top->coordinate<bottom->coordinate) {
            ActivePictureBounds bounds;bounds.rasterWidth=source.width;bounds.rasterHeight=source.height;
            bounds.left=left->coordinate;bounds.top=top->coordinate;bounds.right=right->coordinate;bounds.bottom=bottom->coordinate;
            bounds.aspectRatio=double(bounds.right-bounds.left)/(bounds.bottom-bounds.top);
            const bool fullRaster=bounds.left==0 && bounds.top==0 && bounds.right==source.width && bounds.bottom==source.height;
            // Incomplete topology cannot certify any discarded margin. Four
            // independently proved raster contacts discard nothing and need
            // no claim about the unclassified interior components.
            if(out.topologyComplete || fullRaster) {
                out.complete=true;
                const auto reconciliation=out.topologyComplete?ReconcileSourcePictureCandidate(out,bounds,budget):SourcePictureCandidateAssessment{};
                if(reconciliation.contradicted) {
                    out.canvasSupported=reconciliation.canvasScaffoldSupported;
                    out.blankMarginsUnresolved=out.canvasSupported;
                    out.outcome=out.canvasSupported?SourcePictureOutcome::CanvasEstimate:SourcePictureOutcome::Partial;
                    out.reason=out.canvasSupported?"composite-canvas-with-unresolved-blank-margins":"candidate-contradicted-canvas-unproven";
                    for(auto& side:out.edges)for(auto& candidate:side)candidate.contradicted=true;
                } else {
                    out.measuredBounds=bounds;out.rectangleAvailable=true;out.outcome=SourcePictureOutcome::Measured;
                    const SourcePictureEdgeCandidate* selected[4]={left,top,right,bottom};
                    for(unsigned side=0;side<4;++side) {
                        out.edgeMinimum[side]=selected[side]->minimum;out.edgeMaximum[side]=selected[side]->maximum;
                        out.transitionMinimum[side]=out.edgeMinimum[side];out.transitionMaximum[side]=out.edgeMaximum[side];
                    }
                    if(out.transitionProfilesComplete) {
                        auto& profiles=out.transitionProfiles;
                        profiles.erase(std::remove_if(profiles.begin(),profiles.end(),[&](const auto& p) {
                            return selected[p.side]->coordinate<p.minimum || selected[p.side]->coordinate>p.maximum;
                        }),profiles.end());
                        for(const auto& p:profiles) {
                            out.transitionMinimum[p.side]=std::min(out.transitionMinimum[p.side],p.transitionMinimum);
                            out.transitionMaximum[p.side]=std::max(out.transitionMaximum[p.side],p.transitionMaximum);
                            if(p.filtered)out.filteredEdgeMask|=1u<<p.side;
                        }
                    }

                    out.reason=out.perpendicularInset.accepted?"distributed-source-boundaries-with-perpendicular-inset-contact":
                        sparseTerminalCompleted?"native-raster-contact-with-distributed-sparse-texture":
                        !out.topologyComplete?"distributed-raster-contacts-with-bounded-topology":darkTerminalCompleted?
                        "distributed-source-boundaries-with-dark-terminal-continuity":"distributed-source-boundaries";
                }
            }
        }
    } catch(const SourceReadFailure&) {
        out.complete=false;out.rectangleAvailable=false;out.canvasSupported=false;
        out.outcome=SourcePictureOutcome::NotMeasured;out.reason="measurement-source-unavailable";
    } catch(const AnalysisEvaluationCancelled&) {
        // Partial work never masquerades as a complete measurement.
        out.complete=false;out.rectangleAvailable=false;out.outcome=SourcePictureOutcome::NotMeasured;out.reason="measurement-budget-exhausted";
    }
    out.samples=reader.samples;return out;
}
