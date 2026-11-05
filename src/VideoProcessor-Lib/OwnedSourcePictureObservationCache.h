#pragma once
#include <SourcePictureMeasurement.h>
#include <SourcePictureEquivalenceToken.h>
#include <ActivePictureDecisionTimeline.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>

// Worker-local source geometry history, never movie/caption/render pixels.
// Private snapshots do not retain capture-driver buffers. The pixel allocation
// cap is per cache/worker, not process-wide; fact vectors/allocator overhead are
// separate. Output viewport/style does not change source-pixel interpretation.
class OwnedSourcePictureObservationCache {
public:
    static constexpr size_t MaximumSourceBytes=64u*1024u*1024u;
    static constexpr size_t MaximumRetainedBytes=128u*1024u*1024u;
    static constexpr size_t MaximumEntries=4;
    static constexpr size_t MaximumRecurringSourceBytes=MaximumRetainedBytes/MaximumEntries;
    ~OwnedSourcePictureObservationCache() {Clear();}
    void Clear() {
        for(auto& entry:entries_) {Revoke(entry);entry=Entry{};}
        history_={};context_={};contextIdentity_={};contextKnown_=false;continuity_=0;
        retainedBytes_=0;clock_=0;
    }
    bool Store(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity,
        std::shared_ptr<const SourcePictureObservation> facts,uint64_t continuityGeneration=0,
        bool recurringOnly=false,SourcePictureEquivalenceToken* token=nullptr) {
        if(token)*token={};
        if(!Eligible(source,identity) || !facts || !facts->complete ||
            facts->outcome==SourcePictureOutcome::NotMeasured || facts->sourceGeneration!=source.generation ||
            facts->width!=source.width || facts->height!=source.height) {Clear();return false;}
        // Keep the complete bounded recurring working set affordable. Larger
        // views may use explicit one-shot cache storage, but cannot make the
        // production four-phase history copy/evict on every source.
        if(recurringOnly && source.dataBytes>MaximumRecurringSourceBytes) {Clear();return false;}
        if(contextKnown_ && !CompatibleContext(source,identity,continuityGeneration))Clear();
        if(!contextKnown_) {
            context_=source;context_.data=nullptr;context_.backingOwner.reset();
            contextIdentity_=identity;continuity_=continuityGeneration;contextKnown_=true;
        }
        const auto signature=QuickSignature(source);
        bool recurring=false;History* history=nullptr;
        for(auto& old:history_)if(old.valid && old.signature==signature) {history=&old;break;}
        if(history) {
            recurring=identity.acceptedSequence>history->sequence;
            history->sequence=(std::max)(history->sequence,identity.acceptedSequence);
        } else {
            history=&*std::min_element(history_.begin(),history_.end(),[](const History& a,const History& b) {
                return (a.valid?a.sequence:0)<(b.valid?b.sequence:0);
            });
            *history={true,signature,identity.acceptedSequence};
        }
        for(auto& entry:entries_)if(entry.facts && entry.signature==signature) {
            // Recurring admission follows a failed lookup in production. A
            // matching signature may be a collision: decline storage immediately,
            // never treat it as equality or repeat Find's expensive byte comparison.
            if(recurringOnly)return false;
            // Signatures only reject. Even a matching address or owner cannot
            // replace full-byte equality, including all source padding.
            if(std::memcmp(source.data,entry.pixels.get(),source.dataBytes)==0) {
                if(identity.acceptedSequence<entry.identity.acceptedSequence)return false;
                entry.identity=identity;entry.facts=std::move(facts);entry.lastUse=++clock_;Issue(entry,source,identity,token);return true;
            }
        }
        // A moving source with a new signature does not allocate/copy. Repeated
        // signatures with different bytes cannot cause an endless copy stream.
        if(recurringOnly && !recurring)return false;
        auto evict=[&](Entry& entry) {Revoke(entry);retainedBytes_-=entry.bytes;entry=Entry{};};
        while(retainedBytes_+source.dataBytes>MaximumRetainedBytes) {
            Entry* oldest=nullptr;
            for(auto& entry:entries_)if(entry.facts && (!oldest || entry.lastUse<oldest->lastUse))oldest=&entry;
            if(!oldest)return false;evict(*oldest);
        }
        Entry* slot=nullptr;
        for(auto& entry:entries_)if(!entry.facts) {slot=&entry;break;}
        if(!slot) {
            slot=&*std::min_element(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b) {return a.lastUse<b.lastUse;});
            evict(*slot);
        }
        std::unique_ptr<uint8_t[]> pixels(new(std::nothrow) uint8_t[source.dataBytes]);
        if(!pixels)return false;
        std::memcpy(pixels.get(),source.data,source.dataBytes);
        slot->lifetime=std::make_shared<SourcePictureEquivalenceToken::EntryLifetime>();
        slot->pixels=std::move(pixels);slot->bytes=source.dataBytes;slot->identity=identity;
        slot->facts=std::move(facts);slot->signature=signature;slot->lastUse=++clock_;
        retainedBytes_+=source.dataBytes;++snapshotCopies_;Issue(*slot,source,identity,token);return true;
    }
    std::shared_ptr<const SourcePictureObservation> Find(const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity& identity,uint64_t continuityGeneration=0,
        SourcePictureEquivalenceToken* token=nullptr) const {
        if(token)*token={};
        if(!contextKnown_ || !Eligible(source,identity) || !CompatibleContext(source,identity,continuityGeneration))return {};
        const auto signature=QuickSignature(source);
        for(const auto& entry:entries_) {
            if(!entry.facts || signature!=entry.signature || identity.acceptedSequence<entry.identity.acceptedSequence)continue;
            if(std::memcmp(source.data,entry.pixels.get(),source.dataBytes)!=0)continue;
            entry.lastUse=++clock_;
            auto rebound=std::make_shared<SourcePictureObservation>(*entry.facts);
            rebound->sourceGeneration=source.generation;
            Issue(entry,source,identity,token);return rebound;
        }
        return {};
    }
    size_t RetainedSourceBytes() const {return retainedBytes_;}
    size_t EntryCount() const {size_t count=0;for(const auto& entry:entries_)count+=bool(entry.facts);return count;}
    uint64_t SnapshotCopyCount() const {return snapshotCopies_;}
