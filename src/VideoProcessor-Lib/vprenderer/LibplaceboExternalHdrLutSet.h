#pragma once

#include <string>
#include <cstdint>
#include <utility>

#include <vprenderer/LibplaceboDisplayLut.h>
#include <vprenderer/LibplaceboExternalHdrLutPolicy.h>


namespace LibplaceboExternalHdrLut
{
	struct SlotDeclaration
	{
		// path is the canonical/safely resolved load target. configuredPath keeps
		// the user's declaration visible when preflight rejects that target.
		std::string path;
		std::string constrainedBaseDirectory;
		std::string configuredPath;
		bool pathRejected = false;
	};

	struct Declarations
	{
		SlotDeclaration bt709;
		SlotDeclaration p3D65;
		SlotDeclaration bt2020;
	};

	class SlotResource
	{
	public:
		SlotResource() = default;
		~SlotResource() { pl_lut_free(&result_.lut); }
		SlotResource(const SlotResource&) = delete;
		SlotResource& operator=(const SlotResource&) = delete;

		SlotResource(SlotResource&& other) noexcept
		{
			MoveFrom(other);
		}

		SlotResource& operator=(SlotResource&& other) noexcept
		{
			if (this != &other)
			{
				pl_lut_free(&result_.lut);
				MoveFrom(other);
			}
			return *this;
		}

		bool Configured() const { return !configuredPath_.empty(); }
		bool Available() const
		{
			return result_.status == LibplaceboDisplayLut::Status::ACTIVE &&
				result_.lut != nullptr;
		}
		const std::string& ConfiguredPath() const { return configuredPath_; }
		const LibplaceboDisplayLut::LoadResult& Result() const { return result_; }
		const pl_custom_lut* Lut() const { return result_.lut; }
		bool SameContentIdentity(const SlotResource& other) const
		{
			const int dimension = result_.lut ? result_.lut->size[0] : 0;
			const int otherDimension = other.result_.lut ?
				other.result_.lut->size[0] : 0;
			return result_.status == other.result_.status &&
				result_.rejection == other.result_.rejection &&
				result_.fileBytes == other.result_.fileBytes &&
				result_.canonicalPath == other.result_.canonicalPath &&
				result_.contentSha256 == other.result_.contentSha256 &&
				dimension == otherDimension;
		}

	private:
		friend class CandidateSet;
		SlotResource(const SlotDeclaration& declaration, pl_log log) :
			configuredPath_(declaration.configuredPath.empty() ?
				declaration.path : declaration.configuredPath)
		{
			if (declaration.pathRejected)
			{
				result_.status = LibplaceboDisplayLut::Status::REJECTED;
				result_.rejection =
					LibplaceboDisplayLut::Rejection::PATH_OUTSIDE_BASE;
				return;
			}
			result_ = LibplaceboDisplayLut::Load(log, declaration.path,
				declaration.constrainedBaseDirectory);
		}

		void MoveFrom(SlotResource& other)
		{
			configuredPath_ = std::move(other.configuredPath_);
			result_ = std::move(other.result_);
			other.result_.lut = nullptr;
			other.result_.status = LibplaceboDisplayLut::Status::DISABLED;
			other.result_.rejection = LibplaceboDisplayLut::Rejection::NONE;
		}

		std::string configuredPath_;
		LibplaceboDisplayLut::LoadResult result_;
	};

	// A complete off-side parse of all three declarations. Callers commit or
	// discard this object as one generation; no active set is mutated while any
	// file is being opened, hashed, or parsed.
	class CandidateSet
	{
	public:
		CandidateSet() = default;
		CandidateSet(const CandidateSet&) = delete;
		CandidateSet& operator=(const CandidateSet&) = delete;
		CandidateSet(CandidateSet&&) noexcept = default;
		CandidateSet& operator=(CandidateSet&&) noexcept = default;

