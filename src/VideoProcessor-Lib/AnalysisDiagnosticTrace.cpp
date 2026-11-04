#include <pch.h>

#include "AnalysisDiagnosticTrace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <Windows.h>

namespace
{
	std::atomic<AnalysisDiagnosticTrace::RecordSink> g_recordSink{ nullptr };
	std::atomic<AnalysisDiagnosticTrace::FrameSink> g_frameSink{ nullptr };
	std::atomic<AnalysisDiagnosticTrace::FrameCaptureActiveCallback> g_activeCallback{ nullptr };
	std::atomic<void*> g_sinkContext{ nullptr };
	std::atomic<uint64_t> g_directorySequence{ 0 };

	void WriteJsonString(std::ostream& output, const char* value)
	{
		output << '"';
		if (value != nullptr)
		{
			for (const unsigned char* current =
				reinterpret_cast<const unsigned char*>(value); *current != 0; ++current)
			{
				switch (*current)
				{
				case '"': output << "\\\""; break;
				case '\\': output << "\\\\"; break;
				case '\b': output << "\\b"; break;
				case '\f': output << "\\f"; break;
				case '\n': output << "\\n"; break;
				case '\r': output << "\\r"; break;
				case '\t': output << "\\t"; break;
				default:
					if (*current < 0x20)
						output << "\\u00" << std::hex << std::setw(2) <<
							std::setfill('0') << static_cast<unsigned>(*current) <<
							std::dec << std::setfill(' ');
					else
						output << static_cast<char>(*current);
					break;
				}
			}
		}
		output << '"';
	}

	std::string DefaultOutputRoot()
	{
		char exePath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return "logs";
		std::string path(exePath, length);
		const size_t lastSlash = path.find_last_of("\\/");
		if (lastSlash != std::string::npos)
			return path.substr(0, lastSlash + 1) + "logs";
		return "logs";
	}

	std::string ToUtf8(const std::string& value)
	{
		const int wideLength = MultiByteToWideChar(
			CP_ACP, 0, value.c_str(), -1, nullptr, 0);
		if (wideLength <= 0 || wideLength > MAX_PATH * 4)
			return {};
		wchar_t wide[MAX_PATH * 4] = {};
		if (MultiByteToWideChar(
			CP_ACP, 0, value.c_str(), -1, wide, wideLength) <= 0)
			return {};
		const int utf8Length = WideCharToMultiByte(
			CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (utf8Length <= 0 || utf8Length > MAX_PATH * 8)
			return {};
		char utf8[MAX_PATH * 8] = {};
		if (WideCharToMultiByte(
			CP_UTF8, 0, wide, -1, utf8, utf8Length, nullptr, nullptr) <= 0)
			return {};
		return utf8;
	}

	bool EnsureDirectoryTree(const std::string& directory)
	{
		if (directory.empty())
			return false;
		const DWORD attributes = GetFileAttributesA(directory.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES)
			return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

		if (directory.size() == 2 && directory[1] == ':')
			return true;
		const size_t separator = directory.find_last_of("\\/");
		if (separator != std::string::npos)
		{
			std::string parent = directory.substr(0, separator);
			if (parent.size() == 2 && parent[1] == ':')
				parent.push_back('\\');
			if (!parent.empty() && parent != directory &&
				!EnsureDirectoryTree(parent))
				return false;
		}
		if (CreateDirectoryA(directory.c_str(), nullptr))
			return true;
		const DWORD error = GetLastError();
		const DWORD retryAttributes = GetFileAttributesA(directory.c_str());
		return error == ERROR_ALREADY_EXISTS &&
			retryAttributes != INVALID_FILE_ATTRIBUTES &&
			(retryAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}
}

AnalysisDiagnosticTrace::AnalysisDiagnosticTrace()
	: AnalysisDiagnosticTrace(AnalysisDiagnosticTraceOptions{})
{
}

AnalysisDiagnosticTrace::AnalysisDiagnosticTrace(
	const AnalysisDiagnosticTraceOptions& options)
	: m_options(options),
	m_captureRecordLimit(options.maxCaptureRecords > MAX_CAPTURE_RECORDS ?
		MAX_CAPTURE_RECORDS : options.maxCaptureRecords),
	m_preRollMs(options.preRollMs),
	m_captureMs(options.captureMs)
{
	if (m_preRollMs > 60000)
		m_preRollMs = 60000;
	if (m_captureMs == 0)
		m_captureMs = 1;
	if (m_captureMs > 120000)
		m_captureMs = 120000;

	m_writer = std::thread(&AnalysisDiagnosticTrace::WriterThreadProc, this);
	std::unique_lock<std::mutex> lock(m_mutex);
	m_changed.wait(lock, [this]() {
		return m_writerReady.load(std::memory_order_acquire);
	});
}

AnalysisDiagnosticTrace::~AnalysisDiagnosticTrace()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_recording.exchange(false, std::memory_order_acq_rel))
			m_captureStopSequence = m_nextSequence;
		m_shutdownRequested = true;
	}
	m_changed.notify_all();
	if (m_writer.joinable())
		m_writer.join();

	for (;;)
	{
		QueuedFrame frame;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_frameCount == 0)
				break;
			frame = m_frameQueue[m_frameHead];
			m_frameHead = (m_frameHead + 1) % FRAME_QUEUE_CAPACITY;
			--m_frameCount;
		}
		if (frame.retainedOwner != nullptr && frame.releaseOwner != nullptr)
			frame.releaseOwner(frame.retainedOwner);
	}
}

AnalysisDiagnosticTrace& AnalysisDiagnosticTrace::Instance()
{
	static AnalysisDiagnosticTrace trace;
	return trace;
}

void AnalysisDiagnosticTrace::SetExternalSinks(
	void* context,
	RecordSink recordSink,
	FrameSink frameSink,
	FrameCaptureActiveCallback activeCallback) noexcept
{
	g_sinkContext.store(context, std::memory_order_release);
	g_frameSink.store(frameSink, std::memory_order_release);
	g_activeCallback.store(activeCallback, std::memory_order_release);
	g_recordSink.store(recordSink, std::memory_order_release);
}

void AnalysisDiagnosticTrace::ClearExternalSinks() noexcept
{
	g_recordSink.store(nullptr, std::memory_order_release);
	g_frameSink.store(nullptr, std::memory_order_release);
	g_activeCallback.store(nullptr, std::memory_order_release);
	g_sinkContext.store(nullptr, std::memory_order_release);
}

void AnalysisDiagnosticTrace::Emit(
	const AnalysisDiagnosticRecordV1& record) noexcept
{
	const RecordSink sink = g_recordSink.load(std::memory_order_acquire);
	if (sink == nullptr)
		return;
	try
	{
		sink(g_sinkContext.load(std::memory_order_acquire), &record);
	}
	catch (...)
	{
	}
}

