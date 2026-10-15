#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <iosfwd>
#include <string>
#include <thread>
#include <vector>

// Fixed-layout, pointer-free analysis diagnostics passed between the host and
// the renderer plugin. Keep the version/size fields first and append fields
// when evolving the wire shape.
enum class AnalysisDiagnosticRecordKindV1 : uint32_t
{
	Frame = 1,
	Event = 2,
	Worker = 3,
};

struct AnalysisDiagnosticRecordV1
{
	uint32_t structSize = 0; // Submit fills this with sizeof(record) when unset.
	uint32_t schemaVersion = 1;
	AnalysisDiagnosticRecordKindV1 kind = AnalysisDiagnosticRecordKindV1::Event;
	uint32_t reserved = 0;
	uint64_t recordSequence = 0;
	uint64_t timestampNs = 0;
	uint64_t frameIdentity = 0;
	uint64_t rendererInstanceKey = 0;
	uint64_t acceptedSequence = 0;
	uint64_t transportGeneration = 0;
	uint64_t sourceFrameNumber = 0;
	uint64_t captureTimestamp = 0;
	uint64_t continuityGeneration = 0;
	uint64_t analysisDomainKey = 0;
	uint64_t sourceFormat = 0;
	uint64_t sourceFormatGeneration = 0;
	uint64_t analysisFormat = 0;
	uint64_t viewportGeneration = 0;
	uint64_t rendererGeneration = 0;
	uint64_t policyGeneration = 0;
	uint64_t nearBarDistance = 0;
	uint64_t optimizationMode = 0;
	int64_t frameDuration100ns = 0;
	uint32_t scriptHint = 0;
	uint32_t discontinuity = 0;
	uint32_t cadenceRepeat = 0;
	uint32_t rgbBitAlignment = 0;
	uint32_t sourceWidth = 0;
	uint32_t sourceHeight = 0;
	uint32_t analysisWidth = 0;
	uint32_t analysisHeight = 0;
	int32_t viewportLeft = 0;
	int32_t viewportTop = 0;
	int32_t viewportRight = 0;
	int32_t viewportBottom = 0;
	int32_t previous[4] = {};
	int32_t measured[4] = {};
	int32_t candidate[4] = {};
	int32_t published[4] = {};
	int32_t displayed[4] = {};
	int32_t subtitle[4] = {};
	uint64_t counters[8] = {};
	double metrics[24] = {};
	uint32_t fullScans = 0;
	uint32_t requestMisses = 0;
	uint32_t pendingFrames = 0;
	uint32_t acceptedSeeds = 0;
	uint32_t flags = 0;
	int32_t resultCode = 0;
	char stage[32] = {};
	char detail[192] = {};
	char reason[192] = {};
};

// Native-frame sidecar metadata. The first ten fields retain the legacy
// sidecar order; identityV1 then describes the exact production frame
// authority used by the live detector. Pixel ownership is retained only after
// the host has checked all opt-in size and queue bounds.
struct AnalysisDiagnosticFramePacketV1
{
	uint32_t structSize = 0; // SubmitFrame fills this with sizeof(packet) if unset.
	uint32_t schemaVersion = 1;
	uint32_t width = 0;
	uint32_t height = 0;
	uint64_t stride0 = 0;
	uint64_t stride1 = 0;
	uint32_t format = 0;
	uint32_t encoding = 0;
	uint32_t colorspace = 0;
	int32_t top = 0;
	int32_t bottom = 0;
	int32_t scriptHint = 0;
	uint32_t reserved0 = 0;
	uint64_t transportGeneration = 0;
	uint64_t acceptedSequence = 0;
	uint64_t sourceFrameNumber = 0;
	uint64_t captureTimestamp = 0;
	uint64_t sourceFormatGeneration = 0;
	uint64_t viewportGeneration = 0;
	uint64_t rendererGeneration = 0;
	uint64_t analysisDomainKey = 0;
	uint64_t policyGeneration = 0;
	uint64_t continuityGeneration = 0;
	uint64_t nearBarDistance = 0;
	uint64_t optimizationMode = 0;
	int64_t frameDuration100ns = 0;
	uint32_t discontinuity = 0;
	uint32_t cadenceRepeat = 0;
	uint32_t rgbBitAlignment = 0;
	uint32_t analysisSampleRange = 0; // AnalysisLumaRange; replaces reserved ABI word.
	const uint8_t* data = nullptr;
	uint64_t dataBytes = 0;
	const void* ownerContext = nullptr;
	// retainOwner must return a new independent token, usually a heap-owned copy
	// of a shared owner. releaseOwner consumes exactly that token.
	void* (__cdecl* retainOwner)(const void* ownerContext) = nullptr;
	void (__cdecl* releaseOwner)(void* retainedOwner) = nullptr;
};

