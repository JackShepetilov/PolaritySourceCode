// ApexMovementComponent.h
// Titanfall 2 / Apex Legends style movement system

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ApexMovementComponent.generated.h"

class UMovementSettings;
class IVelocityModifier;

UENUM(BlueprintType)
enum class EPolarityMovementState : uint8
{
	None,
	Walking,
	Sprinting,
	Crouching,
	Sliding,
	Falling,
	Mantling,
	WallRunning
};

UENUM(BlueprintType)
enum class EWallSide : uint8
{
	None,
	Left,
	Right
};

/** How the wallrun ended - determines if player can double jump after */
UENUM(BlueprintType)
enum class EWallRunEndReason : uint8
{
	None,
	/** Player jumped off wall - no double jump allowed */
	JumpedOff,
	/** Wallrun time expired - one jump allowed */
	TimeExpired,
	/** Lost contact with wall or other reason */
	LostWall
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMovementStateChanged, EPolarityMovementState, PreviousState, EPolarityMovementState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWallRunChanged, bool, bIsWallRunning, EWallSide, Side);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLanded_Movement, const FHitResult&, Hit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlideStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlideEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWallrunStarted, EWallSide, Side);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWallrunEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWallBounce, FVector, BounceDirection);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPreVelocityUpdate, float, FVector&);

/**
 * Titanfall 2 / Apex Legends style movement component.
 * Features: Slide with proper friction, WallRun (now slide-style), WallBounce, Mantle, Air Dash, Double Jump
 */
UCLASS(ClassGroup = "Movement", meta = (BlueprintSpawnableComponent))
class POLARITY_API UApexMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UApexMovementComponent();

	// ==================== Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apex|Settings")
	TObjectPtr<UMovementSettings> MovementSettings;

	/** Camera shake on Jump */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> JumpCameraShake;

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> LandCameraShake;

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SlideStartCameraShake;

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SlideEndCameraShake;


	// ==================== State ====================

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	EPolarityMovementState CurrentMovementState = EPolarityMovementState::None;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	int32 CurrentJumpCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bWantsToSprint = false;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bIsSliding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bIsMantling = false;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bIsAirDashing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bIsWallRunning = false;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	EWallSide WallRunSide = EWallSide::None;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	int32 RemainingAirDashCount = 1;

	/** True when player holds crouch in air - will slide on landing (Titanfall 2 mechanic) */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bWantsSlideOnLand = false;

	/** How the last wallrun ended - affects post-wallrun jump availability */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	EWallRunEndReason LastWallRunEndReason = EWallRunEndReason::None;

	// ==================== Delegates ====================

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnMovementStateChanged OnMovementStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallRunChanged OnWallRunChanged;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnLanded_Movement OnLanded_Movement;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnSlideStarted OnSlideStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnSlideEnded OnSlideEnded;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallrunStarted OnWallrunStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallrunEnded OnWallrunEnded;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallBounce OnWallBounce;

	FOnPreVelocityUpdate OnPreVelocityUpdate;

	// ==================== Overrides ====================

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	virtual bool DoJump(bool bReplayingMoves) override;

	// ==================== Input ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void StopSprint();

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void TryCrouchSlide();

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void StopCrouchSlide();

	// ==================== Slide ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanSlide() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartSlide();

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void EndSlide();

	/** Start slide from air landing - preserves and boosts momentum (Titanfall 2 mechanic) */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartSlideFromAir(float FallSpeed);

	// ==================== WallRun (slide-style) ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanWallRun() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartWallRun(const FHitResult& WallHit, EWallSide Side);

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void EndWallRun(EWallRunEndReason Reason = EWallRunEndReason::LostWall);

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsWallRunning() const { return bIsWallRunning; }

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	FRotator GetWallRunCameraTilt() const { return CurrentWallRunCameraTilt; }

	// ==================== Wall Bounce ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanWallBounce() const;

	// ==================== Mantle ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanMantle() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void TryMantle();

	// ==================== Air Dash ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanAirDash() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void TryAirDash();

	// ==================== Smooth Crouch ====================

	/** Start crouching with smooth capsule interpolation */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartCrouching();

	/** Stop crouching with smooth capsule interpolation (checks for clearance) */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StopCrouching();

	/** Check if there's enough room to stand up */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool CanStandUp() const;

	/** Speed of capsule height interpolation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apex|Crouch")
	float CapsuleInterpSpeed = 15.0f;

	// ==================== Queries ====================

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsSprinting() const { return bWantsToSprint && !bIsSliding && !IsCrouching() && IsMovingOnGround(); }

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsSliding() const { return bIsSliding; }

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	float GetSpeedRatio() const;

	/** Get current slide duration in seconds */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	float GetSlideDuration() const { return SlideDuration; }

	/** Get current slide fatigue level (0-5) */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	int32 GetSlideFatigue() const { return SlideFatigueCounter; }

	// ==================== Input Tracking ====================

	/** Set current move input for jump lurch calculations */
	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void SetMoveInput(const FVector2D& Input) { CurrentMoveInput = Input; }

	/** Get current move input */
	UFUNCTION(BlueprintPure, Category = "Apex|Input")
	FVector2D GetMoveInput() const { return CurrentMoveInput; }

	/** Check if forward input is held */
	UFUNCTION(BlueprintPure, Category = "Apex|Input")
	bool IsForwardHeld() const { return CurrentMoveInput.Y > 0.5f; }

	/** Try to perform jump with all checks */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool TryJump();

	// ==================== Camera State (for Character to read) ====================

	/** Current camera roll from wallrun - ONLY roll is used */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float CurrentWallRunCameraRoll = 0.0f;

	/** Current camera offset from wallrun */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	FVector CurrentWallRunCameraOffset = FVector::ZeroVector;

	/** Current mesh roll from wallrun - same logic as camera */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float CurrentWallRunMeshRoll = 0.0f;

	/** Current mesh pitch from wallrun */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float CurrentWallRunMeshPitch = 0.0f;

	/** [DEPRECATED] Use CurrentWallRunCameraRoll instead */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	FRotator CurrentCameraTilt = FRotator::ZeroRotator;

	/** [DEPRECATED] Use CurrentWallRunCameraOffset instead */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	FVector CurrentCameraOffset = FVector::ZeroVector;

	/** Last fall velocity before landing (for camera shake) */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float LastFallVelocity = 0.0f;

	// ==================== EMF ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|EMF")
	void SetEMFForce(const FVector& Force);

	UFUNCTION(BlueprintPure, Category = "Apex|EMF")
	FVector GetEMFForce() const { return CurrentEMFForce; }

	// ==================== Velocity Modifiers ====================

	void RegisterVelocityModifier(TScriptInterface<IVelocityModifier> Modifier);
	void UnregisterVelocityModifier(TScriptInterface<IVelocityModifier> Modifier);

