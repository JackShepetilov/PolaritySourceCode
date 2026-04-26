// ChargeAnimationComponent.h
// Charge toggle animation system with channeling ability
// Supports tap (instant toggle) and hold (channeling plate) modes

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChargeAnimationComponent.generated.h"

class UAnimMontage;
class USoundBase;
class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UCurveFloat;
class UEMF_FieldComponent;
class UEMFVelocityModifier;
class AEMFChannelingPlateActor;
class AEMFPhysicsProp;
class AEMFAcceleratorPlate;
class ADroppedMeleeWeapon;
class ADroppedRangedWeapon;
class AUpgradePickup;
class AScriptedPickup;

/**
 * Charge animation state
 */
UENUM(BlueprintType)
enum class EChargeAnimationState : uint8
{
	Ready,				// Can activate
	HidingWeapon,		// Transitioning FirstPersonMesh down
	// -- TAP PATH (ToggleCharge button) --
	Playing,			// Toggle animation playing, VFX active
	ShowingWeapon,		// Transitioning back to FirstPersonMesh
	Cooldown,			// Brief cooldown before next activation
	// -- CHANNELING PATH (Channel button) --
	Channeling,			// Plate active, montage frozen, player field disabled.
						// In press-press mode this means "target captured, awaiting throw press".
	ReverseChanneling,	// Channel/ToggleCharge during channeling: plate with opposite charge, timed
	FinishingAnimation,	// Montage resumes and plays to completion
	CaptureLockout,		// Press-press mode: brief window after a successful capture during which
						// channel button input is ignored, preventing instant-launch from spam.
						// Auto-transitions to Channeling after CaptureToLaunchLockout seconds.
};

/**
 * Animation data for charge toggle
 */
USTRUCT(BlueprintType)
struct FChargeAnimationData
{
	GENERATED_BODY()

	/** Animation montage for charge toggle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ChargeMontage;

	/** Play rate curve (X = normalized time 0-1, Y = play rate multiplier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UCurveFloat> PlayRateCurve;

	/** Base play rate multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float BasePlayRate = 1.0f;

	/** Location offset for MeleeMesh during animation (relative to camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FVector MeshLocationOffset = FVector::ZeroVector;

	/** Rotation offset for MeleeMesh during animation (added to camera rotation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FRotator MeshRotationOffset = FRotator::ZeroRotator;

	/** Bones to hide during animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TArray<FName> HiddenBones;
};

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChargeAnimationStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChargeAnimationEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChannelingStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChannelingEnded);

/**
 * Component that handles charge toggle animation and channeling ability.
 *
 * TOGGLE CHARGE (ToggleChargeAction): Tap toggles charge sign with animation + VFX.
 *   If pressed during channeling: triggers reverse channeling (opposite-charge plate burst).
 * CHANNEL (ChannelAction): Hold to channel — spawns invisible charged plate in front of camera.
 *       - Player's own EMF field is disabled during channeling
 *       - Plate affects enemies and physics objects
 *       - Player is moved by plate's interaction with static environment fields
 *       - Release to exit channeling
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UChargeAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChargeAnimationComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Animation Settings ====================

	/** Animation data for charge toggle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	FChargeAnimationData AnimationData;

	// ==================== Timing ====================

	/** Time to transition FirstPersonMesh down before animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float HideWeaponTime = 0.15f;

	/** Time to transition back to FirstPersonMesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ShowWeaponTime = 0.15f;

	/** Total animation play duration (for tap toggle path) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float AnimationDuration = 0.5f;

	/** Cooldown before next activation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Cooldown = 0.3f;

	// ==================== Channeling Settings ====================

	/** Offset of the channeling plate from camera (local space). X = forward */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling")
	FVector PlateOffset = FVector(200.0f, 0.0f, 0.0f);

	/** Dimensions of the channeling plate (Width x Height in cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling")
	FVector2D PlateDimensions = FVector2D(200.0f, 200.0f);

	/** Multiplier applied to the player's charge to determine plate charge density */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.1"))
	float PlateChargeDensityMultiplier = 1.0f;

	/** Class to spawn for the channeling plate actor. If not set, default class is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling")
	TSubclassOf<AEMFChannelingPlateActor> PlateActorClass;

	/** Normalized montage position (0-1) at which to freeze during channeling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChannelingFreezeFrame = 0.5f;

	/** Duration of the reverse-charge channeling burst */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ReverseChargeDuration = 0.4f;

