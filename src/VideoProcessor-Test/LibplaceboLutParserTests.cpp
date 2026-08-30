#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/LibplaceboDisplayLut.h>
#include <vprenderer/LibplaceboExternalHdrLutFrame.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>

#include <cstdint>
#include <algorithm>
#include <cctype>
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

	const char* SwapRedBlue3dCube =
		"TITLE \"VP synthetic R/B swap\"\n"
		"LUT_3D_SIZE 2\n"
		"0.0 0.0 0.0\n"
		"0.0 0.0 1.0\n"
		"0.0 1.0 0.0\n"
		"0.0 1.0 1.0\n"
		"1.0 0.0 0.0\n"
		"1.0 0.0 1.0\n"
		"1.0 1.0 0.0\n"
		"1.0 1.0 1.0\n";

	// Unmodified ASWF OpenColorIO interoperability fixture, pinned at commit
	// 5a808fb57a94c7229640a97835c420c9a1fbd1fe (Git blob
	// 04934465ae6e99416c347897aa5b2cc1a8257432). OpenColorIO is BSD-3-Clause;
	// redistributed under 3rdparty/opencolorio-fixture/LICENSE.txt.
	const char* OpenColorIoIridas3dCube =
		"TITLE \"A test 3D-LUT.\"\n"
		"LUT_3D_SIZE 2\n"
		"DOMAIN_MIN 0.0 1.0 0.0\n"
		"DOMAIN_MAX 2.0 2.0 1.0\n"
		"\n"
		"0.0 0.0 0.0\n"
		"2.0 0.0 0.0\n"
		"0.0 2.0 0.0\n"
		"2.0 2.0 0.0\n"
		"0.0 0.0 2.0\n"
		"2.0 0.0 2.0\n"
		"0.0 2.0 2.0\n"
		"2.0 2.0 2.0\n";

	// Unmodified nonlinear Cube example from cube-lut-factory.js README,
	// pinned at commit fde633ad057e514bd3f04049cee3289af93cef2b
	// (README Git blob 0ed20e76853c4da321785813b5a608ccc16127ab).
	// The repository is MIT licensed; see the matching 3rdparty fixture license.
	const char* CubeLutFactoryNonlinear3dCube = R"VP_CUBE(# Cube LUT file generated with cube-lut-factory
# https://github.com/diegoinacio/cube-lut-factory.js
TITLE "Test"

# Cube LUT size
LUT_3D_SIZE 4

# Cube LUT domain
DOMAIN_MIN 0 0 0
DOMAIN_MAX 1 1 1

