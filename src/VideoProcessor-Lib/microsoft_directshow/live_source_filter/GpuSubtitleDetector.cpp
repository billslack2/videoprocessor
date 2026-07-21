#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "GpuSubtitleDetector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN64
#include <Windows.h>
#include <onnxruntime_c_api.h>
#endif

namespace
{
	int RoundToMultipleOf32(int value)
	{
		return std::max(32, ((value + 16) / 32) * 32);
	}

	int RectWidth(const GpuSubtitleRegion& rectangle)
	{
		return std::max(0, rectangle.right - rectangle.left);
	}

	int RectHeight(const GpuSubtitleRegion& rectangle)
	{
		return std::max(0, rectangle.bottom - rectangle.top);
	}

	GpuSubtitleRegion UnionRect(
		const GpuSubtitleRegion& first,
		const GpuSubtitleRegion& second)
	{
		return {
			std::min(first.left, second.left),
			std::min(first.top, second.top),
			std::max(first.right, second.right),
			std::max(first.bottom, second.bottom)
		};
	}
}

class GpuSubtitleDetector::Impl
{
public:
	Impl()
	{
#ifdef _WIN64
		Initialize();
#else
		m_lastError = "DirectML subtitle detection requires the x64 build";
#endif
	}

	~Impl()
	{
#ifdef _WIN64
		if (m_api)
		{
			if (m_session)
				m_api->ReleaseSession(m_session);
			if (m_runOptions)
				m_api->ReleaseRunOptions(m_runOptions);
			if (m_memoryInfo)
				m_api->ReleaseMemoryInfo(m_memoryInfo);
			if (m_sessionOptions)
				m_api->ReleaseSessionOptions(m_sessionOptions);
			if (m_environment)
				m_api->ReleaseEnv(m_environment);
		}
		if (m_runtime)
			FreeLibrary(m_runtime);
#endif
	}

