// MeleeAttackComponent.h
// Quick melee attack system - works independently of equipped weapon
// Features mesh-switching animation system with camera-aligned attacks

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeleeAttackComponent.generated.h"

class UAnimMontage;
class USoundBase;
class UDamageType;
class UCameraShakeBase;
class UCameraComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UCurveFloat;

/**
 * Type of melee attack based on movement state
 */
UENUM(BlueprintType)
enum class EMeleeAttackType : uint8
{
	Ground,		// Standing, walking, running
	Airborne,	// In air (jumping, falling)
	Sliding		// During slide
};

/**
 * Melee attack state
 */
UENUM(BlueprintType)
enum class EMeleeAttackState : uint8
{
	Ready,			// Can attack
	HidingWeapon,	// Transitioning FirstPersonMesh down, hiding weapon
	InputDelay,		// Input delay before windup
	Windup,			// Wind-up phase (can be interrupted)
	Active,			// Damage-dealing phase
	Recovery,		// Recovery phase
	ShowingWeapon,	// Transitioning MeleeMesh out, showing FirstPersonMesh
	Cooldown		// On cooldown
};

/**
 * Animation data for a specific melee attack type
 */
USTRUCT(BlueprintType)
struct FMeleeAnimationData
{
	GENERATED_BODY()

	/** Animation montage for this attack type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> AttackMontage;

	/** Play rate curve (X = normalized time 0-1, Y = play rate multiplier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UCurveFloat> PlayRateCurve;

	/** Camera shake for swing (create in Editor as Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SwingCameraShake;

	/** Camera shake scale for this attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SwingShakeScale = 1.0f;

	/** Base play rate multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float BasePlayRate = 1.0f;

	/** Location offset for MeleeMesh during this attack (relative to camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FVector MeshLocationOffset = FVector::ZeroVector;

	/** Rotation offset for MeleeMesh during this attack (added to camera rotation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FRotator MeshRotationOffset = FRotator::ZeroRotator;

	/** Bones to hide for this specific attack (e.g., hide arms for kick, hide right arm for left punch) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TArray<FName> HiddenBones;
};

/**
 * Melee attack settings
 */
USTRUCT(BlueprintType)
struct FMeleeAttackSettings
{
	GENERATED_BODY()

	// ==================== Damage ====================

	/** Base damage per hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float BaseDamage = 50.0f;

	/** Damage multiplier for headshots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 1.5f;

	/** Damage type class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TSubclassOf<UDamageType> DamageType;

	/** Impulse applied to hit physics objects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float HitImpulse = 500.0f;

	// ==================== Momentum Damage ====================

	/** Additional damage per 100 units of player velocity towards target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Momentum Damage", meta = (ClampMin = "0"))
	float MomentumDamagePerSpeed = 10.0f;

	/** Maximum bonus damage from momentum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Momentum Damage", meta = (ClampMin = "0"))
	float MaxMomentumDamage = 50.0f;

	/** Additional impulse multiplier based on player speed (impulse = HitImpulse * (1 + speed * MomentumImpulseMultiplier)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Momentum Damage", meta = (ClampMin = "0"))
	float MomentumImpulseMultiplier = 0.002f;

	// ==================== Titanfall 2 Momentum System ====================

	/** Enable Titanfall 2 style momentum preservation - player keeps velocity during melee */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum")
	bool bPreserveMomentum = true;