enum class AnalysisDiagnosticCaptureResult : uint32_t
{
	Started,
	AlreadyRecording,
	NoProducer,
	FrameCaptureUnavailable,
	Busy,
	InvalidRequest,
};

struct AnalysisDiagnosticTraceStatus
{
	bool recording = false;
	bool includeFrames = false;
	bool producerConnected = false;
	bool frameCaptureSupported = false;
	bool writerReady = false;
	bool captureWriting = false;
	uint64_t recordsWritten = 0;
	uint64_t framesWritten = 0;
	uint64_t frameBytesWritten = 0;
	uint64_t droppedRecords = 0;
	uint64_t droppedFrames = 0;
	uint64_t historyOverwrites = 0;
	uint64_t writeFailures = 0;
	// UTF-8 path to the unique capture session directory, when one was started.
	char outputPath[512] = {};
};

struct AnalysisDiagnosticTraceOptions
{
	uint32_t preRollMs = 10000;
	uint32_t captureMs = 20000;
	size_t maxRecordHistory = 8192;
	size_t maxCaptureRecords = 65536;
	uint64_t maxFrameBytes = 128ull * 1024ull * 1024ull;
	uint64_t maxCaptureFrameBytes = 2ull * 1024ull * 1024ull * 1024ull;
	size_t maxOutstandingFrames = 2;
	std::string outputRoot;
};

class AnalysisDiagnosticTrace
{
public:
	using RecordSink = void (__cdecl*)(void*, const AnalysisDiagnosticRecordV1*);
	using FrameSink = void (__cdecl*)(void*, const AnalysisDiagnosticFramePacketV1*);
	using FrameCaptureActiveCallback = bool (__cdecl*)(void*);
	static constexpr uint32_t RECORD_SCHEMA_VERSION = 1;
	static constexpr uint32_t FRAME_SCHEMA_VERSION = 1;
	static constexpr size_t RECORD_HISTORY_CAPACITY = 8192;
	static constexpr size_t FRAME_QUEUE_CAPACITY = 2;
	static constexpr size_t MAX_CAPTURE_RECORDS = 65536;
	static constexpr uint64_t MAX_FRAME_BYTES = 128ull * 1024ull * 1024ull;
	static constexpr uint64_t MAX_CAPTURE_FRAME_BYTES = 2ull * 1024ull * 1024ull * 1024ull;

	AnalysisDiagnosticTrace();
	explicit AnalysisDiagnosticTrace(const AnalysisDiagnosticTraceOptions& options);
	~AnalysisDiagnosticTrace();
	AnalysisDiagnosticTrace(const AnalysisDiagnosticTrace&) = delete;
	AnalysisDiagnosticTrace& operator=(const AnalysisDiagnosticTrace&) = delete;

	static AnalysisDiagnosticTrace& Instance();