protected:
	// Velocity Modifiers
	UPROPERTY()
	TArray<TScriptInterface<IVelocityModifier>> VelocityModifiers;


	/** Cached owner character */
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	/** Cached owner controller */
	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;
	// Input tracking for jump lurch
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;

	// Slide state
	float SlideCooldownRemaining = 0.0f;
	float SlideBoostCooldownRemaining = 0.0f;
	float SlideDuration = 0.0f;
	int32 SlideFatigueCounter = 0;
	float SlideFatigueDecayTimer = 0.0f;
	FVector SlideDirection;

	// Saved default values (restored after slide)
	float DefaultGroundFriction = 8.0f;
	float DefaultBrakingDeceleration = 2048.0f;

	// Smooth Crouch state
	float StandingCapsuleHalfHeight = 0.0f;  // Cached from capsule on init
	float TargetCapsuleHalfHeight = 0.0f;
	bool bWantsToCrouchSmooth = false;  // Our internal crouch flag

	// WallRun state (now slide-style)
	float WallRunTimeRemaining = 0.0f;
	float WallRunSameWallCooldown = 0.0f;
	FVector WallRunNormal;
	FVector WallRunDirection;
	FVector WallRunEntryVelocity;  // Entry velocity for momentum preservation
	FRotator CurrentWallRunCameraTilt = FRotator::ZeroRotator;  // Internal, use GetWallRunCameraTilt()
	TWeakObjectPtr<AActor> LastWallRunActor;

	// WallRun capsule state (Titanfall 2 style - smaller capsule, NO TILT to avoid mesh rotation)
	float WallRunOriginalCapsuleHalfHeight = 0.0f;
	float WallRunOriginalCapsuleRadius = 0.0f;
	bool bWallRunCapsuleModified = false;

	// Wall Bounce state
	float WallBounceCooldownRemaining = 0.0f;

	// Jump state
	float JumpHoldTimeRemaining = 0.0f;
	bool bJumpHeld = false;

	// Air Dash state
	float AirDashCooldownRemaining = 0.0f;
	float AirDashDecayTimeRemaining = 0.0f;

	// Mantle state
	FVector MantleStartLocation;
	FVector MantleTargetLocation;
	float MantleAlpha = 0.0f;

	// EMF
	FVector CurrentEMFForce = FVector::ZeroVector;

	// ==================== Internal Methods ====================

	void UpdateMovementState();
	void SetMovementState(EPolarityMovementState NewState);

	// Slide
	void UpdateSlide(float DeltaTime);
	float GetSlopeAngle() const;

	// WallRun (slide-style)
	void CheckForWallRun();
	void UpdateWallRun(float DeltaTime);
	bool TraceForWall(EWallSide Side, FHitResult& OutHit) const;
	bool IsValidWallRunSurface(const FHitResult& Hit) const;
	bool IsAboveGround() const;
	void UpdateWallRunCameraTilt(float DeltaTime);

	// WallRun capsule (Titanfall 2 style - size only, no tilt)
	void ApplyWallRunCapsule();
	void RestoreWallRunCapsule();

	// WallRun speed boost calculation
	float CalculateWallRunBoost(float ParallelSpeed) const;

	// Wall Bounce
	void CheckForWallBounce();
	void PerformWallBounce(const FHitResult& WallHit);

	// Mantle
	void UpdateMantle(float DeltaTime);
	bool TraceMantleSurface(FHitResult& OutHit) const;

	// Air
	void ApplyAirStrafe(float DeltaTime);
	void UpdateJumpHold(float DeltaTime);
	void UpdateAirDash(float DeltaTime);
	void UpdateAirDashDecay(float DeltaTime);

	// EMF & Modifiers
	void ApplyEMFForces(float DeltaTime);
	void ApplyVelocityModifiers(float DeltaTime);

	// Utility
	void ResetAirAbilities();

	// Smooth Crouch
	void UpdateCapsuleHeight(float DeltaTime);

	/** Play camera shake */
	void PlayCameraShake(TSubclassOf<UCameraShakeBase>);
};