	GpuSubtitleDetectionResult Detect(
		const uint16_t* luma,
		int width,
		int height,
		int pictureTop,
		int pictureBottom)
	{
		GpuSubtitleDetectionResult result;
#ifdef _WIN64
		result.available = m_available;
		if (!m_available || !luma || width < 80 || height < 60 ||
			pictureTop < 0 || pictureBottom <= pictureTop ||
			pictureBottom > height)
			return result;

		try
		{
			// Subtitle strokes remain legible at this resolution while keeping a
			// complete DirectML pass comfortably below a 16ms 60fps budget on a
			// mid-range discrete GPU. The worker never runs on the delivery path.
			// Subtitle panels occupy a small central portion of the frame. 384px
			// preserves their line geometry after the existing 960px analysis
			// downscale, while substantially reducing DirectML dispatch and CPU
			// pre/post-processing so ADVANCED mode can keep up with 60fps.
			constexpr int modelWidth = 352;
			const int modelHeight = std::min(352, std::max(128,
				RoundToMultipleOf32(
					static_cast<int>(std::lround(
						static_cast<double>(modelWidth) * height / width)))));
			const size_t planeWords =
				static_cast<size_t>(modelWidth) * modelHeight;
			m_input.resize(planeWords * 3);

			// PP-OCRv3 expects RGB normalized with ImageNet channel values.
			// P010 supplies luma only, so replicate it into RGB. Text-region
			// geometry is driven primarily by contrast and remains independent
			// of language, glyph identity, or subtitle color.
			constexpr float means[3] = { 123.675f, 116.28f, 103.53f };
			constexpr float deviations[3] = {
				255.0f * 0.229f,
				255.0f * 0.224f,
				255.0f * 0.225f
			};
			for (int modelY = 0; modelY < modelHeight; ++modelY)
			{
				const int sourceY = std::min(height - 1,
					(modelY * height + modelHeight / 2) / modelHeight);
				for (int modelX = 0; modelX < modelWidth; ++modelX)
				{
					const int sourceX = std::min(width - 1,
						(modelX * width + modelWidth / 2) / modelWidth);
					const int code = luma[
						static_cast<size_t>(sourceY) * width + sourceX];
					const float gray = static_cast<float>(std::max(0,
						std::min(255, (code - 64) * 255 / (940 - 64))));
					const size_t pixel =
						static_cast<size_t>(modelY) * modelWidth + modelX;
					for (size_t channel = 0; channel < 3; ++channel)
						m_input[channel * planeWords + pixel] =
							(gray - means[channel]) / deviations[channel];
				}
			}

			const int64_t inputShape[] =
				{ 1, 3, modelHeight, modelWidth };
			Check(m_api->RunOptionsUnsetTerminate(m_runOptions));
			OrtValue* inputValue = nullptr;
			Check(m_api->CreateTensorWithDataAsOrtValue(
				m_memoryInfo,
				m_input.data(),
				m_input.size() * sizeof(float),
				inputShape,
				4,
				ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
				&inputValue));

			OrtValue* outputValue = nullptr;
			const char* inputNames[] = { "x" };
			const char* outputNames[] = { "4" };
			const OrtValue* inputValues[] = { inputValue };
			OrtStatus* runStatus = m_api->Run(
				m_session,
				m_runOptions,
				inputNames,
				inputValues,
				1,
				outputNames,
				1,
				&outputValue);
			m_api->ReleaseValue(inputValue);
			Check(runStatus);

			OrtTensorTypeAndShapeInfo* outputInfo = nullptr;
			Check(m_api->GetTensorTypeAndShape(outputValue, &outputInfo));
			size_t dimensionCount = 0;
			Check(m_api->GetDimensionsCount(outputInfo, &dimensionCount));
			std::vector<int64_t> dimensions(dimensionCount);
			Check(m_api->GetDimensions(
				outputInfo, dimensions.data(), dimensions.size()));
			size_t elementCount = 0;
			Check(m_api->GetTensorShapeElementCount(
				outputInfo, &elementCount));
			m_api->ReleaseTensorTypeAndShapeInfo(outputInfo);
			if (dimensionCount != 2 ||
				dimensions[0] != modelHeight ||
				dimensions[1] != modelWidth ||
				elementCount != planeWords)
			{
				m_api->ReleaseValue(outputValue);
				throw std::runtime_error(
					"unexpected PP-OCRv3 output tensor shape");
			}

			float* probabilities = nullptr;
			Check(m_api->GetTensorMutableData(
				outputValue,
				reinterpret_cast<void**>(&probabilities)));
			result = BuildResult(
				probabilities,
				modelWidth,
				modelHeight,
				width,
				height,
				pictureTop,
				pictureBottom);
			m_api->ReleaseValue(outputValue);
			result.available = true;
			return result;
		}
		catch (const std::exception& error)
		{
			m_lastError = error.what();
			m_available = false;
			result.available = false;
			return result;
		}
#else
		(void)luma;
		(void)width;
		(void)height;
		(void)pictureTop;
		(void)pictureBottom;
		return result;
#endif
	}

	const std::string& LastError() const
	{
		return m_lastError;
	}

	void Cancel() noexcept
	{
		if (!m_api || !m_runOptions)
			return;
		OrtStatus* status = m_api->RunOptionsSetTerminate(m_runOptions);
		if (status)
			m_api->ReleaseStatus(status);
	}

	int SelectedDeviceId() const
	{
		return m_selectedDeviceId;
	}

private:
#ifdef _WIN64
	using OrtGetApiBaseFunction = const OrtApiBase* (ORT_API_CALL*)();
	using AppendDirectMlFunction =
		OrtStatus* (ORT_API_CALL*)(OrtSessionOptions*, int);

	void Check(OrtStatus* status)
	{
		if (!status)
			return;
		const char* message = m_api ?
			m_api->GetErrorMessage(status) : "ONNX Runtime error";
		const std::string text = message ? message : "ONNX Runtime error";
		if (m_api)
			m_api->ReleaseStatus(status);
		throw std::runtime_error(text);
	}

	static std::wstring ModuleDirectory()
	{
		std::wstring path(32768, L'\0');
		const DWORD length = GetModuleFileNameW(
			nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size())
			throw std::runtime_error("cannot locate executable directory");
		path.resize(length);
		const size_t separator = path.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
			throw std::runtime_error("cannot locate executable directory");
		path.resize(separator);
		return path;
	}

