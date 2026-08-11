// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_ChargeFlip.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * Data asset for the "Charge Flip" upgrade.
 *
 * When the player shoots an EMF projectile (charge launcher), or detonates an
 * explosive EMF prop with the hitscan rifle, the explosion fires rifle shots
 * with multiplied damage + ionization at all visible targets from the explosion point.
 * Chain-reacts with other projectiles.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_ChargeFlip : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Tuning ====================

	/** Damage multiplier applied to HitscanDamage for each Charge Flip shot (1 = normal, 2 = double) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float DamageMultiplier = 2.0f;

	/** Charge delta applied to each hit target. Can be negative. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip")
	float IonizationChargePerHit = 5.0f;

	/** Maximum chain reaction depth (safety limit). 0 = no chaining, -1 = unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip", meta = (ClampMin = "-1"))
	int32 MaxChainDepth = 10;

	// ==================== VFX/SFX ====================

	/** Explosion VFX at the projectile's location when Charge Flip triggers */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip|Effects")
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	/** Beam VFX for each outgoing shot from explosion to target */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip|Effects")
	TObjectPtr<UNiagaraSystem> BeamFX;

	/** Color of the outgoing beams */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip|Effects")
	FLinearColor BeamColor = FLinearColor(0.2f, 0.5f, 1.0f, 1.0f);

	/** Sound played when the Charge Flip explosion triggers */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge Flip|Effects")
	TObjectPtr<USoundBase> ExplosionSound;
};
