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

/**
 * Charge animation state
 */
UENUM(BlueprintType)
enum class EChargeAnimationState : uint8
{
	Ready,				// Can activate
	HidingWeapon,		// Transitioning FirstPersonMesh down
	// -- TAP PATH (press < TapThreshold) --
	Playing,			// Toggle animation playing, VFX active
	ShowingWeapon,		// Transitioning back to FirstPersonMesh
	Cooldown,			// Brief cooldown before next activation
	// -- CHANNELING PATH (hold >= TapThreshold) --
	Channeling,			// Plate active, montage frozen, player field disabled
	ChannelingRelease,	// Released — post-release window for reverse tap
	ReverseChanneling,	// Post-release tap: plate with opposite charge, timed
	FinishingAnimation,	// Montage resumes and plays to completion
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
 * TAP: Quick press (< TapThreshold) toggles charge sign with animation + VFX.
 * HOLD: Sustained press spawns an invisible charged plate in front of camera.
 *       - Player's own EMF field is disabled during channeling
 *       - Plate affects enemies and physics objects
 *       - Player is moved by plate's interaction with static environment fields
 *       - On release: short window to tap again for reverse-charge burst
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

	// ==================== Tap vs Hold ====================

	/** Maximum press duration to count as "tap". Hold longer = channeling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float TapThreshold = 0.15f;

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

	/** Post-release window duration for reverse-charge tap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ReverseChargeWindow = 0.2f;

	/** Duration of the reverse-charge channeling burst */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ReverseChargeDuration = 0.4f;

	/** Enable debug visualization of the channeling plate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling|Debug")
	bool bDrawDebugPlate = false;

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

	/** Called when charge button is pressed (from PolarityCharacter) */
	void OnChargeButtonPressed();

	/** Called when charge button is released (from PolarityCharacter) */
	void OnChargeButtonReleased();

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
	 * Check if currently channeling (Channeling or ReverseChanneling)
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsChanneling() const;

	/**
	 * Check if input is currently locked
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsInputLocked() const { return bInputLocked; }

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

	// ==================== Tap/Hold Detection ====================

	/** Time when button was pressed */
	float ButtonPressTime = 0.0f;

	/** Is the button currently held down? */
	bool bButtonHeld = false;

	/** Has this press been committed as a hold (passed threshold)? */
	bool bCommittedAsHold = false;

	/** Has tap toggle been performed for the current press? */
	bool bTapToggleDone = false;

	// ==================== Channeling State ====================

	/** Spawned plate actor (valid during channeling) */
	UPROPERTY()
	TObjectPtr<AEMFChannelingPlateActor> ChannelingPlateActor;

	/** Cached player charge sign at channeling start (1 or -1) */
	int32 ChannelingChargeSign = 1;

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

	/** Perform the tap toggle: switch charge sign */
	void PerformTapToggle();

	/** Enter the finishing animation state: re-enable field, resume montage */
	void EnterFinishingAnimation();

	/** Cleanup all channeling state (safety — called on EndPlay/cancel) */
	void CleanupChanneling();
};
