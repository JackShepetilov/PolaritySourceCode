// StreamConfig.h
// PrimaryDataAsset configuring the global stream economy: viewer curve shape,
// rank multiplier from likes-per-second, donation rates, and per-category style scoring.
//
// One asset per game build (designer tunes curves in editor). Per-arena tweaks live in
// UStreamArenaConfig and override/multiply against this base.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StyleAction.h"
#include "StreamConfig.generated.h"

class UCurveFloat;
class UChatScript;

USTRUCT(BlueprintType)
struct FFreshnessFalloff
{
	GENERATED_BODY()

	/** Curve mapping repetition count (0 = first use of category, 1 = second, ...) -> freshness multiplier (e.g. 1.5 -> 0.2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freshness")
	TObjectPtr<UCurveFloat> RepetitionMultiplier;

	/** Time window in seconds within which repeats count. Older actions decay out of the buffer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freshness")
	float WindowSeconds = 6.0f;
};

UCLASS(BlueprintType)
class POLARITY_API UStreamConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==================== Viewer math ====================

	/** Peak viewer target before time-curve and rank multiplier (e.g. 50000). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Viewers")
	int32 BasePopulation = 20000;

	/** Time-of-run shape: input = seconds since run start, output = [0..1] curve shape. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Viewers")
	TObjectPtr<UCurveFloat> BaselineCurve;

	/** Maps current LikesPerSecond -> rank multiplier on viewer target (e.g. 0.3 .. 5.0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Viewers")
	TObjectPtr<UCurveFloat> RankMultiplierCurve;

	/** How fast Viewers chase ViewerTarget per second (0.1..0.3 typical; full settle in ~5-10s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Viewers")
	float ViewerApproachSpeed = 0.2f;

	// ==================== Style → Likes ====================

	/** Maps current style score -> likes per second (rate of cascading hearts). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Style")
	TObjectPtr<UCurveFloat> LikesPerSecondCurve;

	/** Style decay per second (after grace period). Applied while idle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Style")
	float StyleDecayPerSecond = 50.0f;

	/** Seconds after last style action before decay kicks in. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Style")
	float StyleDecayGracePeriod = 2.0f;

	/** Style score cap (caps LPS curve input as well). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Style")
	float MaxStyle = 10000.0f;

	/** Per-category base spectacle score for an action. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Style")
	TMap<EStyleCategory, float> SpectacleScores;

	/** Freshness rules: repeated same-category actions lose value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Style")
	FFreshnessFalloff Freshness;

	// ==================== Donations ====================

	/** Higher = fewer donations per viewer. e.g. 10000 -> ~1 donation/sec at 10k viewers, LPS=normal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Donations")
	float DonationDivisor = 10000.0f;

	/** Maps LPS -> donation chance multiplier (peak moments donate more often). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Donations")
	TObjectPtr<UCurveFloat> DonationChanceCurve;

	/** Maps roll [0..1] -> donation amount in currency. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Donations")
	TObjectPtr<UCurveFloat> DonationAmountCurve;

	/** Random pool of donor names sampled for alerts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Donations")
	TArray<FString> DonorNamePool;

	// ==================== Defaults ====================

	/** Fallback chat script when an arena doesn't define its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Defaults")
	TObjectPtr<UChatScript> DefaultChatScript;
};
