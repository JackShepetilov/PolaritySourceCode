// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolarityCharacter.h"
#include "ShooterWeaponHolder.h"
#include "GenericTeamAgentInterface.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCDeath, AShooterNPC*, DeadNPC);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPolarityChangedDelegate_NPC, uint8, NewPolarity, float, ChargeValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChargeUpdatedDelegate_NPC, float, ChargeValue, uint8, Polarity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnNPCDamageTaken, AShooterNPC*, DamagedNPC, float, Damage, TSubclassOf<UDamageType>, DamageType, FVector, HitLocation, AActor*, DamageCauser);

class AShooterWeapon;
class UAnimMontage;
class UAIAccuracyComponent;
class UMeleeRetreatComponent;
class AAICombatCoordinator;
class USoundBase;
class UMaterialInterface;
class UEMFVelocityModifier;
class UEMF_FieldComponent;
class UNiagaraSystem;

/**
 *  A simple AI-controlled shooter game NPC
 *  Executes its behavior through a StateTree managed by its AI Controller
 *  Holds and manages a weapon
 */
UCLASS(abstract)
class POLARITY_API AShooterNPC : public APolarityCharacter, public IShooterWeaponHolder, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:

	AShooterNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Current HP for this character. It dies if it reaches zero through damage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float CurrentHP = 100.0f;