	void Initialize()
	{
		try
		{
			const std::wstring directory = ModuleDirectory();
			const std::wstring runtimePath =
				directory + L"\\onnxruntime.dll";
			const std::wstring modelPath =
				directory + L"\\subtitle_text_detection.onnx";
			m_runtime = LoadLibraryExW(
				runtimePath.c_str(),
				nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
					LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (!m_runtime)
				throw std::runtime_error(
					"onnxruntime.dll is not present beside VideoProcessor.exe");

			const auto getApiBase = reinterpret_cast<OrtGetApiBaseFunction>(
				GetProcAddress(m_runtime, "OrtGetApiBase"));
			const auto appendDirectMl =
				reinterpret_cast<AppendDirectMlFunction>(
					GetProcAddress(
						m_runtime,
						"OrtSessionOptionsAppendExecutionProvider_DML"));
			if (!getApiBase || !appendDirectMl)
				throw std::runtime_error(
					"ONNX Runtime DirectML exports are unavailable");
			const OrtApiBase* base = getApiBase();
			m_api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
			if (!m_api)
				throw std::runtime_error("ONNX Runtime API is unavailable");

			Check(m_api->CreateEnv(
				ORT_LOGGING_LEVEL_WARNING,
				"VideoProcessorSubtitle",
				&m_environment));

			// Systems with both integrated and discrete GPUs do not guarantee
			// that DXGI adapter 0 is DirectML-capable. Probe a small adapter
			// range once at startup and keep the first provider that can open the
			// model. This is deliberately outside the live delivery thread.
			std::string lastAdapterError;
			for (int deviceId = 0; deviceId < 8 && !m_session; ++deviceId)
			{
				OrtSessionOptions* candidateOptions = nullptr;
				OrtSession* candidateSession = nullptr;
				try
				{
					Check(m_api->CreateSessionOptions(&candidateOptions));
					Check(m_api->DisableMemPattern(candidateOptions));
					Check(m_api->SetSessionExecutionMode(
						candidateOptions, ORT_SEQUENTIAL));
					Check(m_api->SetSessionGraphOptimizationLevel(
						candidateOptions, ORT_ENABLE_ALL));
					Check(appendDirectMl(candidateOptions, deviceId));
					Check(m_api->CreateSession(
						m_environment,
						modelPath.c_str(),
						candidateOptions,
						&candidateSession));
					m_sessionOptions = candidateOptions;
					m_session = candidateSession;
					m_selectedDeviceId = deviceId;
				}
				catch (const std::exception& error)
				{
					lastAdapterError = error.what();
					if (candidateSession)
						m_api->ReleaseSession(candidateSession);
					if (candidateOptions)
						m_api->ReleaseSessionOptions(candidateOptions);
				}
			}
			if (!m_session)
				throw std::runtime_error(
					"no DirectML-capable graphics adapter was found" +
					(lastAdapterError.empty() ? std::string() :
						std::string("; last adapter error: ") + lastAdapterError));
			Check(m_api->CreateCpuMemoryInfo(
				OrtArenaAllocator,
				OrtMemTypeDefault,
				&m_memoryInfo));
			Check(m_api->CreateRunOptions(&m_runOptions));
			m_available = true;
		}
		catch (const std::exception& error)
		{
			m_lastError = error.what();
			m_available = false;
		}
	}

	GpuSubtitleDetectionResult BuildResult(
		const float* probabilities,
		int modelWidth,
		int modelHeight,
		int sourceWidth,
		int sourceHeight,
		int pictureTop,
		int pictureBottom)
	{
		GpuSubtitleDetectionResult result;
		result.available = true;
		if (!probabilities)
			return result;

		const size_t modelWords =
			static_cast<size_t>(modelWidth) * modelHeight;
		m_binaryMask.resize(modelWords);
		for (size_t index = 0; index < modelWords; ++index)
			m_binaryMask[index] =
				probabilities[index] >= 0.30f ? 1 : 0;

		struct Component
		{
			GpuSubtitleRegion modelBox;
			GpuSubtitleRegion sourceBox;
			int pixels = 0;
			float probability = 0.0f;
		};
		std::vector<Component> components;
		m_flood.clear();
		for (int y = 1; y + 1 < modelHeight; ++y)
			for (int x = 1; x + 1 < modelWidth; ++x)
			{
				const int start = y * modelWidth + x;
				if (!m_binaryMask[start])
					continue;
				Component component;
				component.modelBox = { x, y, x + 1, y + 1 };
				m_flood.clear();
				m_flood.push_back(start);
				m_binaryMask[start] = 0;
				for (size_t positionIndex = 0;
					positionIndex < m_flood.size(); ++positionIndex)
				{
					const int position = m_flood[positionIndex];
					const int componentY = position / modelWidth;
					const int componentX =
						position - componentY * modelWidth;
					++component.pixels;
					component.probability += probabilities[position];
					component.modelBox.left =
						std::min(component.modelBox.left, componentX);
					component.modelBox.top =
						std::min(component.modelBox.top, componentY);
					component.modelBox.right =
						std::max(component.modelBox.right, componentX + 1);
					component.modelBox.bottom =
						std::max(component.modelBox.bottom, componentY + 1);
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							if (dx == 0 && dy == 0)
								continue;
							const int nextX = componentX + dx;
							const int nextY = componentY + dy;
							if (nextX <= 0 || nextX + 1 >= modelWidth ||
								nextY <= 0 || nextY + 1 >= modelHeight)
								continue;
							const int next = nextY * modelWidth + nextX;
							if (m_binaryMask[next])
							{
								m_binaryMask[next] = 0;
								m_flood.push_back(next);
							}
						}
				}
				if (component.pixels < 10)
					continue;
				component.sourceBox = {
					component.modelBox.left * sourceWidth / modelWidth,
					component.modelBox.top * sourceHeight / modelHeight,
					(component.modelBox.right * sourceWidth +
						modelWidth - 1) / modelWidth,
					(component.modelBox.bottom * sourceHeight +
						modelHeight - 1) / modelHeight
				};
				const int componentWidth = RectWidth(component.sourceBox);
				const int componentHeight = RectHeight(component.sourceBox);
				const int center =
					component.sourceBox.left + componentWidth / 2;
				const bool centered =
					center >= sourceWidth / 5 &&
					center <= sourceWidth * 4 / 5 &&
					component.sourceBox.right >= sourceWidth * 7 / 20 &&
					component.sourceBox.left <= sourceWidth * 13 / 20;
				const bool plausible =
					componentWidth >= std::max(8, sourceWidth / 80) &&
					componentWidth <= sourceWidth * 9 / 10 &&
					componentHeight >= 2 &&
					componentHeight <= std::max(12, sourceHeight / 8) &&
					centered;
				if (plausible)
					components.push_back(component);
			}

		const int boundaryTolerance = std::max(3, sourceHeight / 180);
		const int searchDistance = std::max(16, sourceHeight / 7);
		const Component* anchor = nullptr;
		bool anchorAtTop = false;
		int bestScore = std::numeric_limits<int>::min();
		for (const Component& component : components)
		{
			const bool atTop =
				pictureTop > 0 &&
				component.sourceBox.top < pictureTop + boundaryTolerance &&
				component.sourceBox.bottom >
					std::max(0, pictureTop - searchDistance);
			const bool atBottom =
				component.sourceBox.bottom >
					pictureBottom - boundaryTolerance &&
				component.sourceBox.top <
					std::min(sourceHeight, pictureBottom + searchDistance);
			if (!atTop && !atBottom)
				continue;
			const int componentWidth = RectWidth(component.sourceBox);
			const int center = component.sourceBox.left +
				componentWidth / 2;
			const int score =
				component.pixels +
				static_cast<int>(component.probability * 4.0f) +
				componentWidth * 4 -
				std::abs(center - sourceWidth / 2);
			if (score > bestScore)
			{
				bestScore = score;
				anchor = &component;
				anchorAtTop = atTop && !atBottom;
			}
		}
		if (!anchor)
			return result;

		GpuSubtitleRegion block = anchor->sourceBox;
		std::vector<const Component*> selected{ anchor };
		for (const Component& component : components)
		{
			if (&component == anchor || selected.size() >= 3)
				continue;
			const int gap =
				component.sourceBox.bottom <= block.top ?
					block.top - component.sourceBox.bottom :
				(block.bottom <= component.sourceBox.top ?
					component.sourceBox.top - block.bottom : 0);
			const int blockCenter =
				block.left + RectWidth(block) / 2;
			const int componentCenter =
				component.sourceBox.left +
					RectWidth(component.sourceBox) / 2;
			const int maximumGap = std::max(
				sourceHeight / 18,
				2 * std::max(
					RectHeight(block),
					RectHeight(component.sourceBox)));
			if (gap <= maximumGap &&
				std::abs(blockCenter - componentCenter) <= sourceWidth / 5)
			{
				block = UnionRect(block, component.sourceBox);
				selected.push_back(&component);
			}
		}

		if (RectHeight(block) > sourceHeight / 4)
			return result;
		result.textMask.assign(
			static_cast<size_t>(sourceWidth) * sourceHeight,
			static_cast<uint8_t>(0));
		for (const Component* component : selected)
		{
			result.regions.push_back(component->sourceBox);
			const int modelLeft =
				std::max(0, component->modelBox.left - 2);
			const int modelTop =
				std::max(0, component->modelBox.top - 2);
			const int modelRight =
				std::min(modelWidth, component->modelBox.right + 2);
			const int modelBottom =
				std::min(modelHeight, component->modelBox.bottom + 2);
			for (int sourceY = component->sourceBox.top;
				sourceY < component->sourceBox.bottom; ++sourceY)
				for (int sourceX = component->sourceBox.left;
					sourceX < component->sourceBox.right; ++sourceX)
				{
					const int modelX = std::max(modelLeft,
						std::min(modelRight - 1,
							sourceX * modelWidth / sourceWidth));
					const int modelY = std::max(modelTop,
						std::min(modelBottom - 1,
							sourceY * modelHeight / sourceHeight));
					if (probabilities[
						static_cast<size_t>(modelY) * modelWidth +
							modelX] >= 0.20f)
						result.textMask[
							static_cast<size_t>(sourceY) * sourceWidth +
								sourceX] = 1;
				}
		}
		result.left = block.left;
		result.top = block.top;
		result.right = block.right;
		result.bottom = block.bottom;
		result.score = std::max(100, bestScore);
		result.lineCount = static_cast<int>(selected.size());
		result.representativeLineHeight =
			std::max(1, RectHeight(block) /
				std::max(1, result.lineCount));
		result.atTop = anchorAtTop;
		result.detected = true;
		return result;
	}

