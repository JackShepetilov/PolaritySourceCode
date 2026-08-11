// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_MaxHealth.generated.h"

USTRUCT(BlueprintType)
struct FMaxHealthLevelData
{
	GENERATED_BODY()

	/** Total max HP bonus active at this upgrade level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Max Health", meta = (ClampMin = "0.0"))
	float MaxHPBonus = 100.0f;

	/** When gaining max HP, also add the gained amount to current HP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Max Health")
	bool bHealAddedMaxHP = true;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_MaxHealth : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Max Health")
	TArray<FMaxHealthLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "Max Health")
	const FMaxHealthLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