	/** When true (default), channel button uses press-press cycle (Void Breaker-style):
	 *  press 1 = attempt capture, press 2 (after lockout) = launch.
	 *  When false, falls back to the legacy hold-to-channel / release-to-exit mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling")
	bool bUsePressPressCaptureMode = true;

	/** Lockout (seconds) after a successful capture during which channel button input is
	 *  ignored. Prevents accidental instant-launch from button spam. Press-press mode only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CaptureToLaunchLockout = 0.25f;

	/** Charge cost per second while channeling (continuous drain) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling|Charge Cost", meta = (ClampMin = "0.0"))
	float ChannelingChargeCostPerSecond = 5.0f;

	/** Fixed charge cost to activate reverse channeling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling|Charge Cost", meta = (ClampMin = "0.0"))
	float ReverseChannelingChargeCost = 3.0f;

	/** Enable debug visualization of the channeling plate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling|Debug")
	bool bDrawDebugPlate = false;

	// ==================== Capture Targeting ====================

	/** Maximum distance from player to consider NPC for capture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling|Capture", meta = (ClampMin = "100.0", Units = "cm"))
	float CaptureSearchRadius = 2000.0f;

	/** Max angle (degrees) from camera forward to consider NPC as target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling|Capture", meta = (ClampMin = "1.0", ClampMax = "90.0", Units = "deg"))
	float CaptureMaxAngle = 30.0f;

	// ==================== VFX ====================

	/** Niagara effect to spawn during charge toggle (legacy - used when polarity-based VFX not set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> ChargeVFX;

	/** VFX for positive charge - played when switching TO positive polarity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> PositiveChargeVFX;

	/** VFX for negative charge - played when switching TO negative polarity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> NegativeChargeVFX;

	/** Socket name on MeleeMesh to attach VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FName ChargeVFXSocket = FName("ChargeSocket");

	// ==================== Channeling VFX ====================

	// --- Capture VFX (lightning beam from plate to captured object, one-shot) ---

	/** VFX for capturing with positive polarity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Capture")
	TObjectPtr<UNiagaraSystem> PositiveCaptureVFX;

	/** VFX for capturing with negative polarity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Capture")
	TObjectPtr<UNiagaraSystem> NegativeCaptureVFX;

	/** Offset from plate position for capture VFX spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Capture")
	FVector CaptureVFXOffset = FVector::ZeroVector;

	/** Scale of capture VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Capture")
	FVector CaptureVFXScale = FVector(1.0f);

	// --- Launch VFX (lightning beam from plate to launched object, one-shot) ---

	/** VFX for launching with positive polarity (checked AFTER polarity flip) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Launch")
	TObjectPtr<UNiagaraSystem> PositiveLaunchVFX;

	/** VFX for launching with negative polarity (checked AFTER polarity flip) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Launch")
	TObjectPtr<UNiagaraSystem> NegativeLaunchVFX;

	/** Offset from plate position for launch VFX spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Launch")
	FVector LaunchVFXOffset = FVector::ZeroVector;

	/** Scale of launch VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Launch")
	FVector LaunchVFXScale = FVector(1.0f);

	// --- Hold VFX (continuous effect on hand while holding captured object) ---

	/** Continuous VFX while holding with positive polarity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Hold")
	TObjectPtr<UNiagaraSystem> PositiveHoldVFX;

	/** Continuous VFX while holding with negative polarity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Hold")
	TObjectPtr<UNiagaraSystem> NegativeHoldVFX;

	/** Offset from socket for hold VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Hold")
	FVector HoldVFXOffset = FVector::ZeroVector;

	/** Scale of hold VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Hold")
	FVector HoldVFXScale = FVector(1.0f);

	/** Socket on MeleeMesh for hold VFX attachment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Channeling|Hold")
	FName HoldVFXSocket = FName("ChargeSocket");

	// ==================== Audio ====================

	/** Sound to play when charge animation starts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> ChargeSound;

	// ==================== Mesh References ====================

	/** Global rotation offset for MeleeMesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FRotator MeleeMeshRotationOffset = FRotator::ZeroRotator;

	/** Reference to FirstPersonMesh (auto-detected if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh References")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** Reference to MeleeMesh for animation playback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh References")
	TObjectPtr<USkeletalMeshComponent> MeleeMesh;

	// ==================== Events ====================

	/** Called when charge animation starts (tap path) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChargeAnimationStarted OnChargeAnimationStarted;

	/** Called when charge animation ends */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChargeAnimationEnded OnChargeAnimationEnded;

