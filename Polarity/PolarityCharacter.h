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

	/** Dedicated dash action. On the ground it activates Ground Dash; in air it activates the unlocked Air Dash. */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* DashAction;

	/** Toggle Charge Sign Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ToggleChargeAction;

	/** Channel (Capture) Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ChannelAction;

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

	// ==================== Movement Abilities (unlockable via upgrades) ====================

	/** Can the player sprint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Abilities")
	bool bCanSprint = true;

	/** Can the player perform air jumps (any jump after the first one — uses MovementSettings->MaxJumpCount as the cap) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Abilities")
	bool bCanDoubleJump = true;

	/** Can the player air dash. Default false — granted by the "Air Dash" upgrade pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Abilities")
	bool bCanAirDash = false;

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

	/** Routes the dedicated dash input by movement context. */
	void DashPressed(const FInputActionValue& Value);

	/** Toggle charge button pressed */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void DoToggleChargePressed();

	/** Toggle charge button released */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void DoToggleChargeReleased();

	/** Channel button pressed */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	virtual void DoChannelPressed();

	/** Channel button released */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	virtual void DoChannelReleased();

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

	// ==================== First Person Mesh Placement (camera-attached) ====================

	/** Rest location of FirstPersonMesh relative to the camera. Source of truth for the pose
	 *  pipeline — the component's own relative transform is rewritten every tick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View|Placement")
	FVector FirstPersonMeshCameraOffset = FVector::ZeroVector;

	/** Rest rotation of FirstPersonMesh relative to the camera. Yaw -90 makes a standard UE
	 *  skeleton face along the camera's forward axis. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View|Placement")
	FRotator FirstPersonMeshCameraRotation = FRotator(0.0f, -90.0f, 0.0f);

	/** How much of the camera shake roll is mirrored onto the mesh. 1.0 reproduces the behaviour
	 *  from before the camera-attach refactor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View|Camera Follow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShakeRollFollowAlpha = 1.0f;

	/** How much of the wallrun camera roll is mirrored onto the mesh. 0.0 reproduces the previous
	 *  behaviour, where the weapon deliberately did not roll into the wall being run on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View|Camera Follow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallrunRollFollowAlpha = 0.0f;

	// ==================== First Person View State ====================

	/** Base relative location of FirstPersonMesh (from FirstPersonMeshCameraOffset on BeginPlay) */
	FVector FirstPersonMeshBaseLocation = FVector::ZeroVector;

	/** Base relative rotation of FirstPersonMesh (from FirstPersonMeshCameraRotation on BeginPlay) */
	FRotator FirstPersonMeshBaseRotation = FRotator::ZeroRotator;

	/** Current Z offset applied to FirstPersonMesh */
	float CurrentFirstPersonZOffset = 0.0f;

	/** Current crouch/slide camera offset */
	FVector CurrentCrouchOffset = FVector::ZeroVector;

	/** Current weapon tilt rotation */
	FRotator CurrentWeaponTilt = FRotator::ZeroRotator;

	/** Crouch/slide transition progress (0.0 = standing, 1.0 = fully crouched/sliding) */
	float CrouchSlideProgress = 0.0f;

	/** Saved target offset for crouch/slide (used during exit transition) */
	FVector SavedCrouchSlideOffset = FVector::ZeroVector;

	/** Saved target tilt for crouch/slide (used during exit transition) */
	FRotator SavedCrouchSlideTilt = FRotator::ZeroRotator;

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

	/** Interpolate the run/sprint aim offset. Applied as a camera-space pose layer. */
	void UpdateRunAimOffset(float DeltaTime);

	/** Play a procedural footstep sound - override in Blueprint for custom behavior */
	UFUNCTION(BlueprintNativeEvent, Category = "Audio")
	void PlayProceduralFootstep(bool bIsWallrun, bool bLeftFoot);

	/** Builds the FP mesh pose for this frame and applies it in a single write. Do not override
	 *  to add offsets — override AccumulateFirstPersonPose instead. */
	virtual void UpdateFirstPersonView(float DeltaTime);

	/** Appends this class's pose layers to the accumulated FP mesh transform. All layers are in
	 *  camera space (the mesh is parented to FirstPersonCameraComponent). Subclasses call Super
	 *  first, then add their own. */
	virtual void AccumulateFirstPersonPose(float DeltaTime, FVector& Location, FRotator& Rotation);

	/** Pushes wallrun + shake roll onto the PlayerCameraManager (POV-level roll, not the camera
	 *  component — hence the separate mesh-side follow factors below). */
	void ApplyCameraManagerRoll();

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

	/** Returns the grab/toggle-charge input action (for external bindings) */
	UInputAction* GetToggleChargeAction() const { return ToggleChargeAction; }

	/** Returns the channel/capture input action (for external bindings) */
	UInputAction* GetChannelAction() const { return ChannelAction; }

	/** Returns the dedicated ground/air dash action for HUD keybind hints. */
	UInputAction* GetDashAction() const { return DashAction; }

	/** Returns camera shake component */
	UFUNCTION(BlueprintPure, Category = "Camera")
	UCameraShakeComponent* GetCameraShake() const { return CameraShakeComponent; }

	/** Current procedural run-sway position offset (in FP-mesh local space). Updated each tick. */
	const FVector& GetCurrentRunSwayPosition() const { return CurrentRunSwayPosition; }

	/** Current procedural run-sway rotation offset. Updated each tick. */
	const FRotator& GetCurrentRunSwayRotation() const { return CurrentRunSwayRotation; }

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
