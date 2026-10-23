#include "pch.h"
#include "CppUnitTest.h"
#include <QualityProfileMigration.h>
#include <ConfigFile.h>
#include <RendererProfileConfig.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace VideoProcessorTest {
TEST_CLASS(QualityProfileMigrationTests) {
    using Sections = QualityProfileMigration::Sections;
    static void Value(const Sections& sections, const char* section, const char* key, const char* expected) {
        Assert::AreEqual(expected, QualityProfileMigration::Get(sections, section, key).c_str());
    }
public:
    TEST_METHOD(OmittedHighDefaultsBecomeExplicitAndMigrationIsIdempotent) {
        Sections sections{{"vprenderer.default", {}}};
        std::vector<std::string> order{"vprenderer.default"};
        QualityProfileMigration::Apply(sections, order);
        Value(sections,"vprenderer.scaling.default","upscaler","ewa_lanczossharp");
        Value(sections,"vprenderer.scaling.default","downscaler","hermite");
        Value(sections,"vprenderer.scaling.default","sigmoid","on");
        Value(sections,"vprenderer.scaling.default","deband_strength","default");
        Value(sections,"vprenderer.scaling.default","dithering","blue_noise");
        Value(sections,"vprenderer.scaling.default","display_bit_depth","output");
        Value(sections,"vprenderer.default","peak_detection","high_quality");
        Value(sections,"vprenderer.default","contrast_recovery","0.3");
        Assert::IsTrue(QualityProfileMigration::Build(sections, order).Empty());
    }
    TEST_METHOD(ExplicitAutoDoesNotInheritAnotherProfilesOffAndEightBit) {
        Sections sections{{"vprenderer.default", {{"dithering","off"},{"display_bit_depth","8"}}},
            {"vprenderer.hdr", {{"shortcut","Ctrl+2"},{"dithering","AUTO"},{"display_bit_depth","AUTO"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.hdr"};
        QualityProfileMigration::Apply(sections, order);
        Value(sections,"vprenderer.scaling.default","dithering","off");
        Value(sections,"vprenderer.scaling.default","display_bit_depth","8");
        Value(sections,"vprenderer.scaling.hdr","dithering","blue_noise");
        Value(sections,"vprenderer.scaling.hdr","display_bit_depth","output");
        Assert::IsTrue(QualityProfileMigration::Build(sections, order).Empty());
    }
    TEST_METHOD(FastAndBalancedDefaultsRetainExplicitUserChoices) {
        for (const char* quality : {"fast", "balanced"}) {
            Sections sections{{"vprenderer", {{"quality",quality},{"contrast_recovery","0.7"}}}};
            std::vector<std::string> order{"vprenderer"};
            QualityProfileMigration::Apply(sections, order);
            const bool fast=std::string(quality)=="fast";
            Value(sections,"vprenderer.scaling.default","upscaler",fast?"none":"lanczos");
            Value(sections,"vprenderer.scaling.default","downscaler",fast?"gpu":"hermite");
            Value(sections,"vprenderer.scaling.default","dithering",fast?"off":"blue_noise");
            Value(sections,"vprenderer","peak_detection",fast?"off":"on");
            Value(sections,"vprenderer","contrast_recovery","0.7");
            Assert::IsTrue(QualityProfileMigration::Build(sections, order).Empty());
        }
    }
    TEST_METHOD(ExplicitToneAutoUsesOwnPresetAndMissingQualityInherits) {
        Sections sections{{"vprenderer.default",{{"quality","balanced"},{"peak_detection","high_quality"},{"contrast_recovery","0.7"}}},
            {"vprenderer.fast",{{"quality","fast"},{"shortcut","Ctrl+2"},{"peak_detection","auto"},{"contrast_recovery","auto"}}},
            {"vprenderer.inherited",{{"shortcut","Ctrl+3"},{"peak_detection","auto"},{"contrast_recovery","auto"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.fast","vprenderer.inherited"};
        QualityProfileMigration::Apply(sections,order);
        Value(sections,"vprenderer.fast","peak_detection","off");
        Value(sections,"vprenderer.fast","contrast_recovery","0");
        Value(sections,"vprenderer.inherited","peak_detection","on");
        Value(sections,"vprenderer.inherited","contrast_recovery","0");
        Value(sections,"vprenderer.default","contrast_recovery","0.7");
    }
    TEST_METHOD(ScalingAutoResetsPresetButMissingSettingsStillInherit) {
        Sections sections{{"vprenderer.scaling.default",{{"upscaler","bilinear"},{"dithering","off"},{"display_bit_depth","8"}}},
            {"vprenderer.scaling.reset",{{"upscaler","auto"},{"dithering","auto"},{"display_bit_depth","auto"}}},
            {"vprenderer.scaling.inherited",{}}};
        std::vector<std::string> order{"vprenderer.scaling.default","vprenderer.scaling.reset","vprenderer.scaling.inherited"};
        QualityProfileMigration::Apply(sections,order);
        Value(sections,"vprenderer.scaling.default","upscaler","bilinear");
        Value(sections,"vprenderer.scaling.reset","upscaler","ewa_lanczossharp");
        Value(sections,"vprenderer.scaling.reset","dithering","blue_noise");
        Value(sections,"vprenderer.scaling.reset","display_bit_depth","output");
        Assert::IsTrue(sections.at("vprenderer.scaling.inherited").empty());
        Assert::IsTrue(QualityProfileMigration::Build(sections,order).Empty());
    }
    TEST_METHOD(RenderingAutoIsResolvedBeforeItsQualityIsRemoved) {
        Sections sections{{"vprenderer.default",{{"quality","balanced"}}},
            {"vprenderer.fast",{{"quality","fast"},{"shortcut","Ctrl+2"},{"upscaler","auto"},{"downscaler","auto"},{"sigmoid","auto"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.fast"};
        QualityProfileMigration::Apply(sections,order);
        Value(sections,"vprenderer.fast","upscaler","none");
        Value(sections,"vprenderer.fast","downscaler","gpu");
        Value(sections,"vprenderer.fast","sigmoid","off");
    }
    TEST_METHOD(RetiredDownscalersResolveBeforeQualityIsRemoved) {
        Sections sections{{"vprenderer.default",{{"quality","balanced"},{"downscaler","ewa_lanczos"}}},
            {"vprenderer.scaling.fast",{{"quality","fast"},{"downscaler","none"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.scaling.fast"};
        QualityProfileMigration::Apply(sections,order);
        Value(sections,"vprenderer.default","downscaler","hermite");
        Value(sections,"vprenderer.scaling.fast","downscaler","gpu");
        Assert::IsTrue(QualityProfileMigration::Build(sections,order).Empty());
    }
    TEST_METHOD(MatchOutputFormatIsAcceptedInBothProfileLocations) {
        std::string error;
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("display","display_bit_depth","output",error));
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("scaling","display_bit_depth","output",error));
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("scaling","display_bit_depth","auto",error));
    }
};
}
