#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/LibplaceboDisplayLut.h>
#include <vprenderer/LibplaceboCalibrationLutPolicy.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

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

	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			char tempPath[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(tempPath), tempPath) > 0);
			char directory[MAX_PATH] = {};
			Assert::IsTrue(GetTempFileNameA(tempPath, "vpl", 0, directory) != 0);
			Assert::IsTrue(DeleteFileA(directory));
			Assert::IsTrue(CreateDirectoryA(directory, nullptr));
			m_path = directory;
		}

		~TemporaryDirectory()
		{
			for (const std::string& file : m_files)
				DeleteFileA(file.c_str());
			if (!m_path.empty())
				RemoveDirectoryA(m_path.c_str());
		}

		const std::string& Path() const { return m_path; }

		std::string Write(const char* fileName, const char* contents)
		{
			const std::string path = m_path + "\\" + fileName;
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			Assert::IsTrue(static_cast<bool>(output));
			output << contents;
			output.close();
			m_files.push_back(path);
			return path;
		}

	private:
		std::string m_path;
		std::vector<std::string> m_files;
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

	// Encodes the LUT input's red coordinate as green and its inverse as red.
	// A 25% linear-light gray encoded with Gamma 2.2 reaches the LUT near 53%,
	// so green must exceed red if the target LUT receives gamma-coded RGB.
	const char* GammaCoordinateProbeCube =
		"TITLE \"VP0166 gamma-coordinate probe\"\n"
		"LUT_3D_SIZE 2\n"
		"1.0 0.0 0.0\n"
		"0.0 1.0 0.0\n"
		"1.0 0.0 0.0\n"
		"0.0 1.0 0.0\n"
		"1.0 0.0 0.0\n"
		"0.0 1.0 0.0\n"
		"1.0 0.0 0.0\n"
		"0.0 1.0 0.0\n";

	struct RgbaPixel
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	struct RenderStepCapture
	{
		bool errorDiffusion = false;

		static void Callback(void* privateData, const pl_render_info* info)
		{
			RenderStepCapture* capture =
				static_cast<RenderStepCapture*>(privateData);
			if (!capture || !info || !info->pass || !info->pass->shader)
				return;
			const pl_shader_info shader = info->pass->shader;
			for (int index = 0; index < shader->num_steps; ++index)
			{
				std::string step = shader->steps[index] ? shader->steps[index] : "";
				std::transform(step.begin(), step.end(), step.begin(),
					[](unsigned char value) {
						return static_cast<char>(std::tolower(value));
					});
				if (step.find("error diffusion") != std::string::npos ||
					step.find("error-diffusion") != std::string::npos)
					capture->errorDiffusion = true;
			}
		}
	};

	std::string LoadBundledShader(const char* fileName)
	{
		std::string path = __FILE__;
		for (int level = 0; level < 3; ++level)
		{
			const size_t separator = path.find_last_of("\\/");
			if (separator == std::string::npos)
				return {};
			path.resize(separator);
		}
		path += "\\shaders\\";
		path += fileName;
		std::ifstream input(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	void ReplaceShaderToken(std::string& source, const std::string& name,
		const std::string& value)
	{
		const std::string token = "{{" + name + "}}";
		size_t position = 0;
		while ((position = source.find(token, position)) != std::string::npos)
		{
			source.replace(position, token.size(), value);
			position += value.size();
		}
	}

	const pl_hook* ParseBundledNlsShader(pl_gpu gpu, const char* fileName,
		double axisBalance, double strength = 1.0)
	{
		std::string source = LoadBundledShader(fileName);
		Assert::IsFalse(source.empty(), L"Bundled NLS shader was not found");
		const std::map<std::string, std::string> parameters = {
			{ "strength", std::to_string(strength) },
			{ "curve", "2.0" },
			{ "geometry", "1" },
			{ "center_protection", "0.35" },
			{ "axis_balance", std::to_string(axisBalance) },
			{ "max_center_zoom", "1.08" },
			{ "horizontal_center_protection", "0.35" },
			{ "vertical_center_protection", "0.25" }
		};
		for (const auto& parameter : parameters)
			ReplaceShaderToken(source, parameter.first, parameter.second);
		Assert::IsTrue(source.find("{{") == std::string::npos,
			L"Bundled NLS shader still contains an unsubstituted token");
		const pl_hook* hook = pl_mpv_user_shader_parse(
			gpu, source.data(), source.size());
		Assert::IsNotNull(hook, L"libplacebo rejected bundled NLS shader");
		return hook;
	}

	void BindNlsShader(const pl_hook* hook, float stretchRatio, float warpAxis)
	{
		bool stretchBound = false;
		bool axisBound = false;
		for (int index = 0; index < hook->num_parameters; ++index)
		{
			const pl_hook_par& parameter = hook->parameters[index];
			if (!parameter.name || !parameter.data ||
				parameter.type != PL_VAR_FLOAT)
				continue;
			if (strcmp(parameter.name, "stretch_ratio") == 0)
			{
				parameter.data->f = stretchRatio;
				stretchBound = true;
			}
			else if (strcmp(parameter.name, "warp_axis") == 0)
			{
				parameter.data->f = warpAxis;
				axisBound = true;
			}
		}
		Assert::IsTrue(stretchBound && axisBound,
			L"Bundled NLS shader did not expose both dynamic parameters");
	}

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
			return Render(lut, pl_render_fast_params,
				PL_COLOR_LEVELS_FULL, PL_LUT_NORMALIZED);
		}

		RgbaPixel Render(
			const pl_custom_lut* lut,
			const struct pl_render_params& params,
			enum pl_color_levels targetLevels = PL_COLOR_LEVELS_FULL,
			enum pl_lut_type lutType = PL_LUT_NORMALIZED,
			RgbaPixel sourcePixel = { 255, 0, 0, 255 },
			enum pl_color_transfer sourceTransfer = PL_COLOR_TRC_SRGB,
			enum pl_color_transfer targetTransfer = PL_COLOR_TRC_SRGB,
			enum pl_color_primaries sourcePrimaries = PL_COLOR_PRIM_BT_709,
			enum pl_color_primaries targetPrimaries = PL_COLOR_PRIM_BT_709,
			float sourceMaxNits = 0.0f,
			float targetMaxNits = 0.0f)
		{
			pl_gpu gpu = m_d3d11->gpu;
			const enum pl_fmt_caps requiredCaps = static_cast<enum pl_fmt_caps>(
				PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE);
			pl_fmt format = pl_find_fmt(gpu, PL_FMT_UNORM, 4, 8, 8, requiredCaps);
			Assert::IsNotNull(format, L"No host-readable RGBA8 render format is available");

			const RgbaPixel sourcePixels[4] = {
				sourcePixel, sourcePixel, sourcePixel, sourcePixel,
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
			image.color.transfer = sourceTransfer;
			image.color.primaries = sourcePrimaries;
			image.color.hdr.max_luma = sourceMaxNits;
			if (sourceMaxNits > 0.0f)
				image.color.hdr.min_luma = PL_COLOR_HDR_BLACK;
			image.color.hdr.max_cll = sourceMaxNits;
			image.color.hdr.max_fall = sourceMaxNits;
			target.repr.levels = targetLevels;
			target.color.transfer = targetTransfer;
			target.color.primaries = targetPrimaries;
			target.color.hdr.max_luma = targetMaxNits;
			if (targetMaxNits > 0.0f)
				target.color.hdr.min_luma = PL_COLOR_HDR_BLACK;
			target.lut = lut;
			target.lut_type = lut ? lutType : PL_LUT_UNKNOWN;
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

		pl_gpu Gpu() const
		{
			return m_d3d11 ? m_d3d11->gpu : nullptr;
		}

		pl_render_errors Errors() const
		{
			return pl_renderer_get_errors(m_renderer);
		}

		bool DetectedHdrMetadata(pl_hdr_metadata& metadata) const
		{
			return pl_renderer_get_hdr_metadata(m_renderer, &metadata);
		}

		std::vector<RgbaPixel> RenderCoordinateField(
			const pl_hook* hook, int width = 64, int height = 64)
		{
			pl_gpu gpu = m_d3d11->gpu;
			const enum pl_fmt_caps requiredCaps = static_cast<enum pl_fmt_caps>(
				PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE |
				PL_FMT_CAP_HOST_READABLE);
			pl_fmt format = pl_find_fmt(
				gpu, PL_FMT_UNORM, 4, 8, 8, requiredCaps);
			Assert::IsNotNull(format);

			std::vector<RgbaPixel> sourcePixels(width * height);
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
					sourcePixels[y * width + x] = {
						static_cast<uint8_t>(x * 255 / (width - 1)),
						static_cast<uint8_t>(y * 255 / (height - 1)),
						static_cast<uint8_t>((x + y) * 255 /
							(width + height - 2)), 255 };

			pl_tex_params sourceParams{};
			sourceParams.w = width;
			sourceParams.h = height;
			sourceParams.format = format;
			sourceParams.sampleable = true;
			sourceParams.initial_data = sourcePixels.data();
			pl_tex sourceTexture = pl_tex_create(gpu, &sourceParams);
			Assert::IsNotNull(sourceTexture);

			pl_tex_params targetParams{};
			targetParams.w = width;
			targetParams.h = height;
			targetParams.format = format;
			targetParams.renderable = true;
			targetParams.host_readable = true;
			pl_tex targetTexture = pl_tex_create(gpu, &targetParams);
			Assert::IsNotNull(targetTexture);

			pl_frame image = MakeRgbFrame(sourceTexture);
			pl_frame target = MakeRgbFrame(targetTexture);
			pl_render_params params = pl_render_fast_params;
			if (hook)
			{
				params.hooks = &hook;
				params.num_hooks = 1;
			}
			Assert::IsTrue(pl_render_image(
				m_renderer, &image, &target, &params));
			pl_gpu_finish(gpu);

			std::vector<RgbaPixel> result(width * height);
			pl_tex_transfer_params download{};
			download.tex = targetTexture;
			download.ptr = result.data();
			Assert::IsTrue(pl_tex_download(gpu, &download));
			pl_tex_destroy(gpu, &targetTexture);
			pl_tex_destroy(gpu, &sourceTexture);
			return result;
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

		TEST_METHOD(NonDefaultCubeDomainIsRejectedInsteadOfMisinterpreted)
		{
			TemporaryFile file;
			const std::string contents = std::string(
				"DOMAIN_MIN 0.1 0.0 0.0\n"
				"DOMAIN_MAX 1.0 1.0 1.0\n") + Valid3dCube;
			file.Write(contents.c_str());

			const LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::REJECTED),
				static_cast<int>(result.status));
			Assert::AreEqual(static_cast<int>(Rejection::UNSUPPORTED_DOMAIN),
				static_cast<int>(result.rejection));
			Assert::AreEqual("non-default domain", ShortReason(result.rejection));
			Assert::IsNull(result.lut);
		}

		TEST_METHOD(DefaultCubeDomainRemainsSupported)
		{
			TemporaryFile file;
			const std::string contents = std::string(
				"DOMAIN_MIN 0.0 0.0 0.0\n"
				"DOMAIN_MAX 1.0 1.0 1.0\n") + Valid3dCube;
			file.Write(contents.c_str());

			LoadResult result = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(result.status));
			Free(result);
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

		TEST_METHOD(PartialSamePathReplacementRetainsThenRetriesFinalCube)
		{
			TemporaryFile file;
			file.Write(Valid3dCube);
			LoadResult initial = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(initial.status));
			const uint64_t initialSignature = initial.lut->signature;

			file.Write("invalid replacement");
			const LoadResult partial = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::REJECTED),
				static_cast<int>(partial.status));
			Assert::IsTrue(partial.fileVersion.available);
			Assert::AreEqual(
				static_cast<int>(LibplaceboCalibrationLut::ReloadFailureAction::RETAIN_LAST_KNOWN_GOOD),
				static_cast<int>(LibplaceboCalibrationLut::ResolveReloadFailure(true, true)));
			Assert::AreEqual(initialSignature, initial.lut->signature,
				L"The last-known-good Cube must remain owned during a failed reload");

			file.Write(Green3dCube);
			const FileVersion completed = ProbeFileVersion(file.Path());
			Assert::IsFalse(SameFileVersion(partial.fileVersion, completed),
				L"Completion after a partial write must schedule another reload");
			LoadResult replacement = Load(nullptr, file.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(replacement.status));
			Assert::AreNotEqual(initialSignature, replacement.lut->signature,
				L"The completed same-path Cube was not validated and swapped");
			Free(replacement);
			Free(initial);
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

		TEST_METHOD(ConstrainedLutPathUsesTheOpenedFileHandleForContainment)
		{
			TemporaryDirectory configurationDirectory;
			TemporaryDirectory outsideDirectory;
			const std::string inside =
				configurationDirectory.Write("inside.cube", Valid3dCube);
			const std::string outside =
				outsideDirectory.Write("outside.cube", Valid3dCube);

			LoadResult accepted = Load(
				nullptr, inside, configurationDirectory.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE), static_cast<int>(accepted.status));
			Free(accepted);

			const LoadResult rejected = Load(
				nullptr, outside, configurationDirectory.Path());
			Assert::AreEqual(
				static_cast<int>(Status::REJECTED), static_cast<int>(rejected.status));
			Assert::AreEqual(
				static_cast<int>(Rejection::PATH_OUTSIDE_BASE),
				static_cast<int>(rejected.rejection));
			Assert::AreEqual("bad path", ShortReason(rejected.rejection));
			Assert::IsNull(rejected.lut);
		}

		TEST_METHOD(EveryRejectionHasAShortOsdSafeReason)
		{
			for (const Rejection rejection : {
				Rejection::UNREADABLE, Rejection::EMPTY, Rejection::TOO_LARGE,
				Rejection::READ_FAILED, Rejection::PATH_OUTSIDE_BASE,
				Rejection::INVALID_CUBE,
				Rejection::ONE_DIMENSIONAL, Rejection::UNSUPPORTED_DOMAIN,
				Rejection::UNSAFE_DIMENSIONS })
			{
				const std::string reason = ShortReason(rejection);
				Assert::IsFalse(reason.empty());
				Assert::IsTrue(reason.size() <= 20, L"LUT rejection reason is too long for the OSD");
			}
		}

		TEST_METHOD(ConfiguredCalibrationCubeExamplesLoadWhenProvided)
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

		TEST_METHOD(NormalizedCalibrationLutRunsBeforeLimitedRangeEncoding)
		{
			TemporaryFile greenFile;
			greenFile.Write(Green3dCube);
			LoadResult greenLut = Load(nullptr, greenFile.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(greenLut.status));

			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(),
				L"Could not create the libplacebo WARP test device");
			const RgbaPixel normalized = fixture.Render(greenLut.lut,
				pl_render_fast_params, PL_COLOR_LEVELS_LIMITED,
				PL_LUT_NORMALIZED);
			const RgbaPixel native = fixture.Render(greenLut.lut,
				pl_render_fast_params, PL_COLOR_LEVELS_LIMITED,
				PL_LUT_NATIVE);
			Free(greenLut);

			Assert::IsTrue(normalized.r >= 14 && normalized.r <= 18 &&
				normalized.g >= 233 && normalized.g <= 237 &&
				normalized.b >= 14 && normalized.b <= 18,
				L"Normalized target LUT was not followed by legal-range encoding");
			Assert::IsTrue(native.r < 4 && native.g > 251 && native.b < 4,
				L"Native target LUT unexpectedly ran before legal-range encoding");
		}

		TEST_METHOD(NormalizedCalibrationLutReceivesTargetGammaCoordinates)
		{
			TemporaryFile probeFile;
			probeFile.Write(GammaCoordinateProbeCube);
			LoadResult probeLut = Load(nullptr, probeFile.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(probeLut.status));

			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(),
				L"Could not create the libplacebo WARP test device");
			const RgbaPixel result = fixture.Render(probeLut.lut,
				pl_render_fast_params, PL_COLOR_LEVELS_FULL,
				PL_LUT_NORMALIZED, { 64, 64, 64, 255 },
				PL_COLOR_TRC_LINEAR, PL_COLOR_TRC_GAMMA22);
			Free(probeLut);

			Assert::IsTrue(result.g > result.r && result.g >= 130,
				L"Calibration Cube did not receive Gamma-2.2 encoded target RGB");
		}

		TEST_METHOD(TargetLutGpuReadbackPreservesHighQualityErrorDiffusion)
		{
			TemporaryFile greenFile;
			greenFile.Write(Green3dCube);
			LoadResult greenLut = Load(nullptr, greenFile.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE),
				static_cast<int>(greenLut.status));

			pl_render_params compatibleParams = pl_render_high_quality_params;
			compatibleParams.error_diffusion =
				&pl_error_diffusion_floyd_steinberg;
			compatibleParams.dither_params = nullptr;
			RenderStepCapture steps;
			compatibleParams.info_callback = &RenderStepCapture::Callback;
			compatibleParams.info_priv = &steps;
			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(), L"Could not create the libplacebo WARP test device");
			const RgbaPixel calibrated = fixture.Render(greenLut.lut, compatibleParams);
			Free(greenLut);

			Assert::IsTrue(calibrated.r < 15 && calibrated.g > 240 && calibrated.b < 15,
				L"Calibration LUT plus final error diffusion did not produce green output");
			Assert::IsTrue(steps.errorDiffusion,
				L"The renderer did not dispatch the requested error-diffusion shader");
			Assert::IsTrue((fixture.Errors().errors &
				PL_RENDER_ERR_ERROR_DIFFUSION) == 0,
				L"libplacebo reported error-diffusion failure");
		}

		TEST_METHOD(PqHdrDynamicToneMappingRunsBeforeCalibrationLut)
		{
			TemporaryFile identityFile;
			identityFile.Write(Valid3dCube);
			LoadResult identity = Load(nullptr, identityFile.Path());
			TemporaryFile greenFile;
			greenFile.Write(Green3dCube);
			LoadResult green = Load(nullptr, greenFile.Path());
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(identity.status));
			Assert::AreEqual(static_cast<int>(Status::ACTIVE),
				static_cast<int>(green.status));

			pl_render_params params = pl_render_high_quality_params;
			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(),
				L"Could not create the libplacebo WARP test device");
			const auto render = [&](const pl_custom_lut* lut, float targetNits)
			{
				return fixture.Render(lut, params, PL_COLOR_LEVELS_FULL,
					PL_LUT_NORMALIZED, { 160, 160, 160, 255 },
					PL_COLOR_TRC_PQ, PL_COLOR_TRC_GAMMA22,
					PL_COLOR_PRIM_BT_2020, PL_COLOR_PRIM_BT_709,
					1000.0f, targetNits);
			};
			const RgbaPixel baseline = render(nullptr, 100.0f);
			pl_hdr_metadata detected{};
			const bool detectedAvailable = fixture.DetectedHdrMetadata(detected);
			const RgbaPixel alternateTarget = render(nullptr, 400.0f);
			const RgbaPixel withIdentity = render(identity.lut, 100.0f);
			const RgbaPixel withCalibration = render(green.lut, 100.0f);
			Free(identity);
			Free(green);

			Assert::IsTrue(std::abs(static_cast<int>(baseline.r) -
					static_cast<int>(baseline.g)) <= 2 &&
				std::abs(static_cast<int>(baseline.g) -
					static_cast<int>(baseline.b)) <= 2 &&
				std::abs(static_cast<int>(baseline.r) -
					static_cast<int>(alternateTarget.r)) >= 3,
				L"HDR/PQ result did not respond to the SDR DTM target luminance");
			Assert::IsTrue(detectedAvailable && detected.max_pq_y > 0.0f,
				L"Dynamic HDR peak analysis did not produce CIE-Y metadata");
			Assert::IsTrue((fixture.Errors().errors &
				PL_RENDER_ERR_PEAK_DETECT) == 0,
				L"libplacebo reported a dynamic peak-analysis failure");
			Assert::IsTrue(std::abs(static_cast<int>(baseline.r) -
				static_cast<int>(withIdentity.r)) <= 2 &&
				std::abs(static_cast<int>(baseline.g) -
					static_cast<int>(withIdentity.g)) <= 2 &&
				std::abs(static_cast<int>(baseline.b) -
					static_cast<int>(withIdentity.b)) <= 2,
				L"Identity target LUT replaced or perturbed the HDR-to-SDR DTM result");
			Assert::IsTrue(withCalibration.r < 15 &&
				withCalibration.g > 240 && withCalibration.b < 15,
				L"Distinct calibration LUT was not applied after HDR-to-SDR DTM");
		}

		TEST_METHOD(BundledNlsGlSlHooksMovePixelsOnTheRealGpuPath)
		{
			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(),
				L"Could not create the libplacebo WARP test device");
			const std::vector<RgbaPixel> baseline =
				fixture.RenderCoordinateField(nullptr);

			const pl_hook* balanced = ParseBundledNlsShader(
				fixture.Gpu(), "NLSPlus.glsl", 0.25, 0.8);
			BindNlsShader(balanced, 1.32f, 0.0f);
			const std::vector<RgbaPixel> balancedPixels =
				fixture.RenderCoordinateField(balanced);
			const size_t quarter = 16 * 64 + 16;
			Assert::IsTrue(std::abs(static_cast<int>(balancedPixels[quarter].r) -
				static_cast<int>(baseline[quarter].r)) >= 3,
				L"NLS+ did not move horizontal coordinate pixels");
			Assert::IsTrue(std::abs(static_cast<int>(balancedPixels[quarter].g) -
				static_cast<int>(baseline[quarter].g)) >= 3,
				L"NLS+ did not move vertical coordinate pixels");
			Assert::IsTrue(std::abs(static_cast<int>(balancedPixels.front().r) -
				static_cast<int>(baseline.front().r)) <= 2 &&
				std::abs(static_cast<int>(balancedPixels.front().g) -
					static_cast<int>(baseline.front().g)) <= 2,
				L"NLS+ did not keep the fixed image edge bounded");

			// Dynamic changes must affect the already-parsed hook. This catches a
			// compiled shader whose runtime parameter data is never consumed.
			BindNlsShader(balanced, 1.0f, 0.0f);
			const std::vector<RgbaPixel> identityPixels =
				fixture.RenderCoordinateField(balanced);
			Assert::IsTrue(std::abs(static_cast<int>(identityPixels[quarter].r) -
				static_cast<int>(baseline[quarter].r)) <= 2 &&
				std::abs(static_cast<int>(identityPixels[quarter].g) -
					static_cast<int>(baseline[quarter].g)) <= 2,
				L"Updating the same NLS+ hook to ratio 1 was not identity");

			const pl_hook* oneAxisPlus = ParseBundledNlsShader(
				fixture.Gpu(), "NLSPlus.glsl", 0.0);
			const pl_hook* established = ParseBundledNlsShader(
				fixture.Gpu(), "NLS.glsl", 0.0);
			BindNlsShader(oneAxisPlus, 1.32f, 0.0f);
			BindNlsShader(established, 1.32f, 0.0f);
			const std::vector<RgbaPixel> oneAxisPlusPixels =
				fixture.RenderCoordinateField(oneAxisPlus);
			const std::vector<RgbaPixel> establishedPixels =
				fixture.RenderCoordinateField(established);
			for (size_t index = 0; index < establishedPixels.size(); ++index)
			{
				Assert::IsTrue(std::abs(
					static_cast<int>(oneAxisPlusPixels[index].r) -
					static_cast<int>(establishedPixels[index].r)) <= 2);
				Assert::IsTrue(std::abs(
					static_cast<int>(oneAxisPlusPixels[index].g) -
					static_cast<int>(establishedPixels[index].g)) <= 2);
			}

			BindNlsShader(established, 1.125f, 1.0f);
			const std::vector<RgbaPixel> verticalPixels =
				fixture.RenderCoordinateField(established);
			Assert::IsTrue(std::abs(static_cast<int>(verticalPixels[quarter].g) -
				static_cast<int>(baseline[quarter].g)) >= 3,
				L"Existing NLS GLSL did not move vertical coordinate pixels");
			Assert::IsTrue(std::abs(static_cast<int>(verticalPixels[quarter].r) -
				static_cast<int>(baseline[quarter].r)) <= 2,
				L"Vertical NLS unexpectedly altered the horizontal coordinate");

			pl_mpv_user_shader_destroy(&balanced);
			pl_mpv_user_shader_destroy(&oneAxisPlus);
			pl_mpv_user_shader_destroy(&established);
		}
	};
}
