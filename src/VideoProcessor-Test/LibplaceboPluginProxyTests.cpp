#include "pch.h"
#include "CppUnitTest.h"

#include "../VideoProcessor-Lib/vprenderer/LibplaceboPluginVideoRenderer.h"

#include <type_traits>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace VideoProcessorTest
{
	TEST_CLASS(LibplaceboPluginProxyTests)
	{
	public:
		TEST_METHOD(ActivePictureLookaheadHasExplicitProxyOverride)
		{
			using ExpectedMethod = void (
				LibplaceboPluginVideoRenderer::*)(size_t);
			Assert::IsTrue((std::is_same_v<
				decltype(&LibplaceboPluginVideoRenderer::
					SetActivePictureLookaheadFrames),
				ExpectedMethod>));
		}
	};
}
