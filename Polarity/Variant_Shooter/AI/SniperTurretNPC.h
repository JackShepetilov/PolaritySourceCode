// SniperTurretNPC.h
// Stationary sniper turret NPC - progressive aim system locks onto target before firing hitscan

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "SniperTurretNPC.generated.h"

/** Turret aiming state machine */
UENUM(BlueprintType)
enum class ETurretAimState : uint8
{
	/** No target, idle */
	Idle,
	/** Target acquired, aiming in progress (0.0 -> 1.0) */
	Aiming,
	/** Aim complete, firing weapon */
	Firing,
	/** Recovering from damage interruption (waiting before re-aim) */
	DamageRecovery,
	/** Cooldown after firing before re-aiming */
	PostFireCooldown
};

/** Broadcast when aim progress or state changes (for outline/UI) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTurretAimProgressChanged,
	float, AimProgress, ETurretAimState, AimState);

/** Broadcast when turret fires */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTurretFired);

class UPoseableMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;

/**
 * Stationary sniper turret NPC.
 * Uses PoseableMeshComponent (skeletal) with bone-based rotation for Yaw/Pitch.
 * Progressive aiming: locks onto target over configurable duration before firing hitscan.
 *
 * Aim interrupted by:
 * - Damage above threshold: resets progress, enters recovery delay
 * - LOS break: resets progress, re-aims immediately when LOS restored
 */
UCLASS()
class POLARITY_API ASniperTurretNPC : public AShooterNPC
{
	GENERATED_BODY()

public:

	ASniperTurretNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	// ==================== Components ====================

	/** Skeletal mesh for turret visual (set mesh in Blueprint) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPoseableMeshComponent> TurretMesh;

	// ==================== Aiming Parameters ====================

	/** Duration in seconds for aim progress to go from 0.0 to 1.0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aiming",
		meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float AimDuration = 2.0f;

	/** Delay after damage interruption before turret re-aims (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aiming",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float DamageRecoveryDelay = 1.5f;

	/** Minimum damage in a single hit to interrupt aiming (below this, aim continues) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aiming",
		meta = (ClampMin = "0.0"))
	float AimInterruptDamageThreshold = 5.0f;

	/** Cooldown after firing before re-aiming (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aiming",
		meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float PostFireCooldownDuration = 1.0f;

	// ==================== Turret Rotation ====================

	/** Speed at which turret rotates to face the target (degrees/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Rotation",
		meta = (ClampMin = "10.0", ClampMax = "720.0"))
	float TurretRotationSpeed = 90.0f;

	/** Maximum pitch angle the turret can tilt up (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Rotation",
		meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxPitchUp = 45.0f;

	/** Maximum pitch angle the turret can tilt down (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Rotation",
		meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxPitchDown = 30.0f;

	// ==================== Bone Names ====================

	/** Bone name for horizontal rotation (Yaw). Set in Blueprint to match skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Bones")
	FName YawBoneName = NAME_None;

	/** Bone name for vertical rotation (Pitch). If None, turret is yaw-only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Bones")
	FName PitchBoneName = NAME_None;

	// ==================== Weapon Socket ====================

	/** Socket name on TurretMesh for weapon attachment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Weapon")
	FName TurretWeaponSocket = FName("Muzzle");

	// ==================== Aim Laser Telegraph ====================

	/** Niagara system spawned at TurretWeaponSocket while turret is aiming. Deactivates on aim interrupt, reactivates on resume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aim Telegraph")
	TObjectPtr<UNiagaraSystem> AimLaserVFX;

	/** Additional offset applied to laser spawn point relative to the weapon socket (local space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aim Telegraph")
	FVector AimLaserSpawnOffset = FVector::ZeroVector;

	/** User-parameter name (float) in the Niagara system that receives aim progress (0..1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aim Telegraph")
	FName AimLaserIntensityParam = FName("Intensity");

	/** User-parameter name (vector) in the Niagara system that receives the beam end location (world space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Aim Telegraph")
	FName AimLaserBeamEndParam = FName("BeamEnd");

	/** Currently active laser VFX component (null when not aiming). Spawn-once, Activate/Deactivate during aim cycles. */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveAimLaser;

	/** Last player notified as being targeted. Needed to send a disengage notification after AimTarget is cleared. */
	TWeakObjectPtr<AActor> LastTelegraphedPlayer;