void AnalysisDiagnosticTrace::EmitFrame(
	const AnalysisDiagnosticFramePacketV1& frame) noexcept
{
	if (!FramesActive())
		return;
	const FrameSink sink = g_frameSink.load(std::memory_order_acquire);
	if (sink == nullptr)
		return;
	try
	{
		sink(g_sinkContext.load(std::memory_order_acquire), &frame);
	}
	catch (...)
	{
	}
}

bool AnalysisDiagnosticTrace::FramesActive() noexcept
{
	const FrameCaptureActiveCallback active =
		g_activeCallback.load(std::memory_order_acquire);
	if (active == nullptr)
		return false;
	try
	{
		return active(g_sinkContext.load(std::memory_order_acquire));
	}
	catch (...)
	{
		return false;
	}
}

void AnalysisDiagnosticTrace::SetProducerAvailable(
	bool connected,
	bool frameCaptureSupported) noexcept
{
	m_frameCaptureSupported.store(
		connected && frameCaptureSupported, std::memory_order_release);
	m_producerConnected.store(connected, std::memory_order_release);
}

void AnalysisDiagnosticTrace::Submit(
	const AnalysisDiagnosticRecordV1& input) noexcept
{
	if ((input.schemaVersion != 0 &&
		input.schemaVersion != RECORD_SCHEMA_VERSION) ||
		(input.structSize != 0 &&
			input.structSize < sizeof(AnalysisDiagnosticRecordV1)))
	{
		m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
	if (!lock.owns_lock())
	{
		m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (m_recording.load(std::memory_order_acquire) &&
		NowMs() >= m_captureDeadlineMs.load(std::memory_order_acquire))
	{
		m_recording.store(false, std::memory_order_release);
		m_captureStopSequence = m_nextSequence;
	}

	AnalysisDiagnosticRecordV1 record = input;
	record.structSize = sizeof(record);
	record.schemaVersion = RECORD_SCHEMA_VERSION;
	record.recordSequence = ++m_nextSequence;
	if (record.timestampNs == 0)
	{
		record.timestampNs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
	}
	record.stage[sizeof(record.stage) - 1] = '\0';
	record.detail[sizeof(record.detail) - 1] = '\0';
	record.reason[sizeof(record.reason) - 1] = '\0';

	const bool recording = m_recording.load(std::memory_order_acquire);
	const bool captureEligible = recording &&
		m_captureAcceptedRecords < m_captureRecordLimit;
	if (recording)
	{
		if (captureEligible)
			++m_captureAcceptedRecords;
		else
			m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
	}

	const size_t historyCapacity = std::min(
		m_options.maxRecordHistory, RECORD_HISTORY_CAPACITY);
	if (historyCapacity == 0)
	{
		m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
		lock.unlock();
		m_changed.notify_one();
		return;
	}
	if (record.recordSequence > historyCapacity)
		++m_historyOverwrites;
	StoredRecord& slot = m_history[(record.recordSequence - 1) % historyCapacity];
	slot.record = record;
	slot.receivedMs = NowMs();
	slot.captureEligible = captureEligible;
	lock.unlock();
	m_changed.notify_one();
}

AnalysisDiagnosticRecordV1 AnalysisDiagnosticTrace::BoundaryChain(
    const AnalysisDiagnosticRecordV1& current, const AnalysisBoundaryChainFacts& f) noexcept
{
    auto r = current;
    strcpy_s(r.stage, "ar-boundary-chain");
    const bool physical = f.physicalEvaluated && f.physicalAvailable && (f.physicalConfidence == 1 || f.physicalConfidence == 2) &&
        f.physical[0] >= 0 && f.physical[1] >= 0 && f.physical[2] > f.physical[0] && f.physical[3] > f.physical[1] &&
        f.physicalSource == current.acceptedSequence;
    strcpy_s(r.detail, physical ? "qualified-physical-estimate" : "physical-estimate-unknown");
    strcpy_s(r.reason, "computed-only; guard and nomination are not physical measurements");
    for (int i = 0; i < 4; ++i) {
        r.previous[i] = f.nomination[i]; r.measured[i] = physical ? f.physical[i] : 0;
        r.candidate[i] = f.guard[i]; r.published[i] = f.accepted[i];
        r.displayed[i] = f.displayed[i]; r.subtitle[i] = f.subtitle[i];
    }
    r.flags = (f.physicalEvaluated ? 1u : 0u) | (physical ? 2u : 0u) |
        (f.cropEnabled ? 4u : 0u) | (f.cadenceSkipped ? 8u : 0u) | (f.submitted ? 16u : 0u) |
        (f.fillMeasurementAvailable ? 32u : 0u);
    r.counters[0] = current.acceptedSequence; r.counters[1] = f.selectedSource;
    r.counters[2] = f.physicalSource; r.counters[3] = f.analyzedSource;
    r.counters[4] = f.nativeAuthoritySource; r.counters[5] = current.viewportGeneration;
    r.counters[6] = current.policyGeneration; r.counters[7] = f.analysisStride;
    for (auto& metric : r.metrics) metric = 0;
    if (physical && f.physical[3] > f.physical[1])
        r.metrics[0] = double(f.physical[2]-f.physical[0]) / (f.physical[3]-f.physical[1]);
    if (f.guard[3] > f.guard[1]) r.metrics[1] = double(f.guard[2]-f.guard[0]) / (f.guard[3]-f.guard[1]);
    if (f.fillMeasurementAvailable) {
        r.metrics[2] = f.fillAspect; r.metrics[3] = f.removedRows;
        r.metrics[4] = f.topInset; r.metrics[5] = f.bottomInset;
    }
    r.metrics[6] = f.currentPts; r.metrics[7] = f.selectedPts;
    r.metrics[8] = f.originalAge; r.metrics[9] = f.inputFps;
    r.metrics[10] = physical ? f.physicalConfidence : 0;
    return r;
}

void AnalysisDiagnosticTrace::SubmitFrame(
	const AnalysisDiagnosticFramePacketV1& input) noexcept
{
	if ((input.schemaVersion != 0 &&
		input.schemaVersion != FRAME_SCHEMA_VERSION) ||
		(input.structSize != 0 &&
			input.structSize < sizeof(AnalysisDiagnosticFramePacketV1)) ||
		input.data == nullptr || input.dataBytes == 0 ||
		input.ownerContext == nullptr || input.retainOwner == nullptr ||
		input.releaseOwner == nullptr)
	{
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
	if (!lock.owns_lock())
	{
        if(m_nativeSampling.load(std::memory_order_relaxed)==AnalysisDiagnosticNativeSampling::ConsecutiveBurst &&
            IsFrameCaptureActive())m_nativeBurstLockMisses.fetch_add(1,std::memory_order_relaxed);
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		return;
	}
    if(m_nativeFramesFinished.load(std::memory_order_acquire))return;
	if (!m_recording.load(std::memory_order_acquire) ||
		NowMs() >= m_captureDeadlineMs.load(std::memory_order_acquire) ||
		!m_includeFrames.load(std::memory_order_acquire) ||
		!m_producerConnected.load(std::memory_order_acquire) ||
		!m_frameCaptureSupported.load(std::memory_order_acquire))
	{
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		return;
	}

    ++m_captureFrameOffered;
    AnalysisDiagnosticRecordV1 sampling;
    strcpy_s(sampling.stage, "frame-sampling");
    sampling.acceptedSequence = input.acceptedSequence;
    sampling.sourceFrameNumber = input.sourceFrameNumber;
    sampling.transportGeneration = input.transportGeneration;
    sampling.analysisDomainKey = input.analysisDomainKey;
    sampling.frameDuration100ns = input.frameDuration100ns;
    sampling.sourceWidth = input.width; sampling.sourceHeight = input.height;
    sampling.captureTimestamp = input.captureTimestamp;
    sampling.sourceFormatGeneration=input.sourceFormatGeneration;
    sampling.viewportGeneration=input.viewportGeneration;sampling.rendererGeneration=input.rendererGeneration;
    sampling.policyGeneration=input.policyGeneration;sampling.continuityGeneration=input.continuityGeneration;
    const bool burst=m_nativeSampling.load(std::memory_order_relaxed)==AnalysisDiagnosticNativeSampling::ConsecutiveBurst;
    auto finishSampling = [&](const char* reason, bool retained) {
        strcpy_s(sampling.reason, reason);
        sampling.flags = retained ? 1u : 0u;
        sampling.counters[0] = m_captureFrameOffered;
        sampling.counters[1] = m_captureFrameSamplingStep;
        sampling.counters[2] = m_captureFrameSamplingSkipped;
        sampling.counters[3] = m_captureFrameMissed;
        sampling.counters[4]=m_nativeBurstTarget;sampling.counters[5]=m_nativeBurstFirstSource;
        sampling.counters[6]=m_nativeBurstLastSource;sampling.counters[7]=burst?1u:0u;
        lock.unlock();
        Submit(sampling);
    };

	const uint64_t maxPacketBytes = std::min(m_options.maxFrameBytes, MAX_FRAME_BYTES);
	const uint64_t maxCaptureBytes = std::min(
		m_options.maxCaptureFrameBytes, MAX_CAPTURE_FRAME_BYTES);
	const size_t maxOutstanding = std::min(
		m_options.maxOutstandingFrames, FRAME_QUEUE_CAPACITY);
    if(burst) {
        const uint64_t source=input.sourceFrameNumber?input.sourceFrameNumber:input.acceptedSequence;
        if(!m_nativeBurstTarget) {
            const uint64_t slots=input.dataBytes?maxCaptureBytes/input.dataBytes:0;
            if(!source || !input.acceptedSequence || !slots || input.dataBytes>maxPacketBytes ||
                source>UINT64_MAX-(slots-1)) {
                ++m_captureFrameMissed;FinishNativeBurstLocked("invalid-source-or-frame-budget");
                finishSampling("burst-source-or-budget-unavailable",false);return;
            }
            m_nativeBurstFirst=input;
            m_nativeBurstFirst.data=nullptr;m_nativeBurstFirst.ownerContext=nullptr;
            m_nativeBurstFirst.retainOwner=nullptr;m_nativeBurstFirst.releaseOwner=nullptr;
            m_nativeBurstUsesPhysicalSource=input.sourceFrameNumber!=0;
            m_nativeBurstTarget=slots;m_nativeBurstFirstSource=source;m_nativeBurstLastSource=source+slots-1;
            m_nativeBurstEndReason="recording";
        } else if(!SameNativeBurstContext(input) || input.discontinuity ||
            (input.sourceFrameNumber!=0)!=m_nativeBurstUsesPhysicalSource || source<m_nativeBurstLastOffered ||
            (source>m_nativeBurstLastOffered && (input.acceptedSequence<=m_nativeBurstLastAccepted ||
                (m_nativeBurstLastTimestamp && input.captureTimestamp<=m_nativeBurstLastTimestamp)))) {
            FinishNativeBurstLocked("source-context-changed");
            finishSampling("burst-source-context-changed",false);return;
        }
        m_captureFrameSamplingStep=1;
        if(m_nativeBurstOffered && source==m_nativeBurstLastOffered) {
            ++m_nativeBurstDuplicates;finishSampling("burst-duplicate-source",false);return;
        }
        if(source>m_nativeBurstLastSource) {
            m_nativeBurstSourceGaps+=m_nativeBurstLastSource-m_nativeBurstLastOffered;
            m_nativeBurstWindowEnded=true;FinishNativeBurstLocked("source-window-ended");
            finishSampling("burst-source-window-ended",false);return;
        }
        if(m_nativeBurstOffered && source>m_nativeBurstLastOffered+1)
            m_nativeBurstSourceGaps+=source-m_nativeBurstLastOffered-1;
        m_nativeBurstLastOffered=source;m_nativeBurstLastAccepted=input.acceptedSequence;
        m_nativeBurstLastTimestamp=input.captureTimestamp;++m_nativeBurstOffered;
        // Finish at the requested SOURCE boundary even when this admission fails.
        // Queue pressure must never extend the burst into later movie frames.
        if(source==m_nativeBurstLastSource) {
            m_nativeBurstWindowEnded=true;FinishNativeBurstLocked("source-window-ended");
        }
    }
    // Spread the byte budget across SOURCE time, rather than spending it on
    // the first seconds. Odd source steps visit both >30 Hz analysis parities.
    const uint64_t slots = input.dataBytes ? maxCaptureBytes / input.dataBytes : 0;
    if (!burst && slots && input.frameDuration100ns > 0) {
        const uint64_t duration = static_cast<uint64_t>(input.frameDuration100ns);
        const uint64_t expected = (uint64_t(m_captureMs) * 10000 + duration - 1) / duration;
        m_captureFrameSamplingStep = std::max<uint64_t>(1, (expected + slots - 1) / slots);
        if (duration < 333333 && (m_captureFrameSamplingStep % 2) == 0)
            ++m_captureFrameSamplingStep;
    } else m_captureFrameSamplingStep = 1;
    const uint64_t source = input.sourceFrameNumber ? input.sourceFrameNumber : input.acceptedSequence;
    if (!m_captureSamplingFirstSource || input.transportGeneration != m_captureSamplingGeneration ||
        source < m_captureSamplingFirstSource) {
        m_captureSamplingFirstSource = source;
        m_captureSamplingGeneration = input.transportGeneration;
    }
    const uint64_t sourceOffset = source ? source - m_captureSamplingFirstSource : m_captureFrameOffered - 1;
    if (!burst && sourceOffset % m_captureFrameSamplingStep != 0) {
        ++m_captureFrameSamplingSkipped;
        finishSampling("uniform-source-sampling-skip", false);
        return;
    }
	if (input.dataBytes > maxPacketBytes ||
		input.dataBytes > maxCaptureBytes ||
		m_captureFrameBytes > maxCaptureBytes - input.dataBytes ||
		m_outstandingFrames >= maxOutstanding ||
		m_frameCount >= FRAME_QUEUE_CAPACITY)
	{
        ++m_captureFrameMissed;
        m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
        finishSampling("selected-frame-size-budget-or-queue-unavailable", false);
        return;
    }

    void* retainedOwner = nullptr;
	try
	{
		retainedOwner = input.retainOwner(input.ownerContext);
	}
	catch (...)
	{
		retainedOwner = nullptr;
	}
    if (retainedOwner == nullptr) {
        ++m_captureFrameMissed;
        m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
        finishSampling("selected-frame-owner-retain-failed", false);
        return;
    }

	QueuedFrame queued;
	queued.metadata = input;
	queued.metadata.structSize = sizeof(queued.metadata);
	queued.metadata.schemaVersion = FRAME_SCHEMA_VERSION;
	queued.metadata.ownerContext = nullptr;
	queued.metadata.retainOwner = nullptr;
	queued.metadata.releaseOwner = nullptr;
	queued.retainedOwner = retainedOwner;
	queued.releaseOwner = input.releaseOwner;
	queued.captureIndex = ++m_captureFrameIndex;

	const size_t tail = (m_frameHead + m_frameCount) % FRAME_QUEUE_CAPACITY;
	m_frameQueue[tail] = queued;
	++m_frameCount;
	++m_outstandingFrames;
	m_captureFrameBytes += input.dataBytes;
    finishSampling("selected-native-frame-retained", true);
    m_changed.notify_one();
}

void AnalysisDiagnosticTrace::FinishNativeBurstLocked(const char* reason) noexcept
{
    if(m_nativeSampling.load(std::memory_order_relaxed)!=AnalysisDiagnosticNativeSampling::ConsecutiveBurst ||
        m_nativeFramesFinished.load(std::memory_order_relaxed))return;
    m_nativeBurstEndReason=reason;m_nativeFramesFinished.store(true,std::memory_order_release);
}

bool AnalysisDiagnosticTrace::SameNativeBurstContext(const AnalysisDiagnosticFramePacketV1& b) const noexcept
{
    const auto& a=m_nativeBurstFirst;
    return a.transportGeneration==b.transportGeneration && a.sourceFormatGeneration==b.sourceFormatGeneration &&
        a.viewportGeneration==b.viewportGeneration && a.rendererGeneration==b.rendererGeneration &&
        a.analysisDomainKey==b.analysisDomainKey && a.policyGeneration==b.policyGeneration &&
        a.continuityGeneration==b.continuityGeneration && a.width==b.width && a.height==b.height &&
        a.stride0==b.stride0 && a.stride1==b.stride1 && a.format==b.format && a.encoding==b.encoding &&
        a.colorspace==b.colorspace && a.analysisSampleRange==b.analysisSampleRange &&
        a.rgbBitAlignment==b.rgbBitAlignment && a.dataBytes==b.dataBytes &&
        a.frameDuration100ns==b.frameDuration100ns && a.scriptHint==b.scriptHint &&
        a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode &&
        (a.captureTimestamp!=0)==(b.captureTimestamp!=0);
}

bool AnalysisDiagnosticTrace::IsFrameCaptureActive() const noexcept
{
	return m_recording.load(std::memory_order_acquire) &&
        !m_nativeFramesFinished.load(std::memory_order_acquire) &&
		NowMs() < m_captureDeadlineMs.load(std::memory_order_acquire) &&
		m_includeFrames.load(std::memory_order_acquire) &&
		m_producerConnected.load(std::memory_order_acquire) &&
		m_frameCaptureSupported.load(std::memory_order_acquire);
}

bool AnalysisDiagnosticTrace::IsRecording() const noexcept
{
	return m_recording.load(std::memory_order_acquire) &&
		NowMs() < m_captureDeadlineMs.load(std::memory_order_acquire);
}

AnalysisDiagnosticCaptureResult AnalysisDiagnosticTrace::StartCapture(
	bool includeFrames,AnalysisDiagnosticNativeSampling sampling)
{
    if((sampling!=AnalysisDiagnosticNativeSampling::Uniform && sampling!=AnalysisDiagnosticNativeSampling::ConsecutiveBurst) ||
        (!includeFrames && sampling==AnalysisDiagnosticNativeSampling::ConsecutiveBurst))
        return AnalysisDiagnosticCaptureResult::InvalidRequest;
	std::lock_guard<std::mutex> lock(m_mutex);
	const uint64_t now = NowMs();
	if (m_recording.load(std::memory_order_acquire) &&
		now >= m_captureDeadlineMs.load(std::memory_order_acquire))
	{
		m_recording.store(false, std::memory_order_release);
		m_captureStopSequence = m_nextSequence;
	}
	if (m_recording.load(std::memory_order_acquire) || m_captureRequested)
		return AnalysisDiagnosticCaptureResult::AlreadyRecording;
	if (m_captureInProgress.load(std::memory_order_acquire))
		return AnalysisDiagnosticCaptureResult::Busy;
	if (!m_writerReady.load(std::memory_order_acquire) || m_shutdownRequested)
		return AnalysisDiagnosticCaptureResult::Busy;
	if (!m_producerConnected.load(std::memory_order_acquire))
		return AnalysisDiagnosticCaptureResult::NoProducer;
	if (includeFrames &&
		!m_frameCaptureSupported.load(std::memory_order_acquire))
		return AnalysisDiagnosticCaptureResult::FrameCaptureUnavailable;

	const uint64_t oldestAllowed = now > m_preRollMs ? now - m_preRollMs : 0;
	m_preRoll.clear();
	const size_t historyCapacity = std::min(
		m_options.maxRecordHistory, RECORD_HISTORY_CAPACITY);
	const uint64_t firstSequence = m_nextSequence > historyCapacity ?
		m_nextSequence - historyCapacity + 1 : 1;
	for (uint64_t sequence = firstSequence;
		sequence <= m_nextSequence && historyCapacity != 0; ++sequence)
	{
		const StoredRecord& slot = m_history[(sequence - 1) % historyCapacity];
		if (slot.record.recordSequence == sequence &&
			slot.receivedMs >= oldestAllowed)
			m_preRoll.push_back(slot.record);
	}

	if (m_preRoll.size() > m_captureRecordLimit)
	{
		const size_t removeCount = m_preRoll.size() - m_captureRecordLimit;
		m_preRoll.erase(m_preRoll.begin(), m_preRoll.begin() + removeCount);
		m_droppedRecords.fetch_add(removeCount, std::memory_order_relaxed);
	}

	m_captureDirectory = MakeCaptureDirectory();
	m_captureStartSequence = m_nextSequence + 1;
	m_captureStopSequence = 0;
	m_captureDeadlineMs.store(now + m_captureMs, std::memory_order_release);
	m_captureAcceptedRecords = m_preRoll.size();
	m_captureFrameBytes = 0;
	m_captureFrameIndex = 0;
	m_captureFrameOffered = m_captureFrameSamplingSkipped = m_captureFrameMissed = 0;
	m_captureFrameSamplingStep = 1;
	m_captureSamplingFirstSource = m_captureSamplingGeneration = 0;
    m_nativeSampling.store(sampling,std::memory_order_relaxed);m_nativeFramesFinished.store(false,std::memory_order_release);
    m_nativeBurstLockMisses.store(0,std::memory_order_relaxed);m_nativeBurstFirst={};
    m_nativeBurstTarget=m_nativeBurstFirstSource=m_nativeBurstLastSource=m_nativeBurstLastOffered=0;
    m_nativeBurstLastAccepted=m_nativeBurstLastTimestamp=m_nativeBurstOffered=m_nativeBurstSourceGaps=m_nativeBurstDuplicates=0;
    m_nativeBurstUsesPhysicalSource=m_nativeBurstWindowEnded=false;m_captureWriteFailuresAtStart=m_writeFailures;
    m_nativeBurstEndReason=sampling==AnalysisDiagnosticNativeSampling::ConsecutiveBurst?"waiting-for-source":"not-requested";
	m_recordsWritten = 0;
	m_framesWritten = 0;
	m_frameBytesWritten = 0;
	m_includeFrames.store(includeFrames, std::memory_order_release);
	m_recording.store(true, std::memory_order_release);
	m_captureRequested = true;
	m_changed.notify_one();
	return AnalysisDiagnosticCaptureResult::Started;
}

void AnalysisDiagnosticTrace::StopCapture() noexcept
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
        FinishNativeBurstLocked("capture-stopped");
		if (m_recording.exchange(false, std::memory_order_acq_rel))
			m_captureStopSequence = m_nextSequence;
	}
	m_changed.notify_all();
}

AnalysisDiagnosticTraceStatus AnalysisDiagnosticTrace::Status() const
{
	AnalysisDiagnosticTraceStatus status;
	std::lock_guard<std::mutex> lock(m_mutex);
	status.recording = m_recording.load(std::memory_order_acquire) &&
		NowMs() < m_captureDeadlineMs.load(std::memory_order_acquire);
	status.includeFrames = m_includeFrames.load(std::memory_order_acquire);
	status.producerConnected = m_producerConnected.load(std::memory_order_acquire);
	status.frameCaptureSupported =
		m_frameCaptureSupported.load(std::memory_order_acquire);
	status.writerReady = m_writerReady.load(std::memory_order_acquire);
	status.captureWriting = m_captureInProgress.load(std::memory_order_acquire) ||
		m_captureRequested;
	status.recordsWritten = m_recordsWritten;
	status.framesWritten = m_framesWritten;
	status.frameBytesWritten = m_frameBytesWritten;
	status.droppedRecords = m_droppedRecords.load(std::memory_order_relaxed);
	status.droppedFrames = m_droppedFrames.load(std::memory_order_relaxed);
	status.historyOverwrites = m_historyOverwrites;
	status.writeFailures = m_writeFailures;

	if (!m_captureDirectory.empty())
	{
		const std::string utf8Path = ToUtf8(m_captureDirectory);
		if (!utf8Path.empty() && utf8Path.size() < sizeof(status.outputPath))
			std::copy(utf8Path.begin(), utf8Path.end(), status.outputPath);
	}
	return status;
}

void AnalysisDiagnosticTrace::WriterThreadProc()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_writerReady.store(true, std::memory_order_release);
	}
	m_changed.notify_all();

	for (;;)
	{
		std::string directory;
		std::vector<AnalysisDiagnosticRecordV1> preRoll;
		uint64_t firstSequence = 0;
		uint64_t deadlineMs = 0;
		bool includeFrames = false;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_changed.wait(lock, [this]() {
				return m_captureRequested || m_shutdownRequested;
			});
			if (m_shutdownRequested && !m_captureRequested)
				break;
			m_captureRequested = false;
			directory = m_captureDirectory;
			preRoll.swap(m_preRoll);
			firstSequence = m_captureStartSequence;
			deadlineMs = m_captureDeadlineMs.load(std::memory_order_acquire);
			includeFrames = m_includeFrames.load(std::memory_order_acquire);
			m_captureInProgress.store(true, std::memory_order_release);
		}
		WriteCapture(directory, preRoll, firstSequence, deadlineMs, includeFrames);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_captureInProgress.store(false, std::memory_order_release);
			if (m_shutdownRequested)
				break;
		}
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_writerReady.store(false, std::memory_order_release);
	}
}

