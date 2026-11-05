#pragma once
#include <memory>

struct SubtitleCurrentCueProofPublication;
// One exact-source completion, owned by the existing queued source payload.
// Only the worker can publish verified evidence; readers never acquire its job mutex.
class SubtitleCurrentCueProofSlot {
    friend class SubtitleMeasurementWorker;
    std::shared_ptr<const SubtitleCurrentCueProofPublication> published;
};
