// AbilityDefinition_Burst.h
// Definition for "burst" archetype: CastStart → LoopMontage × N → CastFinish, where each
// iteration spawns a per-shot effect (typically a projectile) at an AnimNotify.
//
// Subclasses are not required — designers create a UAbilityDefinition_Burst DataAsset and
// pair it with a UAbilityHandler_Burst-derived HandlerClass that overrides OnPerShotEffect.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "Variant_Shooter/Weapons/EMFProjectile.h"
#include "AbilityDefinition_Burst.generated.h"

class UAnimMontage;

/**
 * One montage variant for the per-shot loop, with a weight used in random picking.
 */
USTRUCT(BlueprintType)
struct FWeightedAnimMontage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> Montage;

	/** Relative weight (only meaningful relative to siblings in the same array). 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

/**
 * Per-level stats for burst-archetype abilities.
 */
USTRUCT(BlueprintType)
struct FBurstLevelStats
{
	GENERATED_BODY()

	// ==== Common (mirrored from FAbilityCommonStats — kept here so designers see all per-level
	// data in one struct rather than two parallel arrays.) ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 3.0f;

	// ==== Burst-specific ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge", meta = (ClampMin = "0.0"))
	float ChargePerShot = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst", meta = (ClampMin = "1", ClampMax = "32"))
	int32 NumProjectiles = 1;

	/** Total time across all per-shot iterations.
	 *  TimePerShot = CastDuration / NumProjectiles. LoopMontage play rate is computed so each
	 *  iteration's spacing equals TimePerShot, factoring in the overlap crossfade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst", meta = (ClampMin = "0.05", Units = "s"))
	float CastDuration = 1.0f;

	/** Optional projectile class override for this level. Falls back to DefaultProjectileClass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<AEMFProjectile> ProjectileClassOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0.0"))
	float ProjectileSpeed = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "deg"))
	float AimVariance = 1.0f;
};

/**
 * Burst-archetype ability definition.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_Burst : public UAbilityDefinition
{
	GENERATED_BODY()

public:

	UAbilityDefinition_Burst();

	// ==================== Burst-shared (NOT per-level) ====================

	/** Socket on FirstPersonMesh from which to spawn projectiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst|Spawn")
	FName ProjectileSpawnSocket = FName("AbilitySocket");

	/** Default projectile class. Per-level FBurstLevelStats::ProjectileClassOverride wins if set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst|Spawn")
	TSubclassOf<AEMFProjectile> DefaultProjectileClass;

	/** Per-shot variants — each iteration picks one weighted-randomly. Place
	 *  UAnimNotify_AbilityFire inside each variant at the frame where the shot fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst|Animation", meta = (TitleProperty = "Weight"))
	TArray<FWeightedAnimMontage> LoopMontages;

	/** Sound played at each per-shot fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst|Audio")
	TObjectPtr<USoundBase> PerShotSound;

	// ==================== Per-level ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Burst|Levels", meta = (TitleProperty = "NumProjectiles"))
	TArray<FBurstLevelStats> Levels;

	// ==================== Base overrides ====================

	virtual int32 GetMaxLevel() const override { return Levels.Num(); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	// ==================== Burst-specific accessors ====================

	UFUNCTION(BlueprintPure, Category = "Burst|Levels")
	FBurstLevelStats GetBurstStatsAtLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "Burst|Spawn")
	TSubclassOf<AEMFProjectile> GetProjectileClassAtLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "Burst|Animation")
	UAnimMontage* PickRandomLoopMontage() const;
};