protected:

	/** AI accuracy component for speed-based spread */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	TObjectPtr<UAIAccuracyComponent> AccuracyComponent;

	/** Melee retreat component for proximity-based retreat */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	TObjectPtr<UMeleeRetreatComponent> MeleeRetreatComponent;

	/** EMF velocity modifier for charge-based interactions */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	TObjectPtr<UEMFVelocityModifier> EMFVelocityModifier;

	/** EMF field component for electromagnetic charge storage */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	/** Name of the collision profile to use during ragdoll death */
	UPROPERTY(EditAnywhere, Category = "Damage")
	FName RagdollCollisionProfile = FName("Ragdoll");

	/** Time to wait after death before destroying this actor */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float DeferredDestructionTime = 5.0f;

	/** Sound to play when this NPC dies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|SFX")
	TObjectPtr<USoundBase> DeathSound;

	/** Team byte for this character */
	UPROPERTY(EditAnywhere, Category = "Team")
	uint8 TeamByte = 1;

	// ==================== IGenericTeamAgentInterface ====================

	/** Returns the TeamId for this NPC (used by AI Perception Team Sense) */
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamByte); }

	/** Sets the TeamId for this NPC */
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { TeamByte = NewTeamId.GetId(); }

	/** Pointer to the equipped weapon */
	TObjectPtr<AShooterWeapon> Weapon;

	/** Type of weapon to spawn for this character */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max range for aiming calculations */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float AimRange = 10000.0f;

	// ==================== Hit Reactions ====================

	/** Animation montage when hit from the front */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions")
	TObjectPtr<UAnimMontage> HitReactionFrontMontage;

	/** Animation montage when hit from behind */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions")
	TObjectPtr<UAnimMontage> HitReactionBackMontage;

	/** Animation montage played during knockback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions")
	TObjectPtr<UAnimMontage> KnockbackMontage;

	/** Animation montage played while captured by channeling plate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions")
	TObjectPtr<UAnimMontage> CapturedMontage;

	/** Animation montage played while launched (in flight) after reverse channeling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions")
	TObjectPtr<UAnimMontage> LaunchedMontage;

	/** Minimum speed to stay in launched state. Below this, launched state ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions", meta = (ClampMin = "50.0"))
	float LaunchedMinSpeed = 200.0f;

	/** Minimum time between hit reaction animations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reactions", meta = (ClampMin = "0"))
	float HitReactionCooldown = 0.5f;

	/** Last time a hit reaction was played */
	float LastHitReactionTime = -1.0f;

	/** Cone variance to apply while aiming */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float AimVarianceHalfAngle = 10.0f;

	/** Minimum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float MinAimOffsetZ = -35.0f;

	/** Maximum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float MaxAimOffsetZ = -60.0f;

	// ==================== Burst Fire & Coordination ====================

	/** Maximum shots per burst */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat", meta = (ClampMin = "1", ClampMax = "20"))
	int32 BurstShotCount = 5;

	/** Cooldown between bursts (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float BurstCooldown = 1.5f;

	/** If true, NPC will use combat coordinator for attack permission */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat")
	bool bUseCoordinator = true;

	/** How often to retry getting attack permission (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PermissionRetryInterval = 0.25f;

	/** Current shot count in this burst */
	int32 CurrentBurstShots = 0;

	/** Timer handle for burst cooldown */
	FTimerHandle BurstCooldownTimer;

	/** Timer handle for permission retry */
	FTimerHandle PermissionRetryTimer;

	/** If true, NPC is in burst cooldown and cannot shoot */
	bool bInBurstCooldown = false;

	/** If true, NPC has permission to attack from coordinator */
	bool bHasAttackPermission = false;

	/** If true, NPC wants to shoot but waiting for permission */
	bool bWantsToShoot = false;

	/** Actor currently being targeted */
	TWeakObjectPtr<AActor> CurrentAimTarget;

	/** If true, this character is currently shooting its weapon */
	bool bIsShooting = false;

	/** If true, this character has already died */
	bool bIsDead = false;

	/** Deferred destruction on death timer */
	FTimerHandle DeathTimer;

	// ==================== Charge Overlay Materials ====================

	/** If true, overlay material will be applied based on charge state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay")
	bool bUseChargeOverlay = false;

	/** Overlay material to apply when charge is neutral (near zero) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NeutralChargeOverlayMaterial;

	/** Overlay material to apply when charge is positive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> PositiveChargeOverlayMaterial;

	/** Overlay material to apply when charge is negative */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NegativeChargeOverlayMaterial;

	/** Previous polarity state for change detection (0=Neutral, 1=Positive, 2=Negative) */
	uint8 PreviousPolarity = 0;

	/** Previous charge value for change detection */
	float PreviousChargeValue = 0.0f;

	// ==================== Knockback ====================

	/** Multiplier for knockback distance (1.0 = normal, 0.5 = half distance, 2.0 = double distance).
	 *  Use this to create "heavier" enemies that resist knockback without modifying mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float KnockbackDistanceMultiplier = 1.0f;

	/** Ground friction during knockback slide (lower = more slippery) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float KnockbackGroundFriction = 0.2f;

	/** If true, disable EMF forces during knockback for consistent physics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback")
	bool bDisableEMFDuringKnockback = true;

	/** Timer for knockback stun recovery */
	FTimerHandle KnockbackStunTimer;

	/** Timer for knockback position interpolation */
	FTimerHandle KnockbackInterpTimer;

	/** True when NPC is in knockback state */
	bool bIsInKnockback = false;

	/** True when NPC is captured by channeling plate */
	bool bIsCaptured = false;

	/** True when NPC was launched by reverse channeling and is in flight */
	bool bIsLaunched = false;

	/** True when NPC is being interpolated to knockback target position */
	bool bIsKnockbackInterpolating = false;

	/** Start position for knockback interpolation */
	FVector KnockbackStartPosition = FVector::ZeroVector;

	/** Target position for knockback interpolation */
	FVector KnockbackTargetPosition = FVector::ZeroVector;

	/** Direction of current knockback (for wall collision) */
	FVector KnockbackDirection = FVector::ZeroVector;

	/** Position of attacker during knockback (for facing during animation) */
	FVector KnockbackAttackerPosition = FVector::ZeroVector;

	/** Total duration of current knockback interpolation */
	float KnockbackTotalDuration = 0.0f;

	/** Elapsed time in current knockback interpolation */
	float KnockbackElapsedTime = 0.0f;

	/** Cached ground friction for restore after knockback */
	float CachedGroundFriction = 8.0f;

	/** Cached braking deceleration for restore after knockback */
	float CachedBrakingDeceleration = 2048.0f;

	/** Time of last EMF proximity knockback trigger (for cooldown) */
	float LastEMFProximityTriggerTime = -10.0f;

	// ==================== Wall Slam ====================

	/** Minimum orthogonal impulse to trigger surface slam damage (any surface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam", meta = (ClampMin = "0"))
	float WallSlamVelocityThreshold = 800.0f;

	/** Damage dealt per 100 units of orthogonal impulse above threshold */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam", meta = (ClampMin = "0"))
	float WallSlamDamagePerVelocity = 10.0f;

	/** Sound to play on wall slam */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam|Effects")
	TObjectPtr<USoundBase> WallSlamSound;

	/** VFX to spawn on wall slam */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam|Effects")
	TObjectPtr<UNiagaraSystem> WallSlamVFX;

	/** Scale for wall slam VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam|Effects", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float WallSlamVFXScale = 1.0f;

	/** Cooldown between wall slam damage events (prevents multi-trigger) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float WallSlamCooldown = 0.2f;

	// ==================== Wall Bounce ====================

	/** Enable wall bounce during knockback (NPC bounces off walls instead of stopping) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam|Bounce")
	bool bEnableWallBounce = true;

	/** Elasticity coefficient for wall bounce (0 = no bounce, 1 = perfect bounce) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam|Bounce", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableWallBounce"))
	float WallBounceElasticity = 0.5f;

	/** Minimum velocity to trigger wall bounce (below this NPC just stops) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Slam|Bounce", meta = (ClampMin = "0", EditCondition = "bEnableWallBounce"))
	float WallBounceMinVelocity = 200.0f;

	/** Velocity from previous tick (used for impact damage calculation) */
	FVector PreviousTickVelocity = FVector::ZeroVector;

	/** Time of last wall slam damage (for cooldown) */
	float LastWallSlamTime = -1.0f;

	// ==================== NPC Collision ====================

	/** Enable explosion-like elastic collision between NPCs during knockback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|NPC Collision")
	bool bEnableNPCCollision = true;

	/** Impulse multiplier when NPCs collide (both get this fraction of original speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|NPC Collision", meta = (ClampMin = "0.5", ClampMax = "1.5", EditCondition = "bEnableNPCCollision"))
	float NPCCollisionImpulseMultiplier = 0.7f;

	/** Damage multiplier relative to wall slam damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|NPC Collision", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableNPCCollision"))
	float NPCCollisionDamageMultiplier = 0.4f;

	/** Minimum velocity for NPC collision to trigger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|NPC Collision", meta = (ClampMin = "0", EditCondition = "bEnableNPCCollision"))
	float NPCCollisionMinVelocity = 300.0f;

	// ==================== EMF Proximity Knockback ====================

	/** Enable EMF-based attraction knockback when NPCs with opposite charges get close */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity")
	bool bEnableEMFProximityKnockback = true;

	/** Minimum EMF acceleration threshold to trigger proximity knockback (cm/s²) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFProximityAccelerationThreshold = 1000.0f;

	/** Knockback distance for EMF proximity attraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFProximityKnockbackDistance = 100.0f;

	/** Knockback duration for EMF proximity attraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0.1", ClampMax = "2.0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFProximityKnockbackDuration = 0.5f;

	/** Cooldown between EMF proximity knockback triggers (prevents spam) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFProximityTriggerCooldown = 0.5f;

	/** Impulse multiplier based on total charge magnitude when EMF NPCs collide */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0.0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFDischargeImpulsePerCharge = 10.0f;

	/** VFX to spawn at EMF discharge collision point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (EditCondition = "bEnableEMFProximityKnockback"))
	TObjectPtr<UNiagaraSystem> EMFDischargeVFX;

	/** Scale for EMF discharge VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFDischargeVFXScale = 1.0f;

	/** Sound to play at EMF discharge collision */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (EditCondition = "bEnableEMFProximityKnockback"))
	TObjectPtr<USoundBase> EMFDischargeSound;

	/** Damage dealt to both NPCs when EMF proximity knockback triggers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFProximityDamage = 10.0f;

	/** Delay before EMF proximity damage is applied (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback|EMF Proximity", meta = (ClampMin = "0", ClampMax = "2.0", EditCondition = "bEnableEMFProximityKnockback"))
	float EMFProximityDamageDelay = 0.2f;

	/** Timer for delayed EMF proximity damage */
	FTimerHandle EMFProximityDamageTimer;

	// ==================== Melee Charge Transfer ====================

	/** Charge change when hit by melee attack (opposite sign to player's gain) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Charge", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float ChargeChangeOnMeleeHit = -25.0f;

public:

	/** Delegate called when this NPC dies - can be bound in Blueprints */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNPCDeath OnNPCDeath;

	/** Called when polarity changes (0=Neutral, 1=Positive, 2=Negative) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FPolarityChangedDelegate_NPC OnPolarityChanged;

	/** Called every tick with current charge and polarity */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FChargeUpdatedDelegate_NPC OnChargeUpdated;

	/** Called when this NPC takes damage - used for damage number display */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNPCDamageTaken OnDamageTaken;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Called when possessed by a controller */
	virtual void PossessedBy(AController* NewController) override;

	/** Gameplay cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Per-frame updates */
	virtual void Tick(float DeltaTime) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Called when HP is depleted and the character should die */
	void Die();

	/** Called after death to destroy the actor */
	void DeferredDestruction();

	/** Play hit reaction animation based on damage direction */
	void PlayHitReaction(const FVector& DamageDirection);

	/** Update overlay material based on current charge polarity */
	void UpdateChargeOverlay(uint8 NewPolarity);

	// ==================== Coordinator Integration ====================

	/** Register with combat coordinator */
	void RegisterWithCoordinator();

	/** Unregister from combat coordinator */
	void UnregisterFromCoordinator();

	/** Request attack permission from coordinator */
	bool RequestAttackPermission();

	/** Release attack permission */
	void ReleaseAttackPermission();

	// ==================== Burst Fire ====================

	/** Called when burst cooldown ends */
	void OnBurstCooldownEnd();

	/** Called when weapon fires a shot - increment burst counter and check if burst complete */
	UFUNCTION()
	void OnWeaponShotFired();

	/** Try to start shooting (called periodically while waiting for permission) */
	void TryStartShooting();

	/** Start the permission retry timer */
	void StartPermissionRetryTimer();

	/** Stop the permission retry timer */
	void StopPermissionRetryTimer();