# Cube LUT data points
0 0 0
0.05555555555555555 0 0.1111111111111111
0.2222222222222222 0 0.2222222222222222
0.5 0 0.3333333333333333
0 0.012345679012345677 0
0.05555555555555555 0.012345679012345677 0.1111111111111111
0.2222222222222222 0.012345679012345677 0.2222222222222222
0.5 0.012345679012345677 0.3333333333333333
0 0.09876543209876541 0
0.05555555555555555 0.09876543209876541 0.1111111111111111
0.2222222222222222 0.09876543209876541 0.2222222222222222
0.5 0.09876543209876541 0.3333333333333333
0 0.3333333333333333 0
0.05555555555555555 0.3333333333333333 0.1111111111111111
0.2222222222222222 0.3333333333333333 0.2222222222222222
0.5 0.3333333333333333 0.3333333333333333
0 0 0.11522633744855966
0.05555555555555555 0.07407407407407407 0.22633744855967075
0.2222222222222222 0.14814814814814814 0.33744855967078186
0.5 0.2222222222222222 0.448559670781893
0.05555555555555555 0.012345679012345677 0.11522633744855966
0.1111111111111111 0.08641975308641975 0.22633744855967075
0.2777777777777778 0.16049382716049382 0.33744855967078186
0.5555555555555556 0.23456790123456786 0.448559670781893
0.1111111111111111 0.09876543209876541 0.11522633744855966
0.16666666666666666 0.17283950617283947 0.22633744855967075
0.3333333333333333 0.24691358024691357 0.33744855967078186
0.6111111111111112 0.3209876543209876 0.448559670781893
0.16666666666666666 0.3333333333333333 0.11522633744855966
0.2222222222222222 0.40740740740740744 0.22633744855967075
0.38888888888888884 0.48148148148148145 0.33744855967078186
0.6666666666666666 0.5555555555555555 0.448559670781893
0 0 0.28806584362139914
0.05555555555555555 0.14814814814814814 0.3991769547325103
0.2222222222222222 0.2962962962962963 0.5102880658436214
0.5 0.4444444444444444 0.6213991769547325
0.1111111111111111 0.012345679012345677 0.28806584362139914
0.16666666666666666 0.16049382716049382 0.3991769547325103
0.3333333333333333 0.30864197530864196 0.5102880658436214
0.6111111111111112 0.4567901234567901 0.6213991769547325
0.2222222222222222 0.09876543209876541 0.28806584362139914
0.2777777777777778 0.24691358024691357 0.3991769547325103
0.4444444444444444 0.3950617283950617 0.5102880658436214
0.7222222222222222 0.5432098765432098 0.6213991769547325
0.3333333333333333 0.3333333333333333 0.28806584362139914
0.38888888888888884 0.48148148148148145 0.3991769547325103
0.5555555555555556 0.6296296296296297 0.5102880658436214
0.8333333333333333 0.7777777777777777 0.6213991769547325
0 0 0.6666666666666666
0.05555555555555555 0.2222222222222222 0.7777777777777777
0.2222222222222222 0.4444444444444444 0.8888888888888888
0.5 0.6666666666666666 1
0.16666666666666666 0.012345679012345677 0.6666666666666666
0.2222222222222222 0.23456790123456786 0.7777777777777777
0.38888888888888884 0.4567901234567901 0.8888888888888888
0.6666666666666666 0.6790123456790124 1
0.3333333333333333 0.09876543209876541 0.6666666666666666
0.38888888888888884 0.3209876543209876 0.7777777777777777
0.5555555555555556 0.5432098765432098 0.8888888888888888
0.8333333333333333 0.7654320987654321 1
0.5 0.3333333333333333 0.6666666666666666
0.5555555555555556 0.5555555555555555 0.7777777777777777
0.7222222222222222 0.7777777777777777 0.8888888888888888
1 1 1
)VP_CUBE";

	struct RgbaPixel
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
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
			return Render(lut, pl_render_fast_params, { 255, 0, 0, 255 });
		}

		RgbaPixel Render(const pl_custom_lut* lut, RgbaPixel source)
		{
			return Render(lut, pl_render_fast_params, source);
		}

		RgbaPixel Render(
			const pl_custom_lut* lut,
			const struct pl_render_params& params)
		{
			return Render(lut, params, { 255, 0, 0, 255 });
		}

		RgbaPixel Render(
			const pl_custom_lut* lut,
			const struct pl_render_params& params,
			RgbaPixel source)
		{
			pl_gpu gpu = m_d3d11->gpu;
			const enum pl_fmt_caps requiredCaps = static_cast<enum pl_fmt_caps>(
				PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE);
			pl_fmt format = pl_find_fmt(gpu, PL_FMT_UNORM, 4, 8, 8, requiredCaps);
			Assert::IsNotNull(format, L"No host-readable RGBA8 render format is available");

			const RgbaPixel sourcePixels[4] = {
				source, source, source, source,
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
			std::string expectedPath = file.Path();
			std::replace(expectedPath.begin(), expectedPath.end(), '/', '\\');
			std::transform(expectedPath.begin(), expectedPath.end(),
				expectedPath.begin(), [](unsigned char character)
				{ return static_cast<char>(std::tolower(character)); });
			Assert::AreEqual(expectedPath.c_str(), result.canonicalPath.c_str());
			Assert::AreEqual(
				"a36fd00b830ba2f99a71de82196021619310c7a00aa2a96a3a202d3a6bc2a312",
				result.contentSha256.c_str());
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
				Rejection::READ_FAILED, Rejection::PATH_IDENTITY_FAILED,
				Rejection::HASH_FAILED, Rejection::PATH_OUTSIDE_BASE,
				Rejection::INVALID_CUBE,
				Rejection::ONE_DIMENSIONAL, Rejection::UNSUPPORTED_DOMAIN,
				Rejection::UNSAFE_DIMENSIONS })
			{
				const std::string reason = ShortReason(rejection);
				Assert::IsFalse(reason.empty());
				Assert::IsTrue(reason.size() <= 20, L"LUT rejection reason is too long for the OSD");
			}
		}

		TEST_METHOD(PinnedOpenColorIoCubeRejectsUnsupportedDomainSemantics)
		{
			TemporaryFile file;
			file.Write(OpenColorIoIridas3dCube);
			const LoadResult lut = Load(nullptr, file.Path());
			Assert::AreEqual(
				static_cast<int>(Status::REJECTED), static_cast<int>(lut.status));
			Assert::AreEqual(static_cast<int>(Rejection::UNSUPPORTED_DOMAIN),
				static_cast<int>(lut.rejection));
			Assert::AreEqual("domain unsupported", ShortReason(lut.rejection));
			Assert::IsNull(lut.lut);
		}

		TEST_METHOD(ExternalHdrCandidateSetLoadsAllSlotsBeforeSelection)
		{
			TemporaryDirectory directory;
			const std::string bt709 = directory.Write("bt709.cube", Valid3dCube);
			const std::string p3 = directory.Write("p3.cube", Green3dCube);
			const std::string missing = directory.Path() + "\\missing.cube";
			LibplaceboExternalHdrLut::Declarations declarations;
			declarations.bt709 = { bt709, directory.Path() };
			declarations.p3D65 = { p3, directory.Path() };
			declarations.bt2020 = { missing, directory.Path() };

			auto candidate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, declarations, 1);
			const auto availability = candidate.Availability();
			Assert::IsTrue(availability.bt709);
			Assert::IsTrue(availability.p3D65);
			Assert::IsFalse(availability.bt2020);
			const auto& missingResource = candidate.Resource(
				LibplaceboExternalHdrLut::Slot::BT2020);
			Assert::IsTrue(missingResource.Configured());
			Assert::AreEqual(missing.c_str(),
				missingResource.ConfiguredPath().c_str());
			Assert::AreEqual(static_cast<int>(Status::REJECTED),
				static_cast<int>(missingResource.Result().status));
			Assert::AreEqual(static_cast<int>(Rejection::UNREADABLE),
				static_cast<int>(missingResource.Result().rejection));
			LibplaceboExternalHdrLut::ActiveSet active;
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::COMMIT_USABLE_GENERATION),
				static_cast<int>(active.Commit(std::move(candidate))));

			const auto resolved = active.Resolve(
				1,
				LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
				true, LibplaceboExternalHdrLut::Primaries::BT2020);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::Slot::P3_D65),
				static_cast<int>(resolved.selection.slot));
			Assert::IsTrue(resolved.selection.requiresExplicitPrimariesTransform);
			Assert::IsNotNull(resolved.lut);
			Assert::IsTrue(active.IsCurrent(resolved));
		}

		TEST_METHOD(ExternalHdrCandidatePreservesRejectedConfiguredPath)
		{
			LibplaceboExternalHdrLut::Declarations declarations;
			declarations.bt2020.path.clear();
			declarations.bt2020.configuredPath = "..\\outside\\hdr.cube";
			declarations.bt2020.pathRejected = true;
			auto candidate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, declarations, 1);
			const auto& resource = candidate.Resource(
				LibplaceboExternalHdrLut::Slot::BT2020);
			Assert::IsTrue(resource.Configured());
			Assert::AreEqual("..\\outside\\hdr.cube",
				resource.ConfiguredPath().c_str());
			Assert::AreEqual(static_cast<int>(Status::REJECTED),
				static_cast<int>(resource.Result().status));
			Assert::AreEqual(static_cast<int>(Rejection::PATH_OUTSIDE_BASE),
				static_cast<int>(resource.Result().rejection));
			Assert::IsFalse(resource.Available());
		}

		TEST_METHOD(ExternalHdrFrameProjectionCannotAttachWithoutCarrier)
		{
			TemporaryDirectory directory;
			const std::string cube = directory.Write("bt2020.cube", Valid3dCube);
			LibplaceboExternalHdrLut::Declarations declarations;
			declarations.bt2020 = { cube, directory.Path() };
			auto candidate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, declarations, 1);
			LibplaceboExternalHdrLut::ActiveSet active;
			active.Commit(std::move(candidate));

			struct pl_peak_detect_params peak{};
			struct pl_dither_params dither{};
			struct pl_render_params shared{};
			shared.peak_detect_params = &peak;
			shared.dither_params = &dither;
			struct pl_render_params frame{};
			struct pl_custom_lut frameLut{};
			struct pl_color_space source{};
			source.primaries = PL_COLOR_PRIM_BT_2020;
			source.transfer = PL_COLOR_TRC_PQ;
			struct pl_frame target{};
			target.color = source;
			target.repr.sys = PL_COLOR_SYSTEM_RGB;
			target.repr.levels = PL_COLOR_LEVELS_FULL;
			target.repr.bits.sample_depth = 10;
			target.repr.bits.color_depth = 10;
			struct pl_custom_lut legacyTargetLut{};
			target.lut = &legacyTargetLut;
			target.lut_type = PL_LUT_NATIVE;
			struct pl_frame frameTarget{};
			struct pl_color_map_params frameColorMap{};
			struct pl_gamut_map_function clip{};

			const auto projection =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					false, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, frameTarget, frame, frameLut,
					frameColorMap);
			Assert::IsFalse(projection.attached);
			Assert::IsNull(frame.lut);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::EffectiveMode::PIXEL_SHADERS),
				static_cast<int>(projection.resolved.selection.effectiveMode));
			Assert::IsTrue(frame.peak_detect_params == &peak);
			Assert::IsTrue(frame.dither_params == &dither);
			Assert::IsTrue(frameTarget.lut == &legacyTargetLut);
			Assert::AreEqual(static_cast<int>(PL_LUT_NATIVE),
				static_cast<int>(frameTarget.lut_type));
		}

		TEST_METHOD(ExternalHdrFrameProjectionUsesConversionLutAndExactMetadata)
		{
			TemporaryDirectory directory;
			const std::string cube = directory.Write("bt2020.cube", Valid3dCube);
			LibplaceboExternalHdrLut::Declarations declarations;
			declarations.bt2020 = { cube, directory.Path() };
			auto candidate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, declarations, 1);
			LibplaceboExternalHdrLut::ActiveSet active;
			active.Commit(std::move(candidate));

			struct pl_peak_detect_params peak{};
			struct pl_dither_params dither{};
			struct pl_render_params shared{};
			shared.peak_detect_params = &peak;
			shared.dither_params = &dither;
			struct pl_render_params frame{};
			struct pl_custom_lut frameLut{};
			struct pl_color_space source{};
			source.primaries = PL_COLOR_PRIM_BT_2020;
			source.transfer = PL_COLOR_TRC_PQ;
			struct pl_frame target{};
			target.color = source;
			target.repr.sys = PL_COLOR_SYSTEM_RGB;
			target.repr.levels = PL_COLOR_LEVELS_FULL;
			target.repr.bits.sample_depth = 10;
			target.repr.bits.color_depth = 10;
			struct pl_custom_lut legacyTargetLut{};
			target.lut = &legacyTargetLut;
			target.lut_type = PL_LUT_NATIVE;
			struct pl_frame frameTarget{};
			struct pl_color_map_params frameColorMap{};
			struct pl_gamut_map_function clip{};

			const auto projection =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, frameTarget, frame, frameLut,
					frameColorMap);
			Assert::IsTrue(projection.attached);
			Assert::IsTrue(frame.lut == &frameLut);
			Assert::AreEqual(static_cast<int>(PL_LUT_CONVERSION),
				static_cast<int>(frame.lut_type));
			Assert::IsNull(frame.peak_detect_params);
			Assert::IsTrue(frame.dither_params == &dither);
			Assert::IsNull(frameTarget.lut);
			Assert::AreEqual(static_cast<int>(PL_LUT_UNKNOWN),
				static_cast<int>(frameTarget.lut_type));
			Assert::AreEqual(static_cast<int>(PL_COLOR_PRIM_BT_2020),
				static_cast<int>(frameLut.color_in.primaries));
			Assert::AreEqual(static_cast<int>(PL_COLOR_TRC_PQ),
				static_cast<int>(frameLut.color_in.transfer));
			Assert::AreEqual(static_cast<int>(PL_COLOR_PRIM_BT_2020),
				static_cast<int>(frameLut.color_out.primaries));

			target.color.transfer = PL_COLOR_TRC_SRGB;
			struct pl_frame sdrFrameTarget{};
			struct pl_render_params sdrFrame{};
			struct pl_custom_lut sdrFrameLut{};
			struct pl_color_map_params sdrColorMap{};
			const auto sdrProjection =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, sdrFrameTarget, sdrFrame,
					sdrFrameLut, sdrColorMap);
			Assert::IsFalse(sdrProjection.attached);
			Assert::IsNull(sdrFrame.lut);
			Assert::IsTrue(sdrFrameTarget.lut == &legacyTargetLut);

			target.color.transfer = PL_COLOR_TRC_PQ;
			target.repr.sys = PL_COLOR_SYSTEM_BT_2020_NC;
			struct pl_frame yuvFrameTarget{};
			struct pl_render_params yuvFrame{};
			struct pl_custom_lut yuvFrameLut{};
			struct pl_color_map_params yuvColorMap{};
			const auto yuvProjection =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, yuvFrameTarget, yuvFrame,
					yuvFrameLut, yuvColorMap);
			Assert::IsFalse(yuvProjection.attached);
			Assert::IsNull(yuvFrame.lut);
			Assert::IsTrue(yuvFrameTarget.lut == &legacyTargetLut);
		}

		TEST_METHOD(ExternalHdrFrameProjectionTagsFallbackInputPrimaries)
		{
			TemporaryDirectory directory;
			const std::string cube = directory.Write("p3.cube", Green3dCube);
			LibplaceboExternalHdrLut::Declarations declarations;
			declarations.p3D65 = { cube, directory.Path() };
			auto candidate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, declarations, 1);
			LibplaceboExternalHdrLut::ActiveSet active;
			active.Commit(std::move(candidate));
			struct pl_render_params shared{};
			struct pl_render_params frame{};
			struct pl_custom_lut frameLut{};
			struct pl_color_space source{};
			source.primaries = PL_COLOR_PRIM_BT_2020;
			source.transfer = PL_COLOR_TRC_PQ;
			struct pl_frame target{};
			target.color = source;
			target.repr.sys = PL_COLOR_SYSTEM_RGB;
			target.repr.levels = PL_COLOR_LEVELS_FULL;
			target.repr.bits.sample_depth = 10;
			target.repr.bits.color_depth = 10;
			struct pl_frame frameTarget{};
			struct pl_color_map_params frameColorMap{};
			struct pl_gamut_map_function sharedMapper{};
			struct pl_gamut_map_function clip{};
			struct pl_color_map_params sharedColorMap{};
			sharedColorMap.gamut_mapping = &sharedMapper;
			shared.color_map_params = &sharedColorMap;

			const auto projection =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, frameTarget, frame, frameLut,
					frameColorMap);
			Assert::IsTrue(projection.attached);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::Slot::P3_D65),
				static_cast<int>(projection.resolved.selection.slot));
			Assert::IsTrue(
				projection.resolved.selection.requiresExplicitPrimariesTransform);
			Assert::AreEqual(static_cast<int>(PL_COLOR_PRIM_DISPLAY_P3),
				static_cast<int>(frameLut.color_in.primaries));
			Assert::AreEqual(static_cast<int>(PL_COLOR_TRC_PQ),
				static_cast<int>(frameLut.color_in.transfer));
			Assert::AreEqual(static_cast<int>(PL_COLOR_PRIM_BT_2020),
				static_cast<int>(frameLut.color_out.primaries));
			Assert::IsTrue(shared.color_map_params->gamut_mapping == &sharedMapper);
			Assert::IsTrue(frame.color_map_params == &frameColorMap);
			Assert::IsTrue(frameColorMap.gamut_mapping == &clip);

			struct pl_frame missingMapperTarget{};
			struct pl_render_params missingMapperFrame{};
			struct pl_custom_lut missingMapperLut{};
			struct pl_color_map_params missingMapperColorMap{};
			const auto missingMapper =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, nullptr, missingMapperTarget,
					missingMapperFrame, missingMapperLut, missingMapperColorMap);
			Assert::IsFalse(missingMapper.attached);
			Assert::IsNull(missingMapperFrame.lut);
			Assert::IsTrue(missingMapperFrame.color_map_params == &sharedColorMap);
		}

		TEST_METHOD(ExternalHdrFrameProjectionRejectsReplacedProfileGeneration)
		{
			TemporaryDirectory directory;
			const std::string bt2020 = directory.Write("bt2020.cube", Valid3dCube);
			const std::string p3 = directory.Write("p3.cube", Green3dCube);
			LibplaceboExternalHdrLut::Declarations firstDeclarations;
			firstDeclarations.bt2020 = { bt2020, directory.Path() };
			auto first = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, firstDeclarations, 1);
			LibplaceboExternalHdrLut::ActiveSet active;
			active.Commit(std::move(first));

			LibplaceboExternalHdrLut::Declarations secondDeclarations;
			secondDeclarations.p3D65 = { p3, directory.Path() };
			auto second = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, secondDeclarations, 2);
			active.Commit(std::move(second));

			struct pl_peak_detect_params peak{};
			struct pl_render_params shared{};
			shared.peak_detect_params = &peak;
			struct pl_render_params staleFrame{};
			struct pl_custom_lut staleLut{};
			struct pl_color_space source{};
			source.primaries = PL_COLOR_PRIM_BT_2020;
			source.transfer = PL_COLOR_TRC_PQ;
			struct pl_frame target{};
			target.color = source;
			target.repr.sys = PL_COLOR_SYSTEM_RGB;
			target.repr.levels = PL_COLOR_LEVELS_FULL;
			target.repr.bits.sample_depth = 10;
			target.repr.bits.color_depth = 10;
			struct pl_frame staleTarget{};
			struct pl_color_map_params staleColorMap{};
			struct pl_gamut_map_function clip{};
			const auto stale =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 1,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, staleTarget, staleFrame, staleLut,
					staleColorMap);
			Assert::IsFalse(stale.attached);
			Assert::IsNull(staleFrame.lut);
			Assert::IsTrue(staleFrame.peak_detect_params == &peak);

			struct pl_render_params currentFrame{};
			struct pl_custom_lut currentLut{};
			struct pl_frame currentTarget{};
			struct pl_color_map_params currentColorMap{};
			const auto current =
				LibplaceboExternalHdrLut::PrepareFrameProjection(
					active, 2,
					LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
					true, LibplaceboExternalHdrLut::Primaries::BT2020,
					source, target, shared, &clip, currentTarget, currentFrame,
					currentLut, currentColorMap);
			Assert::IsTrue(current.attached);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::Slot::P3_D65),
				static_cast<int>(current.resolved.selection.slot));
			Assert::AreEqual(static_cast<int>(PL_COLOR_PRIM_DISPLAY_P3),
				static_cast<int>(currentLut.color_in.primaries));
		}

		TEST_METHOD(ExternalHdrCommitPublishesFallbackAndRejectsStaleWork)
		{
			TemporaryDirectory directory;
			const std::string valid = directory.Write("valid.cube", Valid3dCube);
			const std::string invalid = directory.Write("invalid.cube",
				"LUT_3D_SIZE 2\n0 0 0\n");
			LibplaceboExternalHdrLut::Declarations activeDeclarations;
			activeDeclarations.bt2020 = { valid, directory.Path() };
			auto initialCandidate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, activeDeclarations, 1);
			Assert::IsFalse(initialCandidate.Resource(
				LibplaceboExternalHdrLut::Slot::BT709).Configured());
			Assert::AreEqual(static_cast<int>(Status::DISABLED),
				static_cast<int>(initialCandidate.Resource(
					LibplaceboExternalHdrLut::Slot::BT709).Result().status));
			LibplaceboExternalHdrLut::ActiveSet active;
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::COMMIT_USABLE_GENERATION),
				static_cast<int>(active.Commit(std::move(initialCandidate))));
			const auto prior = active.Resolve(
				1,
				LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
				true, LibplaceboExternalHdrLut::Primaries::BT2020);
			Assert::IsNotNull(prior.lut);

			const auto inFlight = active.Resolve(
				2,
				LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
				true, LibplaceboExternalHdrLut::Primaries::BT2020);
			Assert::IsNull(inFlight.lut);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::EffectiveMode::PIXEL_SHADERS),
				static_cast<int>(inFlight.selection.effectiveMode));
			Assert::AreEqual("external 3D LUT profile generation is not ready",
				inFlight.selection.reason);

			LibplaceboExternalHdrLut::Declarations badDeclarations;
			badDeclarations.bt2020 = { invalid, directory.Path() };
			auto rejected = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, badDeclarations, 2);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::COMMIT_INTERNAL_FALLBACK),
				static_cast<int>(active.Commit(std::move(rejected))));
			const auto fallback = active.Resolve(
				2,
				LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
				true, LibplaceboExternalHdrLut::Primaries::BT2020);
			Assert::IsNull(fallback.lut);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::EffectiveMode::PIXEL_SHADERS),
				static_cast<int>(fallback.selection.effectiveMode));
			Assert::IsFalse(active.IsCurrent(prior));

			auto stale = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, activeDeclarations, 1);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::REJECT_STALE_TRANSACTION),
				static_cast<int>(active.Commit(std::move(stale))));
			auto duplicate = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, activeDeclarations, 2);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::REJECT_STALE_TRANSACTION),
				static_cast<int>(active.Commit(std::move(duplicate))));
			Assert::AreEqual<uint64_t>(2, active.TransactionGeneration());
			Assert::IsTrue(active.IsCurrent(fallback));
		}

		TEST_METHOD(ExternalHdrPartialGenerationNeverMixesPriorProfileSlots)
		{
			TemporaryDirectory directory;
			const std::string bt2020 = directory.Write("bt2020.cube", Valid3dCube);
			const std::string p3 = directory.Write("p3.cube", Green3dCube);
			LibplaceboExternalHdrLut::Declarations firstDeclarations;
			firstDeclarations.bt2020 = { bt2020, directory.Path() };
			auto first = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, firstDeclarations, 1);
			LibplaceboExternalHdrLut::ActiveSet active;
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::COMMIT_USABLE_GENERATION),
				static_cast<int>(active.Commit(std::move(first))));
			const auto prior = active.Resolve(
				1,
				LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
				true, LibplaceboExternalHdrLut::Primaries::BT2020);
			Assert::AreEqual(static_cast<int>(LibplaceboExternalHdrLut::Slot::BT2020),
				static_cast<int>(prior.selection.slot));

			LibplaceboExternalHdrLut::Declarations secondDeclarations;
			secondDeclarations.p3D65 = { p3, directory.Path() };
			auto second = LibplaceboExternalHdrLut::CandidateSet::Load(
				nullptr, secondDeclarations, 2);
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::CommitDisposition::COMMIT_USABLE_GENERATION),
				static_cast<int>(active.Commit(std::move(second))));
			Assert::IsFalse(active.IsCurrent(prior));
			Assert::IsFalse(active.Resources().Resource(
				LibplaceboExternalHdrLut::Slot::BT2020).Available());
			const auto current = active.Resolve(
				2,
				LibplaceboExternalHdrLut::ToneMappingMode::EXTERNAL_3DLUT,
				true, LibplaceboExternalHdrLut::Primaries::BT2020);
			Assert::AreEqual(static_cast<int>(LibplaceboExternalHdrLut::Slot::P3_D65),
				static_cast<int>(current.selection.slot));
			Assert::IsTrue(current.selection.requiresExplicitPrimariesTransform);
			Assert::IsNotNull(current.lut);
		}

		TEST_METHOD(PinnedInternetNonlinearCubeAppliesItsPublishedLatticeValue)
		{
			TemporaryFile file;
			file.Write(CubeLutFactoryNonlinear3dCube);
			LoadResult lut = Load(nullptr, file.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE), static_cast<int>(lut.status));
			Assert::AreEqual(4, lut.lut->size[0]);

			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(),
				L"Could not create the libplacebo WARP test device");
			// Published sample at r=1/3, g=2/3, b=1 is
			// (7/18, 26/81, 7/9), or approximately (99, 82, 198) in 8-bit.
			const RgbaPixel transformed = fixture.Render(
				lut.lut, { 85, 170, 255, 255 });
			Free(lut);
			Assert::IsTrue(transformed.r >= 97 && transformed.r <= 101);
			Assert::IsTrue(transformed.g >= 80 && transformed.g <= 84);
			Assert::IsTrue(transformed.b >= 196 && transformed.b <= 200);
		}

		TEST_METHOD(SyntheticChannelSwapProvesCubeApplicationAndChannelOrdering)
		{
			TemporaryFile file;
			file.Write(SwapRedBlue3dCube);
			LoadResult lut = Load(nullptr, file.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE), static_cast<int>(lut.status));

			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(),
				L"Could not create the libplacebo WARP test device");
			const RgbaPixel transformed = fixture.Render(
				lut.lut, { 64, 128, 192, 255 });
			Free(lut);
			Assert::IsTrue(transformed.r >= 190 && transformed.r <= 194);
			Assert::IsTrue(transformed.g >= 126 && transformed.g <= 130);
			Assert::IsTrue(transformed.b >= 62 && transformed.b <= 66);
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

		TEST_METHOD(TargetLutGpuReadbackSupportsHighQualityWithoutErrorDiffusion)
		{
			TemporaryFile greenFile;
			greenFile.Write(Green3dCube);
			LoadResult greenLut = Load(nullptr, greenFile.Path());
			Assert::AreEqual(
				static_cast<int>(Status::ACTIVE),
				static_cast<int>(greenLut.status));

			pl_render_params compatibleParams = pl_render_high_quality_params;
			compatibleParams.error_diffusion = nullptr;
			TargetLutGpuFixture fixture;
			Assert::IsTrue(fixture.Create(), L"Could not create the libplacebo WARP test device");
			const RgbaPixel calibrated = fixture.Render(greenLut.lut, compatibleParams);
			Free(greenLut);

			Assert::IsTrue(calibrated.r < 15 && calibrated.g > 240 && calibrated.b < 15,
				L"The compatible high-quality target LUT path did not produce green output");
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
