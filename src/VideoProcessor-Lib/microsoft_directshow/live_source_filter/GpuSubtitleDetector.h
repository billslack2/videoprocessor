#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GpuSubtitleRegion
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
};

struct GpuSubtitleDetectionResult
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int score = 0;
	int lineCount = 0;
	int representativeLineHeight = 0;
	bool available = false;
	bool detected = false;
	bool atTop = false;
	std::vector<uint8_t> textMask;
	std::vector<GpuSubtitleRegion> regions;
};

// Runs the PP-OCRv3 text-region model through ONNX Runtime's DirectML
// execution provider. It detects geometry only; no OCR text is generated and
// no pixels are redrawn by the model.
class GpuSubtitleDetector
{
public:
	GpuSubtitleDetector();
	~GpuSubtitleDetector();

	GpuSubtitleDetector(const GpuSubtitleDetector&) = delete;
	GpuSubtitleDetector& operator=(const GpuSubtitleDetector&) = delete;

	GpuSubtitleDetectionResult Detect(
		const uint16_t* luma,
		int width,
		int height,
		int pictureTop,
		int pictureBottom);

	// Requests termination of an in-flight inference during graph shutdown.
	void Cancel() noexcept;

	const std::string& LastError() const;
	int SelectedDeviceId() const;

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
