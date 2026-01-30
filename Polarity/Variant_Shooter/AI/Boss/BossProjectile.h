// BossProjectile.h
// Specialized EMF projectile for boss with parry detection

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Weapons/EMFProjectile.h"
#include "BossProjectile.generated.h"

class ABossCharacter;

/**
 * Boss projectile with parry detection system.
 *
 * Behavior:
 * - Spawns with charge OPPOSITE to player (attracts to player)
 * - NPCForceMultiplier starts at 0 (ignores boss EMF field)
 * - When player changes polarity to match projectile, it's "parried"
 * - On parry: NPCForceMultiplier increases, projectile attracts to boss
 * - Notifies boss of parry for counter and dodge reaction
 */
UCLASS()
class POLARITY_API ABossProjectile : public AEMFProjectile
{
	GENERATED_BODY()

public:
	ABossProjectile();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	// ==================== Parry Settings ====================

	/** Radius around player to detect parry (when charges become same sign) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Projectile|Parry")
	float ParryDetectionRadius = 400.0f;

	/** NPC force multiplier after parry (attracts to boss) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Projectile|Parry")
	float ParriedNPCForceMultiplier = 2.0f;

	/** Initial NPC force multiplier (0 = ignore boss field initially) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Projectile|Parry")
	float InitialNPCForceMultiplier = 0.0f;

	// ==================== Debug ====================

	/** Draw debug sphere for parry detection radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Projectile|Debug")
	bool bDrawParryDebug = false;

	// ==================== Public API ====================

	/** Initialize projectile after spawn - sets charge opposite to player */
	UFUNCTION(BlueprintCallable, Category = "Boss Projectile")
	void InitializeForBoss(ABossCharacter* Boss, AActor* Target);

	/** Check if this projectile was parried */
	UFUNCTION(BlueprintPure, Category = "Boss Projectile")
	bool WasParried() const { return bWasParried; }

protected:
	// ==================== Runtime State ====================

	/** Reference to the player target */
	TWeakObjectPtr<AActor> ParryTarget;

	/** Reference to the boss owner */
	TWeakObjectPtr<ABossCharacter> OwnerBoss;

	/** Was this projectile parried (player changed polarity) */
	bool bWasParried = false;

	/** Has the projectile been initialized */
	bool bInitialized = false;

	// ==================== Internal Methods ====================

	/** Check if parry occurred (player and projectile have same charge sign) */
	void CheckForParry();

	/** Handle overlap with player (damage without blocking) */
	UFUNCTION()
	void OnProjectileOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