public:

	/** Signals this character to start shooting at the passed actor
	 *  @param ActorToShoot Target to shoot at
	 *  @param bHasExternalPermission If true, skip coordinator permission check (StateTree already got permission)
	 */
	void StartShooting(AActor* ActorToShoot, bool bHasExternalPermission = false);

	/** Signals this character to stop shooting */
	void StopShooting();

protected:
	/** If true, external system (StateTree) has already obtained permission - skip internal check */
	bool bExternalPermissionGranted = false;

public:
	/** Apply distance-based knockback with smooth interpolation to target position
	 *  @param KnockbackDirection Direction to knock the NPC (normalized)
	 *  @param Distance Total distance to travel in cm
	 *  @param Duration Duration of the knockback interpolation in seconds
	 *  @param AttackerLocation Position of the attacker (for rotation during knockback, optional)
	 *  @param bKeepEMFEnabled If true, don't disable EMF forces during knockback (for EMF attraction mechanic)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void ApplyKnockback(const FVector& KnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false);

	/** Legacy knockback using velocity (converts to distance-based internally)
	 *  @param KnockbackVelocity Velocity vector for knockback
	 *  @param StunDuration Duration of AI stun
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void ApplyKnockbackVelocity(const FVector& KnockbackVelocity, float StunDuration = 0.3f);

	/** Returns true if this NPC is currently in knockback state */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInKnockback() const { return bIsInKnockback; }

	/** Enter captured state (channeling plate grab). Blocks AI, plays montage. */
	void EnterCapturedState(UAnimMontage* OverrideMontage = nullptr);

	/** Exit captured state. Restores AI, stops montage. */
	void ExitCapturedState();

