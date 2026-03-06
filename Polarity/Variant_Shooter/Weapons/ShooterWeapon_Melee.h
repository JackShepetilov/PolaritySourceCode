// ShooterWeapon_Melee.h
// Melee weapon that occupies a weapon slot (Doom Eternal Crucible style)
// Attacks on Fire button, no cooldown, blocks MeleeAttackComponent while equipped
// Full combat mechanics matching MeleeAttackComponent (magnetism, momentum, dropkick, etc.)

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Melee.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UCameraShakeBase;
class ABossCharacter;
class UGeometryCollection;

/** Side from which a melee swing comes */
UENUM(BlueprintType)
enum class EMeleeSwingSide : uint8
{
	Left,
	Right
};

/**
 * Animation data for a single melee weapon swing variant
 */
USTRUCT(BlueprintType)
struct FMeleeWeaponSwingData
{
	GENERATED_BODY()

	/** Which side this swing comes from (Left or Right) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection")
	EMeleeSwingSide SwingSide = EMeleeSwingSide::Right;

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
 *
 * Combat mechanics match MeleeAttackComponent:
 * - Target magnetism with pre-attack lock-on
 * - Titanfall 2 momentum preservation and transfer
 * - Cool kick (airborne speed boost on hit)
 * - Drop kick (dive attack from height)
 * - Distance-based knockback with NPC multiplier
 * - Camera focus on lunge target
 * - Multiple damage types (base, momentum, dropkick)
 * - Boss finisher support
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

	// ==================== Damage Window (AnimNotify API) ====================

	UFUNCTION(BlueprintCallable, Category = "Melee")
	void ActivateDamageWindow();

	UFUNCTION(BlueprintCallable, Category = "Melee")
	void DeactivateDamageWindow();

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsDamageWindowActive() const { return bDamageWindowActive; }

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

	/** Additional impulse multiplier based on player speed (impulse = HitImpulse * (1 + speed * MomentumImpulseMultiplier)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage", meta = (ClampMin = "0"))
	float MomentumImpulseMultiplier = 0.002f;

	// ==================== Durability (Hit Count) ====================

	/** Maximum hits before weapon breaks. 0 = infinite (no breaking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Durability", meta = (ClampMin = "0"))
	int32 MaxHitCount = 0;

	/** Remaining hits before weapon breaks (set by SetRemainingHits when equipped from a drop) */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Durability")
	int32 RemainingHits = 0;

	/** Sound played when weapon breaks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Durability")
	TObjectPtr<USoundBase> BreakSound;

	/** Set remaining hits (called when equipping from a DroppedMeleeWeapon) */
	void SetRemainingHits(int32 Hits);

	/** Set break geometry data from a DroppedMeleeWeapon source */
	void SetBreakData(UGeometryCollection* GC, float Impulse, float AngularImpulse, float GibLifetime);

	/** True if this weapon has limited durability (will break after hits) */
	bool HasLimitedDurability() const { return MaxHitCount > 0; }

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

	/** Angle for cone-based hit detection (degrees, 0 = line trace only) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Range", meta = (ClampMin = "0", ClampMax = "45"))
	float AttackAngle = 15.0f;

	// ==================== Momentum Damage ====================

	/** Additional damage per 100 cm/s of player velocity toward target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Momentum", meta = (ClampMin = "0"))
	float MomentumDamagePerSpeed = 10.0f;

	/** Maximum bonus damage from momentum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Momentum", meta = (ClampMin = "0"))
	float MaxMomentumDamage = 50.0f;

	// ==================== Titanfall 2 Momentum System ====================

	/** Enable Titanfall 2 style momentum preservation - player keeps velocity during melee */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Titanfall Momentum")
	bool bPreserveMomentum = true;