void AnalysisDiagnosticTrace::WriteCapture(
	const std::string& directory,
	const std::vector<AnalysisDiagnosticRecordV1>& preRoll,
	uint64_t firstSequence,
	uint64_t deadlineMs,
	bool includeFrames)
{
	std::ofstream records;
	std::ofstream frameMetadata;
	try
	{
		if (!EnsureDirectoryTree(directory))
			throw std::runtime_error("cannot create analysis trace directory");
		records.open((directory + "\\analysis.jsonl").c_str(),
			std::ios::out | std::ios::binary | std::ios::trunc);
		if (includeFrames)
			frameMetadata.open((directory + "\\frames.jsonl").c_str(),
				std::ios::out | std::ios::binary | std::ios::trunc);
	}
	catch (...)
	{
	}
	if (!records.is_open() || (includeFrames && !frameMetadata.is_open()))
	{
		std::array<QueuedFrame, FRAME_QUEUE_CAPACITY> dropped{};
		size_t droppedCount = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_writeFailures;
			m_recording.store(false, std::memory_order_release);
			m_captureStopSequence = m_nextSequence;
			while (m_frameCount != 0)
			{
				dropped[droppedCount++] = m_frameQueue[m_frameHead];
				m_frameHead = (m_frameHead + 1) % FRAME_QUEUE_CAPACITY;
				--m_frameCount;
				if (m_outstandingFrames != 0)
					--m_outstandingFrames;
			}
		}
		for (size_t index = 0; index < droppedCount; ++index)
		{
			if (dropped[index].retainedOwner != nullptr &&
				dropped[index].releaseOwner != nullptr)
				dropped[index].releaseOwner(dropped[index].retainedOwner);
		}
		if (records.is_open())
			records.close();
		if (frameMetadata.is_open())
			frameMetadata.close();
		WriteCaptureSummary(directory, includeFrames);
		return;
	}

	for (const AnalysisDiagnosticRecordV1& record : preRoll)
	{
		WriteRecord(records, record);
		if (!records.good())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_writeFailures;
			m_recording.store(false, std::memory_order_release);
			m_captureStopSequence = m_nextSequence;
			break;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_recordsWritten;
	}

	uint64_t nextSequence = firstSequence;
	for (;;)
	{
		QueuedFrame frame;
		bool hasFrame = false;
		AnalysisDiagnosticRecordV1 record;
		bool hasRecord = false;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if (m_recording.load(std::memory_order_acquire) && NowMs() >= deadlineMs)
			{
                FinishNativeBurstLocked("capture-deadline");
				m_recording.store(false, std::memory_order_release);
				m_captureStopSequence = m_nextSequence;
			}

			const uint64_t finalSequence =
				m_recording.load(std::memory_order_acquire) ?
				m_nextSequence : m_captureStopSequence;
			const uint64_t historyCapacity = std::min(
				m_options.maxRecordHistory, RECORD_HISTORY_CAPACITY);
			if (nextSequence <= finalSequence && historyCapacity != 0)
			{
				const uint64_t earliestAvailable = finalSequence > historyCapacity ?
					finalSequence - historyCapacity + 1 : 1;
				if (nextSequence < earliestAvailable)
				{
					m_droppedRecords.fetch_add(
						earliestAvailable - nextSequence, std::memory_order_relaxed);
					nextSequence = earliestAvailable;
				}
				const StoredRecord& slot =
					m_history[(nextSequence - 1) % historyCapacity];
				if (slot.record.recordSequence == nextSequence)
				{
					record = slot.record;
					hasRecord = slot.captureEligible;
				}
				else
				{
					m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
				}
				++nextSequence;
			}
			else if (m_frameCount != 0)
			{
				frame = m_frameQueue[m_frameHead];
				m_frameHead = (m_frameHead + 1) % FRAME_QUEUE_CAPACITY;
				--m_frameCount;
				hasFrame = true;
			}
			else if (!m_recording.load(std::memory_order_acquire) &&
				nextSequence > m_captureStopSequence && m_outstandingFrames == 0)
			{
				break;
			}
			else
			{
				m_changed.wait_for(lock, std::chrono::milliseconds(25));
			}
		}

		if (hasRecord)
		{
			WriteRecord(records, record);
			if (!records.good())
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_writeFailures;
				m_recording.store(false, std::memory_order_release);
				m_captureStopSequence = m_nextSequence;
			}
			else
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_recordsWritten;
			}
		}
		if (hasFrame)
		{
			WriteFrame(frameMetadata, directory, frame);
			if (frame.retainedOwner != nullptr && frame.releaseOwner != nullptr)
				frame.releaseOwner(frame.retainedOwner);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_outstandingFrames != 0)
					--m_outstandingFrames;
			}
		}
	}

	records.flush();
	if (includeFrames)
		frameMetadata.flush();
	bool writeFailed = !records.good() ||
		(includeFrames && !frameMetadata.good());
	if (records.is_open())
	{
		records.close();
		writeFailed = writeFailed || records.fail();
	}
	if (frameMetadata.is_open())
	{
		frameMetadata.close();
		writeFailed = writeFailed || frameMetadata.fail();
	}
	if (writeFailed)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_writeFailures;
	}
	WriteCaptureSummary(directory, includeFrames);
}