private:
	/** Currently playing captured montage (for looping and stop) */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveCapturedMontage;

	/** Callback to re-play captured montage when it ends (looping) */
	UFUNCTION()
	void OnCapturedMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Currently playing launched montage (for looping and stop) */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveLaunchedMontage;

	/** Callback to re-play launched montage when it ends (looping) */
	UFUNCTION()
	void OnLaunchedMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Exit launched state — stop montage, clear flags */
	void ExitLaunchedState();

	/** Check for NPC collisions during launched flight */
	void UpdateLaunchedCollision();

public:

	/** Returns true if captured by channeling plate */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCaptured() const { return bIsCaptured; }

	/** Enter launched state after reverse channeling release. Plays montage, enables NPC-NPC collision. */
	void EnterLaunchedState();

	/** Returns true if NPC is in flight after reverse channeling launch */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsLaunched() const { return bIsLaunched; }

	/** Returns true if this NPC is dead */
	UFUNCTION(BlueprintPure, Category = "Status")
	bool IsDead() const { return bIsDead; }

	/** Returns true if this NPC is currently shooting */
	UFUNCTION(BlueprintPure, Category = "Status")
	bool IsCurrentlyShooting() const { return bIsShooting && !bIsDead; }

	/** Returns true if this NPC is in burst cooldown (burst completed, waiting to fire again) */
	UFUNCTION(BlueprintPure, Category = "Status")
	bool IsInBurstCooldown() const { return bInBurstCooldown; }

	/** Check if NPC has line of sight to target */
	UFUNCTION(BlueprintPure, Category = "Combat")
	virtual bool HasLineOfSightTo(AActor* Target) const;

	/** Get the knockback distance multiplier for this NPC */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetKnockbackDistanceMultiplier() const { return KnockbackDistanceMultiplier; }

