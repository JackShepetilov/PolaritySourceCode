// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "UpgradeDefinition_SMGAmmoRefund.generated.h"

USTRUCT(BlueprintType)
struct FSMGAmmoRefundLevelData
{
	GENERATED_BODY()

	/** Specific weapon class this upgrade applies to. Set this to the SMG Blueprint class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SMG Ammo Refund")
	TSubclassOf<AShooterWeapon> RequiredWeaponClass;

	/** Chance per damaging weapon hit to refund ammo into the current magazine. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SMG Ammo Refund", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RefundChance = 0.2f;

	/** Ammo returned to the magazine when the chance succeeds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SMG Ammo Refund", meta = (ClampMin = "1", ClampMax = "100"))
	int32 AmmoToRefund = 1;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_SMGAmmoRefund : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SMG Ammo Refund")
	TArray<FSMGAmmoRefundLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "SMG Ammo Refund")
	const FSMGAmmoRefundLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