void AnalysisDiagnosticTrace::WriteCaptureSummary(
	const std::string& directory,
	bool includeFrames) noexcept
{
	uint64_t sessionAcceptedRecords = 0;
	uint64_t sessionRecordsWritten = 0;
	uint64_t sessionFramesWritten = 0;
	uint64_t sessionFrameBytesWritten = 0;
	uint64_t offered = 0, samplingSkipped = 0, missed = 0, samplingStep = 1;
	uint64_t lifetimeHistoryOverwrites = 0;
	uint64_t lifetimeWriteFailures = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		sessionAcceptedRecords = m_captureAcceptedRecords;
		sessionRecordsWritten = m_recordsWritten;
		sessionFramesWritten = m_framesWritten;
		sessionFrameBytesWritten = m_frameBytesWritten;
		offered = m_captureFrameOffered; samplingSkipped = m_captureFrameSamplingSkipped;
		missed = m_captureFrameMissed; samplingStep = m_captureFrameSamplingStep;
		lifetimeHistoryOverwrites = m_historyOverwrites;
		lifetimeWriteFailures = m_writeFailures;
	}
	const uint64_t lifetimeDroppedRecords =
		m_droppedRecords.load(std::memory_order_relaxed);
	const uint64_t lifetimeDroppedFrames =
		m_droppedFrames.load(std::memory_order_relaxed);
	const size_t recordHistoryCapacity = std::min(
		m_options.maxRecordHistory, RECORD_HISTORY_CAPACITY);
	const uint64_t maxFrameBytes = std::min(
		m_options.maxFrameBytes, MAX_FRAME_BYTES);
	const uint64_t maxCaptureFrameBytes = std::min(
		m_options.maxCaptureFrameBytes, MAX_CAPTURE_FRAME_BYTES);
	const size_t maxOutstandingFrames = std::min(
		m_options.maxOutstandingFrames, FRAME_QUEUE_CAPACITY);

	try
	{
		std::ofstream summary(
			(directory + "\\capture-summary.json").c_str(),
			std::ios::out | std::ios::binary | std::ios::trunc);
		if (!summary.is_open())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_writeFailures;
			return;
		}
		summary << "{\"schemaVersion\":1,\"includeFrames\":"
			<< (includeFrames ? "true" : "false")
			<< ",\"session\":{\"acceptedRecords\":"
			<< sessionAcceptedRecords
			<< ",\"recordsWritten\":" << sessionRecordsWritten
			<< ",\"framesWritten\":" << sessionFramesWritten
			<< ",\"frameBytesWritten\":" << sessionFrameBytesWritten
            << ",\"nativeFramesOffered\":" << offered
            << ",\"nativeSamplingSkipped\":" << samplingSkipped
            << ",\"nativeSelectedMissed\":" << missed
            << ",\"nativeSamplingSourceStep\":" << samplingStep
			<< "},\"lifetime\":{\"droppedRecords\":"
			<< lifetimeDroppedRecords
			<< ",\"droppedFrames\":" << lifetimeDroppedFrames
			<< ",\"historyOverwrites\":" << lifetimeHistoryOverwrites
			<< ",\"writeFailures\":" << lifetimeWriteFailures
			<< "},\"limits\":{\"preRollMs\":" << m_preRollMs
			<< ",\"captureMs\":" << m_captureMs
			<< ",\"recordHistoryCapacity\":" << recordHistoryCapacity
			<< ",\"maxCaptureRecords\":" << m_captureRecordLimit
			<< ",\"maxFrameBytes\":" << maxFrameBytes
			<< ",\"maxCaptureFrameBytes\":" << maxCaptureFrameBytes
			<< ",\"maxOutstandingFrames\":" << maxOutstandingFrames
			<< "},\"lifetimeCounterMeaning\":";
		WriteJsonString(summary,
			"Lifetime counters are cumulative since recorder construction and may "
			"include events outside this capture.");
        summary << ",\"nativeSamplingMode\":";
        WriteJsonString(summary,m_nativeSampling.load(std::memory_order_relaxed)==AnalysisDiagnosticNativeSampling::ConsecutiveBurst?
            "consecutive-burst":"uniform");
        if(m_nativeSampling.load(std::memory_order_relaxed)==AnalysisDiagnosticNativeSampling::ConsecutiveBurst) {
            std::lock_guard<std::mutex> lock(m_mutex);
            const uint64_t lockMisses=m_nativeBurstLockMisses.load(std::memory_order_relaxed);
            const uint64_t writesFailed=m_writeFailures-m_captureWriteFailuresAtStart;
            const bool complete=m_nativeBurstWindowEnded && m_nativeBurstTarget>0 &&
                m_framesWritten==m_nativeBurstTarget && m_captureFrameIndex==m_nativeBurstTarget &&
                !m_nativeBurstSourceGaps && !m_captureFrameMissed && !lockMisses && !writesFailed;
            summary << ",\"nativeBurst\":{\"targetFrames\":" << m_nativeBurstTarget
                << ",\"firstSource\":" << m_nativeBurstFirstSource << ",\"lastSource\":" << m_nativeBurstLastSource
                << ",\"lastOfferedSource\":" << m_nativeBurstLastOffered
                << ",\"lastOfferedAccepted\":" << m_nativeBurstLastAccepted
                << ",\"lastOfferedTimestamp\":" << m_nativeBurstLastTimestamp
                << ",\"offeredUnique\":" << m_nativeBurstOffered << ",\"sourceGaps\":" << m_nativeBurstSourceGaps
                << ",\"duplicateOffers\":" << m_nativeBurstDuplicates << ",\"lockAdmissionMisses\":" << lockMisses
                << ",\"admissionMisses\":" << m_captureFrameMissed << ",\"sessionWriteFailures\":" << writesFailed
                << ",\"complete\":" << (complete?"true":"false") << ",\"windowEnded\":" << (m_nativeBurstWindowEnded?"true":"false")
                << ",\"sourceCounter\":";
            WriteJsonString(summary,m_nativeBurstUsesPhysicalSource?"sourceFrameNumber":"acceptedSequence");
            summary << ",\"endReason\":";WriteJsonString(summary,m_nativeBurstEndReason);
            const auto& f=m_nativeBurstFirst;
            summary << ",\"firstIdentity\":{\"transportGeneration\":" << f.transportGeneration
                << ",\"acceptedSequence\":" << f.acceptedSequence << ",\"sourceFrameNumber\":" << f.sourceFrameNumber
                << ",\"captureTimestamp\":" << f.captureTimestamp << ",\"sourceFormatGeneration\":" << f.sourceFormatGeneration
                << ",\"viewportGeneration\":" << f.viewportGeneration << ",\"rendererGeneration\":" << f.rendererGeneration
                << ",\"analysisDomainKey\":" << f.analysisDomainKey << ",\"policyGeneration\":" << f.policyGeneration
                << ",\"continuityGeneration\":" << f.continuityGeneration << "}}";
        }
		summary << "}\n";
		summary.flush();
		bool writeFailed = !summary.good();
		summary.close();
		writeFailed = writeFailed || summary.fail();
		if (writeFailed)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_writeFailures;
		}
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_writeFailures;
	}
}

