// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_HealthBlast.generated.h"

class UNiagaraSystem;
class USoundBase;
class UStaticMesh;
class UCameraShakeBase;

/**
 * Data asset for the "Health Blast" upgrade.
 * When the player collects health pickups at full HP, they are stored.
 * On empty capture (channeling with nothing to grab), stored pickups fire
 * as a shotgun blast — dealing damage, knocking targets forward,
 * and launching the player backward.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_HealthBlast : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Damage & Physics ====================

	/** Damage dealt per stored pickup */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Damage", meta = (ClampMin = "1.0"))
	float DamagePerPickup = 30.0f;

	/** Knockback force applied to the PLAYER (backward) per stored pickup */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Knockback", meta = (ClampMin = "0.0"))
	float PlayerKnockbackPerPickup = 400.0f;

	/** Knockback force applied to hit TARGETS (forward) per stored pickup */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Knockback", meta = (ClampMin = "0.0"))
	float TargetKnockbackPerPickup = 600.0f;

	/** Speed of the health blast projectiles (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Projectile", meta = (ClampMin = "500.0"))
	float ProjectileSpeed = 3000.0f;

	/** How long projectiles live before self-destructing (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Projectile", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ProjectileLifetime = 2.0f;

	/** Damage radius on projectile impact. 0 = direct hit only */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Damage", meta = (ClampMin = "0.0"))
	float DamageRadius = 100.0f;

	/** Collision radius for projectile overlap detection (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Projectile", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float ProjectileCollisionRadius = 30.0f;

	// ==================== Spread & Limits ====================

	/** Half-angle of the shotgun cone spread (degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Spread", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float SpreadHalfAngle = 15.0f;

	/** Maximum number of health pickups that can be stored */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Limits", meta = (ClampMin = "1", ClampMax = "50"))
	int32 MaxStoredPickups = 10;

	/** Delay after channeling starts before firing if nothing is captured (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Timing", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float EmptyCaptureDelay = 0.2f;

	/** Cooldown after firing before the blast can be used again (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Timing", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float Cooldown = 0.5f;

	// ==================== Visuals ====================

	/** Mesh used for the health blast projectiles (purely visual, no collision) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Visuals")
	TObjectPtr<UStaticMesh> ProjectileMesh;

	/** Scale of the projectile mesh */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Visuals")
	FVector ProjectileMeshScale = FVector(1.0f);

	/** VFX spawned at the player when blast fires (muzzle flash) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Effects")
	TObjectPtr<UNiagaraSystem> ShotVFX;

	/** VFX spawned at projectile impact location */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Effects")
	TObjectPtr<UNiagaraSystem> HitVFX;

	/** VFX spawned on the player when a health pickup is stored at full HP */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Effects")
	TObjectPtr<UNiagaraSystem> StoredVFX;

	// ==================== Audio ====================

	/** Sound played when the blast fires */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Audio")
	TObjectPtr<USoundBase> ShotSound;

	/** Sound played when a projectile hits a target */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Audio")
	TObjectPtr<USoundBase> HitSound;

	/** Sound played when a health pickup is stored at full HP */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Audio")
	TObjectPtr<USoundBase> StoredSound;

	// ==================== Camera Shake ====================

	/** Camera shake class played on blast fire */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Camera")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	/** Base scale for camera shake — multiplied by number of stored pickups fired */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Blast|Camera", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float CameraShakeBaseScale = 0.3f;
};
