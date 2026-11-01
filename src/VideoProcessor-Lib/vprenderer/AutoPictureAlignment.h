#pragma once
#include "AnamorphicPresentation.h"
#include <cstring>

// Read-only consumption of settled source geometry. Never returns source crops.
namespace AutoPictureAlignment
{
    using Rect = AlphaSourceCrop::PresentationRect;
    using Alignment = AlphaSourceCrop::VerticalPictureAlignment;
    struct ActivePicture { Rect bounds{}; bool available=false, protectEnvelope=false; };
    struct Warp { double scale=1.0, curve=2.0, protection=0.0; bool known=true; };
    inline double Bound(double value, double low, double high)
    { return std::max(low, std::min(high, value)); }
    inline bool Valid(const Rect& r)
    { return std::isfinite(r.left) && std::isfinite(r.top) && std::isfinite(r.right) &&
        std::isfinite(r.bottom) && r.right>r.left && r.bottom>r.top; }
    inline AnamorphicPresentation::Screen FitScreen(bool automatic, bool configured,
        double aspect, double lens, const Rect& output, Alignment alignment, int padding)
    {
        const bool autoEdge=automatic && alignment!=Alignment::CENTER;
        return AnamorphicPresentation::FitScreen(configured,aspect,lens,output,
            autoEdge ? Alignment::CENTER : alignment,autoEdge ? 0 : padding);
    }
    // Same inverse-coordinate curve as shipped NLS/NLS+. Invert it to locate
    // interior source edges in output coordinates; endpoints remain exact.
    inline double SampleCoordinate(double coordinate, const Warp& w)
    {
        const double centered=coordinate*2.0-1.0, r=std::abs(centered);
        double mapped;
        if (w.protection<=0.0)
            mapped=w.scale*r-(w.scale-1.0)*std::pow(r,w.curve);
        else if (r<=w.protection) mapped=r*w.scale;
        else {
            const double span=1.0-w.protection,t=(r-w.protection)/span;
            const double t2=t*t,t3=t2*t,t4=t3*t,t5=t4*t;
            mapped=(1-10*t3+15*t4-6*t5)*w.protection*w.scale+
                (t-6*t3+8*t4-3*t5)*span*w.scale+(10*t3-15*t4+6*t5)+
                (-4*t3+7*t4-3*t5)*span*std::max(0.20,1-0.5*(w.scale-1)*w.curve);
        }
        return (centered<0 ? -mapped : mapped)*0.5+0.5;
    }
    inline double OutputCoordinate(double source, const Warp& w)
    {
        if (source<=0 || source>=1 || w.scale==1.0) return source;
        double low=0,high=1;
        for (int i=0;i<40;++i) {
            const double middle=(low+high)*0.5;
            if (SampleCoordinate(middle,w)<source) low=middle; else high=middle;
        }
        return (low+high)*0.5;
    }
    struct Decision {
        Rect picture{}; double baseline=0, requestedShift=0, effectiveShift=0, effectiveTrim=0;
        const char* reason="manual";
    };
    inline Decision Place(bool automatic, Alignment alignment, int trim,
        const Rect& output, const Rect& screen, const Rect& source,
        const Rect& destination, const ActivePicture& active, const Warp& warp = {})
    {
        Decision d; d.picture=destination;
        if (!automatic) return d;
        if (alignment==Alignment::CENTER) { d.reason="center"; return d; }
        d.reason="geometry-unavailable";
        if (!active.available || !Valid(active.bounds) || !Valid(output) ||
            !Valid(screen) || !Valid(source) || !Valid(destination)) return d;
        const double top=std::max(active.bounds.top,source.top),
            bottom=std::min(active.bounds.bottom,source.bottom);
        if (bottom<=top || std::min(active.bounds.right,source.right)<=std::max(active.bounds.left,source.left)) return d;
        if (!warp.known && (top>source.top || bottom<source.bottom))
        { d.reason="unknown-nls-mapping"; return d; }
        const double fraction=(alignment==Alignment::TOP ? top-source.top : bottom-source.top)/(source.bottom-source.top);
        const double edge=destination.top+OutputCoordinate(fraction,warp)*(destination.bottom-destination.top);
        // Round the destination translation to the nearest whole output pixel,
        // with half pixels away from zero. Signed trim is applied exactly once.
        d.baseline=std::round((alignment==Alignment::TOP ? screen.top : screen.bottom)-edge);
        double low=std::ceil(output.top-destination.top), high=std::floor(output.bottom-destination.bottom);
        if (low>high) { d.reason="picture-exceeds-output"; return d; }
        d.requestedShift=d.baseline+trim;
        d.reason="pinned";
        if (active.protectEnvelope && (top>source.top || bottom<source.bottom)) {
            // Retain the whole subtitle accommodation, including its current
            // resting pose when it cannot fit in the physical screen.
            low=std::max(low,std::min(0.0,std::ceil(screen.top-destination.top)));
            high=std::min(high,std::max(0.0,std::floor(screen.bottom-destination.bottom)));
            if (Bound(d.requestedShift,low,high)!=d.requestedShift) d.reason="subtitle-envelope";
        }
        d.effectiveShift=Bound(d.requestedShift,low,high);
        d.effectiveTrim=d.effectiveShift-d.baseline;
        if (d.effectiveShift!=d.requestedShift && std::strcmp(d.reason,"pinned")==0) d.reason="output-limit";
        d.picture.top+=d.effectiveShift; d.picture.bottom+=d.effectiveShift;
        return d;
    }
}