	HMODULE m_runtime = nullptr;
	const OrtApi* m_api = nullptr;
	OrtEnv* m_environment = nullptr;
	OrtSessionOptions* m_sessionOptions = nullptr;
	OrtSession* m_session = nullptr;
	OrtMemoryInfo* m_memoryInfo = nullptr;
	OrtRunOptions* m_runOptions = nullptr;
	bool m_available = false;
	int m_selectedDeviceId = -1;
	std::vector<float> m_input;
	std::vector<uint8_t> m_binaryMask;
	std::vector<int> m_flood;
#endif
	std::string m_lastError;
};

GpuSubtitleDetector::GpuSubtitleDetector()
	: m_impl(std::make_unique<Impl>())
{
}

GpuSubtitleDetector::~GpuSubtitleDetector() = default;

GpuSubtitleDetectionResult GpuSubtitleDetector::Detect(
	const uint16_t* luma,
	int width,
	int height,
	int pictureTop,
	int pictureBottom)
{
	return m_impl->Detect(
		luma, width, height, pictureTop, pictureBottom);
}

void GpuSubtitleDetector::Cancel() noexcept
{
	m_impl->Cancel();
}

const std::string& GpuSubtitleDetector::LastError() const
{
	return m_impl->LastError();
}

int GpuSubtitleDetector::SelectedDeviceId() const
{
	return m_impl->SelectedDeviceId();
}
