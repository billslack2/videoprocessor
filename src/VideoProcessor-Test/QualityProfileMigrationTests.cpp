#include "pch.h"
#include "CppUnitTest.h"
#include <QualityProfileMigration.h>
#include <ConfigFile.h>
#include <RendererProfileConfig.h>
#include <UnifiedProfileRuntime.h>
#include <fstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace VideoProcessorTest {
TEST_CLASS(QualityProfileMigrationTests) {
    using Sections = QualityProfileMigration::Sections;
    static void Value(const Sections& sections, const char* section, const char* key, const char* expected) {
        Assert::AreEqual(expected, QualityProfileMigration::Get(sections, section, key).c_str());
    }
    static ConfigFile Load(const char* contents) {
        char folder[MAX_PATH], path[MAX_PATH]; GetTempPathA(MAX_PATH, folder);
        Assert::IsTrue(GetTempFileNameA(folder,"qpm",0,path)!=0);
        std::ofstream(path) << "[general]\npersist_profile_selection: false\n" << contents;
        ConfigFile config; Assert::IsTrue(config.Load(path,ConfigFile::ReadPolicy::Fresh));
        DeleteFileA(path); return config;
    }
    static std::string Selected(const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot, const char* group) {
        return snapshot->effectiveSelections.at(group).front();
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
    TEST_METHOD(ExplicitAutoRemainsWithItsOwnerAndResetsOffAndEightBit) {
        Sections sections{{"vprenderer.default", {{"dithering","off"},{"display_bit_depth","8"}}},
            {"vprenderer.hdr", {{"shortcut","Ctrl+2"},{"dithering","AUTO"},{"display_bit_depth","AUTO"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.hdr"};
        QualityProfileMigration::Apply(sections, order);
        Value(sections,"vprenderer.default","dithering","off");
        Value(sections,"vprenderer.default","display_bit_depth","8");
        Value(sections,"vprenderer.hdr","dithering","blue_noise");
        Value(sections,"vprenderer.hdr","display_bit_depth","output");
        Assert::IsTrue(sections.count("vprenderer.scaling.hdr")==0);
        Assert::IsTrue(QualityProfileMigration::Build(sections, order).Empty());
    }
    TEST_METHOD(RedundantRenderingScalersMoveToVisibleScalingWithoutChangingSelections) {
        Sections sections{{"vprenderer.a",{{"upscaler","ewa_lanczossharp"},{"when","${source_rate} < 30"}}},
            {"vprenderer.scaling.default",{{"upscaler","ewa_lanczossharp"}}},
            {"vprenderer.scaling.inherited",{}}};
        std::vector<std::string> order{"vprenderer.a","vprenderer.scaling.default","vprenderer.scaling.inherited"};
        QualityProfileMigration::Apply(sections,order);
        Assert::IsTrue(sections.at("vprenderer.a").count("upscaler")==0);
        Value(sections,"vprenderer.a","when","${source_rate} < 30");
        Assert::IsTrue(QualityProfileMigration::Build(sections,order).Empty());
    }
    TEST_METHOD(ConflictingRenderingScalersKeepBehaviorAndReportTheirOwner) {
        Sections sections{{"vprenderer.a",{{"upscaler","lanczos"}}},
            {"vprenderer.scaling.default",{{"upscaler","bicubic"}}}};
        std::vector<std::string> order{"vprenderer.a","vprenderer.scaling.default"};
        const auto warnings=QualityProfileMigration::Apply(sections,order);
        Value(sections,"vprenderer.a","upscaler","lanczos");
        Value(sections,"vprenderer.scaling.default","upscaler","bicubic");
        Assert::IsTrue(!warnings.empty());
    }
    TEST_METHOD(LegacyPresetsRetainFallbackPrecedenceAndConcreteUserChoices) {
        for (const char* quality : {"fast", "balanced"}) {
            Sections sections{{"vprenderer", {{"quality",quality},{"contrast_recovery","0.7"},{"upscaler","auto"}}},
                {"vprenderer.scaling.a", {{"upscaler","bilinear"}}},
                {"vprenderer.scaling.b", {{"upscaler","auto"},{"quality","high"}}}};
            std::vector<std::string> order{"vprenderer","vprenderer.scaling.a","vprenderer.scaling.b"};
            const auto original=sections;
            QualityProfileMigration::Apply(sections, order);
            Assert::IsTrue(sections==original);
            Value(sections,"vprenderer","quality",quality);
            Value(sections,"vprenderer.scaling.a","upscaler","bilinear");
        }
    }
    TEST_METHOD(LegacyDebandTogglePreservesIndependentStrengthPrecedence) {
        Sections sections{{"vprenderer",{{"deband","off"}}},
            {"vprenderer.scaling.default",{{"deband_strength","default"}}}};
        std::vector<std::string> order{"vprenderer","vprenderer.scaling.default"};
        const auto original=sections;QualityProfileMigration::Apply(sections,order);
        Assert::IsTrue(sections==original);
    }
    TEST_METHOD(InvalidPresetIsNotSilentlyConvertedIntoHigh) {
        auto config=Load("[vprenderer.Default]\nquality: invalid-preset\n");
        RendererProfileConfig::Model model;std::string error;
        Assert::IsFalse(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(error.find("quality")!=std::string::npos);
    }
    TEST_METHOD(ScalingAutoResetsHighButMissingNamedSettingsStillInherit) {
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
    TEST_METHOD(RenderingSelectorsAndRuleOrderNeverCreateScalingCopies) {
        Sections sections{{"vprenderer.default",{{"shortcut","Ctrl+1"},{"dithering","off"}}},
            {"vprenderer.a",{{"cycle_shortcut","Ctrl+2"},{"when","${source_rate} <= 30"},{"dithering","on"}}},
            {"vprenderer.b",{{"cycle_shortcut","Ctrl+2"},{"display_bit_depth","10"}}},
            {"vprenderer.scaling.a",{{"shortcut","Ctrl+1"},{"upscaler","bilinear"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.a","vprenderer.b","vprenderer.scaling.a"};
        const auto before=order;QualityProfileMigration::Apply(sections,order);
        Assert::IsTrue(before==order);
        Value(sections,"vprenderer.a","when","${source_rate} <= 30");
        Value(sections,"vprenderer.a","cycle_shortcut","Ctrl+2");
        Value(sections,"vprenderer.b","cycle_shortcut","Ctrl+2");
        Value(sections,"vprenderer.scaling.a","shortcut","Ctrl+1");
        Assert::IsTrue(sections.count("vprenderer.scaling.b")==0);
    }
    TEST_METHOD(CycleAndDefaultShortcutsPreserveIndependentScalingSelection) {
        auto config=Load("[vprenderer.Default]\nshortcut: Ctrl+1\ndithering: off\n"
            "[vprenderer.A]\ncycle_shortcut: Ctrl+2\ndithering: on\n"
            "[vprenderer.B]\ncycle_shortcut: Ctrl+2\ndithering: off\n"
            "[vprenderer.scaling.Default]\nupscaler: lanczos\n"
            "[vprenderer.scaling.Other]\nshortcut: Ctrl+3\nupscaler: bilinear\n");
        UnifiedProfileRuntime::Runtime runtime;std::string error;
        Assert::IsTrue(runtime.Initialize(config,{},error));
        UnifiedProfileRuntime::SelectionResult result;
        Assert::IsTrue(runtime.SelectKey("Ctrl+3",{},result,error));
        for(const char* expected:{"a","b","a"}) {
            Assert::IsTrue(runtime.SelectCycleKey("Ctrl+2",{},result,error));
            Assert::AreEqual(expected,Selected(result.snapshot,"display").c_str());
            Assert::AreEqual("other",Selected(result.snapshot,"scaling").c_str());
        }
        Assert::IsTrue(runtime.SelectKey("Ctrl+1",{},result,error));
        Assert::AreEqual("default",Selected(result.snapshot,"display").c_str());
        Assert::AreEqual("other",Selected(result.snapshot,"scaling").c_str());
        RendererProfileConfig::Model model;Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::AreEqual("off",model.profiles.at("display.default").settings.at("dithering").c_str());
        Assert::AreEqual("on",model.profiles.at("display.a").settings.at("dithering").c_str());
    }
    TEST_METHOD(DirectSelectionAndReloadRetainProcessingOwner) {
        auto config=Load("[vprenderer.Default]\ndithering: off\n"
            "[vprenderer.HDR]\ndithering: on\ndisplay_bit_depth: 10\n"
            "[vprenderer.scaling.Default]\nupscaler: bicubic\n");
        UnifiedProfileRuntime::Runtime runtime;std::string error;Assert::IsTrue(runtime.Initialize(config,{},error));
        UnifiedProfileRuntime::SelectionResult result;
        for(const char* profile:{"hdr","default","hdr"}) {
            Assert::IsTrue(runtime.SelectProfile("display",profile,true,{},result,error));
            Assert::AreEqual(profile,Selected(result.snapshot,"display").c_str());
            Assert::AreEqual("default",Selected(result.snapshot,"scaling").c_str());
        }
        UnifiedProfileRuntime::RefreshResult reload;Assert::IsTrue(runtime.Reload(config,{},reload,error));
        // Persistence is disabled in this fixture, so reload returns to the
        // configured default; the profile's values must still survive it.
        Assert::AreEqual("default",Selected(reload.snapshot,"display").c_str());
        Assert::IsTrue(runtime.SelectProfile("display","hdr",true,{},result,error));
        Assert::AreEqual("hdr",Selected(result.snapshot,"display").c_str());
        RendererProfileConfig::Model model;Assert::IsTrue(RendererProfileConfig::Read(*reload.snapshot->configuration,model,error));
        Assert::AreEqual("on",model.profiles.at("display.hdr").settings.at("dithering").c_str());
        Assert::AreEqual("off",model.profiles.at("display.default").settings.at("dithering").c_str());
    }
    TEST_METHOD(RulesSelectRenderingWithoutMovingItsProcessingValues) {
        auto config=Load("[vprenderer.Default]\ndithering: off\n"
            "[vprenderer.Movie]\nwhen: ${source_rate} < 30\ndithering: on\n"
            "[vprenderer.scaling.Default]\nupscaler: bicubic\n");
        RendererProfileConfig::Model model;std::string error;Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        for(const char* rate:{"24","60"}) {
            std::vector<RendererProfileConfig::AutomaticSelection> selected;
            Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,[&](const std::string& name,std::string& value){
                if(name!="source_rate")return false;value=rate;return true;},selected,error));
            const auto found=std::find_if(selected.begin(),selected.end(),[](const auto& item){return item.group=="display";});
            Assert::IsTrue(found!=selected.end());
            Assert::AreEqual(std::string(rate)=="24"?"movie":"default",found->profiles.front().c_str());
        }
    }
    TEST_METHOD(MatchOutputFormatIsAcceptedInBothProfileLocations) {
        std::string error;
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("display","display_bit_depth","output",error));
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("scaling","display_bit_depth","output",error));
    }
};
}