void AnalysisDiagnosticTrace::WriteRecord(
	std::ostream& output,
	const AnalysisDiagnosticRecordV1& record)
{
	output << "{\"schema\":" << record.schemaVersion
		<< ",\"kind\":" << static_cast<uint32_t>(record.kind)
		<< ",\"sequence\":" << record.recordSequence
		<< ",\"timestampNs\":" << record.timestampNs
		<< ",\"frameIdentity\":" << record.frameIdentity
		<< ",\"rendererInstanceKey\":" << record.rendererInstanceKey
		<< ",\"acceptedSequence\":" << record.acceptedSequence
		<< ",\"transportGeneration\":" << record.transportGeneration
		<< ",\"sourceFrameNumber\":" << record.sourceFrameNumber
		<< ",\"captureTimestamp\":" << record.captureTimestamp
		<< ",\"continuityGeneration\":" << record.continuityGeneration
		<< ",\"analysisDomainKey\":" << record.analysisDomainKey
		<< ",\"sourceFormat\":" << record.sourceFormat
		<< ",\"sourceFormatGeneration\":" << record.sourceFormatGeneration
		<< ",\"analysisFormat\":" << record.analysisFormat
		<< ",\"viewportGeneration\":" << record.viewportGeneration
		<< ",\"rendererGeneration\":" << record.rendererGeneration
		<< ",\"policyGeneration\":" << record.policyGeneration
		<< ",\"nearBarDistance\":" << record.nearBarDistance
		<< ",\"optimizationMode\":" << record.optimizationMode
		<< ",\"frameDuration100ns\":" << record.frameDuration100ns
		<< ",\"scriptHint\":" << record.scriptHint
		<< ",\"discontinuity\":" << record.discontinuity
		<< ",\"cadenceRepeat\":" << record.cadenceRepeat
		<< ",\"rgbBitAlignment\":" << record.rgbBitAlignment
		<< ",\"sourceWidth\":" << record.sourceWidth
		<< ",\"sourceHeight\":" << record.sourceHeight
		<< ",\"analysisWidth\":" << record.analysisWidth
		<< ",\"analysisHeight\":" << record.analysisHeight
		<< ",\"viewport\":[" << record.viewportLeft << ','
		<< record.viewportTop << ',' << record.viewportRight << ','
		<< record.viewportBottom << "],\"previous\":[";
	for (size_t index = 0; index < 4; ++index)
		output << (index == 0 ? "" : ",") << record.previous[index];
	output << "],\"measured\":[";
	for (size_t index = 0; index < 4; ++index)
		output << (index == 0 ? "" : ",") << record.measured[index];
	output << "],\"candidate\":[";
	for (size_t index = 0; index < 4; ++index)
		output << (index == 0 ? "" : ",") << record.candidate[index];
	output << "],\"published\":[";
	for (size_t index = 0; index < 4; ++index)
		output << (index == 0 ? "" : ",") << record.published[index];
	output << "],\"displayed\":[";
	for (size_t index = 0; index < 4; ++index)
		output << (index == 0 ? "" : ",") << record.displayed[index];
	output << "],\"subtitle\":[";
	for (size_t index = 0; index < 4; ++index)
		output << (index == 0 ? "" : ",") << record.subtitle[index];
	output << "],\"counters\":[";
	for (size_t index = 0; index < 8; ++index)
		output << (index == 0 ? "" : ",") << record.counters[index];
	output << "],\"metrics\":[";
	for (size_t index = 0; index < 24; ++index)
	{
		if (index != 0)
			output << ',';
		if (std::isfinite(record.metrics[index]))
			output << std::setprecision(17) << record.metrics[index];
		else
			output << "null";
	}
	output << "],\"fullScans\":" << record.fullScans
		<< ",\"requestMisses\":" << record.requestMisses
		<< ",\"pendingFrames\":" << record.pendingFrames
		<< ",\"acceptedSeeds\":" << record.acceptedSeeds
		<< ",\"flags\":" << record.flags << ",\"resultCode\":"
		<< record.resultCode << ",\"stage\":";
	WriteJsonString(output, record.stage);
	output << ",\"detail\":";
	WriteJsonString(output, record.detail);
	output << ",\"reason\":";
	WriteJsonString(output, record.reason);
	output << "}\n";
}