	// Plugin-local forwarding hooks. The host loader installs/removes these via
	// the additive plugin export; they do not instantiate a recorder in the DLL.
	static void SetExternalSinks(void* context, RecordSink recordSink,
		FrameSink frameSink, FrameCaptureActiveCallback activeCallback) noexcept;
	static void ClearExternalSinks() noexcept;
	static void Emit(const AnalysisDiagnosticRecordV1& record) noexcept;
	static void EmitFrame(const AnalysisDiagnosticFramePacketV1& frame) noexcept;
	static bool FramesActive() noexcept;

	// Capability is set by the host loader. It intentionally survives renderer
	// recreation and does not clear the rolling history or an active capture.
	void SetProducerAvailable(bool connected, bool frameCaptureSupported) noexcept;
	void Submit(const AnalysisDiagnosticRecordV1& record) noexcept;
	void SubmitFrame(const AnalysisDiagnosticFramePacketV1& frame) noexcept;
	bool IsFrameCaptureActive() const noexcept;
	bool IsRecording() const noexcept;

	AnalysisDiagnosticCaptureResult StartCapture(bool includeFrames = false);
	void StopCapture() noexcept;
	AnalysisDiagnosticTraceStatus Status() const;

private:
	struct StoredRecord
	{
		AnalysisDiagnosticRecordV1 record;
		uint64_t receivedMs = 0;
		bool captureEligible = false;
	};

	struct QueuedFrame
	{
		AnalysisDiagnosticFramePacketV1 metadata;
		void* retainedOwner = nullptr;
		void (__cdecl* releaseOwner)(void*) = nullptr;
		uint64_t captureIndex = 0;
	};

	void WriterThreadProc();
	void WriteCapture(const std::string& directory,
		const std::vector<AnalysisDiagnosticRecordV1>& preRoll,
		uint64_t firstSequence, uint64_t deadlineMs, bool includeFrames);
	void WriteCaptureSummary(const std::string& directory, bool includeFrames) noexcept;
	void WriteRecord(std::ostream& output, const AnalysisDiagnosticRecordV1& record);
	void WriteFrame(std::ostream& metadata, const std::string& directory,
		QueuedFrame& frame);
	std::string MakeCaptureDirectory() const;
	uint64_t NowMs() const noexcept;

	const AnalysisDiagnosticTraceOptions m_options;
	const size_t m_captureRecordLimit;
	mutable std::mutex m_mutex;
	std::condition_variable m_changed;
	std::array<StoredRecord, RECORD_HISTORY_CAPACITY> m_history{};
	std::array<QueuedFrame, FRAME_QUEUE_CAPACITY> m_frameQueue{};
	size_t m_frameHead = 0;
	size_t m_frameCount = 0;
	size_t m_outstandingFrames = 0;
	uint64_t m_nextSequence = 0;
	uint64_t m_captureStartSequence = 0;
	uint64_t m_captureStopSequence = 0;
	std::atomic<uint64_t> m_captureDeadlineMs{ 0 };
	uint64_t m_captureFrameBytes = 0;
	uint64_t m_captureFrameIndex = 0;
	uint64_t m_captureAcceptedRecords = 0;
	uint64_t m_recordsWritten = 0;
	uint64_t m_framesWritten = 0;
	uint64_t m_frameBytesWritten = 0;
	std::atomic<uint64_t> m_droppedRecords{ 0 };
	std::atomic<uint64_t> m_droppedFrames{ 0 };
	uint64_t m_historyOverwrites = 0;
	uint64_t m_writeFailures = 0;
	uint32_t m_captureMs = 0;
	uint32_t m_preRollMs = 0;
	std::atomic<bool> m_producerConnected{ false };
	std::atomic<bool> m_frameCaptureSupported{ false };
	std::atomic<bool> m_recording{ false };
	std::atomic<bool> m_includeFrames{ false };
	bool m_captureRequested = false;
	bool m_shutdownRequested = false;
	std::atomic<bool> m_captureInProgress{ false };
	std::atomic<bool> m_writerReady{ false };
	std::string m_captureDirectory;
	std::vector<AnalysisDiagnosticRecordV1> m_preRoll;
	std::thread m_writer;
};
