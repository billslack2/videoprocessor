#include "pch.h"
#include "CppUnitTest.h"

#include <libplacebo/LibplaceboDisplayLut.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>

#include <cstdint>
#include <fstream>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace LibplaceboDisplayLut;

namespace
{
	class TemporaryFile
	{
	public:
		TemporaryFile()
		{
			char directory[MAX_PATH] = {};
			GetTempPathA(ARRAYSIZE(directory), directory);
			char path[MAX_PATH] = {};
			GetTempFileNameA(directory, "vpl", 0, path);
			m_path = path;
		}

		~TemporaryFile()
		{
			if (!m_path.empty())
				DeleteFileA(m_path.c_str());
		}

		const std::string& Path() const { return m_path; }

		void Write(const char* contents) const
		{
			std::ofstream output(m_path, std::ios::binary | std::ios::trunc);
			output << contents;
		}

		void ResizeTo(size_t bytes) const
		{
			HANDLE file = CreateFileA(
				m_path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			Assert::IsTrue(file != INVALID_HANDLE_VALUE);
			LARGE_INTEGER offset{};
			offset.QuadPart = static_cast<LONGLONG>(bytes);
			Assert::IsTrue(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
			Assert::IsTrue(SetEndOfFile(file));
			CloseHandle(file);
		}

	private:
		std::string m_path;
	};

	void Free(LoadResult& result)
	{
		pl_lut_free(&result.lut);
	}

	const char* Valid3dCube =
		"TITLE \"VP012 3D loader test\"\n"
		"LUT_3D_SIZE 2\n"
		"0.0 0.0 0.0\n"
		"1.0 0.0 0.0\n"
		"0.0 1.0 0.0\n"
		"1.0 1.0 0.0\n"
		"0.0 0.0 1.0\n"
		"1.0 0.0 1.0\n"
		"0.0 1.0 1.0\n"
		"1.0 1.0 1.0\n";

	const char* Valid1dCube =
		"TITLE \"VP012 1D rejection test\"\n"
		"LUT_1D_SIZE 2\n"
		"0.0 0.0 0.0\n"
		"1.0 1.0 1.0\n";

	const char* Green3dCube =
		"TITLE \"VP012 target LUT GPU test\"\n"
		"LUT_3D_SIZE 2\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 0.0\n";

	struct RgbaPixel
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	class TargetLutGpuFixture
	{
	public:
		~TargetLutGpuFixture()
		{
			if (m_renderer)
				pl_renderer_destroy(&m_renderer);
			if (m_d3d11)
				pl_d3d11_destroy(&m_d3d11);
			if (m_log)
				pl_log_destroy(&m_log);
		}

		bool Create()
		{
			pl_log_params logParams{};
			logParams.log_level = PL_LOG_NONE;
			m_log = pl_log_create(PL_API_VER, &logParams);
			if (!m_log)
				return false;

			pl_d3d11_params deviceParams{};
			deviceParams.force_software = true;
			deviceParams.allow_software = true;
			m_d3d11 = pl_d3d11_create(m_log, &deviceParams);
			if (!m_d3d11)
				return false;

			m_renderer = pl_renderer_create(m_log, m_d3d11->gpu);
			return m_renderer != nullptr;
		}

		RgbaPixel Render(const pl_custom_lut* lut)
		{
			pl_gpu gpu = m_d3d11->gpu;
			const enum pl_fmt_caps requiredCaps = static_cast<enum pl_fmt_caps>(
				PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE);
			pl_fmt format = pl_find_fmt(gpu, PL_FMT_UNORM, 4, 8, 8, requiredCaps);
			Assert::IsNotNull(format, L"No host-readable RGBA8 render format is available");

			const RgbaPixel sourcePixels[4] = {
				{ 255, 0, 0, 255 }, { 255, 0, 0, 255 },
				{ 255, 0, 0, 255 }, { 255, 0, 0, 255 },
			};
			pl_tex_params sourceParams{};
			sourceParams.w = 2;
			sourceParams.h = 2;
			sourceParams.format = format;
			sourceParams.sampleable = true;
			sourceParams.initial_data = sourcePixels;
			pl_tex sourceTexture = pl_tex_create(gpu, &sourceParams);
			Assert::IsNotNull(sourceTexture);

			pl_tex_params targetParams{};
			targetParams.w = 2;
			targetParams.h = 2;
			targetParams.format = format;
			targetParams.renderable = true;
			targetParams.host_readable = true;
			pl_tex targetTexture = pl_tex_create(gpu, &targetParams);
			Assert::IsNotNull(targetTexture);

			pl_frame image = MakeRgbFrame(sourceTexture);
			pl_frame target = MakeRgbFrame(targetTexture);
			target.lut = lut;
			target.lut_type = PL_LUT_NATIVE;
			pl_render_params params = pl_render_fast_params;
			Assert::IsTrue(pl_render_image(m_renderer, &image, &target, &params));
			pl_gpu_finish(gpu);

			RgbaPixel result[4] = {};
			pl_tex_transfer_params download{};
			download.tex = targetTexture;
			download.ptr = result;
			Assert::IsTrue(pl_tex_download(gpu, &download));
			pl_tex_destroy(gpu, &targetTexture);
			pl_tex_destroy(gpu, &sourceTexture);
			return result[0];
		}

	private:
		static pl_frame MakeRgbFrame(pl_tex texture)
		{
			pl_frame frame{};
			frame.num_planes = 1;
			frame.planes[0].texture = texture;
			frame.planes[0].components = 4;
			frame.planes[0].component_mapping[0] = 0;
			frame.planes[0].component_mapping[1] = 1;
			frame.planes[0].component_mapping[2] = 2;
			frame.planes[0].component_mapping[3] = 3;
			frame.repr.sys = PL_COLOR_SYSTEM_RGB;
			frame.repr.levels = PL_COLOR_LEVELS_FULL;
			frame.color.primaries = PL_COLOR_PRIM_BT_709;
			frame.color.transfer = PL_COLOR_TRC_SRGB;
			return frame;
		}

		pl_log m_log = nullptr;
		pl_d3d11 m_d3d11 = nullptr;
		pl_renderer m_renderer = nullptr;
	};
}


namespace VideoProcessorTest
{
	TEST_CLASS(LibplaceboLutParserTests)
	{
	public:
		TEST_METHOD(EmptyPathLeavesLutDisabled)
		{
			const LoadResult result = Load(nullptr, "");
			Assert::AreEqual(static_cast<int>(Status::DISABLED), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::NONE), static_cast<int>(result.rejection));
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(ThreeDimensionalCubeLoadsForTargetLut)
		{
			TemporaryFile file;
			file.Write(Valid3dCube);

			LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::NONE), static_cast<int>(result.rejection));
			Assert::IsNotNull(result.lut);
			Assert::AreEqual(2, result.lut->size[0]);
			Assert::AreEqual(2, result.lut->size[1]);
			Assert::AreEqual(2, result.lut->size[2]);
			Assert::IsTrue(result.fileBytes > 0);
			Free(result);
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(OneDimensionalCubeIsExplicitlyRejected)
		{
			TemporaryFile file;
			file.Write(Valid1dCube);

			LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::REJECTED), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::ONE_DIMENSIONAL), static_cast<int>(result.rejection));
			Assert::IsNull(result.lut);
			Assert::AreEqual("1D not supported", ShortReason(result.rejection));
		}

		TEST_METHOD(MixedOneAndThreeDimensionalCubeIsExplicitlyRejected)
		{
			TemporaryFile file;
			file.Write(
				"LUT_1D_SIZE 2\n"
				"LUT_3D_SIZE 2\n"
				"0.0 0.0 0.0\n"
				"1.0 0.0 0.0\n"
				"0.0 1.0 0.0\n"
				"1.0 1.0 0.0\n"
				"0.0 0.0 1.0\n"
				"1.0 0.0 1.0\n"
				"0.0 1.0 1.0\n"
				"1.0 1.0 1.0\n");

			const LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(
				static_cast<int>(Status::REJECTED),
				static_cast<int>(result.status));
			Assert::AreEqual(
				static_cast<int>(Rejection::ONE_DIMENSIONAL),
				static_cast<int>(result.rejection));
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(MalformedCubeIsRejectedWithoutLut)
		{
			TemporaryFile file;
			file.Write("LUT_3D_SIZE 2\n0.0 0.0 0.0\n");

			LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::REJECTED), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::INVALID_CUBE), static_cast<int>(result.rejection));
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(EmptyFileIsRejectedWithoutLut)
		{
			TemporaryFile file;

			LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::REJECTED), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::EMPTY), static_cast<int>(result.rejection));
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(UnreadableFileIsRejectedWithoutLut)
		{
			const LoadResult result = Load(nullptr, "Z:\\VideoProcessor\\missing-vp012-lut.cube");
			Assert::AreEqual(static_cast<int>(Status::REJECTED), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::UNREADABLE), static_cast<int>(result.rejection));
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(FileLargerThanLimitIsRejectedBeforeParsing)
		{
			TemporaryFile file;
			file.ResizeTo(MAX_FILE_BYTES + 1);

			const LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::REJECTED), static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::TOO_LARGE), static_cast<int>(result.rejection));
			Assert::AreEqual(static_cast<size_t>(0), result.fileBytes);
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(EveryRejectionHasAShortOsdSafeReason)
		{
			for (const Rejection rejection : {
				Rejection::UNREADABLE, Rejection::EMPTY, Rejection::TOO_LARGE,
				Rejection::READ_FAILED, Rejection::INVALID_CUBE,
				Rejection::ONE_DIMENSIONAL, Rejection::UNSAFE_DIMENSIONS })
			{
				const std::string reason = ShortReason(rejection);
				Assert::IsFalse(reason.empty());
				Assert::IsTrue(reason.size() <= 20, L"LUT rejection reason is too long for the OSD");
			}
		}

		TEST_METHOD(ExactDisplayContractIsAccepted)
		{
			const ContractRejection rejection = ValidateContract(
				PL_COLOR_PRIM_BT_2020,
				PL_COLOR_TRC_GAMMA22,
				PL_COLOR_LEVELS_LIMITED,
				100.0,
				PL_COLOR_PRIM_BT_2020,
				PL_COLOR_TRC_GAMMA22,
				PL_COLOR_LEVELS_LIMITED,
				100.0,
				true);
			Assert::AreEqual(
				static_cast<int>(ContractRejection::NONE),
				static_cast<int>(rejection));
		}

		TEST_METHOD(TargetSignalMatchRequiresPrimariesTransferAndRange)
		{
			Assert::IsTrue(TargetMatchesSignal(
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_GAMMA24,
				PL_COLOR_LEVELS_LIMITED,
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_GAMMA24,
				PL_COLOR_LEVELS_LIMITED));
			Assert::IsFalse(TargetMatchesSignal(
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_SRGB,
				PL_COLOR_LEVELS_LIMITED,
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_GAMMA24,
				PL_COLOR_LEVELS_LIMITED));
			Assert::IsFalse(TargetMatchesSignal(
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_GAMMA24,
				PL_COLOR_LEVELS_FULL,
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_GAMMA24,
				PL_COLOR_LEVELS_LIMITED));
			Assert::IsFalse(TargetMatchesSignal(
				PL_COLOR_PRIM_BT_709, PL_COLOR_TRC_SRGB,
				PL_COLOR_LEVELS_FULL,
				PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_SRGB,
				PL_COLOR_LEVELS_FULL));
		}

		TEST_METHOD(AutoDisplayContractAcceptsTheSignaledTarget)
		{
			const ContractRejection rejection = ValidateContract(
				PL_COLOR_PRIM_UNKNOWN,
				PL_COLOR_TRC_UNKNOWN,
				PL_COLOR_LEVELS_UNKNOWN,
				0.0,
				PL_COLOR_PRIM_BT_709,
				PL_COLOR_TRC_SRGB,
				PL_COLOR_LEVELS_FULL,
				120.0,
				true);
			Assert::AreEqual(
				static_cast<int>(ContractRejection::NONE),
				static_cast<int>(rejection));
		}

		TEST_METHOD(P3DisplayContractIsExplicitlyRejected)
		{
			const ContractRejection rejection = ValidateContract(
				PL_COLOR_PRIM_DISPLAY_P3,
				PL_COLOR_TRC_SRGB,
				PL_COLOR_LEVELS_FULL,
				100.0,
				PL_COLOR_PRIM_BT_709,
				PL_COLOR_TRC_SRGB,
				PL_COLOR_LEVELS_FULL,
				100.0,
				true);
			Assert::AreEqual(
				static_cast<int>(ContractRejection::P3_NOT_SUPPORTED),
				static_cast<int>(rejection));
			Assert::AreEqual("P3 not supported", ShortReason(rejection));
		}

		TEST_METHOD(UnsignaledAndMismatchedContractsAreRejected)
		{
			Assert::AreEqual(
				static_cast<int>(ContractRejection::OUTPUT_NOT_SIGNALED),
				static_cast<int>(ValidateContract(
					PL_COLOR_PRIM_UNKNOWN,
					PL_COLOR_TRC_UNKNOWN,
					PL_COLOR_LEVELS_UNKNOWN,
					0.0,
					PL_COLOR_PRIM_BT_2020,
					PL_COLOR_TRC_GAMMA22,
					PL_COLOR_LEVELS_FULL,
					100.0,
					false)));

			for (const ContractRejection rejection : {
				ValidateContract(
					PL_COLOR_PRIM_BT_2020, PL_COLOR_TRC_UNKNOWN,
					PL_COLOR_LEVELS_UNKNOWN, 0.0,
					PL_COLOR_PRIM_BT_709, PL_COLOR_TRC_SRGB,
					PL_COLOR_LEVELS_FULL, 100.0, true),
				ValidateContract(
					PL_COLOR_PRIM_UNKNOWN, PL_COLOR_TRC_GAMMA24,
					PL_COLOR_LEVELS_UNKNOWN, 0.0,
					PL_COLOR_PRIM_BT_709, PL_COLOR_TRC_SRGB,
					PL_COLOR_LEVELS_FULL, 100.0, true),
				ValidateContract(
					PL_COLOR_PRIM_UNKNOWN, PL_COLOR_TRC_UNKNOWN,
					PL_COLOR_LEVELS_LIMITED, 0.0,
					PL_COLOR_PRIM_BT_709, PL_COLOR_TRC_SRGB,
					PL_COLOR_LEVELS_FULL, 100.0, true),
				ValidateContract(
					PL_COLOR_PRIM_UNKNOWN, PL_COLOR_TRC_UNKNOWN,
					PL_COLOR_LEVELS_UNKNOWN, 120.0,
					PL_COLOR_PRIM_BT_709, PL_COLOR_TRC_SRGB,
					PL_COLOR_LEVELS_FULL, 100.0, true) })
			{
				Assert::AreEqual(
					static_cast<int>(ContractRejection::PROFILE_MISMATCH),
					static_cast<int>(rejection));
			}
		}

		TEST_METHOD(EveryContractRejectionHasAShortOsdSafeReason)
		{
			for (const ContractRejection rejection : {
				ContractRejection::OUTPUT_NOT_SIGNALED,
				ContractRejection::P3_NOT_SUPPORTED,
				ContractRejection::PROFILE_MISMATCH })
			{
				const std::string reason = ShortReason(rejection);
				Assert::IsFalse(reason.empty());
				Assert::IsTrue(
					reason.size() <= 20,
					L"LUT contract rejection reason is too long for the OSD");
			}
		}

		TEST_METHOD(ConfiguredExternalCubeExamplesLoadWhenProvided)
		{
			char directory[MAX_PATH] = {};
			const DWORD length = GetEnvironmentVariableA(
				"VP_LUT_EXAMPLE_DIR", directory, ARRAYSIZE(directory));
			if (length == 0)
				return;
			Assert::IsTrue(length < ARRAYSIZE(directory));

			std::string root(directory);
			if (!root.empty() && root.back() != '\\' && root.back() != '/')
				root.push_back('\\');
			for (const char* name : {
				"10^3 CENTER PKCHR 1886 20260126_BMD65.cube",
				"BW_BMD65.cube",
				"Unity_BMD65.cube" })
			{
				LoadResult result = Load(nullptr, root + name);
				Assert::AreEqual(
					static_cast<int>(Status::ACTIVE),
					static_cast<int>(result.status),
					std::wstring(name, name + strlen(name)).c_str());
				Assert::IsNotNull(result.lut);
				Assert::AreEqual(65, result.lut->size[0]);
				Assert::AreEqual(65, result.lut->size[1]);
				Assert::AreEqual(65, result.lut->size[2]);
				Free(result);
			}
		}

		TEST_METHOD(TargetLutGpuReadbackProvesIdentityAndNonIdentityPathsDiffer)
		{
			TemporaryFile identityFile;
			identityFile.Write(Valid3dCube);
			LoadResult identityLut = Load(nullptr, identityFile.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE),
				static_cast<int>(identityLut.status));

			TemporaryFile greenFile;
			greenFile.Write(Green3dCube);
			LoadResult greenLut = Load(nullptr, greenFile.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE),
				static_cast<int>(greenLut.status));

			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(), L"Could not create the libplacebo WARP test device");
			const RgbaPixel baseline = fixture.Render(nullptr);
			const RgbaPixel identity = fixture.Render(identityLut.lut);
			const RgbaPixel calibrated = fixture.Render(greenLut.lut);
			Free(identityLut);
			Free(greenLut);

			Assert::IsTrue(baseline.r > 240 && baseline.g < 15 && baseline.b < 15,
				L"The no-LUT target path did not preserve the red input");
			Assert::AreEqual(baseline.r, identity.r);
			Assert::AreEqual(baseline.g, identity.g);
			Assert::AreEqual(baseline.b, identity.b);
			Assert::IsTrue(calibrated.r < 15 && calibrated.g > 240 && calibrated.b < 15,
				L"The target LUT did not produce its expected green output");
		}
	};
}
