// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "UpgradeDefinition_SwordSlide.generated.h"

USTRUCT(BlueprintType)
struct FSwordSlideLevelData
{
	GENERATED_BODY()

	/** Specific weapon class this upgrade applies to. Null means any melee weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword Slide")
	TSubclassOf<AShooterWeapon> RequiredWeaponClass;

	/** Overrides MovementSettings->SlideMinSpeedBurst while the required weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword Slide|Movement", meta = (ClampMin = "0.0", ClampMax = "5000.0"))
	float SlideMinSpeedBurst = 200.0f;

	/** Overrides MovementSettings->SlideMaxSpeedBurst while the required weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword Slide|Movement", meta = (ClampMin = "0.0", ClampMax = "5000.0"))
	float SlideMaxSpeedBurst = 650.0f;

	/** Melee weapon damage multiplier while sliding with the required weapon equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword Slide|Damage", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float SlidingDamageMultiplier = 1.35f;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_SwordSlide : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword Slide")
	TArray<FSwordSlideLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "Sword Slide")
	const FSwordSlideLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