	// ==================== Aiming State (Runtime) ====================

	/** Current aim state */
	ETurretAimState CurrentAimState = ETurretAimState::Idle;

	/** Current aim progress (0.0 to 1.0) */
	float AimProgress = 0.0f;

	/** Current target for aiming */
	TWeakObjectPtr<AActor> AimTarget;

	/** Timer handle for damage recovery delay */
	FTimerHandle DamageRecoveryTimer;

	/** Timer handle for post-fire cooldown */
	FTimerHandle PostFireCooldownTimer;

	/** True if turret currently has LOS to target */
	bool bHasLOS = false;

	/** Current interpolated yaw angle (relative to actor forward) */
	float CurrentYaw = 0.0f;

	/** Current interpolated pitch angle */
	float CurrentPitch = 0.0f;

public:

	// ==================== Delegates ====================

	/** Broadcast each tick with current aim progress and state.
	 *  Bind in Blueprint to update aim outline/reticle/indicator. */
	UPROPERTY(BlueprintAssignable, Category = "Turret|Events")
	FOnTurretAimProgressChanged OnAimProgressChanged;

	/** Broadcast when turret fires its weapon */
	UPROPERTY(BlueprintAssignable, Category = "Turret|Events")
	FOnTurretFired OnTurretFired;

	// ==================== Aiming Interface (for StateTree) ====================

	/** Begin aiming at target. Starts aim progress from 0. */
	UFUNCTION(BlueprintCallable, Category = "Turret|Aiming")
	void StartAiming(AActor* Target);

	/** Stop aiming. Resets aim progress and clears target. */
	UFUNCTION(BlueprintCallable, Category = "Turret|Aiming")
	void StopAiming();

	/** Notify that LOS status changed. Lost LOS resets progress but no recovery delay. */
	UFUNCTION(BlueprintCallable, Category = "Turret|Aiming")
	void SetLOSStatus(bool bNewHasLOS);

	// ==================== State Queries ====================

	/** Returns current aim state */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	ETurretAimState GetAimState() const { return CurrentAimState; }

	/** Returns current aim progress (0.0 to 1.0) */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	float GetAimProgress() const { return AimProgress; }

	/** Returns true if turret is in damage recovery state */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsInDamageRecovery() const { return CurrentAimState == ETurretAimState::DamageRecovery; }

	/** Returns true if turret is actively aiming */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsAiming() const { return CurrentAimState == ETurretAimState::Aiming; }

	/** Returns true if turret is in post-fire cooldown */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsInPostFireCooldown() const { return CurrentAimState == ETurretAimState::PostFireCooldown; }

protected:

	// ==================== Lifecycle Overrides ====================

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Overrides from AShooterNPC ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;
	virtual FVector GetWeaponTargetLocation() override;

	virtual void ApplyKnockback(const FVector& InKnockbackDirection, float Distance,
		float Duration, const FVector& AttackerLocation = FVector::ZeroVector,
		bool bKeepEMFEnabled = false) override;

	virtual void ApplyKnockbackVelocity(const FVector& KnockbackVelocity,
		float StunDuration = 0.3f) override;

public:

	/** LOS check from turret mesh position (public for StateTree tasks) */
	virtual bool HasLineOfSightTo(AActor* Target) const override;

private:

	// ==================== Internal Aiming Logic ====================

	/** Advance aim progress. Called from Tick when state is Aiming. */
	void UpdateAimProgress(float DeltaTime);

	/** Rotate turret mesh toward AimTarget. Called from Tick. */
	void UpdateTurretRotation(float DeltaTime);

	/** Fire the weapon at the current target. Transitions to PostFireCooldown. */
	void FireAtTarget();

	/** Called when damage recovery delay expires */
	void OnDamageRecoveryEnd();

	/** Called when post-fire cooldown expires */
	void OnPostFireCooldownEnd();

	/** Reset aim progress and broadcast */
	void ResetAimProgress();

	/** Transition to a new aim state and broadcast */
	void SetAimState(ETurretAimState NewState);

	/** Handler bound to OnAimProgressChanged — drives laser VFX lifecycle and notifies player character */
	UFUNCTION()
	void HandleAimProgressChanged(float Progress, ETurretAimState AimState);
};