	/** How much of the original velocity to preserve during melee (1.0 = 100%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum", meta = (ClampMin = "0", ClampMax = "1.0", EditCondition = "bPreserveMomentum"))
	float MomentumPreservationRatio = 1.0f;

	/** Enable "lunge to target" - player moves toward magnetism target instead of target being pulled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum")
	bool bLungeToTarget = true;

	/** Speed at which player lunges toward target (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum", meta = (ClampMin = "0", EditCondition = "bLungeToTarget"))
	float LungeToTargetSpeed = 2000.0f;

	/** Transfer player momentum to target on hit (Titanfall 2 flying kick feel) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum")
	bool bTransferMomentumOnHit = true;

	/** Multiplier for momentum transferred to target (1.0 = full velocity transfer) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum", meta = (ClampMin = "0", ClampMax = "2.0", EditCondition = "bTransferMomentumOnHit"))
	float MomentumTransferMultiplier = 1.0f;

	/** Minimum speed to trigger lunge-to-target (prevents weak lunges when stationary) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum", meta = (ClampMin = "0", EditCondition = "bLungeToTarget"))
	float MinSpeedForLungeToTarget = 300.0f;

	// ==================== Cool Kick ====================

	/** Duration of the cool kick period (applied when hitting enemy in air without lunge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cool Kick", meta = (ClampMin = "0", ClampMax = "2.0"))
	float CoolKickDuration = 0.3f;

	/** Speed boost added gradually over the cool kick period (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cool Kick", meta = (ClampMin = "0", ClampMax = "2000.0"))
	float CoolKickSpeedBoost = 400.0f;

	// ==================== Target Magnetism ====================

	/** Enable predictive target magnetism (pulls targets toward punch center) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Magnetism")
	bool bEnableTargetMagnetism = true;

	/** Range for predictive magnetism trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Magnetism", meta = (ClampMin = "0", EditCondition = "bEnableTargetMagnetism"))
	float MagnetismRange = 300.0f;

	/** Radius for magnetism sphere trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Magnetism", meta = (ClampMin = "0", EditCondition = "bEnableTargetMagnetism"))
	float MagnetismRadius = 80.0f;

	/** How fast to pull target toward attack center (units per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Magnetism", meta = (ClampMin = "0", EditCondition = "bEnableTargetMagnetism"))
	float MagnetismPullSpeed = 800.0f;

	// ==================== Range & Detection ====================

	/** Maximum range of the melee attack (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "50", ClampMax = "500"))
	float AttackRange = 150.0f;

	/** Radius of the sphere trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "10", ClampMax = "100"))
	float AttackRadius = 30.0f;

	/** Forward offset from camera for trace start (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "0", ClampMax = "100"))
	float TraceForwardOffset = 20.0f;

	/** Angle for cone-based hit detection (degrees, 0 = line trace only) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "0", ClampMax = "45"))
	float AttackAngle = 15.0f;

	// ==================== Timing ====================

	/** Time to transition FirstPersonMesh down before attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing|Mesh Transition", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float HideWeaponTime = 0.15f;

	/** Time to transition MeleeMesh out and FirstPersonMesh back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing|Mesh Transition", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ShowWeaponTime = 0.15f;

	/** Delay between input and attack start (prevents spam) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0", ClampMax = "0.5"))
	float InputDelayTime = 0.1f;

	/** Time before damage is dealt (wind-up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0", ClampMax = "1"))
	float WindupTime = 0.05f;

	/** Duration of active damage window */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ActiveTime = 0.15f;

	/** Recovery time after attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0", ClampMax = "1"))
	float RecoveryTime = 0.2f;

	/** Cooldown before next attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0", ClampMax = "2"))
	float Cooldown = 0.5f;

	// ==================== Movement ====================

	/** Lunge distance on attack (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0", ClampMax = "500"))
	float LungeDistance = 100.0f;

	/** Lunge duration (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float LungeDuration = 0.15f;

	/** Can attack while airborne */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bCanAttackInAir = true;

	/** Can attack while sliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bCanAttackWhileSliding = true;
};

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleeAttackStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMeleeHit, AActor*, HitActor, const FVector&, HitLocation, bool, bHeadshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleeAttackEnded);

/**
 * Component that provides quick melee attack capability.
 * Works independently of the weapon system - can be used at any time.
 * Supports sphere trace hit detection, lunge movement, and animation montages.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UMeleeAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMeleeAttackComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Settings ====================

	/** Melee attack settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FMeleeAttackSettings Settings;

	// ==================== Animation ====================

	/** Animation data for ground attacks (standing, walking, running) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Attack Types")
	FMeleeAnimationData GroundAttack;

	/** Animation data for airborne attacks (jumping, falling) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Attack Types")
	FMeleeAnimationData AirborneAttack;

	/** Animation data for sliding attacks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Attack Types")
	FMeleeAnimationData SlidingAttack;

	/** Third person attack montage (optional, plays on character mesh) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ThirdPersonMontage;

	// ==================== Mesh References ====================

	/** Reference to the melee mesh component (set at runtime or in Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TObjectPtr<USkeletalMeshComponent> MeleeMesh;

	/** Reference to the first person mesh component (auto-detected or set manually) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** Rotation offset to align MeleeMesh with camera direction (adjust if mesh faces wrong way) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	FRotator MeleeMeshRotationOffset = FRotator(0.0f, -90.0f, 0.0f);

	// ==================== Audio ====================

	/** Swing sound (plays on attack start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> SwingSound;

	/** Hit sound (plays on successful hit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HitSound;

	/** Miss sound (plays if no hit during active window) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> MissSound;

	// ==================== Camera ====================

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> HitCameraShake;

	/** Camera shake intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "2"))
	float CameraShakeScale = 1.0f;

	/** Enable camera focus on lunge target (rotates camera toward enemy when lunge starts) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bEnableCameraFocusOnLunge = true;

	/** Duration of camera focus rotation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "bEnableCameraFocusOnLunge"))
	float CameraFocusDuration = 0.2f;

	/** Strength of camera focus (1.0 = instant snap, 0.5 = gentle rotation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bEnableCameraFocusOnLunge"))
	float CameraFocusStrength = 0.7f;

	// ==================== Debug ====================

	/** Enable debug visualization for all melee traces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebugVisualization = false;

	/** Duration for debug shapes (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bEnableDebugVisualization"))
	float DebugShapeDuration = 2.0f;

	// ==================== VFX ====================

	/** Niagara effect for swing trail (spawned at attack start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> SwingTrailFX;

	/** Niagara effect for impact (spawned on hit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Socket name on first person mesh for trail attachment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FName TrailSocketName = FName("hand_r");

	/** Offset from socket for trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FVector TrailOffset = FVector(0.0f, 0.0f, 0.0f);

	/** Rotation offset from socket for trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FRotator TrailRotationOffset = FRotator::ZeroRotator;

	/** Scale for impact effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ImpactFXScale = 1.0f;

	// ==================== Events ====================

	/** Called when melee attack begins */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMeleeAttackStarted OnMeleeAttackStarted;

