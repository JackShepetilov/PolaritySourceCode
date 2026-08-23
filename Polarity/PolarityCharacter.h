// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
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
class APolarityCharacter : public ACharacter, public IGenericTeamAgentInterface
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

	// ==================== Team ====================
	//
	// Every character in this game has a side, players included. It lives here rather than on the
	// two subclasses because there is one question ("who is this hostile to") and it must have one
	// answer: AShooterNPC used to own the only implementation, so a player pawn answered NoTeam and
	// was hostile to enemies only by accident of the default solver (FGenericTeamId(255) != 1).
	// That accident stops being survivable the moment there is more than one enemy faction.
	//
	// Which of the two objects is asked depends on the direction: a STIMULUS SOURCE is asked
	// through the actor itself and nothing else (FGenericTeamId::GetTeamIdentifier casts the actor,
	// with no fallback to its controller), while a perception LISTENER is asked through the
	// component's owner, which is the AI controller (UAIPerceptionComponent::GetTeamIdentifier).
	// Hence the value here on the pawn, and the separate one on AShooterAIController: both are
	// needed, and they must not disagree.

	/** 0 = players, 1 = enemies. Second enemy faction takes 2 when factions land. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	uint8 TeamByte = 0;

	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamByte); }

	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { TeamByte = NewTeamId.GetId(); }

	// ==================== Aim ====================

	/** Where this character is aiming vertically, in degrees, for animation to use.
	 *
	 *  Lives here rather than on AShooterCharacter because enemies need it for the same reason
	 *  players do: a fight on two levels is the normal case, and an enemy whose gun is pinned to the
	 *  horizon cannot show the player that it is aiming at them. AShooterNPC is not a
	 *  AShooterCharacter, so the anim blueprints' "Cast To ShooterCharacter" simply failed for
	 *  enemies and left the pitch at zero.
	 *
	 *  Works for an AI-controlled pawn without any extra plumbing: APawn::PreReplication writes
	 *  RemoteViewPitch16 from whatever controller the pawn has, AI included, so the value reaches
	 *  clients on its own. Note that AAIController::UpdateControlRotation zeroes pitch unless the
	 *  focus actor is a Pawn, which is why an enemy staring at a decoy prop looks level at it. */
	UFUNCTION(BlueprintPure, Category = "Coop|Aim")
	float GetAimPitchForAnimation() const;

public:
	APolarityCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// The engine's crouch hooks. It resizes the capsule in one frame and hands us how far the capsule
	// centre moved; we take that distance as a counter-offset on the camera and let it decay, so the
	// eye travels the same distance over CrouchDownTime / CrouchUpTime instead of teleporting.
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

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

	/** ApexMovementComponent::DoJump owns every jump rule this game has (ground, air, wall, slide),
	 *  so the engine's own gate must not veto it first. Kept permissive on purpose: DoJump returns
	 *  false by itself when no jump is available. */
	virtual bool CanJumpInternal_Implementation() const override;

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

	// ==================== Smooth crouch eye height ====================
	//
	// The capsule swaps size in one frame and the camera is parented to it, so without this the eye
	// teleports by the whole half-height difference (46 units at the shipped sizes). OnStartCrouch /
	// OnEndCrouch push the distance the capsule centre just moved into this counter-offset, which
	// keeps the eye where it was, and UpdateCrouchCameraSmoothing walks it back to zero over the
	// configured time. Physics is untouched: this never leaves the camera component.

	/** Counter-offset currently applied to the camera, in capsule Z. Walks to 0. */
	float CrouchCameraSmoothOffsetZ = 0.0f;

	/** The distance CrouchCameraSmoothOffsetZ started from, so the walk is a straight line at a
	 *  constant speed rather than an ease that never quite lands. */
	float CrouchCameraSmoothStartZ = 0.0f;

	/** How long the current walk gets: the configured time for its direction, scaled down by how much
	 *  of the full distance is actually left, so the eye moves at one speed either way. */
	float CrouchCameraSmoothDuration = 0.0f;

	/** Seconds spent in the current walk. */
	float CrouchCameraSmoothElapsed = 0.0f;

	/** Moves CrouchCameraSmoothOffsetZ toward zero. Cosmetic, owner only. */
	void UpdateCrouchCameraSmoothing(float DeltaTime);

	/** Starts a new walk from the offset the capsule just introduced. Positive DeltaZ means the
	 *  capsule centre moved DOWN, so the camera is held UP by that much. */
	void PushCrouchCameraOffset(float DeltaZ, bool bGoingDown);