protected:

	/** Called when stun ends to restore AI movement */
	virtual void EndKnockbackStun();

	/** Update knockback position interpolation (called from Tick) */
	void UpdateKnockbackInterpolation(float DeltaTime);

	/** Check for wall collision during knockback and handle it */
	bool CheckKnockbackWallCollision(const FVector& CurrentPos, const FVector& NextPos, FHitResult& OutHit);

	/** Handle wall collision during knockback - trigger damage and stop */
	void HandleKnockbackWallHit(const FHitResult& WallHit);

	/** Handle explosion-like elastic collision between two NPCs during knockback (legacy - uses stored knockback params) */
	void HandleElasticNPCCollision(AShooterNPC* OtherNPC, const FVector& CollisionPoint);

	/** Handle elastic NPC collision with explicit impact speed (used by overlap sweep detection) */
	void HandleElasticNPCCollisionWithSpeed(AShooterNPC* OtherNPC, const FVector& CollisionPoint, float ImpactSpeed);

	/** Check for EMF proximity with other NPCs and trigger attraction knockback if threshold exceeded */
	void CheckEMFProximityCollision();

	/** Trigger EMF-based attraction knockback towards another NPC */
	void TriggerEMFProximityKnockback(AShooterNPC* OtherNPC);

	/** Called when capsule hits something - checks for wall slam */
	UFUNCTION()
	virtual void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// ==================== AI Perception Handlers ====================

	/** Called when AI controller spots an enemy - override in Blueprint */
	UFUNCTION(BlueprintNativeEvent, Category = "AI|Perception")
	void OnEnemySpotted(AActor* SpottedEnemy, FVector LastKnownLocation);
	virtual void OnEnemySpotted_Implementation(AActor* SpottedEnemy, FVector LastKnownLocation);

	/** Called when AI controller loses sight of an enemy - override in Blueprint */
	UFUNCTION(BlueprintNativeEvent, Category = "AI|Perception")
	void OnEnemyLost(AActor* LostEnemy);
	virtual void OnEnemyLost_Implementation(AActor* LostEnemy);

	/** Called when AI controller receives team perception about an enemy - override in Blueprint */
	UFUNCTION(BlueprintNativeEvent, Category = "AI|Perception")
	void OnTeamPerceptionReceived(AActor* ReportedEnemy, FVector LastKnownLocation);
	virtual void OnTeamPerceptionReceived_Implementation(AActor* ReportedEnemy, FVector LastKnownLocation);

	// ==================== Checkpoint System ====================
public:
	/** Set checkpoint spawn ID (called by CheckpointSubsystem) */
	void SetCheckpointSpawnID(const FGuid& InSpawnID) { CheckpointSpawnID = InSpawnID; }

	/** Get checkpoint spawn ID */
	FGuid GetCheckpointSpawnID() const { return CheckpointSpawnID; }

private:
	/** Unique ID for checkpoint respawn tracking */
	FGuid CheckpointSpawnID;
};