	/** How much of the original velocity to preserve during melee (1.0 = 100%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Titanfall Momentum", meta = (ClampMin = "0", ClampMax = "1.0", EditCondition = "bPreserveMomentum"))
	float MomentumPreservationRatio = 1.0f;

	/** Transfer player momentum to target on hit (Titanfall 2 flying kick feel) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Titanfall Momentum")
	bool bTransferMomentumOnHit = true;

	/** Multiplier for momentum transferred to target (1.0 = full velocity transfer) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Titanfall Momentum", meta = (ClampMin = "0", ClampMax = "2.0", EditCondition = "bTransferMomentumOnHit"))
	float MomentumTransferMultiplier = 1.0f;

	// ==================== Target Magnetism ====================

	/** Enable predictive target magnetism (locks onto targets before swing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Target Magnetism")
	bool bEnableTargetMagnetism = true;

	/** Range for magnetism sphere trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Target Magnetism", meta = (ClampMin = "0", EditCondition = "bEnableTargetMagnetism"))
	float MagnetismRange = 300.0f;

	/** Radius for magnetism sphere trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Target Magnetism", meta = (ClampMin = "0", EditCondition = "bEnableTargetMagnetism"))
	float MagnetismRadius = 80.0f;

	// ==================== Knockback ====================

	/** Base knockback distance in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0"))
	float BaseKnockbackDistance = 200.0f;

	/** Additional knockback distance per cm/s of player velocity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0"))
	float KnockbackDistancePerVelocity = 0.15f;

	/** Base duration for knockback interpolation in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float KnockbackBaseDuration = 0.3f;

	/** Duration multiplier per 100cm of knockback distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockback", meta = (ClampMin = "0"))
	float KnockbackDurationPerDistance = 0.001f;

	// ==================== Lunge ====================

	/** Enable lunge toward targets (pre-attack magnetism lunge, not on-hit) */
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

	/** Lunge duration (seconds) - how long the lunge takes to complete */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lunge", meta = (ClampMin = "0.05", ClampMax = "0.5", EditCondition = "bEnableLunge"))
	float LungeDuration = 0.15f;

	// ==================== Cool Kick ====================

	/** Duration of the cool kick period (applied when hitting enemy in air without lunge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Cool Kick", meta = (ClampMin = "0", ClampMax = "2.0"))
	float CoolKickDuration = 0.3f;

	/** Speed boost added gradually over the cool kick period (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Cool Kick", meta = (ClampMin = "0", ClampMax = "2000.0"))
	float CoolKickSpeedBoost = 400.0f;

	// ==================== Drop Kick ====================

	/** Enable drop kick - airborne attack when looking down, player dives toward enemy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick")
	bool bEnableDropKick = true;

	/** Minimum height difference (cm) - drop kick only triggers when player is at least this much higher than target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "0", ClampMax = "500", EditCondition = "bEnableDropKick"))
	float DropKickMinHeightDifference = 100.0f;

	/** Camera pitch threshold (degrees) - drop kick triggers when looking down more than this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "10", ClampMax = "80", EditCondition = "bEnableDropKick"))
	float DropKickPitchThreshold = 45.0f;

	/** Cone angle for drop kick detection (half-angle in degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "5", ClampMax = "45", EditCondition = "bEnableDropKick"))
	float DropKickConeAngle = 30.0f;

	/** Maximum range for drop kick cone trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "100", ClampMax = "2000", EditCondition = "bEnableDropKick"))
	float DropKickMaxRange = 1000.0f;

	/** Bonus damage per 100cm of height difference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "0", EditCondition = "bEnableDropKick"))
	float DropKickDamagePerHeight = 10.0f;

	/** Maximum bonus damage from height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "0", EditCondition = "bEnableDropKick"))
	float DropKickMaxBonusDamage = 100.0f;

	/** Speed at which player dives toward drop kick target (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Drop Kick", meta = (ClampMin = "500", ClampMax = "5000", EditCondition = "bEnableDropKick"))
	float DropKickDiveSpeed = 2500.0f;

	// ==================== Camera Focus ====================

	/** Enable camera focus on lunge target (rotates camera toward enemy when lunge starts) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Camera")
	bool bEnableCameraFocusOnLunge = true;

	/** Duration of camera focus rotation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Camera", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "bEnableCameraFocusOnLunge"))
	float CameraFocusDuration = 0.2f;

	/** Strength of camera focus (1.0 = instant snap, 0.5 = gentle rotation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Camera", meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bEnableCameraFocusOnLunge"))
	float CameraFocusStrength = 0.7f;

	// ==================== Swing Animations ====================

	/** Montage played on MeleeWeaponFPMesh when weapon is equipped (e.g. draw/unsheathe) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	TObjectPtr<UAnimMontage> EquipMontage;

	/** Which side the first swing comes from (when starting from idle) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	EMeleeSwingSide FirstSwingSide = EMeleeSwingSide::Right;

	/** Array of ground swing animation variants (randomly selected by weight, filtered by current side) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	TArray<FMeleeWeaponSwingData> SwingAnimations;

	/** Array of airborne swing animation variants (used when player is in the air). Side alternation applies here too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	TArray<FMeleeWeaponSwingData> AirSwingAnimations;

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

	/** Check if actor is a valid melee target (Pawn, ShooterDummyTarget, MeleeDestructible) */
	bool IsValidMeleeTarget(AActor* HitActor) const;

