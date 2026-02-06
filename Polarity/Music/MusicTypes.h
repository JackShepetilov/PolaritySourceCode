// MusicTypes.h
// Core types for the dynamic music system

#pragma once

#include "CoreMinimal.h"
#include "MusicTypes.generated.h"

class USoundWave;

/**
 * Current state of the music player
 */
UENUM(BlueprintType)
enum class EMusicPlayerState : uint8
{
	Stopped,    // Music is not playing
	Playing,    // Music is playing normally
	FadingIn,   // Fading in (first entry into MIB)
	FadingOut   // Fading out (entered EMB)
};

/**
 * Single part of a music track (e.g., "intro", "heavy_loop_1", "calm_bridge")
 */
USTRUCT(BlueprintType)
struct FMusicPart
{
	GENERATED_BODY()

	/** Unique identifier for this part */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	FName PartID;

	/** The actual sound to play */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TObjectPtr<USoundWave> Sound;

	/** Volume multiplier for this specific part (0.0 - 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Volume = 1.0f;

	/**
	 * Parts to transition to when this part ends AND player is in MIB (intense zone).
	 * One will be chosen randomly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TArray<FName> NextPartsIntense;

	/**
	 * Parts to transition to when this part ends AND player is NOT in MIB (calm zone).
	 * One will be chosen randomly.
	 * If empty, NextPartsIntense will be used instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TArray<FName> NextPartsCalm;

	bool IsValid() const
	{
		return !PartID.IsNone() && Sound != nullptr;
	}
};
