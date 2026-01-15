// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "PolarityCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

// Movement
class UApexMovementComponent;
class UMovementSettings;
class UCameraShakeComponent;
class USoundBase;
class UCurveVector;
enum class EWallSide : uint8;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  First person character with Titanfall-style movement and EMF integration
 */
UCLASS(abstract)
class APolarityCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Camera shake component for procedural effects */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraShakeComponent* CameraShakeComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	class UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	class UInputAction* MouseLookAction;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SprintAction;

	/** Crouch/Slide Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* CrouchSlideAction;

	/** Toggle Charge Sign Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ToggleChargeAction;

	// ==================== Apex Movement ====================

	/** Custom movement component reference */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	TObjectPtr<UApexMovementComponent> ApexMovement;

	/** Current movement input for jump lurch */
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;

public:

	/** Movement settings DataAsset */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	TObjectPtr<UMovementSettings> MovementSettings;

	// ==================== EMF System ====================

	/** Current electrical charge (-1 to +1, 0 = neutral) */
	UPROPERTY(BlueprintReadWrite, Category = "EMF", meta = (ClampMin = "-1", ClampMax = "1"))
	float CurrentCharge = 0.0f;

	/** Mass for EMF calculations (kg) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMF")
	float EMFMass = 70.0f;

public:
	APolarityCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

	/** Sprint input handlers */
	void SprintStart(const FInputActionValue& Value);
	void SprintStop(const FInputActionValue& Value);

	/** Crouch/Slide input handlers */
	void CrouchSlideStart(const FInputActionValue& Value);
	void CrouchSlideStop(const FInputActionValue& Value);

	/** Toggle charge sign handler */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void DoToggleCharge();

	/** Update camera effects (tilt, pitch offset from shakes) */
	void UpdateCameraEffects(float DeltaTime);

	// ==================== Movement Event Handlers ====================

	UFUNCTION()
	void OnMovementLanded(const FHitResult& Hit);

	UFUNCTION()
	void OnSlideStarted();

	UFUNCTION()
	void OnSlideEnded();

	UFUNCTION()
	void OnWallrunStarted(EWallSide Side);

	UFUNCTION()
	void OnWallrunEnded();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	/** Track last jump count for double jump detection */
	int32 LastJumpCount = 0;

	// ==================== First Person View State ====================

	/** Base relative location of FirstPersonMesh (stored on BeginPlay) */
	FVector FirstPersonMeshBaseLocation = FVector::ZeroVector;

	/** Base relative rotation of FirstPersonMesh (stored on BeginPlay) */
	FRotator FirstPersonMeshBaseRotation = FRotator::ZeroRotator;

	/** Current Z offset applied to FirstPersonMesh */
	float CurrentFirstPersonZOffset = 0.0f;

	/** Current crouch/slide camera offset */
	FVector CurrentCrouchOffset = FVector::ZeroVector;

	/** Current weapon tilt rotation */
	FRotator CurrentWeaponTilt = FRotator::ZeroRotator;

	// ==================== Weapon Run Sway State ====================

	/** Accumulated distance for run sway phase calculation */
	float RunSwayAccumulatedDistance = 0.0f;

	/** Current run sway phase (0-1, loops) */
	float CurrentRunSwayPhase = 0.0f;

	/** Current run sway intensity (0-1, interpolated) */
	float CurrentRunSwayIntensity = 0.0f;

	/** Current run sway rotation offset */
	FRotator CurrentRunSwayRotation = FRotator::ZeroRotator;

	/** Current run sway position offset */
	FVector CurrentRunSwayPosition = FVector::ZeroVector;

	/** Previous frame location for distance calculation */
	FVector PreviousFrameLocation = FVector::ZeroVector;

	/** Has valid previous frame location */
	bool bHasValidPreviousLocation = false;

	/** Current aim offset for AnimBP (interpolated) */
	FVector CurrentAimOffset = FVector::ZeroVector;

	/** Target aim offset based on movement state */
	FVector TargetAimOffset = FVector::ZeroVector;

	/** Current wallrun offset (set by subclass) */
	FVector CurrentWallrunOffset = FVector::ZeroVector;

	/** Current ADS offset (set by subclass) */
	FVector CurrentADSOffset = FVector::ZeroVector;

	/** Target wallrun offset (set by subclass) */
	FVector TargetWallrunOffset = FVector::ZeroVector;

	/** Target ADS offset (set by subclass) */
	FVector TargetADSOffset = FVector::ZeroVector;

	/** Base relative rotation of camera (stored on BeginPlay) */
	FRotator BaseCameraRotation = FRotator::ZeroRotator;

	/** Current applied camera roll for wallrun/effects */
	float CurrentCameraRoll = 0.0f;

	// ==================== Procedural Footsteps ====================

	/** Timer for procedural footstep sounds */
	float FootstepTimer = 0.0f;

	/** Is left foot next (for alternating sounds) */
	bool bIsLeftFoot = false;

	/** Sound to play for regular procedural footsteps (can be Sound Cue with variations) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio|Procedural Footsteps")
	TObjectPtr<USoundBase> ProceduralFootstepSound;

	/** Sound to play for wallrun procedural footsteps (can be Sound Cue with variations) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio|Procedural Footsteps")
	TObjectPtr<USoundBase> ProceduralWallrunFootstepSound;

	/** Update procedural footstep sounds */
	void UpdateProceduralFootsteps(float DeltaTime);

	/** Update procedural weapon sway during running */
	void UpdateWeaponRunSway(float DeltaTime);

	/** Update aim offset in AnimInstance for IK targeting */
	void UpdateAnimInstanceAimOffset(float DeltaTime);

	/** Set AimOffset variable in FirstPersonMesh AnimInstance */
	void SetAnimInstanceAimOffset(const FVector& Offset);

	/** Play a procedural footstep sound - override in Blueprint for custom behavior */
	UFUNCTION(BlueprintNativeEvent, Category = "Audio")
	void PlayProceduralFootstep(bool bIsWallrun, bool bLeftFoot);

	/** Update first person mesh position and rotation based on movement state */
	virtual void UpdateFirstPersonView(float DeltaTime);

public:

	/** Set target ADS offset for interpolation */
	UFUNCTION(BlueprintCallable, Category = "First Person View")
	void SetADSOffset(const FVector& Offset) { TargetADSOffset = Offset; }

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Alias for Arena Shooter BP compatibility */
	UFUNCTION(BlueprintPure, Category = "Camera", meta = (DisplayName = "Get First Person Camera"))
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCameraComponent; }

	/** Returns custom Apex movement component */
	UFUNCTION(BlueprintPure, Category = "Movement")
	UApexMovementComponent* GetApexMovement() const { return ApexMovement; }

	/** Returns camera shake component */
	UFUNCTION(BlueprintPure, Category = "Camera")
	UCameraShakeComponent* GetCameraShake() const { return CameraShakeComponent; }

	// ==================== EMF Methods ====================

	/** Get current charge */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const { return CurrentCharge; }

	/** Set charge (clamped to -1..1) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Add to current charge */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void AddCharge(float Delta);
};