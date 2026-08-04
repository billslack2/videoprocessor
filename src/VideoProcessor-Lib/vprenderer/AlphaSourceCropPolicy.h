#pragma once

#include <cstdint>
#include <string>

#include "ActivePictureTransitionModel.h"


namespace AlphaSourceCrop
{
	enum class BarContentEdge
	{
		NONE,
		TOP,
		BOTTOM,
	};

	// Dense bar inspection is presentation evidence, not format authority. When
	// both encoded bars contain samples, accept only the edge needing the larger
	// displacement. The shared active-picture detector remains responsible for
	// genuine two-edge format changes.
	BarContentEdge SelectVerticalBarContentEdge(
		float upperRequiredShift, float lowerRequiredShift);

	class AmbiguityHold
	{
	public:
		void Reset();
		void Observe(uint64_t currentTick, uint64_t sourceGeneration,
			bool hadCurrentTrustedCrop, bool trustedCropReaffirmed,
			ActivePictureClassification classification,
			uint64_t maximumHoldMs);
		bool IsActive(uint64_t currentTick,
			uint64_t sourceGeneration) const;

	private:
		uint64_t deadlineTick = 0;
		uint64_t ownerSourceGeneration = 0;
		bool eligibleAfterTrustedCrop = false;
	};

	struct Input
	{
		bool automaticCropEnabled = false;
		bool sharedGeometryAvailable = false;
		bool latestObservationSupportsCrop = false;
		bool sceneVerificationHoldActive = false;
		bool ambiguityHoldActive = false;
		bool latestObservationIsProvisional = false;
		bool latestObservationIsUnavailable = false;
		bool outwardPresentationActive = false;
		bool outwardExpansionAvailable = false;
		ActivePictureClassification classification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds geometry;
		ActivePictureBounds outwardExpansion;
		uint64_t geometrySourceGeneration = 0;
		uint64_t outwardExpansionSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		int rasterWidth = 0;
		int rasterHeight = 0;
	};

	struct Decision
	{
		ActivePictureBounds sourceBounds;
		bool applyCrop = false;
		bool outwardExpanded = false;
		std::string reason;
	};

	enum class ScenePresentationAction
	{
		WITHDRAW,
		KEEP_CURRENT,
		HOLD_SNAPSHOT,
	};

	struct SceneInput
	{
		bool geometryAvailable = false;
		bool geometryIsCurrentGeneration = false;
		bool latestEvidenceIsCurrent = false;
		bool latestObservationSupportsCrop = false;
		bool existingCropCanBeSnapshotted = false;
		ActivePictureClassification geometryClassification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureClassification latestClassification =
			ActivePictureClassification::UNAVAILABLE;
	};

	struct SceneDecision
	{
		ScenePresentationAction action =
			ScenePresentationAction::WITHDRAW;
		std::string reason;
	};

	struct SceneHoldInput
	{
		bool snapshotAvailable = false;
		bool nlsRequested = false;
		bool retainedMappingCompatible = false;
		uint64_t snapshotSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		uint64_t deadlineTick = 0;
		uint64_t currentTick = 0;
	};

	struct SceneHoldDecision
	{
		bool cropActive = false;
		bool nlsActive = false;
	};

	// Pure crop-authority boundary for Alpha. Observers and consumers may propose
	// geometry, but only a generation-current shared BAR_CROP_TRUSTED snapshot
	// reaffirmed by the latest symmetric observation can contract the source
	// rectangle. The user-visible Off state always returns the complete raster.
	Decision Evaluate(const Input& input);

	// A scene boundary resets candidate proof, but presentation is atomic:
	// matching trusted geometry keeps crop and NLS together; ambiguous or
	// near-black unavailable cut evidence may retain their existing snapshot
	// briefly; contradiction withdraws both.
	SceneDecision EvaluateSceneBoundary(const SceneInput& input);

	// Latch the bounded scene hold once per rendered frame. Crop and NLS consume
	// the same result so expiry, source replacement, and mapping changes cannot
	// split the presentation for one frame.
	SceneHoldDecision EvaluateSceneHold(const SceneHoldInput& input);
}
