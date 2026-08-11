// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_DropChargeOverride.generated.h"

USTRUCT(BlueprintType)
struct FDropChargeOverrideLevelData
{
	GENERATED_BODY()

	/** Drops with Abs(CurrentCharge) <= this value are overwritten. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop Charge Override", meta = (ClampMin = "0.0"))
	float MaxAbsChargeToOverride = 1.0f;

	/** Charge assigned to matching dropped ranged weapons. Example: -5. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop Charge Override")
	float ReplacementCharge = -5.0f;
};

/**
 * While owned, ranged weapons dropped by killed enemies with weak charge are
 * forced to a configured charge value.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_DropChargeOverride : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop Charge Override")
	TArray<FDropChargeOverrideLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "Drop Charge Override")
	const FDropChargeOverrideLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