		// The caller allocates this monotonic transaction id before loading starts.
		// Carrying it inside the candidate prevents delayed work from being
		// relabeled with a newer id when it eventually reaches the render thread.
		static CandidateSet Load(pl_log log, const Declarations& declarations,
			uint64_t transactionGeneration)
		{
			CandidateSet candidate;
			candidate.transactionGeneration_ = transactionGeneration;
			candidate.bt709_ = SlotResource(declarations.bt709, log);
			candidate.p3D65_ = SlotResource(declarations.p3D65, log);
			candidate.bt2020_ = SlotResource(declarations.bt2020, log);
			return candidate;
		}
		uint64_t TransactionGeneration() const { return transactionGeneration_; }

		AvailableSlots Availability() const
		{
			return { bt709_.Available(), p3D65_.Available(),
				bt2020_.Available() };
		}
		bool HasAvailableSlot() const
		{
			const AvailableSlots available = Availability();
			return available.bt709 || available.p3D65 || available.bt2020;
		}

		const SlotResource& Resource(Slot slot) const
		{
			switch (slot)
			{
			case Slot::BT709: return bt709_;
			case Slot::P3_D65: return p3D65_;
			case Slot::BT2020: return bt2020_;
			default: return none_;
			}
		}

		bool SameContentIdentity(const CandidateSet& other) const
		{
			return bt709_.SameContentIdentity(other.bt709_) &&
				p3D65_.SameContentIdentity(other.p3D65_) &&
				bt2020_.SameContentIdentity(other.bt2020_);
		}

		bool RegressesAvailableSlot(const CandidateSet& active) const
		{
			return (active.bt709_.Available() && !bt709_.Available()) ||
				(active.p3D65_.Available() && !p3D65_.Available()) ||
				(active.bt2020_.Available() && !bt2020_.Available());
		}

	private:
		uint64_t transactionGeneration_ = 0;
		SlotResource none_;
		SlotResource bt709_;
		SlotResource p3D65_;
		SlotResource bt2020_;
	};

	enum class CommitDisposition
	{
		COMMIT_USABLE_GENERATION,
		COMMIT_INTERNAL_FALLBACK,
		RETAIN_UNCHANGED_GENERATION,
		RETAIN_LAST_KNOWN_GOOD,
		REJECT_STALE_TRANSACTION
	};

	enum class ReloadIntent
	{
		CONTRACT_CHANGE,
		SAME_CONTRACT_CONTENT_CHECK
	};

	struct ResolvedResource
	{
		Selection selection;
		const pl_custom_lut* lut = nullptr;
		uint64_t expectedTransactionGeneration = 0;
		uint64_t transactionGeneration = 0;
		uint64_t resourceGeneration = 0;
	};

	// Commit and Resolve are render-thread safe-point operations. A complete
	// current transaction always replaces the entire three-slot generation:
	// partial-valid sets commit without mixing old slots, while zero-valid sets
	// deliberately publish internal fallback so an old profile's LUT cannot
	// remain authorized. Monotonic transaction ids reject delayed work.
	class ActiveSet
	{
	public:
		// Reserve before file I/O begins. A completion is accepted only if no newer
		// request has superseded it, even when that newer load is still in flight.
		bool BeginRequest(uint64_t transactionGeneration)
		{
			if (transactionGeneration <= latestRequestGeneration_)
				return false;
			latestRequestGeneration_ = transactionGeneration;
			return true;
		}

		CommitDisposition Commit(CandidateSet&& candidate)
		{
			const uint64_t transactionGeneration =
				candidate.TransactionGeneration();
			if (transactionGeneration < latestRequestGeneration_ ||
				transactionGeneration <= latestProcessedGeneration_)
				return CommitDisposition::REJECT_STALE_TRANSACTION;
			if (transactionGeneration > latestRequestGeneration_)
				latestRequestGeneration_ = transactionGeneration;
			latestProcessedGeneration_ = transactionGeneration;
			return CommitAccepted(std::move(candidate));
		}

