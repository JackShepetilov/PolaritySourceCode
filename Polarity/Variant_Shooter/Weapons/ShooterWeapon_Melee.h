// ShooterWeapon_Melee.h
// Melee weapon that occupies a weapon slot (Doom Eternal Crucible style)
// Attacks on Fire button, no cooldown, blocks MeleeAttackComponent while equipped

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Melee.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UCameraShakeBase;

/**
 * Animation data for a single melee weapon swing variant
 */
USTRUCT(BlueprintType)
struct FMeleeWeaponSwingData
{
	GENERATED_BODY()

	/** Selection weight for random animation choice */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Animation montage for this swing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> SwingMontage;

	/** Camera shake for this swing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SwingCameraShake;

	/** Camera shake scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SwingShakeScale = 1.0f;

	/** Play rate multiplier for this swing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float BasePlayRate = 1.0f;
};

/**
 * Melee weapon - attacks with sphere trace on Fire button.
 *
 * Inherits from AShooterWeapon to integrate with the weapon switching system,
 * AnimBP system, and weapon inventory. Overrides Fire() to perform melee traces
 * instead of hitscan/projectile.
 *
 * Blocks MeleeAttackComponent while equipped via IsMeleeWeapon() flag.
 */
UCLASS()
class POLARITY_API AShooterWeapon_Melee : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterWeapon_Melee();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Fire() override;

public:
	virtual bool IsMeleeWeapon() const override { return true; }
	virtual bool OnSecondaryAction() override { return true; } // Block ADS

	// ==================== Melee Damage ====================

	/** Base damage per swing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage", meta = (ClampMin = "0"))
	float MeleeDamage = 75.0f;

	/** Damage multiplier for headshots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage", meta = (ClampMin = "1.0"))
	float MeleeHeadshotMultiplier = 1.5f;

	/** Damage type class for melee hits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage")
	TSubclassOf<UDamageType> MeleeDamageType;

	/** Impulse applied to hit physics objects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage", meta = (ClampMin = "0"))
	float HitImpulse = 500.0f;

	// ==================== Melee Range ====================

	/** Maximum range of the melee swing (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Range", meta = (ClampMin = "50", ClampMax = "500"))
	float AttackRange = 200.0f;

	/** Radius of the sphere trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Range", meta = (ClampMin = "10", ClampMax = "100"))
	float AttackRadius = 40.0f;

	/** Forward offset from camera for trace start (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Range", meta = (ClampMin = "0", ClampMax = "100"))
	float TraceForwardOffset = 20.0f;

	// ==================== Momentum Damage ====================

	/** Additional damage per 100 cm/s of player velocity toward target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Momentum", meta = (ClampMin = "0"))
	float MomentumDamagePerSpeed = 10.0f;

	/** Maximum bonus damage from momentum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Momentum", meta = (ClampMin = "0"))
	float MaxMomentumDamage = 50.0f;

	// ==================== Knockback ====================

	/** Base knockback distance in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0"))
	float BaseKnockbackDistance = 200.0f;

	/** Additional knockback distance per cm/s of player velocity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0"))
	float KnockbackDistancePerVelocity = 0.15f;

	/** Duration of knockback interpolation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float KnockbackDuration = 0.3f;

	// ==================== Lunge ====================

	/** Enable lunge toward targets on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lunge")
	bool bEnableLunge = true;

	/** Speed at which player lunges toward target (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lunge", meta = (ClampMin = "0", EditCondition = "bEnableLunge"))
	float LungeSpeed = 2000.0f;

	/** Distance from target where lunge stops (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lunge", meta = (ClampMin = "0", ClampMax = "200", EditCondition = "bEnableLunge"))
	float LungeStopBuffer = 40.0f;

	/** Maximum range for lunge target acquisition (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lunge", meta = (ClampMin = "0", EditCondition = "bEnableLunge"))
	float LungeMaxRange = 400.0f;

	/** Minimum speed to trigger lunge (prevents weak lunges when stationary) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lunge", meta = (ClampMin = "0", EditCondition = "bEnableLunge"))
	float MinSpeedForLunge = 300.0f;

	// ==================== Swing Animations ====================

	/** Array of swing animation variants (randomly selected by weight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	TArray<FMeleeWeaponSwingData> SwingAnimations;

	// ==================== Melee VFX ====================

	/** Niagara effect for swing trail */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|VFX")
	TObjectPtr<UNiagaraSystem> SwingTrailFX;

	/** Socket name for trail attachment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|VFX")
	FName TrailSocketName = FName("Trail");

	/** Niagara effect for impact on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|VFX")
	TObjectPtr<UNiagaraSystem> MeleeImpactFX;

	/** Scale for impact effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|VFX", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ImpactFXScale = 1.0f;

	// ==================== Melee SFX ====================

	/** Sound played on each swing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|SFX")
	TObjectPtr<USoundBase> SwingSound;

	/** Sound played on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|SFX")
	TObjectPtr<USoundBase> HitSound;

	/** Sound played on miss */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|SFX")
	TObjectPtr<USoundBase> MissSound;

	// ==================== Melee Camera ====================

	/** Camera shake on successful hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Camera")
	TSubclassOf<UCameraShakeBase> HitCameraShake;

	/** Camera shake scale on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Camera", meta = (ClampMin = "0", ClampMax = "5.0"))
	float HitShakeScale = 1.0f;

protected:

	// ==================== Hit Detection ====================

	/** Perform sphere trace and return hit results. Returns true if a valid target was hit. */
	bool PerformMeleeTrace(FHitResult& OutHit);

	/** Apply damage and effects to the hit actor. Returns the final damage dealt. */
	float ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult);

	/** Check if the hit is a headshot */
	bool IsHeadshot(const FHitResult& HitResult) const;

	/** Apply knockback to the hit actor */
	void ApplyKnockback(AActor* HitActor);

	/** Calculate momentum-based bonus damage */
	float CalculateMomentumDamage(AActor* HitActor) const;

	// ==================== Animation ====================

	/** Select a random swing animation from the array based on weights */
	const FMeleeWeaponSwingData* SelectWeightedSwing();

	// ==================== Lunge ====================

	/** Start lunge toward target */
	void StartLunge(AActor* Target);

	/** Update lunge movement */
	void UpdateLunge(float DeltaTime);

	// ==================== VFX/SFX ====================

	/** Spawn swing trail effect */
	void SpawnSwingTrail();

	/** Stop and destroy swing trail effect */
	void StopSwingTrail();

	/** Spawn impact effect at hit location */
	void SpawnMeleeImpactFX(const FVector& Location, const FVector& Normal);

	/** Play a sound at weapon location */
	void PlayMeleeSound(USoundBase* Sound);

	/** Play camera shake */
	void PlayMeleeCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale);

	// ==================== State ====================

	/** Velocity at attack start (for momentum calculations) */
	FVector VelocityAtSwingStart = FVector::ZeroVector;

	/** Active trail VFX component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveTrailFX;

	/** Lunge target */
	TWeakObjectPtr<AActor> LungeTarget;

	/** Target position for lunge */
	FVector LungeTargetPosition = FVector::ZeroVector;

	/** True while lunge is active */
	bool bIsLunging = false;

	/** Index to cycle/alternate swing animations (prevents same animation twice in a row) */
	int32 LastSwingIndex = -1;
};