void AnalysisDiagnosticTrace::WriteFrame(
	std::ostream& metadata,
	const std::string& directory,
	QueuedFrame& frame)
{
	bool success = false;
	std::string filename;
	try
	{
		std::ostringstream name;
		name << "frame-" << std::setw(6) << std::setfill('0')
			<< frame.captureIndex << ".bin";
		filename = name.str();
		const std::string framePath = directory + "\\" + filename;
		std::ofstream pixels(framePath.c_str(),
			std::ios::out | std::ios::binary | std::ios::trunc);
		if (pixels.is_open())
		{
			pixels.write(reinterpret_cast<const char*>(frame.metadata.data),
				static_cast<std::streamsize>(frame.metadata.dataBytes));
			pixels.flush();
			success = pixels.good();
			pixels.close();
		}
		if (success)
		{
			const std::string sidecarPath = directory + "\\" +
				filename.substr(0, filename.size() - 4) + ".txt";
			std::ofstream sidecar(sidecarPath.c_str(),
				std::ios::out | std::ios::binary | std::ios::trunc);
			if (sidecar.is_open())
			{
				sidecar << frame.metadata.width << ' ' << frame.metadata.height << ' '
					<< frame.metadata.stride0 << ' ' << frame.metadata.stride1 << ' '
					<< frame.metadata.format << ' ' << frame.metadata.encoding << ' '
					<< frame.metadata.colorspace << ' ' << frame.metadata.top << ' '
					<< frame.metadata.bottom << ' ' << frame.metadata.scriptHint << ' '
                    << frame.metadata.analysisSampleRange << '\n'
					<< "identity_v1 " << frame.metadata.transportGeneration << ' '
					<< frame.metadata.acceptedSequence << ' '
					<< frame.metadata.sourceFrameNumber << ' '
					<< frame.metadata.captureTimestamp << ' '
					<< frame.metadata.sourceFormatGeneration << ' '
					<< frame.metadata.viewportGeneration << ' '
					<< frame.metadata.rendererGeneration << ' '
					<< frame.metadata.analysisDomainKey << ' '
					<< frame.metadata.policyGeneration << ' '
					<< frame.metadata.continuityGeneration << ' '
					<< frame.metadata.nearBarDistance << ' '
					<< frame.metadata.optimizationMode << ' '
					<< frame.metadata.frameDuration100ns << ' '
					<< frame.metadata.discontinuity << ' '
					<< frame.metadata.cadenceRepeat << ' '
					<< frame.metadata.rgbBitAlignment << '\n';
				sidecar.flush();
				success = sidecar.good();
			}
			else
				success = false;
		}
		if (success)
		{
			metadata << "{\"index\":" << frame.captureIndex << ",\"file\":";
			WriteJsonString(metadata, filename.c_str());
			metadata << ",\"bytes\":" << frame.metadata.dataBytes
				<< ",\"width\":" << frame.metadata.width
				<< ",\"height\":" << frame.metadata.height
				<< ",\"acceptedSequence\":" << frame.metadata.acceptedSequence
				<< ",\"sourceFrameNumber\":" << frame.metadata.sourceFrameNumber
				<< ",\"analysisDomainKey\":" << frame.metadata.analysisDomainKey
				<< "}\n";
			success = metadata.good();
		}
	}
	catch (...)
	{
		success = false;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	if (success)
	{
		++m_framesWritten;
		m_frameBytesWritten += frame.metadata.dataBytes;
	}
	else
	{
		++m_writeFailures;
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
	}
}

std::string AnalysisDiagnosticTrace::MakeCaptureDirectory() const
{
	const std::string root = m_options.outputRoot.empty() ?
		DefaultOutputRoot() : m_options.outputRoot;
	SYSTEMTIME now = {};
	GetLocalTime(&now);
	const uint64_t sequence =
		g_directorySequence.fetch_add(1, std::memory_order_relaxed) + 1;
	std::ostringstream name;
	name << "analysis-" << std::setfill('0') << std::setw(4) << now.wYear
		<< std::setw(2) << now.wMonth << std::setw(2) << now.wDay << '-'
		<< std::setw(2) << now.wHour << std::setw(2) << now.wMinute
		<< std::setw(2) << now.wSecond << '-' << GetCurrentProcessId() << '-'
		<< sequence;
	return root + "\\" + name.str();
}

uint64_t AnalysisDiagnosticTrace::NowMs() const noexcept
{
	return GetTickCount64();
}
