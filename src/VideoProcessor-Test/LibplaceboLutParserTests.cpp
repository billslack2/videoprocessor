#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/LibplaceboDisplayLut.h>
#include <vprenderer/LibplaceboRenderParameters.h>
#include <vprenderer/LibplaceboCalibrationLutPolicy.h>
#include <vprenderer/LibplaceboOutputPolicy.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>

#include <cstdint>
#include <cmath>
#include <sstream>
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

	struct LuminanceCase
	{
		float targetNits = 203.0f;
		float blackNits = 0.203f;
		bool legacy = false;
		bool hdr = false;
		bool deband = false;
		pl_color_primaries primaries = PL_COLOR_PRIM_BT_709;
		int outputSize = 64;
	};

    pl_color_transfer TestTransfer(LibplaceboOutput::SdrTransfer transfer)
    {
        using LibplaceboOutput::SdrTransfer;
        switch (transfer)
        {
        case SdrTransfer::BT1886: return PL_COLOR_TRC_BT_1886;
        case SdrTransfer::SRGB: return PL_COLOR_TRC_SRGB;
        case SdrTransfer::GAMMA18: return PL_COLOR_TRC_GAMMA18;
        case SdrTransfer::GAMMA20: return PL_COLOR_TRC_GAMMA20;
        case SdrTransfer::GAMMA22: return PL_COLOR_TRC_GAMMA22;
        case SdrTransfer::GAMMA24: return PL_COLOR_TRC_GAMMA24;
        case SdrTransfer::GAMMA26: return PL_COLOR_TRC_GAMMA26;
        case SdrTransfer::GAMMA28: return PL_COLOR_TRC_GAMMA28;
        default: Assert::Fail(L"GPU case has no concrete SDR transfer");
        }
        return PL_COLOR_TRC_UNKNOWN;
    }

    int PixelDistance(RgbaPixel a, RgbaPixel b)
    {
        return std::abs(static_cast<int>(a.r) - b.r) +
            std::abs(static_cast<int>(a.g) - b.g) +
            std::abs(static_cast<int>(a.b) - b.b);
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
			float targetMaxNits = 0.0f,
            bool useSdrReference = false)
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
            if (useSdrReference)
            {
                LibplaceboRenderParameters::ApplySourceLuminance(true, image.color);
                LibplaceboRenderParameters::ApplyTargetLuminance(true, 100.0f, 0.0f, target.color);
            }
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

        std::vector<int> RenderGammaRamp(int bits, bool limited,
            pl_color_transfer sourceTransfer, pl_color_transfer targetTransfer,
            const pl_custom_lut* lut = nullptr)
        {
            pl_gpu gpu = m_d3d11->gpu;
            constexpr int count = 33;
            std::vector<float> input(count * 4);
            for (int i = 0; i < count; ++i)
            {
                input[4*i] = input[4*i+1] = input[4*i+2] = i / 32.0f;
                input[4*i+3] = 1.0f;
            }
            auto sourceFormat = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_SAMPLEABLE);
            Assert::IsNotNull(sourceFormat);
            pl_fmt targetFormat = nullptr;
            const auto caps = PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE;
            for (int i = 0; i < gpu->num_formats; ++i)
            {
                auto f = gpu->formats[i];
                if (f->type == PL_FMT_UNORM && f->num_components == 4 &&
                    f->component_depth[0] == bits && f->component_depth[1] == bits &&
                    f->component_depth[2] == bits && f->texel_size == 4 &&
                    (f->caps & caps) == caps && f->host_bits[0] == bits)
                { targetFormat = f; break; }
            }
            Assert::IsNotNull(targetFormat, L"Required RGBA8/RGB10A2 readback format unavailable");
            pl_tex_params sp{}; sp.w=count; sp.h=1; sp.format=sourceFormat;
            sp.sampleable=true; sp.initial_data=input.data();
            pl_tex source = pl_tex_create(gpu, &sp);
            pl_tex_params tp{}; tp.w=count; tp.h=1; tp.format=targetFormat;
            tp.renderable=true; tp.host_readable=true;
            pl_tex target = pl_tex_create(gpu, &tp);
            Assert::IsNotNull(source); Assert::IsNotNull(target);
            auto image = MakeRgbFrame(source); auto output = MakeRgbFrame(target);
            image.color.transfer=sourceTransfer; output.color.transfer=targetTransfer;
            LibplaceboRenderParameters::ApplySourceLuminance(true, image.color);
            LibplaceboRenderParameters::ApplyTargetLuminance(true, 100.0f, 0.0f, output.color);
            output.repr.levels=limited ? PL_COLOR_LEVELS_LIMITED : PL_COLOR_LEVELS_FULL;
            output.repr.bits.sample_depth=bits; output.repr.bits.color_depth=bits;
            output.lut=lut; output.lut_type=lut ? PL_LUT_NORMALIZED : PL_LUT_UNKNOWN;
            auto params=pl_render_fast_params;
            params.dither_params=nullptr; params.error_diffusion=nullptr;
            Assert::IsTrue(pl_render_image(m_renderer, &image, &output, &params));
            pl_gpu_finish(gpu);
            std::vector<uint32_t> downloaded(count);
            pl_tex_transfer_params download{}; download.tex=target; download.ptr=downloaded.data();
            Assert::IsTrue(pl_tex_download(gpu, &download));
            std::vector<int> codes;
            const unsigned mask=(1u << bits)-1;
            // Every RGB component has the same grayscale value, irrespective of
            // RGBA/BGRA ordering. Packed formats on D3D11 put RGB before alpha.
            for (auto pixel : downloaded) codes.push_back(static_cast<int>(pixel & mask));
            pl_tex_destroy(gpu,&source); pl_tex_destroy(gpu,&target);
            return codes;
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
			const pl_hook* hook, int width = 64, int height = 64,
			const LuminanceCase* luminance = nullptr)
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
			targetParams.w = luminance ? luminance->outputSize : width;
			targetParams.h = luminance ? luminance->outputSize : height;
			targetParams.format = format;
			targetParams.renderable = true;
			targetParams.host_readable = true;
			pl_tex targetTexture = pl_tex_create(gpu, &targetParams);
			Assert::IsNotNull(targetTexture);

			pl_frame image = MakeRgbFrame(sourceTexture);
			pl_frame target = MakeRgbFrame(targetTexture);
			pl_render_params params = pl_render_fast_params;
			if (luminance)
			{
				params = pl_render_high_quality_params;
				params.dither_params = nullptr; // deterministic readback
				params.deband_params = luminance->deband ? &pl_deband_default_params : nullptr;
				image.color.primaries = luminance->primaries;
				image.color.transfer = luminance->hdr ? PL_COLOR_TRC_PQ : PL_COLOR_TRC_SRGB;
				image.color.hdr.max_luma = luminance->hdr ? 1000.0f : luminance->targetNits;
				image.color.hdr.min_luma = luminance->hdr ? 0.005f : luminance->blackNits;
				target.color.hdr.max_luma = luminance->targetNits;
				target.color.hdr.min_luma = luminance->blackNits;
				if (!luminance->legacy)
				{
					LibplaceboRenderParameters::ApplySourceLuminance(!luminance->hdr, image.color);
					LibplaceboRenderParameters::ApplyTargetLuminance(!luminance->hdr,
						luminance->targetNits, luminance->blackNits, target.color);
				}
			}
			if (hook)
			{
				params.hooks = &hook;
				params.num_hooks = 1;
			}
			Assert::IsTrue(pl_render_image(
				m_renderer, &image, &target, &params));
			pl_gpu_finish(gpu);

			std::vector<RgbaPixel> result(targetParams.w * targetParams.h);
			pl_tex_transfer_params download{};
			download.tex = targetTexture;
			download.ptr = result.data();
			Assert::IsTrue(pl_tex_download(gpu, &download));
			pl_tex_destroy(gpu, &targetTexture);
			pl_tex_destroy(gpu, &sourceTexture);
			return result;
		}

		void CheckSdrSwapchain(float nits, bool force8bit)
		{
			HWND window = CreateWindowExW(0, L"STATIC", L"VP-0173 test", WS_POPUP,
				0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			Assert::IsNotNull(window);
			pl_d3d11_swapchain_params params{};
			params.window = window;
			params.disable_10bit_sdr = force8bit;
			pl_swapchain sw = pl_d3d11_create_swapchain(m_d3d11, &params);
			Assert::IsNotNull(sw);
			pl_color_space output{};
			output.primaries = PL_COLOR_PRIM_BT_709;
			output.transfer = PL_COLOR_TRC_SRGB;
			output.hdr.max_luma = nits;
			const auto hint = LibplaceboRenderParameters::MakeSwapchainColorHint(output);
			pl_swapchain_colorspace_hint(sw, &hint);
			int width = 64, height = 64;
			Assert::IsTrue(pl_swapchain_resize(sw, &width, &height));
			pl_swapchain_frame frame{};
			Assert::IsTrue(pl_swapchain_start_frame(sw, &frame));
			Assert::IsFalse(pl_color_space_is_hdr(&frame.color_space));
			Assert::AreEqual(static_cast<int>(PL_COLOR_TRC_SRGB),
				static_cast<int>(frame.color_space.transfer));
			IDXGISwapChain* dxgi = pl_d3d11_swapchain_unwrap(sw);
			DXGI_SWAP_CHAIN_DESC desc{};
			Assert::IsTrue(SUCCEEDED(dxgi->GetDesc(&desc)));
			if (force8bit)
				Assert::AreEqual(static_cast<int>(DXGI_FORMAT_R8G8B8A8_UNORM),
					static_cast<int>(desc.BufferDesc.Format));
			Logger::WriteMessage(("VP-0173 swapchain nits=" + std::to_string(nits) +
				" force8=" + std::to_string(force8bit) + " DXGI_FORMAT=" +
				std::to_string(desc.BufferDesc.Format) + " transfer=sRGB\n").c_str());
			dxgi->Release();
			Assert::IsTrue(pl_swapchain_submit_frame(sw));
			pl_swapchain_destroy(&sw);
			DestroyWindow(window);
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

        TEST_METHOD(HdrTargetGammaMigratesRenderingAliasWithoutDisplayDependency)
        {
            using namespace LibplaceboCalibrationLut;
            Assert::AreEqual(std::string("2.4"), ResolveHdrTargetGamma("2.4", "2.2"));
            Assert::AreEqual(std::string("2.4"), ResolveHdrTargetGamma("", "2.4"));
            for (const std::string legacy : { "", "display", "auto" })
                Assert::AreEqual(std::string("2.2"), ResolveHdrTargetGamma("", legacy));
            for (const std::string explicitGamma : { "bt1886", "srgb", "1.8", "2.0",
                "2.2", "2.4", "2.6", "2.8" })
                Assert::AreEqual(explicitGamma, ResolveHdrTargetGamma(explicitGamma, "display"));
        }

        TEST_METHOD(RejectedReloadRetainsLutOnlyWithSameInputContract)
        {
            using namespace LibplaceboCalibrationLut;
            Contract loaded{ "luts/screen.cube", "BT709", "2.4", "base" };
            Contract next = loaded;
            Assert::IsTrue(ResolveReloadFailure(loaded == next, true) == ReloadFailureAction::RETAIN_LAST_KNOWN_GOOD);
            next.inputTransfer = "2.2";
            Assert::IsTrue(ResolveReloadFailure(loaded == next, true) == ReloadFailureAction::DETACH);
            next = loaded; next.gamut = "P3-D65";
            Assert::IsTrue(ResolveReloadFailure(loaded == next, true) == ReloadFailureAction::DETACH);
            loaded.inputTransfer = HdrInputContractKey("2.2");
            next = loaded; next.inputTransfer = HdrInputContractKey("2.4");
            Assert::IsTrue(ResolveReloadFailure(loaded == next, true) == ReloadFailureAction::DETACH);
        }

        TEST_METHOD(NoLutGamma22FullLimitedRampsMatchReferenceAt8And10Bits)
        {
            TargetLutGpuFixture fixture;
            Assert::IsTrue(fixture.Create());
            for (int bits : {8,10}) for (bool limited : {false,true})
            {
                const auto pass = fixture.RenderGammaRamp(bits,limited,PL_COLOR_TRC_GAMMA22,PL_COLOR_TRC_GAMMA22);
                const auto converted = fixture.RenderGammaRamp(bits,limited,PL_COLOR_TRC_GAMMA24,PL_COLOR_TRC_GAMMA22);
                const int low=limited ? (16 << (bits-8)) : 0;
                const int high=limited ? (235 << (bits-8)) : ((1 << bits)-1);
                for (int i=0;i<33;++i)
                {
                    const double x=i/32.0;
                    const int expectedPass=static_cast<int>(std::lround(low+(high-low)*x));
                    const int expectedConverted=static_cast<int>(std::lround(low+(high-low)*std::pow(x,2.4/2.2)));
                    Assert::IsTrue(std::abs(pass[i]-expectedPass)<=1,L"Pass-through gamma/range mismatch");
                    Assert::IsTrue(std::abs(converted[i]-expectedConverted)<=2,L"Reference-preserving gamma/range mismatch");
                }
                Assert::IsTrue(converted[16]<pass[16],L"2.4 reference was silently reinterpreted as 2.2");
            }
        }

        TEST_METHOD(ConvertingTargetLutMatchesNoLutGammaCorrectionExactlyOnce)
        {
            std::ostringstream cube; cube << "LUT_3D_SIZE 33\n";
            for (int b=0;b<33;++b) for(int g=0;g<33;++g) for(int r=0;r<33;++r)
                cube << std::pow(r/32.0,2.4/2.2) << ' ' << std::pow(g/32.0,2.4/2.2) << ' '
                    << std::pow(b/32.0,2.4/2.2) << '\n';
            TemporaryFile file; file.Write(cube.str().c_str());
            auto loaded=Load(nullptr,file.Path());
            Assert::IsTrue(loaded.status==Status::ACTIVE);
            TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
            for(int bits : {8,10}) for(bool limited : {false,true})
            {
                using namespace LibplaceboOutput;
                const auto noLut = ResolveCalibrationTransfers(true, true, false,
                    SdrAdjustGamma::ON, GammaRequest::GAMMA22, GammaRequest::GAMMA22,
                    SdrTransfer::GAMMA24, SdrTransfer::GAMMA22);
                const auto withLut = ResolveCalibrationTransfers(true, true, true,
                    SdrAdjustGamma::ON, GammaRequest::GAMMA22, GammaRequest::GAMMA22,
                    SdrTransfer::GAMMA24, SdrTransfer::GAMMA22);
                const auto direct=fixture.RenderGammaRamp(bits,limited,
                    TestTransfer(noLut.sdr.effectiveSource),TestTransfer(noLut.targetTransfer));
                const auto viaLut=fixture.RenderGammaRamp(bits,limited,
                    TestTransfer(withLut.sdr.effectiveSource),TestTransfer(withLut.targetTransfer),loaded.lut);
                const auto duplicate=fixture.RenderGammaRamp(bits,limited,PL_COLOR_TRC_GAMMA24,PL_COLOR_TRC_GAMMA22,loaded.lut);
                for(int i=0;i<33;++i)
                    Assert::IsTrue(std::abs(direct[i]-viaLut[i])<=2,L"LUT-domain conversion does not match direct correction");
                Assert::IsTrue(duplicate[16]<viaLut[16]-3,L"Probe cannot detect duplicate gamma correction");
            }
            Free(loaded);
        }

        TEST_METHOD(CalibrationWorkflowSdrLutPreservesRampsAcrossControlsAndTransports)
        {
            using namespace LibplaceboOutput;
            TemporaryFile file; file.Write(Valid3dCube);
            auto identity = Load(nullptr, file.Path());
            Assert::IsTrue(identity.status == Status::ACTIVE);
            TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
            for (int bits : { 8, 10 }) for (bool limited : { false, true })
            {
                const int low = limited ? (16 << (bits - 8)) : 0;
                const int high = limited ? (235 << (bits - 8)) : ((1 << bits) - 1);
                for (auto source : { SdrTransfer::BT1886, SdrTransfer::GAMMA22, SdrTransfer::GAMMA24 })
                for (auto requested : { SdrAdjustGamma::ON, SdrAdjustGamma::OFF,
                    SdrAdjustGamma::AUTO, SdrAdjustGamma::PRESERVE_CODES })
                for (auto display : { GammaRequest::GAMMA22, GammaRequest::GAMMA24 })
                {
                    const auto hdr = display == GammaRequest::GAMMA22 ?
                        GammaRequest::GAMMA24 : GammaRequest::GAMMA22;
                    const auto decision = ResolveCalibrationTransfers(true, true, true,
                        requested, display, hdr, source,
                        limited ? SdrTransfer::GAMMA22 : SdrTransfer::SRGB);
                    const auto codes = fixture.RenderGammaRamp(bits, limited,
                        TestTransfer(decision.sdr.effectiveSource),
                        TestTransfer(decision.targetTransfer), identity.lut);
                    for (int i = 0; i < 33; ++i)
                    {
                        const int expected = static_cast<int>(std::lround(low + (high - low) * i / 32.0));
                        Assert::IsTrue(std::abs(codes[i] - expected) <= 1,
                            L"An inactive display/conversion/HDR control changed SDR LUT input codes");
                    }
                }
            }
            Free(identity);
        }

        TEST_METHOD(CalibrationWorkflowSdrGamutMappingKeepsDeclaredSourceTransfer)
        {
            using namespace LibplaceboOutput;
            TemporaryFile file; file.Write(Valid3dCube);
            auto identity = Load(nullptr, file.Path());
            Assert::IsTrue(identity.status == Status::ACTIVE);
            TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
            auto params = pl_render_high_quality_params;
            params.dither_params = nullptr; params.error_diffusion = nullptr;
            params.deband_params = nullptr; params.peak_detect_params = nullptr;
            const RgbaPixel patches[] = { { 164, 94, 52, 255 }, { 77, 151, 109, 255 },
                { 114, 73, 171, 255 } };
            int maximumDifference = 0;
            for (const auto patch : patches)
            {
                RgbaPixel previous{};
                for (auto declared : { SdrTransfer::GAMMA22, SdrTransfer::GAMMA24 })
                {
                    const auto decision = ResolveCalibrationTransfers(true, true, true,
                        SdrAdjustGamma::ON, GammaRequest::GAMMA28, GammaRequest::GAMMA18,
                        declared, SdrTransfer::SRGB);
                    const auto render = [&](pl_color_primaries sourcePrimaries,
                        pl_color_transfer sourceTransfer, pl_color_transfer targetTransfer)
                    {
                        return fixture.Render(identity.lut, params, PL_COLOR_LEVELS_FULL,
                            PL_LUT_NORMALIZED, patch, sourceTransfer, targetTransfer,
                            sourcePrimaries, PL_COLOR_PRIM_BT_709, 0.0f, 0.0f, true);
                    };
                    const auto matched = render(PL_COLOR_PRIM_BT_709,
                        TestTransfer(decision.sdr.effectiveSource), TestTransfer(decision.targetTransfer));
                    Assert::IsTrue(PixelDistance(matched, patch) <= 3,
                        L"Matched-gamut LUT input must retain normalized SDR tone codes");
                    const auto mapped = render(PL_COLOR_PRIM_BT_2020,
                        TestTransfer(decision.sdr.effectiveSource), TestTransfer(decision.targetTransfer));
                    // Independent reference declares the expected transfer on both sides
                    // of genuine linear-light BT.2020-to-BT.709 gamut mapping.
                    const auto reference = render(PL_COLOR_PRIM_BT_2020,
                        TestTransfer(declared), TestTransfer(declared));
                    Assert::IsTrue(PixelDistance(mapped, reference) <= 3,
                        L"SDR gamut mapping decoded or encoded with a display/HDR gamma instead of the source declaration");
                    if (declared == SdrTransfer::GAMMA24)
                        maximumDifference = std::max(maximumDifference, PixelDistance(previous, mapped));
                    previous = mapped;
                }
            }
            Assert::IsTrue(maximumDifference >= 3,
                L"Wide-gamut probe did not detect the declared source gamma in linear-light processing");
            Free(identity);
        }

        TEST_METHOD(CalibrationWorkflowFiniteBlackBt1886ChangesWideGamutSdrOnly)
        {
            using namespace LibplaceboOutput;
            pl_color_space sourceReference{}, targetReference{};
            LibplaceboRenderParameters::ApplySourceLuminance(true, sourceReference);
            LibplaceboRenderParameters::ApplyTargetLuminance(true, 100.0f, 0.0f, targetReference);
            Assert::IsTrue(sourceReference.hdr.min_luma > 0.0f,
                L"BT.1886 versus pure 2.4 needs a finite-black reference to be a meaningful probe");
            Assert::AreEqual(sourceReference.hdr.min_luma, targetReference.hdr.min_luma);
            Assert::AreEqual(sourceReference.hdr.max_luma, targetReference.hdr.max_luma);
            TemporaryFile file; file.Write(Valid3dCube);
            auto identity = Load(nullptr, file.Path());
            Assert::IsTrue(identity.status == Status::ACTIVE);
            TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
            auto params = pl_render_high_quality_params;
            params.dither_params = nullptr; params.error_diffusion = nullptr;
            params.deband_params = nullptr; params.peak_detect_params = nullptr;
            const RgbaPixel patches[] = { { 48, 31, 14, 255 }, { 108, 64, 33, 255 },
                { 36, 82, 53, 255 }, { 164, 94, 52, 255 } };
            int maximumDifference = 0;
            for (const auto patch : patches)
            {
                RgbaPixel bt1886{};
                for (auto declared : { SdrTransfer::BT1886, SdrTransfer::GAMMA24 })
                {
                    const auto decision = ResolveCalibrationTransfers(true, true, true,
                        SdrAdjustGamma::ON, GammaRequest::GAMMA22, GammaRequest::GAMMA22,
                        declared, SdrTransfer::SRGB);
                    const auto render = [&](pl_color_primaries primaries,
                        pl_color_transfer source, pl_color_transfer target)
                    {
                        // The same finite-black source/target metadata as the real
                        // renderer is applied by the final true argument.
                        return fixture.Render(identity.lut, params, PL_COLOR_LEVELS_FULL,
                            PL_LUT_NORMALIZED, patch, source, target, primaries,
                            PL_COLOR_PRIM_BT_709, 0.0f, 0.0f, true);
                    };
                    const auto matched = render(PL_COLOR_PRIM_BT_709,
                        TestTransfer(decision.sdr.effectiveSource), TestTransfer(decision.targetTransfer));
                    Assert::IsTrue(PixelDistance(matched, patch) <= 3,
                        L"Finite-black matched-gamut SDR changed tone codes before the identity LUT");
                    const auto mapped = render(PL_COLOR_PRIM_BT_2020,
                        TestTransfer(decision.sdr.effectiveSource), TestTransfer(decision.targetTransfer));
                    const auto reference = render(PL_COLOR_PRIM_BT_2020,
                        TestTransfer(declared), TestTransfer(declared));
                    Assert::IsTrue(PixelDistance(mapped, reference) <= 3,
                        L"The policy substituted another transfer during BT.1886 gamut processing");
                    if (declared == SdrTransfer::BT1886) bt1886 = mapped;
                    else maximumDifference = std::max(maximumDifference, PixelDistance(bt1886, mapped));
                }
            }
            Logger::WriteMessage(("BT.1886 vs 2.4 finite-black SDR gamut maximum RGB code distance=" +
                std::to_string(maximumDifference) + "\n").c_str());
            Assert::IsTrue(maximumDifference >= 3,
                L"Finite-black BT.1886 and pure 2.4 did not produce distinct wide-gamut SDR processing");
            Free(identity);
        }

        TEST_METHOD(CalibrationWorkflowColoredHdrMapsSelectedGamutBeforeLut)
        {
            using namespace LibplaceboOutput;
            TemporaryFile identityFile; identityFile.Write(Valid3dCube);
            auto identity = Load(nullptr, identityFile.Path());
            TemporaryFile probeFile; probeFile.Write(GammaCoordinateProbeCube);
            auto probe = Load(nullptr, probeFile.Path());
            Assert::IsTrue(identity.status == Status::ACTIVE && probe.status == Status::ACTIVE);
            TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
            auto params = pl_render_high_quality_params;
            params.dither_params = nullptr; params.error_diffusion = nullptr;
            params.deband_params = nullptr; params.peak_detect_params = nullptr;
            const RgbaPixel patches[] = { { 140, 90, 60, 255 }, { 75, 130, 95, 255 },
                { 95, 70, 140, 255 } };
            for (auto hdrSource : { PL_COLOR_TRC_PQ, PL_COLOR_TRC_HLG })
            {
                int maximumGamutDifference = 0;
                for (const auto patch : patches)
                {
                    RgbaPixel rec709{};
                    for (auto targetPrimaries : { PL_COLOR_PRIM_BT_709, PL_COLOR_PRIM_DISPLAY_P3 })
                    {
                        const auto selection = LibplaceboCalibrationLut::Select(true,
                            targetPrimaries == PL_COLOR_PRIM_BT_709 ?
                                LibplaceboCalibrationLut::Primaries::BT709 :
                                LibplaceboCalibrationLut::Primaries::P3_D65, true, true, true);
                        Assert::IsTrue(selection.enabled);
                        Assert::IsTrue(selection.slot == (targetPrimaries == PL_COLOR_PRIM_BT_709 ?
                            LibplaceboCalibrationLut::Slot::BT709 : LibplaceboCalibrationLut::Slot::P3_D65));
                        const auto decision = ResolveCalibrationTransfers(false, true, true,
                            SdrAdjustGamma::ON, GammaRequest::GAMMA28, GammaRequest::GAMMA22,
                            SdrTransfer::OTHER, SdrTransfer::SRGB);
                        const auto render = [&](const pl_custom_lut* lut, pl_color_transfer targetTransfer)
                        {
                            return fixture.Render(lut, params, PL_COLOR_LEVELS_FULL, PL_LUT_NORMALIZED,
                                patch, hdrSource, targetTransfer, PL_COLOR_PRIM_BT_2020,
                                targetPrimaries, 1000.0f, 100.0f);
                        };
                        const auto mapped = render(identity.lut, TestTransfer(decision.targetTransfer));
                        // Explicit target reference with no LUT: the identity must
                        // preserve actual HDR tone/gamut mapping for each gamut.
                        const auto reference = render(nullptr, PL_COLOR_TRC_GAMMA22);
                        Assert::IsTrue(PixelDistance(mapped, reference) <= 3,
                            L"Identity LUT disturbed the selected HDR gamut-mapping target");
                        const auto calibrated = render(probe.lut, TestTransfer(decision.targetTransfer));
                        Assert::IsTrue(std::abs(static_cast<int>(calibrated.g) - mapped.r) <= 2 &&
                            std::abs(static_cast<int>(calibrated.r) - (255 - mapped.r)) <= 2 && calibrated.b <= 2,
                            L"Calibration LUT did not receive the gamut-mapped HDR code coordinates");
                        if (targetPrimaries == PL_COLOR_PRIM_BT_709) rec709 = mapped;
                        else maximumGamutDifference = std::max(maximumGamutDifference,
                            PixelDistance(rec709, mapped));
                    }
                }
                Logger::WriteMessage(("Colored HDR target-gamut maximum RGB code distance=" +
                    std::to_string(maximumGamutDifference) + "\n").c_str());
                Assert::IsTrue(maximumGamutDifference >= 3,
                    L"BT.2020 HDR produced the same coded colors for P3-D65 and Rec.709 targets");
            }
            Free(probe); Free(identity);
        }

        TEST_METHOD(CalibrationWorkflowHdrLutUsesHdrGammaAndDetachmentRestoresDisplay)
        {
            using namespace LibplaceboOutput;
            TemporaryFile file; file.Write(Valid3dCube);
            auto identity = Load(nullptr, file.Path());
            Assert::IsTrue(identity.status == Status::ACTIVE);
            TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
            auto params = pl_render_high_quality_params;
            params.dither_params = nullptr; params.error_diffusion = nullptr;
            params.deband_params = nullptr; params.peak_detect_params = nullptr;
            for (auto hdrSourceTransfer : { PL_COLOR_TRC_PQ, PL_COLOR_TRC_HLG })
            {
                const auto render = [&](bool lutActive, GammaRequest display, GammaRequest hdr)
                {
                    const auto decision = ResolveCalibrationTransfers(false, true, lutActive,
                        SdrAdjustGamma::ON, display, hdr, SdrTransfer::OTHER, SdrTransfer::SRGB);
                    return fixture.Render(lutActive ? identity.lut : nullptr, params,
                        PL_COLOR_LEVELS_FULL, PL_LUT_NORMALIZED, { 96, 96, 96, 255 },
                        hdrSourceTransfer, TestTransfer(decision.targetTransfer),
                        PL_COLOR_PRIM_BT_2020, PL_COLOR_PRIM_BT_709, 1000.0f, 100.0f);
                };
                const auto gamma22 = render(true, GammaRequest::GAMMA22, GammaRequest::GAMMA22);
                const auto changedDisplay = render(true, GammaRequest::GAMMA28, GammaRequest::GAMMA22);
                const auto gamma24 = render(true, GammaRequest::GAMMA22, GammaRequest::GAMMA24);
                Assert::IsTrue(PixelDistance(gamma22, changedDisplay) == 0,
                    L"Physical display gamma changed HDR output while the LUT owned calibration");
                Assert::IsTrue(static_cast<int>(gamma24.r) - gamma22.r >= 3,
                    L"HDR target gamma did not change the tone-mapped encoding");
                Assert::IsTrue(std::abs(std::pow(gamma22.r / 255.0, 2.2) -
                    std::pow(gamma24.r / 255.0, 2.4)) < 0.01,
                    L"HDR target gamma unexpectedly changed the tone-mapped linear result");
                const auto detached = render(false, GammaRequest::GAMMA24, GammaRequest::GAMMA22);
                const auto detachedChangedHdr = render(false, GammaRequest::GAMMA24, GammaRequest::GAMMA28);
                Assert::IsTrue(PixelDistance(detached, gamma24) <= 3,
                    L"LUT detachment did not restore physical display encoding");
                Assert::IsTrue(PixelDistance(detached, detachedChangedHdr) == 0,
                    L"Inactive HDR LUT gamma changed the no-LUT output");
            }
            Free(identity);
        }

		TEST_METHOD(SdrLuminanceGpuReadbackIsInvariantForUnityAndScaling)
		{
			for (int size : { 32, 64, 96 })
			{
				std::vector<RgbaPixel> reference;
				for (float nits : { 75.0f, 203.0f, 400.0f, 500.0f })
				{
					TargetLutGpuFixture fixture;
					Assert::IsTrue(fixture.Create());
					LuminanceCase test;
					test.targetNits = nits; test.blackNits = nits / 1000.0f;
					test.outputSize = size;
					const auto pixels = fixture.RenderCoordinateField(nullptr, 64, 64, &test);
					if (reference.empty()) reference = pixels;
					Assert::AreEqual(0, std::memcmp(reference.data(), pixels.data(), pixels.size() * sizeof(RgbaPixel)));
					if (size == 64)
						for (int y = 0; y < 64; ++y)
							for (int x = 0; x < 64; ++x)
							{
								const RgbaPixel expected = { static_cast<uint8_t>(x * 255 / 63),
									static_cast<uint8_t>(y * 255 / 63),
									static_cast<uint8_t>((x + y) * 255 / 126), 255 };
								Assert::AreEqual(0, std::memcmp(&expected, &pixels[y * 64 + x], sizeof(expected)),
									L"Unity SDR did not preserve input code values");
							}
					pl_hdr_metadata metadata{};
					Assert::IsFalse(fixture.DetectedHdrMetadata(metadata), L"SDR triggered HDR peak detection");
				}
			}
		}

		TEST_METHOD(SdrLuminanceGpuRetainsOptionalDebandingWithoutToneMapping)
		{
			std::vector<RgbaPixel> reference;
			for (float nits : { 75.0f, 203.0f, 400.0f, 500.0f })
			{
				LuminanceCase test; test.targetNits = nits;
				test.blackNits = nits / 1000.0f; test.deband = true;
				TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
				const auto pixels = fixture.RenderCoordinateField(nullptr, 64, 64, &test);
				if (reference.empty()) reference = pixels;
				Assert::AreEqual(0, std::memcmp(reference.data(), pixels.data(), pixels.size() * sizeof(RgbaPixel)),
					L"HDR destination changed SDR debanding output");
				pl_hdr_metadata metadata{};
				Assert::IsFalse(fixture.DetectedHdrMetadata(metadata));
			}
			LuminanceCase plain;
			TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
			const auto unprocessed = fixture.RenderCoordinateField(nullptr, 64, 64, &plain);
			Assert::IsTrue(std::memcmp(reference.data(), unprocessed.data(), reference.size() * sizeof(RgbaPixel)) != 0,
				L"SDR must still apply explicitly enabled debanding");
		}

		TEST_METHOD(SdrLuminanceGpuGamutMismatchMeasuresLegacyDifference)
		{
			for (auto primaries : { PL_COLOR_PRIM_BT_2020, PL_COLOR_PRIM_DISPLAY_P3 })
			{
				std::vector<RgbaPixel> reference;
				for (float nits : { 75.0f, 203.0f, 400.0f })
				{
					LuminanceCase test;
					test.primaries = primaries; test.targetNits = nits;
					test.blackNits = nits / 1000.0f;
					TargetLutGpuFixture before, after;
					Assert::IsTrue(before.Create()); Assert::IsTrue(after.Create());
					test.legacy = true;
					const auto oldPixels = before.RenderCoordinateField(nullptr, 64, 64, &test);
					test.legacy = false;
					const auto pixels = after.RenderCoordinateField(nullptr, 64, 64, &test);
					if (reference.empty()) reference = pixels;
					Assert::AreEqual(0, std::memcmp(reference.data(), pixels.data(), pixels.size() * sizeof(RgbaPixel)));
					size_t changed = 0; int maximumDelta = 0;
					const auto* a = reinterpret_cast<const uint8_t*>(oldPixels.data());
					const auto* b = reinterpret_cast<const uint8_t*>(pixels.data());
					for (size_t i = 0; i < pixels.size() * sizeof(RgbaPixel); ++i)
					{
						if (a[i] != b[i]) ++changed;
						maximumDelta = std::max(maximumDelta, std::abs(int(a[i]) - int(b[i])));
					}
					Logger::WriteMessage(("VP-0173 gamut=" + std::to_string(primaries) +
						" nits=" + std::to_string(nits) + " changed_bytes=" + std::to_string(changed) +
						" max_code_delta=" + std::to_string(maximumDelta) + "\n").c_str());
					if (nits == 203.0f) Assert::AreEqual(size_t(0), changed);
				}
			}
		}

		TEST_METHOD(HdrLuminanceGpuReadbackPreservesToneMappingAbove203Nits)
		{
			for (float nits : { 400.0f, 500.0f })
			{
				LuminanceCase test; test.hdr = true; test.targetNits = nits;
				test.blackNits = nits / 1000.0f; test.primaries = PL_COLOR_PRIM_BT_2020;
				TargetLutGpuFixture before, after;
				Assert::IsTrue(before.Create()); Assert::IsTrue(after.Create());
				test.legacy = true;
				const auto original = before.RenderCoordinateField(nullptr, 64, 64, &test);
				test.legacy = false;
				const auto pixels = after.RenderCoordinateField(nullptr, 64, 64, &test);
				Assert::AreEqual(0, std::memcmp(original.data(), pixels.data(), pixels.size() * sizeof(RgbaPixel)));
			}
		}

		TEST_METHOD(SdrLuminanceD3D11HintKeepsSdrAndForced8BitTransport)
		{
			TargetLutGpuFixture fixture; Assert::IsTrue(fixture.Create());
			for (bool force8bit : { false, true })
				for (float nits : { 40.0f, 75.0f, 203.0f, 203.01f, 400.0f, 500.0f })
					fixture.CheckSdrSwapchain(nits, force8bit);
		}

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
