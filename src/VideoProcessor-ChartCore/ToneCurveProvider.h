#pragma once
#include <ToneMappingTuning.h>
#include <string>
#include <vector>

// The tone charts read libplacebo, which only the Config application loads
// (delay-loaded from VP Renderer's folder). This header is the seam: the page
// fills a Request without libplacebo, and the application registers the
// functions at startup. Anything that compiles the page alone sees no
// functions registered and shows the charts as unavailable.
namespace ToneCurveProvider
{
    // One part of a scene: a share of the picture at one brightness.
    struct ScenePart { double share; double nits; };

    struct Request
    {
        // Values as the profile resolves them: lower case, "auto" when unset.
        std::string quality = "high";
        std::string toneMapping = "auto";
        std::string peakDetection = "auto";
        ToneMappingTuning::Overrides toneTuning{};
        bool dynamicTarget = false;
        // Colour only: Gamut mapping and Gamut overflow correction.
        std::string gamutMapping = "auto";
        bool gamutOverflowCorrection = false;
        // The charts draw VP Renderer (Beta) only; false shows them unavailable.
        bool betaRenderer = false;
        bool defaultsKnown = true;
        double targetNits = 203.0;
        double blackNits = 0.0;
        // The scene the charts imagine; not VP settings. With peak detection it
        // is a mix of brightnesses whose brightest part is the peak; without,
        // the peak is the metadata peak and there is no mix.
        double scenePeakNits = 600.0;
        std::vector<ScenePart> sceneMix{ { 0.97, 13.0 }, { 0.03, 600.0 } };
        bool sceneAverageKnown = true;
        // The source's mastering peak (InfoFrame); 0 when it has none.
        double masteringPeakNits = 0.0;
        // Source points in PQ (0 to 1).
        std::vector<double> sourcePq;
    };

    struct Result
    {
        std::vector<double> profilePq;  // the profile, as VP runs it
        std::vector<double> defaultPq;  // libplacebo's own default curve, whatever the profile
        double inputMaxPq = 0.0;        // the top of libplacebo's input range
        double scenePeakPq = 0.0;       // the scene (or mastering) peak
        double outputMaxPq = 0.0;       // the display peak libplacebo uses
        double outputMinPq = 0.0;
        double inputMinPq = 0.0;        // the bottom of libplacebo's input range
        double kneePq = 0.0;            // the knee the film blend measures against
        std::string curve;              // the libplacebo curve in use
        double virtualTargetNits = 0.0; // the dynamic target's virtual peak; 0 when inactive
        double exposure = 1.0;          // the dynamic target's gain on the whole picture
        double masteringClip = 0.0;     // the mastering soft clip's strength as applied
        // What peak detection would measure on the mix; 0 without detection.
        double scenePqAverageNits = 0.0;     // the PQ average, which sets the knee
        double sceneLinearAverageNits = 0.0; // the linear average, which drives the dynamic target
    };

    // The Colour tab. The display is a chart choice, not VP's target primaries
    // setting. The source is P3-mastered in a BT.2020 container, its InfoFrame
    // saying P3.
    enum class Gamut { Rec709, P3D65, Bt2020 };

    // A source colour by its chromaticity: linear RGB in `space`, or 8-bit
    // sRGB (ColorChecker Classic, the published sRGB values) when srgb8.
    struct Patch
    {
        const char* name;
        Gamut space;
        bool srgb8;
        double r, g, b;
    };

    inline const std::vector<Patch>& Patches()
    {
        static const std::vector<Patch> patches = {
            { "P3 red", Gamut::P3D65, false, 1, 0, 0 },
            { "P3 green", Gamut::P3D65, false, 0, 1, 0 },
            { "P3 blue", Gamut::P3D65, false, 0, 0, 1 },
            { "P3 cyan", Gamut::P3D65, false, 0, 1, 1 },
            { "P3 magenta", Gamut::P3D65, false, 1, 0, 1 },
            { "P3 yellow", Gamut::P3D65, false, 1, 1, 0 },
            { "709 red", Gamut::Rec709, false, 1, 0, 0 },
            { "709 green", Gamut::Rec709, false, 0, 1, 0 },
            { "709 blue", Gamut::Rec709, false, 0, 0, 1 },
            { "709 cyan", Gamut::Rec709, false, 0, 1, 1 },
            { "709 magenta", Gamut::Rec709, false, 1, 0, 1 },
            { "709 yellow", Gamut::Rec709, false, 1, 1, 0 },
            { "Dark skin", Gamut::Rec709, true, 115, 82, 68 },
            { "Light skin", Gamut::Rec709, true, 194, 150, 130 },
            { "Blue sky", Gamut::Rec709, true, 98, 122, 157 },
            { "Foliage", Gamut::Rec709, true, 87, 108, 67 },
            { "Blue flower", Gamut::Rec709, true, 133, 128, 177 },
            { "Bluish green", Gamut::Rec709, true, 103, 189, 170 },
            { "Orange", Gamut::Rec709, true, 214, 126, 44 },
            { "Purplish blue", Gamut::Rec709, true, 80, 91, 166 },
            { "Moderate red", Gamut::Rec709, true, 193, 90, 99 },
            { "Purple", Gamut::Rec709, true, 94, 60, 108 },
            { "Yellow green", Gamut::Rec709, true, 157, 188, 64 },
            { "Orange yellow", Gamut::Rec709, true, 224, 163, 46 },
            { "Blue", Gamut::Rec709, true, 56, 61, 150 },
            { "Green", Gamut::Rec709, true, 70, 148, 73 },
            { "Red", Gamut::Rec709, true, 175, 54, 60 },
            { "Yellow", Gamut::Rec709, true, 231, 199, 31 },
            { "Magenta", Gamut::Rec709, true, 187, 86, 149 },
            { "Cyan", Gamut::Rec709, true, 8, 133, 161 } };
        return patches;
    }

    // One patch at one source level (its strongest channel in the mastering
    // space, in nits), through the colour path.
    struct ColourSample { size_t patch = 0; double sourceNits = 0.0; };
    struct ColourPoint
    {
        bool valid = false;      // the scene can hold this colour at this brightness
        bool clipped = false;    // a display channel was pushed past its range
        double sourceU = 0.0, sourceV = 0.0; // the source chromaticity (u'v')
        double u = 0.0, v = 0.0; // where it lands on the display (u'v')
        double nits = 0.0;       // its on-screen luminance
        double saturation = 1.0; // distance from white on screen over in the source
        double hue = 0.0;        // hue shift, degrees
    };

    // u'v' of a gamut's red, green, blue and its white.
    struct GamutCorners { double u[4] = {}; double v[4] = {}; };

    struct Functions
    {
        bool (*sample)(const Request&, Result&, std::wstring& error) = nullptr;
        double (*nitsToPq)(double nits) = nullptr;
        double (*pqToNits)(double pq) = nullptr;
        // libplaceboDefault: libplacebo's own settings, as the default curve.
        bool (*colour)(const Request&, bool libplaceboDefault, Gamut display,
            const std::vector<ColourSample>&, std::vector<ColourPoint>&, std::wstring& error) = nullptr;
        bool (*corners)(Gamut, GamutCorners&) = nullptr;
    };

    inline Functions& Registered()
    {
        static Functions functions;
        return functions;
    }
}
