// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolarityCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCDeath, AShooterNPC*, DeadNPC);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPolarityChangedDelegate_NPC, uint8, NewPolarity, float, ChargeValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChargeUpdatedDelegate_NPC, float, ChargeValue, uint8, Polarity);

class AShooterWeapon;
class UAnimMontage;
class UAIAccuracyComponent;
class UMeleeRetreatComponent;
class AAICombatCoordinator;
class USoundBase;
class UMaterialInterface;
class UEMFVelocityModifier;

/**
 *  A simple AI-controlled shooter game NPC
 *  Executes its behavior through a StateTree managed by its AI Controller
 *  Holds and manages a weapon
 */
UCLASS(abstract)
class POLARITY_API AShooterNPC : public APolarityCharacter, public IShooterWeaponHolder
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

	// ==================== Knockback ====================

	/** Timer for knockback stun recovery */
	FTimerHandle KnockbackStunTimer;

	/** Cached ground friction for restore after knockback */
	float CachedGroundFriction = 8.0f;

	/** Cached braking deceleration for restore after knockback */
	float CachedBrakingDeceleration = 2048.0f;

	/** Previous polarity state for change detection (0=Neutral, 1=Positive, 2=Negative) */
	uint8 PreviousPolarity = 0;

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

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

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

	/** Increment burst shot counter and check if burst complete */
	void OnShotFired();

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
	/** Apply knockback impulse with temporary AI stun */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void ApplyKnockback(const FVector& KnockbackVelocity, float StunDuration = 0.3f);

	/** Returns true if this NPC is dead */
	UFUNCTION(BlueprintPure, Category = "Status")
	bool IsDead() const { return bIsDead; }

	/** Returns true if this NPC is currently shooting */
	UFUNCTION(BlueprintPure, Category = "Status")
	bool IsCurrentlyShooting() const { return bIsShooting && !bIsDead; }

protected:

	/** Called when stun ends to restore AI movement */
	void EndKnockbackStun();
};