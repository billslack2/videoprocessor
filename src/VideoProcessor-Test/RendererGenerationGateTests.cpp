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
	};
}
