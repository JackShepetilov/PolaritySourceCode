// SniperTurretNPC.h
// Stationary turret NPC — TF2-sentry style.
// Continuously fires its weapon at the player while it has line of sight, using the same
// weapon + accuracy-spread path as AShooterNPC. The barrel slews onto the target via bone
// rotation; fire only opens once the barrel is aligned within FireAlignmentAngle.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "SniperTurretNPC.generated.h"

/**
 * Turret engagement state machine.
 * NOTE: DamageRecovery and PostFireCooldown are VESTIGIAL — kept only so existing StateTree
 * assets / Blueprints that reference the enum don't break. The sentry never enters them.
 * Live states: Idle / Aiming / Firing.
 */
UENUM(BlueprintType)
enum class ETurretAimState : uint8
{
	/** No target or no line of sight — not firing */
	Idle,
	/** Target acquired with LOS, barrel still slewing onto it — not firing yet */
	Aiming,
	/** Target + LOS + barrel aligned — firing continuously */
	Firing,
	/** Vestigial (no longer used) */
	DamageRecovery,
	/** Vestigial (no longer used) */
	PostFireCooldown
};

/** Broadcast when engagement state changes. Progress is 1.0 while Firing, else 0.0. Kept for BP/UI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTurretAimProgressChanged,
	float, AimProgress, ETurretAimState, AimState);

/** Broadcast when the turret fires. Vestigial — not emitted in continuous-fire mode (weapon drives its own cadence). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTurretFired);

class UPoseableMeshComponent;
class UCurveFloat;

/**
 * Stationary sentry turret NPC.
 * Uses a PoseableMeshComponent with bone-based Yaw/Pitch rotation to track the target.
 * Drives its weapon directly (StartFiring/StopFiring) for continuous full-auto fire while
 * the target is visible — accuracy/spread come from the inherited AShooterNPC weapon path.
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

	// ==================== Firing ====================

	/** The barrel must be within this angle (degrees) of the target on BOTH yaw and pitch
	 *  before the turret opens fire. Prevents spraying the wall while still slewing onto target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Firing",
		meta = (ClampMin = "0.5", ClampMax = "45.0"))
	float FireAlignmentAngle = 8.0f;

	// ==================== Fire-Rate Spin-Up ====================

	/** Refire-interval multiplier at the START of a burst (spin-up cold). 1.0 = no spin-up,
	 *  3.0 = fires 3x slower when it first opens up, ramping to full speed over SpinUpDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Firing|Spin-Up",
		meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float SpinUpStartMultiplier = 3.0f;

	/** Seconds of continuous fire to reach full fire rate (the weapon's authored RefireRate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Firing|Spin-Up",
		meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float SpinUpDuration = 3.0f;

	/** Seconds for the accumulated spin-up to fully decay back to cold while NOT firing
	 *  (LOS break / player in cover). Brief breaks barely dent it; long ones reset it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Firing|Spin-Up",
		meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float SpinDownDuration = 1.5f;

	/** Optional shaping curve: maps raw spin-up alpha (0..1) to an eased alpha (0..1).
	 *  Null = linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Firing|Spin-Up")
	TObjectPtr<UCurveFloat> SpinUpCurve;

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

	// ==================== Engagement State (Runtime) ====================

	/** Current engagement state */
	ETurretAimState CurrentAimState = ETurretAimState::Idle;

	/** Current target to track and fire at */
	TWeakObjectPtr<AActor> AimTarget;

	/** True if turret currently has LOS to the target (updated by the StateTree task) */
	bool bHasLOS = false;

	/** True while the weapon is actively firing (Weapon->StartFiring() is in effect) */
	bool bSentryFiring = false;

	/** True when the barrel is within FireAlignmentAngle of the target on both axes */
	bool bBarrelAligned = false;

	/** Current spin-up progress (0 = cold/slow, 1 = full fire rate). Ramps up while firing,
	 *  decays while not firing, resets to 0 on a target switch. */
	float SpinUpAlpha = 0.0f;

	/** Target the current spin-up progress belongs to. A new (different) target resets SpinUpAlpha.
	 *  Persists across brief disengagements so spin-down — not a hard reset — handles LOS breaks. */
	TWeakObjectPtr<AActor> SpinUpTarget;

	/** Current interpolated yaw angle (relative to actor forward) */
	float CurrentYaw = 0.0f;

	/** Current interpolated pitch angle */
	float CurrentPitch = 0.0f;