public:

	/** The crouch counter-offset, as a capsule-space vector, for whoever writes the camera's relative
	 *  location. Zero except during a crouch transition. */
	FVector GetCrouchCameraOffset() const { return FVector(0.0f, 0.0f, CrouchCameraSmoothOffsetZ); }

protected:

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

	// ==================== First person spine pose ====================
	//
	// The states that used to shove the whole first person mesh (wallrun, slide, and the weapon's
	// reload) can instead be handed to the animation graph as a bend of the spine. The graph applies
	// them with one Transform (Modify) Bone in component space, so the animation playing on top
	// stays readable instead of riding a rigid mesh offset.
	//
	// C++ decides WHAT the pose is, per state, and adds the states together. The graph decides how
	// it reaches the skeleton. Adding a state later means one more line in
	// AccumulateFirstPersonSpinePose and nothing at all in the graph.

	/** Send wallrun / slide / reload to the spine instead of offsetting the whole mesh.
	 *
	 *  Off by default, and this is a migration switch rather than a taste one: with it on, those
	 *  states do nothing at all until the anim graph reads SpinePoseTranslation / SpinePoseRotation
	 *  and drives a spine bone with them. Turn it on once the graph is wired. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "First Person Pose")
	bool bDriveStatePosesFromSpine = false;

	/** This frame's spine pose, in component space, as handed to the anim graph. */
	FVector SpinePoseTranslation = FVector::ZeroVector;
	FRotator SpinePoseRotation = FRotator::ZeroRotator;

	/** How far each state's spine pose is faded in, 0 to 1. Interpolated so states ease in and out
	 *  instead of snapping, and summed so a reload during a wallrun is both. */
	float SlideSpineAlpha = 0.0f;
	float WallrunSpineAlpha = 0.0f;

	/** Adds this class's spine layers. Subclasses call Super first, then add their own -- the
	 *  weapon-owned ones (reload) live on AShooterCharacter, which is what knows about weapons. */
	virtual void AccumulateFirstPersonSpinePose(float DeltaTime, FVector& Translation, FRotator& Rotation);

	/** Builds the spine pose for this frame and hands it to the first person anim instance. */
	void UpdateFirstPersonSpinePose(float DeltaTime);

	/** Hands the crouch and slide blend weights to both anim instances, first person and third,
	 *  under the names CrouchAlpha and SlideAlpha. Same opt-in rule as the spine pose: a graph that
	 *  declares a float of that name gets it, one that does not is untouched.
	 *
	 *  The values themselves are UApexMovementComponent's, so a graph is free to read them straight
	 *  off the movement component (GetCrouchAlpha / GetSlideAlpha) instead -- this exists so that a
	 *  graph does not have to reach for the pawn at all. */
	void PushCrouchSlideAlphasToAnim();

public:

	// ==================== Anim graph plumbing ====================
	//
	// The same trick AShooterWeapon::PushLeftHandIK uses: write a named property on whatever anim
	// instance is running, by reflection. Any graph that declares a variable of that name picks the
	// value up, and one that does not is left alone -- so C++ never has to know which graph is on
	// which mesh, and a graph can opt in by declaring the variable and nothing else.

	static void PushAnimVector(UAnimInstance* AnimInstance, FName PropertyName, const FVector& Value);
	static void PushAnimRotator(UAnimInstance* AnimInstance, FName PropertyName, const FRotator& Value);
	static void PushAnimFloat(UAnimInstance* AnimInstance, FName PropertyName, float Value);

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