		CommitDisposition CommitReload(CandidateSet&& candidate,
			ReloadIntent intent)
		{
			// RETAIN_* advances request/processed watermarks but deliberately leaves
			// TransactionGeneration() on the authorized last-known-good resource.
			// Callers must keep their expected generation equal to that active value.
			const uint64_t transactionGeneration =
				candidate.TransactionGeneration();
			if (transactionGeneration != latestRequestGeneration_ ||
				transactionGeneration <= latestProcessedGeneration_)
				return CommitDisposition::REJECT_STALE_TRANSACTION;
			latestProcessedGeneration_ = transactionGeneration;
			if (intent == ReloadIntent::SAME_CONTRACT_CONTENT_CHECK)
			{
				if (candidate.SameContentIdentity(resources_))
					return CommitDisposition::RETAIN_UNCHANGED_GENERATION;
				if (candidate.RegressesAvailableSlot(resources_))
					return CommitDisposition::RETAIN_LAST_KNOWN_GOOD;
			}
			return CommitAccepted(std::move(candidate));
		}

		uint64_t LatestRequestGeneration() const
		{
			return latestRequestGeneration_;
		}
		uint64_t LatestProcessedGeneration() const
		{
			return latestProcessedGeneration_;
		}

	private:
		CommitDisposition CommitAccepted(CandidateSet&& candidate)
		{
			const uint64_t transactionGeneration =
				candidate.TransactionGeneration();
			const bool usable = candidate.HasAvailableSlot();
			resources_ = std::move(candidate);
			transactionGeneration_ = transactionGeneration;
			++resourceGeneration_;
			runtimeRejected_ = false;
			return usable ? CommitDisposition::COMMIT_USABLE_GENERATION :
				CommitDisposition::COMMIT_INTERNAL_FALLBACK;
		}

	public:
		ResolvedResource Resolve(uint64_t expectedTransactionGeneration,
			ToneMappingMode mode, bool inputIsPq, Primaries sourcePrimaries) const
		{
			ResolvedResource resolved;
			resolved.expectedTransactionGeneration = expectedTransactionGeneration;
			resolved.transactionGeneration = transactionGeneration_;
			resolved.resourceGeneration = resourceGeneration_;
			if (mode == ToneMappingMode::EXTERNAL_3DLUT &&
				expectedTransactionGeneration != transactionGeneration_)
			{
				resolved.selection = { EffectiveMode::PIXEL_SHADERS,
					MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
					"external 3D LUT profile generation is not ready" };
				return resolved;
			}
			if (mode == ToneMappingMode::EXTERNAL_3DLUT && runtimeRejected_)
			{
				resolved.selection = { EffectiveMode::PIXEL_SHADERS,
					MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
					"external 3D LUT generation failed during rendering" };
				return resolved;
			}
			resolved.selection = Select(mode, inputIsPq, sourcePrimaries,
				resources_.Availability());
			if (resolved.selection.useExternalLut)
				resolved.lut = resources_.Resource(resolved.selection.slot).Lut();
			return resolved;
		}

		// A returned LUT is borrowed until the next Commit. Runtime integration
		// must Resolve, verify IsCurrent, copy the LUT descriptor into frame-local
		// render parameters, and call pl_render_image in one render-thread interval
		// in which Commit cannot run.
		bool IsCurrent(const ResolvedResource& resolved) const
		{
			return resolved.transactionGeneration == transactionGeneration_ &&
				resolved.resourceGeneration == resourceGeneration_;
		}

		bool RejectRuntimeGeneration(const ResolvedResource& resolved)
		{
			if (!resolved.lut || !IsCurrent(resolved))
				return false;
			runtimeRejected_ = true;
			return true;
		}

		uint64_t TransactionGeneration() const { return transactionGeneration_; }
		uint64_t ResourceGeneration() const { return resourceGeneration_; }
		const CandidateSet& Resources() const { return resources_; }

	private:
		CandidateSet resources_;
		uint64_t latestRequestGeneration_ = 0;
		uint64_t latestProcessedGeneration_ = 0;
		uint64_t transactionGeneration_ = 0;
		uint64_t resourceGeneration_ = 0;
		bool runtimeRejected_ = false;
	};
}
