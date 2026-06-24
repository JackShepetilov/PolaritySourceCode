// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "UpgradeDefinition_MeleeCharge.generated.h"

class UDamageType;

USTRUCT(BlueprintType)
struct FMeleeChargeLevelData
{
	GENERATED_BODY()

	/** Specific weapon class this upgrade applies to. Null means any melee weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge")
	TSubclassOf<AShooterWeapon> RequiredWeaponClass;

	/** Total active charge duration in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float ChargeDuration = 1.1f;

	/** Cooldown after the charge ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float Cooldown = 10.0f;

	/** Horizontal charge speed in cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float ChargeSpeed = 2000.0f;

	/** How quickly the charge direction steers toward camera yaw. 0 means fixed initial direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float TurnInterpSpeed = 4.0f;

	/** End charge when CharacterMovement reports too little progress for the requested speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge")
	bool bEndOnBlockedMovement = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEndOnBlockedMovement"))
	float BlockedMoveFraction = 0.2f;

	/** Swept capsule radius used to detect charge bash targets along the travelled path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Bash", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float BashSweepRadius = 70.0f;

	/** Swept capsule half-height used to detect charge bash targets along the travelled path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Bash", meta = (ClampMin = "20.0", ClampMax = "200.0"))
	float BashSweepHalfHeight = 90.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Bash", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float BashDamage = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Bash")
	TSubclassOf<UDamageType> BashDamageType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Bash")
	bool bStopOnBash = true;

	/** Melee weapon damage multiplier granted after a charge/bash. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Melee Boost", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MeleeBoostMultiplier = 1.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Melee Boost", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float MeleeBoostWindow = 2.0f;

	/** Minimum consumed charge fraction required to arm the melee boost if no bash happened. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Melee Boost", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinConsumedFractionForBoost = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge|Debug")
	bool bDebugDrawChargeSweep = false;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_MeleeCharge : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Charge")
	TArray<FMeleeChargeLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "Melee Charge")
	const FMeleeChargeLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