	/** Called when channeling starts */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChannelingStarted OnChannelingStarted;

	/** Called when channeling ends (including reverse charge) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChannelingEnded OnChannelingEnded;

	// ==================== Input API ====================

	/** Called when charge button is pressed — tap toggle, or reverse channeling if channeling */
	void OnChargeButtonPressed();

	/** Called when charge button is released */
	void OnChargeButtonReleased();

	/** Called when channel button is pressed — enters channeling */
	void OnChannelButtonPressed();

	/** Called when channel button is released — exits channeling */
	void OnChannelButtonReleased();

	// ==================== Query API ====================

	/**
	 * Check if animation can be started
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool CanStartAnimation() const;

	/**
	 * Get current animation state
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	EChargeAnimationState GetAnimationState() const { return CurrentState; }

	/**
	 * Check if currently animating (any active phase — blocks melee)
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsAnimating() const;

	/**
	 * Check if currently blocking weapon fire.
	 * Unlike IsAnimating(), this returns false during Channeling and ReverseChanneling
	 * so the player can shoot while using charge abilities.
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsBlockingFiring() const;

	/**
	 * Check if currently channeling (Channeling or ReverseChanneling)
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsChanneling() const;

	/**
	 * Check if input is currently locked
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsInputLocked() const { return bInputLocked; }

	/** Externally lock/unlock input (used by finale sequence to block normal grab) */
	UFUNCTION(BlueprintCallable, Category = "Charge")
	void SetInputLocked(bool bLocked) { bInputLocked = bLocked; }

	/**
	 * Cancel current animation (if in early phases)
	 * @return true if animation was cancelled
	 */
	UFUNCTION(BlueprintCallable, Category = "Charge")
	bool CancelAnimation();

protected:
	// ==================== State ====================

	/** Current animation state */
	EChargeAnimationState CurrentState = EChargeAnimationState::Ready;

	/** Time remaining in current state */
	float StateTimeRemaining = 0.0f;

	/** Input is locked */
	bool bInputLocked = false;

	// ==================== Path Flag ====================

	/** True if current activation is channeling path (channel button), false for tap toggle */
	bool bChannelingPath = false;

	// ==================== Channeling State ====================

	/** Spawned plate actor (valid during channeling) */
	UPROPERTY()
	TObjectPtr<AEMFChannelingPlateActor> ChannelingPlateActor;

	/** Cached player charge sign at channeling start (1 or -1) */
	int32 ChannelingChargeSign = 1;

	/** Press-press mode: time remaining in the post-capture input-lockout window.
	 *  While >0 (and state == Channeling), a second channel-button press is ignored to
	 *  prevent instant-launch from spam. Counted down in UpdateState's Channeling tick.
	 *  Held inside the Channeling state (no separate state) so the animation flow stays
	 *  identical to the legacy hold-mode timeline. */
	float CaptureLockoutTimeRemaining = 0.0f;

	/** Cached reference to EMFVelocityModifier */
	UPROPERTY()
	TObjectPtr<UEMFVelocityModifier> CachedEMFModifier;

	/** Cached reference to UEMF_FieldComponent (player's own field) */
	UPROPERTY()
	TObjectPtr<UEMF_FieldComponent> CachedFieldComponent;

	/** Was the player's field registered before channeling? */
	bool bFieldWasRegistered = false;

	// ==================== Cached References ====================

	/** Cached owner character */
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	/** Cached owner controller */
	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	/** Cached camera component for mesh attachment */
	UPROPERTY()
	TObjectPtr<class UCameraComponent> CameraComponent;

	/** Cached shooter character for LeftHandIK control */
	UPROPERTY()
	TObjectPtr<class AShooterCharacter> ShooterCharacter;

	/** Active VFX component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveChargeFX;

	/** Active hold VFX component (continuous, while holding captured object) */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveHoldVFX;

	/** Active capture/launch beam VFX (tracked each tick while alive) */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveCaptureVFX;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveLaunchVFX;

	/** Update beam VFX positions to follow plate and target */
	void UpdateBeamVFX();

	// ==================== Montage State ====================

	/** Mesh transition progress (0-1) */
	float MeshTransitionProgress = 0.0f;

	/** Current montage being played */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentMontage;

	/** Bones currently hidden */
	TArray<FName> CurrentlyHiddenBones;

	/** Time elapsed in current montage */
	float MontageTimeElapsed = 0.0f;

