// ShooterDummy.h
// Training dummy for testing weapons and melee attacks

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterDummyInterface.h"
#include "ShooterDummy.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class USoundBase;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDummyDeath, AShooterDummy*, Dummy, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDummyDamaged, AShooterDummy*, Dummy, float, Damage, AActor*, DamageCauser);

/**
 * Training dummy target with configurable HP, hitbox size, and charge rewards.
 * Useful for testing weapons, melee combat, and EMF charge mechanics.
 */
UCLASS(Blueprintable)
class POLARITY_API AShooterDummy : public AActor, public IShooterDummyTarget
{
	GENERATED_BODY()

public:

	AShooterDummy();

	// ==================== Health ====================

	/** Maximum HP for this dummy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Health", meta = (ClampMin = "1.0"))
	float MaxHP = 100.0f;

	/** Current HP */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dummy|Health")
	float CurrentHP = 100.0f;

	/** If true, dummy respawns after death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Health")
	bool bRespawnAfterDeath = true;

	/** Time before respawn (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Health", meta = (ClampMin = "0.0", EditCondition = "bRespawnAfterDeath"))
	float RespawnDelay = 3.0f;

	// ==================== Hitbox ====================

	/** Hitbox radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Hitbox", meta = (ClampMin = "10.0"))
	float HitboxRadius = 34.0f;

	/** Hitbox half-height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Hitbox", meta = (ClampMin = "10.0"))
	float HitboxHalfHeight = 88.0f;

	// ==================== Charge Rewards ====================

	/** If true, melee hits grant stable (non-decaying) charge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Charge")
	bool bGrantsStableCharge = true;

	/** Amount of stable charge per melee hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Charge", meta = (ClampMin = "0.0", EditCondition = "bGrantsStableCharge"))
	float StableChargePerHit = 1.0f;

	/** Bonus charge on kill (added to hit charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Charge", meta = (ClampMin = "0.0", EditCondition = "bGrantsStableCharge"))
	float KillChargeBonus = 5.0f;

	// ==================== Audio ====================

	/** Sound played when dummy takes damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Audio")
	TObjectPtr<USoundBase> ImpactSound;

	/** Sound played when dummy dies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Audio")
	TObjectPtr<USoundBase> DeathSound;

	/** Sound played when dummy respawns */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Audio")
	TObjectPtr<USoundBase> RespawnSound;

	/** Impact sound volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ImpactSoundVolume = 1.0f;

	/** Death sound volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DeathSoundVolume = 1.0f;

	// ==================== VFX ====================

	/** VFX played when dummy dies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|VFX")
	TObjectPtr<UNiagaraSystem> DeathVFX;

	/** Scale of death VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|VFX", meta = (ClampMin = "0.1"))
	FVector DeathVFXScale = FVector(1.0f);

	/** VFX played when dummy respawns */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|VFX")
	TObjectPtr<UNiagaraSystem> RespawnVFX;

	// ==================== Events ====================

	/** Called when dummy dies - bind in Level Blueprint */
	UPROPERTY(BlueprintAssignable, Category = "Dummy|Events")
	FOnDummyDeath OnDummyDeath;

	/** Called when dummy takes damage */
	UPROPERTY(BlueprintAssignable, Category = "Dummy|Events")
	FOnDummyDamaged OnDummyDamaged;

	// ==================== Components ====================

	/** Hitbox collision component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCapsuleComponent> HitboxComponent;

	/** Visual mesh component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DummyMesh;

protected:

	virtual void BeginPlay() override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ==================== IShooterDummyTarget Interface ====================

	virtual bool GrantsStableCharge_Implementation() const override;
	virtual float GetStableChargeAmount_Implementation() const override;
	virtual float GetKillChargeBonus_Implementation() const override;
	virtual bool IsDummyDead_Implementation() const override;

	// ==================== Public API ====================

	/** Reset dummy to full health */
	UFUNCTION(BlueprintCallable, Category = "Dummy")
	void ResetHealth();

	/** Check if dummy is dead */
	UFUNCTION(BlueprintPure, Category = "Dummy")
	bool IsDead() const { return bIsDead; }

	/** Get health percentage (0-1) */
	UFUNCTION(BlueprintPure, Category = "Dummy")
	float GetHealthPercent() const { return MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f; }

	/** Update hitbox size at runtime */
	UFUNCTION(BlueprintCallable, Category = "Dummy")
	void SetHitboxSize(float NewRadius, float NewHalfHeight);

protected:

	/** Is dummy currently dead */
	bool bIsDead = false;

	/** Timer for respawn */
	FTimerHandle RespawnTimer;

	/** Called when dummy HP reaches zero */
	void Die(AActor* Killer);

	/** Called after respawn delay */
	void Respawn();

	/** Play impact sound effect */
	void PlayImpactSound();

	/** Play death sound effect */
	void PlayDeathSound();

	/** Play respawn sound effect */
	void PlayRespawnSound();

	/** Spawn death VFX */
	void SpawnDeathVFX();

	/** Spawn respawn VFX */
	void SpawnRespawnVFX();

	/** Update capsule component size */
	void UpdateHitboxSize();
};