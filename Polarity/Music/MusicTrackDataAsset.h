// MusicTrackDataAsset.h
// Data asset containing all parts of a music track for a level section

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MusicTypes.h"
#include "MusicTrackDataAsset.generated.h"

/**
 * Data asset that defines a complete music track with multiple parts.
 * Each level section (e.g., main area, boss arena) should have its own track asset.
 */
UCLASS(BlueprintType)
class POLARITY_API UMusicTrackDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Display name for debugging */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track Info")
	FString TrackName;

	/** All parts that make up this track */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track Parts")
	TArray<FMusicPart> Parts;

	/** Which part to start playing when track begins */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track Parts")
	FName DefaultStartPart;

	// ==================== Fade Settings ====================

	/** Duration of fade in when music first starts (first MIB entry) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade Settings", meta = (ClampMin = "0.1"))
	float FadeInDuration = 1.5f;

	/** Duration of fade out when entering EMB */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade Settings", meta = (ClampMin = "0.1"))
	float FadeOutDuration = 2.0f;

	/** Duration of volume change when entering/exiting MIB (not first time) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade Settings", meta = (ClampMin = "0.1"))
	float IntensityChangeDuration = 0.5f;

	// ==================== Volume Settings ====================

	/** Volume multiplier when player is inside MIB (intense) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntenseVolumeMultiplier = 1.0f;

	/** Volume multiplier when player is outside MIB (calm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CalmVolumeMultiplier = 0.4f;

	// ==================== Public API ====================

	/** Find a part by ID. Returns nullptr if not found. */
	const FMusicPart* FindPart(FName PartID) const;

	/** Get the default start part. Returns nullptr if not found or not set. */
	const FMusicPart* GetStartPart() const;

	/** Check if this track asset is properly configured */
	bool IsValid() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
