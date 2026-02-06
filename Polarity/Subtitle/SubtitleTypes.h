// SubtitleTypes.h
// Core types for the subtitle system

#pragma once

#include "CoreMinimal.h"
#include "SubtitleTypes.generated.h"

class USoundBase;

/**
 * Single subtitle entry in a data asset
 */
USTRUCT(BlueprintType)
struct FSubtitleEntry
{
	GENERATED_BODY()

	/** Unique identifier for this subtitle (e.g., "boss_intro_1") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitle")
	FName ID;

	/** Localized subtitle text */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitle")
	FText Text;

	/** Sound asset - used ONLY to calculate duration (not played by subsystem) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitle")
	TSoftObjectPtr<USoundBase> Sound;

	/**
	 * Manual duration override in seconds.
	 * If > 0, this value is used instead of Sound duration.
	 * Useful for sounds with silence at the end or for text-only subtitles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitle", meta = (ClampMin = "0.0"))
	float DurationOverride = 0.0f;

	/** Speaker name (optional, e.g., "BOSS", "NARRATOR") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitle")
	FText Speaker;
};

/**
 * Request queued in the subtitle subsystem
 */
USTRUCT()
struct FSubtitleRequest
{
	GENERATED_BODY()

	/** Text to display */
	FText Text;

	/** Duration to show subtitle */
	float Duration = 0.0f;

	/** Speaker name (optional) */
	FText Speaker;

	/** Sound to play as 2D (optional, only used with ShowSubtitleWithSound) */
	TObjectPtr<USoundBase> SoundToPlay = nullptr;

	FSubtitleRequest() = default;

	FSubtitleRequest(const FText& InText, float InDuration, const FText& InSpeaker = FText::GetEmpty(), USoundBase* InSound = nullptr)
		: Text(InText)
		, Duration(InDuration)
		, Speaker(InSpeaker)
		, SoundToPlay(InSound)
	{
	}
};