	/** Called when melee attack hits something */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMeleeHit OnMeleeHit;

	/** Called when melee attack ends (regardless of hit) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMeleeAttackEnded OnMeleeAttackEnded;

	// ==================== API ====================

	/**
	 * Attempt to start a melee attack
	 * @return true if attack started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool StartAttack();

	/**
	 * Cancel current attack (if in windup phase)
	 * @return true if attack was cancelled
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool CancelAttack();

	/**
	 * Check if an attack can be started
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool CanAttack() const;

	/**
	 * Get current attack state
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	EMeleeAttackState GetAttackState() const { return CurrentState; }

	/**
	 * Check if currently attacking (any phase)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsAttacking() const;

	/**
	 * Get cooldown progress (0 = just started, 1 = ready)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetCooldownProgress() const;

	/**
	 * Check if input is currently locked (attack in progress)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsInputLocked() const { return bInputLocked; }

	/**
	 * Enable or disable debug visualization for melee traces
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee|Debug")
	void SetDebugVisualizationEnabled(bool bEnabled) { bEnableDebugVisualization = bEnabled; }

	/**
	 * Check if debug visualization is enabled
	 */
	UFUNCTION(BlueprintPure, Category = "Melee|Debug")
	bool IsDebugVisualizationEnabled() const { return bEnableDebugVisualization; }

	// ==================== Animation Notify API ====================

	/**
	 * Activate damage window from animation notify (called by AnimNotify)
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void ActivateDamageWindowFromNotify();

	/**
	 * Deactivate damage window from animation notify (called by AnimNotify)
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void DeactivateDamageWindowFromNotify();

protected:
	// ==================== State ====================

	/** Current attack state */
	EMeleeAttackState CurrentState = EMeleeAttackState::Ready;

	/** Time remaining in current state */
	float StateTimeRemaining = 0.0f;

	/** Has hit something during current attack */
	bool bHasHitThisAttack = false;

	/** Input is locked - prevents starting new attack until current one fully completes */
	bool bInputLocked = false;

	/** Actors already hit during this attack (prevents multi-hit) */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> HitActorsThisAttack;

	/** Lunge direction (stored at attack start) */
	FVector LungeDirection = FVector::ZeroVector;

	/** Lunge progress (0-1) */
	float LungeProgress = 0.0f;

	/** Cached owner character */
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	/** Cached owner controller */
	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	/** Active trail effect component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveTrailFX;

	/** Target being pulled by magnetism */
	UPROPERTY()
	TWeakObjectPtr<AActor> MagnetismTarget;

	/** Cached owner velocity at attack start (for momentum calculations) */
	FVector OwnerVelocityAtAttackStart = FVector::ZeroVector;

	// ==================== Cool Kick State ====================

	/** Time remaining in cool kick period */
	float CoolKickTimeRemaining = 0.0f;

	/** Direction for cool kick boost (movement direction at hit time) */
	FVector CoolKickDirection = FVector::ZeroVector;

	// ==================== Mesh Transition State ====================

	/** Current attack type (determined at attack start) */
	EMeleeAttackType CurrentAttackType = EMeleeAttackType::Ground;

	/** Mesh transition progress (0-1) */
	float MeshTransitionProgress = 0.0f;

	/** Base rotation of FirstPersonMesh (stored for restoration) */
	FRotator FirstPersonMeshBaseRotation = FRotator::ZeroRotator;

	/** Base location of FirstPersonMesh (stored for restoration) */
	FVector FirstPersonMeshBaseLocation = FVector::ZeroVector;

	/** Target rotation for MeleeMesh (camera-aligned) */
	FRotator MeleeMeshTargetRotation = FRotator::ZeroRotator;