public:

	// ==================== Delegates (kept for BP/UI compatibility) ====================

	/** Broadcast on engagement-state change (Progress = 1.0 while Firing, else 0.0). */
	UPROPERTY(BlueprintAssignable, Category = "Turret|Events")
	FOnTurretAimProgressChanged OnAimProgressChanged;

	/** Vestigial — not emitted in continuous-fire mode. */
	UPROPERTY(BlueprintAssignable, Category = "Turret|Events")
	FOnTurretFired OnTurretFired;

	// ==================== Engagement Interface (for StateTree) ====================

	/** Acquire a target to track. Fire opens automatically once LOS + barrel alignment are met. */
	UFUNCTION(BlueprintCallable, Category = "Turret|Aiming")
	void StartAiming(AActor* Target);

	/** Disengage: stop firing and clear the target. */
	UFUNCTION(BlueprintCallable, Category = "Turret|Aiming")
	void StopAiming();

	/** Notify that LOS status changed. Losing LOS stops fire immediately. */
	UFUNCTION(BlueprintCallable, Category = "Turret|Aiming")
	void SetLOSStatus(bool bNewHasLOS);

	// ==================== State Queries ====================

	/** Returns current engagement state */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	ETurretAimState GetAimState() const { return CurrentAimState; }

	/** 1.0 while firing, else 0.0 (kept for BP/UI compatibility) */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	float GetAimProgress() const { return bSentryFiring ? 1.0f : 0.0f; }

	/** Vestigial — always false (damage no longer interrupts the turret) */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsInDamageRecovery() const { return false; }

	/** True if turret is engaged but not yet firing (slewing onto target) */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsAiming() const { return CurrentAimState == ETurretAimState::Aiming; }

	/** Vestigial — always false (no post-fire cooldown in continuous-fire mode) */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsInPostFireCooldown() const { return false; }

	/** True while the turret is actively firing at its target */
	UFUNCTION(BlueprintPure, Category = "Turret|State")
	bool IsFiring() const { return CurrentAimState == ETurretAimState::Firing; }

protected:

	// ==================== Lifecycle Overrides ====================

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Overrides from AShooterNPC ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	virtual void ApplyKnockback(const FVector& InKnockbackDirection, float Distance,
		float Duration, const FVector& AttackerLocation = FVector::ZeroVector,
		bool bKeepEMFEnabled = false, EKnockbackStyle Style = EKnockbackStyle::Standard) override;

	virtual void ApplyKnockbackVelocity(const FVector& KnockbackVelocity,
		float StunDuration = 0.3f) override;

public:

	/** LOS check from the turret muzzle (public for StateTree tasks) */
	virtual bool HasLineOfSightTo(AActor* Target) const override;

private:

	// ==================== Internal Logic ====================

	/** Rotate turret bones toward AimTarget and update bBarrelAligned. Called from Tick. */
	void UpdateTurretRotation(float DeltaTime);

	/** Advance/decay the fire-rate spin-up and push the resulting multiplier onto the weapon. */
	void UpdateSpinUp(float DeltaTime);

	/** Map the current SpinUpAlpha (+ optional curve) to the weapon's refire multiplier and apply it. */
	void ApplySpinUpMultiplier();

	/** Begin continuous fire (idempotent). Requires a valid weapon. */
	void BeginSentryFire();

	/** Stop continuous fire (idempotent). */
	void EndSentryFire();

	/** Transition engagement state and broadcast for BP/UI. */
	void SetAimState(ETurretAimState NewState);
};
