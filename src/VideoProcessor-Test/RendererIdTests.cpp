#include "pch.h"

#include <RendererId.h>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(RendererIdTests)
	{
	public:
		TEST_METHOD(DisplayOrderPutsAlphaFirstAndReversesEligibleExternalRenderers)
		{
			const std::vector<RendererId> ordered = RendererId::OrderForDisplay({
				{ TEXT("Beta Renderer"), RendererBackend::DIRECTSHOW },
				RendererId::Libplacebo(),
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
			const RendererId renderer = RendererId::Libplacebo();
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
				RendererId::Libplacebo(),
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
