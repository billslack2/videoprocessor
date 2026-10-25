#include "pch.h"

#include <RendererId.h>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(RendererIdTests)
	{
	public:
        TEST_METHOD(ExistingVpSelectionsUpgradeAndLegacyRequiresExplicitSelection)
        {
            const auto legacy = RendererId::Libplacebo(false);
            const auto promoted = RendererId::Libplacebo(true);
            Assert::IsTrue(promoted.libplaceboBeta);
            Assert::IsFalse(legacy.libplaceboBeta);
            for (const auto* name : { TEXT("VP Renderer"), TEXT("VP Renderer (Beta)"),
                TEXT("vp renderer (beta)"), TEXT("libplacebo"), TEXT("VideoProcessor Renderer (Alpha)") })
            {
                Assert::IsTrue(promoted.MatchesConfiguredName(name));
                Assert::IsFalse(legacy.MatchesConfiguredName(name));
            }
            Assert::IsTrue(legacy.MatchesConfiguredName(TEXT("VP Renderer (Legacy)")));
            Assert::IsFalse(promoted.MatchesConfiguredName(TEXT("VP Renderer (Legacy)")));
            const auto order = RendererId::OrderForDisplay({legacy, promoted});
            Assert::AreEqual(TEXT("VP Renderer"), order[0].name.GetString());
            Assert::AreEqual(TEXT("VP Renderer (Legacy)"), order[1].name.GetString());
        }

        TEST_METHOD(ExternalRendererNamesAreUnchanged)
        {
            const RendererId madvr{ TEXT("DirectShow - madVR"), RendererBackend::DIRECTSHOW };
            Assert::IsTrue(madvr.MatchesConfiguredName(TEXT("DirectShow - madVR")));
            Assert::IsFalse(RendererId::Libplacebo(true).MatchesConfiguredName(madvr.name));
            Assert::IsFalse(RendererId::Libplacebo(false).MatchesConfiguredName(madvr.name));
        }

		TEST_METHOD(DisplayOrderPutsAlphaFirstAndReversesEligibleExternalRenderers)
		{
			const std::vector<RendererId> ordered = RendererId::OrderForDisplay({
				{ TEXT("Beta Renderer"), RendererBackend::DIRECTSHOW },
				RendererId::Libplacebo(true),
				{ TEXT("DeckLink Video Renderer"), RendererBackend::DIRECTSHOW },
				{ TEXT("alpha external renderer"), RendererBackend::DIRECTSHOW },
				{ TEXT("Gamma Renderer"), RendererBackend::DIRECTSHOW }
			});

			Assert::AreEqual(static_cast<size_t>(4), ordered.size());
			Assert::AreEqual(TEXT("VP Renderer"), ordered[0].name.GetString());
			Assert::AreEqual(TEXT("alpha external renderer"), ordered[1].name.GetString());
			Assert::AreEqual(TEXT("Gamma Renderer"), ordered[2].name.GetString());
			Assert::AreEqual(TEXT("Beta Renderer"), ordered[3].name.GetString());
		}

		TEST_METHOD(LegacyAlphaNameStillSelectsVpRenderer)
		{
			const RendererId renderer = RendererId::Libplacebo(true);
			Assert::IsTrue(renderer.MatchesConfiguredName(
				TEXT("VideoProcessor Renderer (Alpha)")));
		}

		TEST_METHOD(DisplayOrderWithoutAlphaContainsOnlyReversedEligibleExternalRenderers)
		{
			const std::vector<RendererId> ordered = RendererId::OrderForDisplay({
				{ TEXT("Beta Renderer"), RendererBackend::DIRECTSHOW },
				{ TEXT("decklink capture renderer"), RendererBackend::DIRECTSHOW },
				{ TEXT("Alpha Renderer"), RendererBackend::DIRECTSHOW }
			});

			Assert::AreEqual(static_cast<size_t>(2), ordered.size());
			Assert::AreEqual(TEXT("Beta Renderer"), ordered[0].name.GetString());
			Assert::AreEqual(TEXT("Alpha Renderer"), ordered[1].name.GetString());
		}

		TEST_METHOD(ConfiguredRendererNameAndOneBasedShortcutPositionFollowDisplayOrder)
		{
			const std::vector<RendererId> ordered = RendererId::OrderForDisplay({
				{ TEXT("External A"), RendererBackend::DIRECTSHOW },
				RendererId::Libplacebo(true),
				{ TEXT("External B"), RendererBackend::DIRECTSHOW }
			});

			const unsigned int configuredShortcutIndex = 2;
			Assert::IsTrue(ordered[configuredShortcutIndex - 1].MatchesConfiguredName(TEXT("external b")));
			Assert::IsFalse(ordered[0].MatchesConfiguredName(TEXT("external b")));
			Assert::AreEqual(TEXT("External B"), ordered[1].name.GetString());
			Assert::IsTrue(ordered[0].MatchesConfiguredName(TEXT("libplacebo")));
		}
	};
}