private:
    struct Entry {
        std::shared_ptr<SourcePictureEquivalenceToken::EntryLifetime> lifetime;
        std::unique_ptr<uint8_t[]> pixels;
        std::shared_ptr<const SourcePictureObservation> facts;
        ActivePictureFrameIdentity identity;
        size_t bytes=0;uint64_t signature=0;mutable uint64_t lastUse=0;
    };
    static void Revoke(Entry& entry) {
        if(entry.lifetime)entry.lifetime->active.store(false,std::memory_order_release);
    }
    static void Issue(const Entry& entry,const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity& identity,SourcePictureEquivalenceToken* token) {
        if(!token || !entry.lifetime || (source.generation!=identity.transportGeneration &&
            source.generation!=identity.sourceFormatGeneration))return;
        token->entry_=entry.lifetime;token->identity_=identity;
    }
    struct History {bool valid=false;uint64_t signature=0,sequence=0;};
    static bool Eligible(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity) {
        return source.backingOwner && source.dataBytes && source.dataBytes<=MaximumSourceBytes && source.IsValid() && source.generation &&
            identity.transportGeneration && identity.sourceFormatGeneration && identity.analysisDomainKey && identity.acceptedSequence;
    }
    static uint64_t QuickSignature(const AnalysisLumaSource& source) {
        // Bounded rejection filter, deliberately not an equality certificate.
        const size_t width=(std::min)(source.dataBytes,sizeof(uint64_t));
        uint64_t hash=1469598103934665603ull;
        for(size_t i=0;i<128;++i) {
            const size_t offset=i*(source.dataBytes-width)/127;uint64_t word=0;
            std::memcpy(&word,source.data+offset,width);hash^=word;hash*=1099511628211ull;
        }
        return hash;
    }
    bool CompatibleContext(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity,uint64_t continuity) const {
        return continuity==continuity_ && identity.transportGeneration==contextIdentity_.transportGeneration &&
            identity.sourceFormatGeneration==contextIdentity_.sourceFormatGeneration && identity.analysisDomainKey==contextIdentity_.analysisDomainKey &&
            source.dataBytes==context_.dataBytes && source.width==context_.width && source.height==context_.height &&
            source.rowBytes==context_.rowBytes && source.chromaRowBytes==context_.chromaRowBytes &&
            source.format==context_.format && source.encoding==context_.encoding && source.colorspace==context_.colorspace &&
            source.rgbBitAlignment==context_.rgbBitAlignment && source.range==context_.range;
    }
    std::array<Entry,MaximumEntries> entries_;
    std::array<History,MaximumEntries> history_{};
    AnalysisLumaSource context_;
    ActivePictureFrameIdentity contextIdentity_;
    uint64_t continuity_=0,snapshotCopies_=0;mutable uint64_t clock_=0;
    size_t retainedBytes_=0;bool contextKnown_=false;
};
