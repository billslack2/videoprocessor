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
    static void Value(const Sections& s,const char* owner,const char* key,const char* expected) {
        Assert::AreEqual(expected,QualityProfileMigration::Get(s,owner,key).c_str());
    }
    static void NoOldProcessing(const Sections& s) {
        for(const auto& owner:s) {
            const auto root=owner.first;
            if(root!="vprenderer" && (root.rfind("vprenderer.",0)!=0 || root.substr(11).find('.')!=std::string::npos))continue;
            if(root=="vprenderer.scaling"||root=="vprenderer.color")continue;
            for(const char* key:{"upscaler","downscaler","sigmoid","chroma_upscaler","deband","deband_strength","dithering","display_bit_depth"})
                Assert::IsTrue(owner.second.count(key)==0);
        }
    }
    static ConfigFile Load(const char* contents) {
        char folder[MAX_PATH], path[MAX_PATH]; GetTempPathA(MAX_PATH, folder);
        Assert::IsTrue(GetTempFileNameA(folder,"qpm",0,path)!=0);
        std::ofstream(path) << "[general]\npersist_profile_selection: false\n" << contents;
        ConfigFile config; Assert::IsTrue(config.Load(path,ConfigFile::ReadPolicy::Fresh));
        DeleteFileA(path); return config;
    }
