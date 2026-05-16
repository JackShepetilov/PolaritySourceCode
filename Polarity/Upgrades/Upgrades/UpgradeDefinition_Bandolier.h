// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_Bandolier.generated.h"

USTRUCT(BlueprintType)
struct FBandolierLevelData
{
	GENERATED_BODY()

	/** Total copies of the same yanked-weapon class the player may carry at this level —
	 *  includes the one in hand. Lv1 typical value = 2 (one active + one reserve).
	 *  Starter weapons are excluded from this count. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bandolier", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxCopies = 2;
};

/**
 * "Bandolier" upgrade definition.
 *
 * Extends the carrying capacity of the yanked-weapon slot: instead of the strict
 * one-yanked-at-a-time rule, the player may stash extra copies of the class CURRENTLY
 * IN HAND (captured at pull-start). When the active copy runs dry, the next reserve of
 * the same class auto-equips. Overflow pickups spill bullets into existing magazines.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_Bandolier : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** Per-level tuning. Index 0 = Lv 1. Length = MaxLevel (auto-synced in editor). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bandolier")
	TArray<FBandolierLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "Bandolier")
	const FBandolierLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
