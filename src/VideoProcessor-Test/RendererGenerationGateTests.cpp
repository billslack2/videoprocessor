#include "pch.h"
#include "CppUnitTest.h"

#include <RendererGenerationGate.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace Tests
{
	TEST_CLASS(RendererGenerationGateTests)
	{
	public:
		TEST_METHOD(AcceptsOnlyTheLiveRendererGeneration)
		{
			Assert::IsTrue(RendererGenerationGate::Accept(7, 7, true));
			Assert::IsFalse(RendererGenerationGate::Accept(6, 7, true));
			Assert::IsFalse(RendererGenerationGate::Accept(8, 7, true));
		}

		TEST_METHOD(RejectsMessagesWithoutAnOwnedRenderer)
		{
			Assert::IsFalse(RendererGenerationGate::Accept(7, 7, false));
			Assert::IsFalse(RendererGenerationGate::Accept(0, 0, true));
		}

		TEST_METHOD(PostSwapRuleReapplyArmsForBothRendererDirections)
		{
			const uint32_t madvrToAlpha =
				RendererGenerationGate::ArmPostSwapRuleReapply(true, 21);
			const uint32_t alphaToMadvr =
				RendererGenerationGate::ArmPostSwapRuleReapply(true, 22);
			const uint32_t sameRendererRestart =
				RendererGenerationGate::ArmPostSwapRuleReapply(false, 23);

			Assert::AreEqual(static_cast<uint32_t>(21), madvrToAlpha);
			Assert::AreEqual(static_cast<uint32_t>(22), alphaToMadvr);
			Assert::AreEqual(static_cast<uint32_t>(0), sameRendererRestart);
		}

		TEST_METHOD(PostSwapRuleReapplyConsumesOnlyTheRunningSuccessorOnce)
		{
			uint32_t pending =
				RendererGenerationGate::ArmPostSwapRuleReapply(true, 31);

			Assert::IsFalse(RendererGenerationGate::ConsumePostSwapRuleReapply(
				pending, 30, 31, true, true));
			Assert::IsFalse(RendererGenerationGate::ConsumePostSwapRuleReapply(
				pending, 31, 31, true, false));
			Assert::IsFalse(RendererGenerationGate::ConsumePostSwapRuleReapply(
				pending, 31, 32, true, true));
			Assert::IsTrue(RendererGenerationGate::ConsumePostSwapRuleReapply(
				pending, 31, 31, true, true));
			Assert::AreEqual(static_cast<uint32_t>(0), pending);
			Assert::IsFalse(RendererGenerationGate::ConsumePostSwapRuleReapply(
				pending, 31, 31, true, true));
		}

		TEST_METHOD(RetiringRendererAcceptsOnlyExactRefreshRestored)
		{
			Assert::IsTrue(
				RendererGenerationGate::AcceptRetiringRefreshRestored(
					"refresh.restored", 12, 12));
			Assert::IsFalse(
				RendererGenerationGate::AcceptRetiringRefreshRestored(
					"refresh.applied", 12, 12));
			Assert::IsFalse(
				RendererGenerationGate::AcceptRetiringRefreshRestored(
					"refresh.restored", 11, 12));
			Assert::IsFalse(
				RendererGenerationGate::AcceptRetiringRefreshRestored(
					"refresh.restored", 0, 0));
		}

		TEST_METHOD(DetachedRendererContextFiltersWildcardAlphaAndSelector)
		{
			Assert::IsTrue(RendererGenerationGate::MatchesRendererTarget(
				"*", 0, 4, false));
			Assert::IsTrue(RendererGenerationGate::MatchesRendererTarget(
				"vprenderer", 0, 1, true));
			Assert::IsFalse(RendererGenerationGate::MatchesRendererTarget(
				"vprenderer", 0, 4, false));
			Assert::IsTrue(RendererGenerationGate::MatchesRendererTarget(
				"madvr", 4, 4, false));
			Assert::IsFalse(RendererGenerationGate::MatchesRendererTarget(
				"madvr", 4, 3, false));
			Assert::IsFalse(RendererGenerationGate::MatchesRendererTarget(
				"madvr", 0, 0, false));
		}
	};
}