	/** Total duration of current montage at base rate */
	float MontageTotalDuration = 0.0f;

	// ==================== Internal Methods ====================

	/** Transition to a new state */
	void SetState(EChargeAnimationState NewState);

	/** Update current state each tick */
	void UpdateState(float DeltaTime);

	/** Begin hiding FirstPersonMesh */
	void BeginHideWeapon();

	/** Update mesh transition progress */
	void UpdateMeshTransition(float DeltaTime);

	/** Switch to MeleeMesh */
	void SwitchToMeleeMesh();

	/** Switch back to FirstPersonMesh */
	void SwitchToFirstPersonMesh();

	/** Update MeleeMesh rotation to match camera */
	void UpdateMeleeMeshRotation();

	/** Play charge animation montage */
	void PlayChargeAnimation();

	/** Stop charge animation montage */
	void StopChargeAnimation();

	/** Update montage play rate based on curve (skips during channeling freeze) */
	void UpdateMontagePlayRate(float DeltaTime);

	/** Get camera view point (lazy-resolves controller if needed). Returns false if unavailable. */
	bool GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	/** Spawn charge VFX based on new polarity */
	void SpawnChargeVFX();

	/** Get owner's NEW charge value after toggle (inverted from current) */
	float GetNewChargeAfterToggle() const;

	/** Stop and destroy charge VFX */
	void StopChargeVFX();

	/** Spawn one-shot capture VFX (lightning from plate to captured target) */
	void SpawnCaptureVFX(AActor* CapturedTarget);

	/** Spawn one-shot launch VFX (lightning from plate to launched target, uses post-flip polarity) */
	void SpawnLaunchVFX(AActor* LaunchedTarget);

	/** Spawn continuous hold VFX on hand (MeleeMesh) */
	void SpawnHoldVFX();

	/** Stop and destroy hold VFX */
	void StopHoldVFX();

	/** Play sound effect */
	void PlaySound(USoundBase* Sound);

	/** Called when montage ends */
	UFUNCTION()
	void OnMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Auto-detect mesh references if not set */
	void AutoDetectMeshReferences();

	// ==================== Channeling Methods ====================

	/** Enter channeling state: freeze montage, spawn plate, disable player field */
	void EnterChanneling();

	/** Exit channeling: destroy plate, disable proxy mode */
	void ExitChanneling();

	/** Spawn the channeling plate actor with given charge sign */
	void SpawnPlate(int32 ChargeSign);

	/** Destroy the active plate actor */
	void DestroyPlate();

	/** Update plate position to follow camera */
	void UpdatePlatePosition();

	// ==================== Capture Methods ====================

	/** Raycast from camera to find and capture NPCs during channeling */
	void UpdateCaptureRaycast(const FVector& CameraLoc, const FRotator& CameraRot);

	/** Capture a specific NPC */
	void CaptureNPC(class AShooterNPC* NPC);

	/** Capture a specific physics prop */
	void CaptureProp(AEMFPhysicsProp* Prop);

	/** Capture a dropped melee weapon (scripted pull, not physics-based) */
	void CaptureDroppedWeapon(ADroppedMeleeWeapon* Weapon);

	/** Capture a dropped ranged weapon (scripted pull, not physics-based) */
	void CaptureDroppedRangedWeapon(ADroppedRangedWeapon* Weapon);

	/** Capture an upgrade pickup (scripted pull, not physics-based) */
	void CaptureUpgradePickup(AUpgradePickup* Pickup);

	/** Capture a scripted pickup (scripted pull, same as UpgradePickup but no upgrade) */
	void CaptureScriptedPickup(AScriptedPickup* Pickup);

	/** Capture an accelerator plate (lowest priority, no charge dependency) */
	void CaptureAcceleratorPlate(AEMFAcceleratorPlate* Plate);

	/** Release the currently captured NPC or prop */
	void ReleaseCapturedNPC();

	/** Currently captured NPC (managed by raycast during channeling) */
	TWeakObjectPtr<AActor> CurrentCapturedNPC;

	/** Perform the tap toggle: switch charge sign */
	void PerformTapToggle();

	/** Press-press mode: trigger reverse channeling launch from a held capture.
	 *  Extracted from OnChargeButtonPressed Case 2 so the channel button can drive launches. */
	void BeginLaunch();

	/** Enter the finishing animation state: re-enable field, resume montage */
	void EnterFinishingAnimation();

	/** Cleanup all channeling state (safety — called on EndPlay/cancel) */
	void CleanupChanneling();
};