	/** Current montage being played on MeleeMesh */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentMeleeMontage;

	/** Bones currently hidden on MeleeMesh (for restoration) */
	TArray<FName> CurrentlyHiddenBones;

	/** Time elapsed in current montage (for play rate curve sampling) */
	float MontageTimeElapsed = 0.0f;

	/** Total duration of current montage at base rate */
	float MontageTotalDuration = 0.0f;

	// ==================== Camera Focus State ====================

	/** Target actor for camera focus */
	UPROPERTY()
	TWeakObjectPtr<AActor> CameraFocusTarget;

	/** Time remaining for camera focus */
	float CameraFocusTimeRemaining = 0.0f;

	/** Initial rotation when focus started */
	FRotator CameraFocusStartRotation = FRotator::ZeroRotator;

	/** Target rotation for camera focus */
	FRotator CameraFocusTargetRotation = FRotator::ZeroRotator;

	// ==================== Internal ====================

	/** Transition to a new state */
	void SetState(EMeleeAttackState NewState);

	/** Update current state */
	void UpdateState(float DeltaTime);

	/** Perform hit detection */
	void PerformHitDetection();

	/** Apply damage to hit actor */
	void ApplyDamage(AActor* HitActor, const FHitResult& HitResult);

	/** Check if hit is a headshot */
	bool IsHeadshot(const FHitResult& HitResult) const;

	/** Check if actor is a valid melee target (Pawn/Character, not world geometry) */
	bool IsValidMeleeTarget(AActor* HitActor) const;

	/** Apply lunge movement */
	void UpdateLunge(float DeltaTime);

	/** Play attack animation */
	void PlayAttackAnimation();

	/** Stop attack animation */
	void StopAttackAnimation();

	/** Play sound effect */
	void PlaySound(USoundBase* Sound);

	/** Play camera shake */
	void PlayCameraShake();

	/** Spawn swing trail effect */
	void SpawnSwingTrailFX();

	/** Stop and destroy swing trail effect */
	void StopSwingTrailFX();

	/** Spawn impact effect at hit location */
	void SpawnImpactFX(const FVector& Location, const FVector& Normal);

	/** Get trace start location */
	FVector GetTraceStart() const;

	/** Get trace end location */
	FVector GetTraceEnd() const;

	/** Get trace direction */
	FVector GetTraceDirection() const;

	/** Get lunge direction based on current movement velocity */
	FVector GetLungeDirection() const;

	/** Find and start pulling magnetism target */
	void StartMagnetism();

	/** Update magnetism pull */
	void UpdateMagnetism(float DeltaTime);

	/** Stop magnetism and clear target */
	void StopMagnetism();

	/** Apply impulse to character (works with ShooterNPC) */
	void ApplyCharacterImpulse(AActor* HitActor, const FVector& ImpulseDirection, float ImpulseStrength);

	/** Calculate momentum-based bonus damage */
	float CalculateMomentumDamage(AActor* HitActor) const;

	/** Calculate momentum-based impulse multiplier */
	float CalculateMomentumImpulseMultiplier() const;

	/** Get impact center for magnetism pull */
	FVector GetImpactCenter() const;

	/** Start cool kick period (called on airborne hit without lunge) */
	void StartCoolKick();

	/** Update cool kick boost */
	void UpdateCoolKick(float DeltaTime);

	// ==================== Mesh Transition ====================

	/** Determine attack type based on current movement state */
	EMeleeAttackType DetermineAttackType() const;

	/** Get animation data for the current attack type */
	const FMeleeAnimationData& GetCurrentAnimationData() const;

	/** Begin hiding FirstPersonMesh (transition down) */
	void BeginHideWeapon();

	/** Update mesh transition (hide/show progress) */
	void UpdateMeshTransition(float DeltaTime);

	/** Switch visibility: hide FirstPersonMesh, show MeleeMesh */
	void SwitchToMeleeMesh();

	/** Switch visibility: hide MeleeMesh, show FirstPersonMesh */
	void SwitchToFirstPersonMesh();

	/** Update MeleeMesh rotation to match camera */
	void UpdateMeleeMeshRotation();

	/** Play swing camera shake */
	void PlaySwingCameraShake();

	/** Update montage play rate based on curve */
	void UpdateMontagePlayRate(float DeltaTime);

	/** Called when melee montage ends */
	UFUNCTION()
	void OnMeleeMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Auto-detect mesh references if not set */
	void AutoDetectMeshReferences();

	// ==================== Camera Focus ====================

	/** Start camera focus on target */
	void StartCameraFocus(AActor* Target);

	/** Update camera focus interpolation */
	void UpdateCameraFocus(float DeltaTime);

	/** Stop camera focus */
	void StopCameraFocus();
};