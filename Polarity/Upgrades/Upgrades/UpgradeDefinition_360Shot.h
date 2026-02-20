// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_360Shot.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * Data asset for the "360 Shot" upgrade.
 * Contains all tuning parameters and asset references configurable in the editor.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_360Shot : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Tuning ====================

	/** Fixed bonus damage dealt by the 360 shot (on top of normal shot damage) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot", meta = (ClampMin = "1.0"))
	float BonusDamage = 500.0f;

	/** Time window to complete the 360-degree rotation (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SpinTimeWindow = 1.5f;

	/** Duration of the charged state after completing the spin (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ChargedDuration = 1.0f;

	/** Minimum rotation speed to count toward spin (degrees/sec). Prevents slow creeping rotations. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot", meta = (ClampMin = "0.0"))
	float MinRotationSpeed = 180.0f;

	// ==================== VFX/SFX ====================

	/** Special beam VFX for the 360 shot (overrides normal beam) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot|Effects")
	TObjectPtr<UNiagaraSystem> ChargedBeamFX;

	/** Color of the charged beam */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot|Effects")
	FLinearColor ChargedBeamColor = FLinearColor(1.0f, 0.3f, 0.05f, 1.0f);

	/** Sound played when charged state activates */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot|Effects")
	TObjectPtr<USoundBase> ChargedReadySound;

	/** Sound played when the 360 shot fires */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "360 Shot|Effects")
	TObjectPtr<USoundBase> ChargedFireSound;
};