	/** Calculate momentum-based bonus damage */
	float CalculateMomentumDamage(AActor* HitActor) const;

	/** Calculate momentum-based impulse multiplier */
	float CalculateMomentumImpulseMultiplier() const;

	/** Calculate drop kick bonus damage based on height difference */
	float CalculateDropKickBonusDamage() const;

	// ==================== Knockback ====================

	/** Apply full knockback system (distance-based, NPC multiplier, momentum) */
	void ApplyCharacterImpulse(AActor* HitActor, const FVector& ImpulseDirection, float ImpulseStrength);

	// ==================== Target Magnetism ====================

	/** Find and lock onto nearest target before swing */
	void StartMagnetism();

	/** Update magnetism lunge and target tracking */
	void UpdateMagnetism(float DeltaTime);

	/** Stop magnetism, restore gravity, handle exit momentum */
	void StopMagnetism();

	// ==================== Momentum ====================

	/** Update Titanfall 2 momentum preservation during swing */
	void UpdateMomentumPreservation(float DeltaTime);

	// ==================== Cool Kick ====================

	/** Start cool kick period (airborne hit speed boost) */
	void StartCoolKick();

	/** Update cool kick boost */
	void UpdateCoolKick(float DeltaTime);

	// ==================== Drop Kick (delegated to MeleeAttackComponent) ====================

	/** Check if drop kick conditions are met (airborne + looking down) */
	bool ShouldPerformDropKick() const;

	/** Callback when MeleeAttackComponent detects a dropkick hit */
	UFUNCTION()
	void OnDelegatedDropKickHit(AActor* HitActor, const FVector& HitLocation, float Damage);

	/** Callback when MeleeAttackComponent finishes the dropkick attack */
	UFUNCTION()
	void OnDelegatedDropKickEnded();

	// ==================== Camera Focus ====================

	/** Start camera focus on target */
	void StartCameraFocus(AActor* Target);

	/** Update camera focus interpolation */
	void UpdateCameraFocus(float DeltaTime);

	/** Stop camera focus */
	void StopCameraFocus();

	// ==================== Animation ====================

	/** Select a random swing animation from the array based on weights and current side. Uses AirSwingAnimations if airborne. */
	const FMeleeWeaponSwingData* SelectWeightedSwing(bool bAirborne = false);

	// ==================== Montage Control ====================

	/** Stop any currently playing montage on the owner and character's MeleeWeaponFPMesh */
	void StopCurrentMontage();

public:
	/** Play montage on character's MeleeWeaponFPMesh (gets mesh from ShooterCharacter) */
	void PlayMontageOnFPMesh(UAnimMontage* Montage);

protected:

	/** Cached reference to character's MeleeWeaponFPMesh */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> CachedMeleeWeaponFPMesh;

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

	// ==================== Damage Window ====================

	/** Perform hit detection during active damage window (called from Tick) */
	void UpdateDamageWindow();

	/** Process a successful hit during damage window */
	void ProcessHit(const FHitResult& HitResult);

	// ==================== State ====================

	/** Velocity at attack start (for momentum calculations) */
	FVector VelocityAtSwingStart = FVector::ZeroVector;

	/** Active trail VFX component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveTrailFX;

	/** Index to cycle/alternate swing animations (prevents same animation twice in a row) */
	int32 LastSwingIndex = -1;

	/** Current expected swing side (alternates Left↔Right after each swing) */
	EMeleeSwingSide CurrentSwingSide = EMeleeSwingSide::Right;

	/** True after the first swing has been performed (reset when combo resets) */
	bool bIsInCombo = false;

	/** Cached player controller */
	UPROPERTY()
	TObjectPtr<APlayerController> CachedPlayerController;

	// ==================== Damage Window State ====================

	/** True while damage window is open (between ActivateDamageWindow and DeactivateDamageWindow) */
	bool bDamageWindowActive = false;

	/** True if at least one hit occurred during the current damage window */
	bool bHitDuringWindow = false;

	/** Actors already hit during this swing (prevents multi-hit on same target) */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> HitActorsThisSwing;

	/** Currently selected swing data (set in Fire(), used during damage window) */
	const FMeleeWeaponSwingData* CurrentSwingData = nullptr;

	// ==================== Magnetism State ====================

	/** Target locked on by magnetism (set at swing start) */
	TWeakObjectPtr<AActor> MagnetismTarget;

	/** Pre-calculated lunge target position (validated via path sweep) */
	FVector MagnetismLungeTargetPosition = FVector::ZeroVector;

	/** True while magnetism is active (between Fire and DeactivateDamageWindow) */
	bool bIsMagnetismActive = false;

	/** Lunge progress during magnetism (0-1) */
	float LungeProgress = 0.0f;

	// ==================== Cool Kick State ====================

	/** Time remaining in cool kick period */
	float CoolKickTimeRemaining = 0.0f;

	/** Direction for cool kick boost */
	FVector CoolKickDirection = FVector::ZeroVector;

	// ==================== Drop Kick State ====================

	/** True if current attack is a delegated drop kick (movement handled by MeleeAttackComponent) */
	bool bIsDropKick = false;

	/** Height difference at drop kick start (for bonus damage calculation) */
	float DropKickHeightDifference = 0.0f;

	// ==================== Camera Focus State ====================

	/** Target actor for camera focus */
	TWeakObjectPtr<AActor> CameraFocusTarget;

	/** Time remaining for camera focus */
	float CameraFocusTimeRemaining = 0.0f;

	/** Initial rotation when focus started */
	FRotator CameraFocusStartRotation = FRotator::ZeroRotator;

	/** Target rotation for camera focus */
	FRotator CameraFocusTargetRotation = FRotator::ZeroRotator;

	// ==================== Durability Break State ====================

	/** Geometry Collection for break shatter (set from DroppedMeleeWeapon) */
	UPROPERTY()
	TObjectPtr<UGeometryCollection> BreakGeometryCollection;

	/** Break impulse for GC shatter */
	float BreakImpulse = 600.0f;

	/** Break angular impulse for tumbling gibs */
	float BreakAngularImpulse = 100.0f;

	/** Break gib lifetime */
	float BreakGibLifetime = 3.0f;

	/** Decrement hit count and check for break. Returns true if weapon broke. */
	bool DecrementHitCount();

	/** Execute weapon break: spawn GC, remove from inventory, switch weapon */
	void BreakWeapon();

	/** Spawn GC destruction at MeleeWeaponStaticMesh location */
	void SpawnBreakDestructionGC();
};
