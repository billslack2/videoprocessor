#include "pch.h"
#include "CppUnitTest.h"

#include "../VideoProcessor-Lib/vprenderer/LibplaceboPluginVideoRenderer.h"
#include "../VideoProcessor-Lib/vprenderer/OptionalRendererLayout.h"

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

		TEST_METHOD(OptionalRendererUsesOnePrivateDependencyDirectory)
		{
			wchar_t temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathW(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::wstring root = std::wstring(temporaryDirectory) +
				L"VideoProcessor-vp0107-" +
				std::to_wstring(GetCurrentProcessId());
			const std::wstring pluginDirectory =
				OptionalRendererLayout::Directory(root);
			std::wstring missing;
			Assert::IsTrue(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			Assert::AreEqual(OptionalRendererLayout::PluginPath(root).c_str(),
				missing.c_str());
			CreateDirectoryW(root.c_str(), nullptr);
			Assert::IsTrue(CreateDirectoryW(pluginDirectory.c_str(), nullptr) != FALSE);

			auto touch = [](const std::wstring& path)
			{
				const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
					nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				Assert::IsTrue(file != INVALID_HANDLE_VALUE);
				CloseHandle(file);
			};
			touch(OptionalRendererLayout::PluginPath(root));
			for (size_t index = 0;
				index + 1 < OptionalRendererLayout::PrivateDependencies().size();
				++index)
			{
				touch(OptionalRendererLayout::Join(pluginDirectory,
					OptionalRendererLayout::PrivateDependencies()[index]));
			}

			const wchar_t* finalDependency =
				OptionalRendererLayout::PrivateDependencies().back();
			// A same-named root/PATH-style decoy must not satisfy the private
			// plugin contract.
			touch(OptionalRendererLayout::Join(root, finalDependency));
			Assert::IsTrue(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			Assert::AreEqual(OptionalRendererLayout::Join(
				pluginDirectory, finalDependency).c_str(),
				missing.c_str());

			touch(OptionalRendererLayout::Join(
				pluginDirectory, finalDependency));
			Assert::IsFalse(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			DeleteFileW(OptionalRendererLayout::Join(root, finalDependency).c_str());
			DeleteFileW(OptionalRendererLayout::PluginPath(root).c_str());
			for (const wchar_t* dependency :
				OptionalRendererLayout::PrivateDependencies())
			{
				DeleteFileW(OptionalRendererLayout::Join(
					pluginDirectory, dependency).c_str());
			}
			RemoveDirectoryW(pluginDirectory.c_str());
			RemoveDirectoryW(root.c_str());
		}
	};
}