public:
    TEST_METHOD(OmittedHighDefaultsBecomeExplicitAndMigrationIsIdempotent) {
        Sections s{{"vprenderer.default",{}}};std::vector<std::string> order{"vprenderer.default"};
        QualityProfileMigration::Apply(s,order);
        Value(s,"vprenderer.scaling.default","upscaler","ewa_lanczossharp");
        Value(s,"vprenderer.scaling.default","dithering","blue_noise");
        Value(s,"vprenderer.scaling.default","display_bit_depth","output");
        Value(s,"vprenderer.default","peak_detection","high_quality");
        NoOldProcessing(s);Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(ToneDefaultsAreFrozenForNamedProfilesInOneMigration) {
        Sections s{{"vprenderer.default",{{"peak_detection","off"},{"contrast_recovery","0.6"}}},
          {"vprenderer.inherited",{}}, {"vprenderer.reset",{{"peak_detection","AUTO"},{"contrast_recovery","AUTO"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.inherited","vprenderer.reset"};
        QualityProfileMigration::Apply(s,order);
        Value(s,"vprenderer.inherited","peak_detection","off");
        Value(s,"vprenderer.inherited","contrast_recovery","0.6");
        Value(s,"vprenderer.reset","peak_detection","high_quality");
        Value(s,"vprenderer.reset","contrast_recovery","0.3");
        Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(LegacyAliasesMoveToVisibleScalingAndKeepParentInheritance) {
        Sections s{{"vprenderer.default",{{"dithering","off"},{"display_bit_depth","8"}}},
          {"vprenderer.hdr",{{"shortcut","Ctrl+2"},{"dithering","AUTO"},{"display_bit_depth","AUTO"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.hdr"};QualityProfileMigration::Apply(s,order);
        Value(s,"vprenderer.scaling.default","dithering","off");Value(s,"vprenderer.scaling.default","display_bit_depth","8");
        Value(s,"vprenderer.scaling.hdr","dithering","blue_noise");Value(s,"vprenderer.scaling.hdr","display_bit_depth","output");
        Value(s,"vprenderer.scaling.hdr","shortcut","Ctrl+2");NoOldProcessing(s);
        Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(RenderingDefaultKeepsItsPreviousPrecedenceOverEveryScalingProfile) {
        Sections s{{"vprenderer.default",{{"shortcut","Ctrl+1"},{"upscaler","lanczos"},{"dithering","off"}}},
          {"vprenderer.scaling.default",{{"upscaler","bicubic"}}},{"vprenderer.scaling.other",{{"upscaler","bilinear"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.scaling.default","vprenderer.scaling.other"};
        QualityProfileMigration::Apply(s,order);NoOldProcessing(s);
        Value(s,"vprenderer.scaling.default","shortcut","Ctrl+1");
        for(const char* profile:{"vprenderer.scaling.default","vprenderer.scaling.other"}) {Value(s,profile,"upscaler","lanczos");Value(s,profile,"dithering","off");}
        Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(NamedValuesMergeByNameAndRetainRulesShortcutsAndToneValues) {
        Sections s{{"vprenderer.default",{{"tone_mapping","spline"}}},
          {"vprenderer.movie",{{"label","Cinema"},{"when","${source_rate} < 30"},{"cycle_shortcut","Ctrl+2"},{"dithering","error_diffusion_sierra3"},{"display_bit_depth","8"},{"sdr_target_nits","80"}}},
          {"vprenderer.scaling.default",{{"upscaler","ewa_lanczossharp"}}},
          {"vprenderer.scaling.movie",{{"upscaler","lanczos"},{"dithering","blue_noise"},{"shortcut","Ctrl+3"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.movie","vprenderer.scaling.default","vprenderer.scaling.movie"};
        QualityProfileMigration::Apply(s,order);NoOldProcessing(s);
        Value(s,"vprenderer.scaling.movie","dithering","error_diffusion_sierra3");Value(s,"vprenderer.scaling.movie","display_bit_depth","8");
        Value(s,"vprenderer.scaling.movie","upscaler","lanczos");Value(s,"vprenderer.scaling.movie","shortcut","Ctrl+3");
        Value(s,"vprenderer.scaling.movie","cycle_shortcut","Ctrl+2");Value(s,"vprenderer.scaling.movie","when","${source_rate} < 30");
        Value(s,"vprenderer.movie","sdr_target_nits","80");Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(NamedValueCanMergeIntoTheExistingFirstScalingProfile) {
        Sections s{{"vprenderer.default",{}},{"vprenderer.movie",{{"dithering","off"}}},
          {"vprenderer.scaling.movie",{{"dithering","blue_noise"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.movie","vprenderer.scaling.movie"};
        QualityProfileMigration::Apply(s,order);Value(s,"vprenderer.scaling.movie","dithering","off");NoOldProcessing(s);
    }
    TEST_METHOD(LegacyFastAutoLoadsConcreteFastChoicesWithBlueNoise) {
        Sections s{{"vprenderer.default",{{"quality","fast"},{"upscaler","auto"},{"dithering","On"}}},
          {"vprenderer.scaling.default",{{"upscaler","bilinear"},{"chroma_upscaler","ewa_lanczos"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.scaling.default"};QualityProfileMigration::Apply(s,order);
        Value(s,"vprenderer.scaling.default","upscaler","none");Value(s,"vprenderer.scaling.default","dithering","blue_noise");
        Value(s,"vprenderer.scaling.default","chroma_upscaler","ewa_lanczos");Value(s,"vprenderer.default","quality","fast");
        NoOldProcessing(s);Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(LegacyDebandToggleMovesAndCommentsAreLeftToTheEditor) {
        Sections s{{"vprenderer.default",{{"deband","off"}}},{"vprenderer.scaling.default",{{"deband_strength","default"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.scaling.default"};QualityProfileMigration::Apply(s,order);
        Value(s,"vprenderer.scaling.default","deband_strength","off");NoOldProcessing(s);
        Assert::IsTrue(QualityProfileMigration::Build(s,order).Empty());
    }
    TEST_METHOD(CanonicalChoicesAndRetiredMethodsAreNotReplaced) {
        Sections s{{"vprenderer.default",{}},{"vprenderer.scaling.default",{{"dithering","error_diffusion_sierra3"},{"deband_strength","light"},{"chroma_upscaler","ewa_lanczos"}}}};
        std::vector<std::string> order{"vprenderer.default","vprenderer.scaling.default"};QualityProfileMigration::Apply(s,order);
        Value(s,"vprenderer.scaling.default","dithering","error_diffusion_sierra3");Value(s,"vprenderer.scaling.default","deband_strength","light");
        Value(s,"vprenderer.scaling.default","chroma_upscaler","ewa_lanczos");NoOldProcessing(s);
    }
    TEST_METHOD(RuntimeReadsAndSelectsMigratedCanonicalValues) {
        char folder[MAX_PATH],path[MAX_PATH];GetTempPathA(MAX_PATH,folder);Assert::IsTrue(GetTempFileNameA(folder,"qpm",0,path)!=0);
        std::ofstream(path)<<"[general]\npersist_profile_selection: false\n[vprenderer.Default]\ndithering: off\n[vprenderer.HDR]\nshortcut: Ctrl+2\ndithering: on\ndisplay_bit_depth: 10\n[vprenderer.scaling.Default]\nupscaler: bicubic\n";
        ConfigFile config;Assert::IsTrue(config.Load(path,ConfigFile::ReadPolicy::Fresh));DeleteFileA(path);
        RendererProfileConfig::Model model;std::string error;Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(model.profiles.at("display.hdr").settings.count("dithering")==0);
        Assert::AreEqual("blue_noise",model.profiles.at("scaling.hdr").settings.at("dithering").c_str());
        UnifiedProfileRuntime::Runtime runtime;Assert::IsTrue(runtime.Initialize(config,{},error));UnifiedProfileRuntime::SelectionResult result;
        Assert::IsTrue(runtime.SelectKey("Ctrl+2",{},result,error));
        Assert::AreEqual("hdr",result.snapshot->effectiveSelections.at("scaling").front().c_str());
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
    TEST_METHOD(MatchOutputFormatIsAcceptedInBothProfileLocations) {
        std::string error;
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("display","display_bit_depth","output",error));
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("scaling","display_bit_depth","output",error));
    }};
}
