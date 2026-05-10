// StyleComponent.h
// Per-player ActorComponent tracking style score, freshness, and likes-per-second rate.
//
// Owner registers stylish gameplay events via RegisterAction(). The component computes
// freshness-adjusted style points, decays style during inactivity, derives LikesPerSecond
// from a config curve, and broadcasts heart-burst events for UI consumption.
//
// The "rank" is intentionally not exposed — the user-facing signal is the heart cascade
// intensity (driven by LikesPerSecond) plus the resulting viewer growth in UStreamSubsystem.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StyleAction.h"
#include "StyleComponent.generated.h"

class UStreamConfig;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLikesGenerated, int32, LikeCount, FVector, WorldLocation);

UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class POLARITY_API UStyleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStyleComponent();

	// ==================== Public API ====================

	UFUNCTION(BlueprintCallable, Category = "Style")
	void SetConfig(UStreamConfig* InConfig);

	/** Register a stylish gameplay event. Generates style points and a likes burst. */
	UFUNCTION(BlueprintCallable, Category = "Style")
	void RegisterAction(const FStyleAction& Action);

	UFUNCTION(BlueprintPure, Category = "Style")
	float GetCurrentStyle() const { return CurrentStyle; }

	UFUNCTION(BlueprintPure, Category = "Style")
	float GetLikesPerSecond() const { return CurrentLikesPerSecond; }

	/** Reset style and freshness state. Called by UStreamSubsystem on run start. */
	UFUNCTION(BlueprintCallable, Category = "Style")
	void ResetStyleState();

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Style|Events")
	FOnLikesGenerated OnLikesGenerated;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Internals ====================

	/** Compute freshness multiplier for a category given recent repeats within the freshness window. */
	float ComputeFreshness(EStyleCategory Category) const;

	/** Recompute LikesPerSecond from CurrentStyle via config curve. */
	void RecomputeLikesPerSecond();

	/** Drop expired entries from each category's freshness window. */
	void PruneCategoryHistories();

	/** Per-category sliding buffer of recent action timestamps for freshness tracking. */
	struct FCategoryHistory
	{
		TArray<float> Timestamps;
	};

	TMap<EStyleCategory, FCategoryHistory> CategoryHistories;

	UPROPERTY(Transient)
	TWeakObjectPtr<UStreamConfig> Config;

	UPROPERTY(Transient)
	float CurrentStyle = 0.0f;

	UPROPERTY(Transient)
	float CurrentLikesPerSecond = 0.0f;

	UPROPERTY(Transient)
	float TimeSinceLastAction = 0.0f;
};
