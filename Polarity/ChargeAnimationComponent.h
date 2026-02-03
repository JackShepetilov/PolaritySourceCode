// ChargeAnimationComponent.h
// Charge toggle animation system - plays mesh-switching animation with VFX when toggling charge

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

/**
 * Charge animation state
 */
UENUM(BlueprintType)
enum class EChargeAnimationState : uint8
{
	Ready,			// Can activate
	HidingWeapon,	// Transitioning FirstPersonMesh down
	Playing,		// Animation playing, VFX active
	ShowingWeapon,	// Transitioning back to FirstPersonMesh
	Cooldown		// Brief cooldown before next activation
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

/**
 * Component that handles charge toggle animation.
 * Uses the same mesh-switching system as MeleeAttackComponent.
 * Spawns VFX attached to a socket on MeleeMesh.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UChargeAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChargeAnimationComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Settings ====================

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

	/** Total animation play duration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float AnimationDuration = 0.5f;

	/** Cooldown before next activation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Cooldown = 0.3f;

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

	/** Called when charge animation starts */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChargeAnimationStarted OnChargeAnimationStarted;

	/** Called when charge animation ends */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChargeAnimationEnded OnChargeAnimationEnded;

	// ==================== API ====================

	/**
	 * Attempt to start charge animation
	 * @return true if animation started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Charge")
	bool StartChargeAnimation();

	/**
	 * Cancel current animation (if in early phases)
	 * @return true if animation was cancelled
	 */
	UFUNCTION(BlueprintCallable, Category = "Charge")
	bool CancelAnimation();

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
	 * Check if currently animating (any phase)
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsAnimating() const;

	/**
	 * Check if input is currently locked
	 */
	UFUNCTION(BlueprintPure, Category = "Charge")
	bool IsInputLocked() const { return bInputLocked; }

protected:
	// ==================== State ====================

	/** Current animation state */
	EChargeAnimationState CurrentState = EChargeAnimationState::Ready;

	/** Time remaining in current state */
	float StateTimeRemaining = 0.0f;

	/** Input is locked */
	bool bInputLocked = false;

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

	// ==================== Mesh Transition State ====================

	/** Mesh transition progress (0-1) - used for state timing only */
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

	// ==================== Internal ====================

	/** Transition to a new state */
	void SetState(EChargeAnimationState NewState);

	/** Update current state */
	void UpdateState(float DeltaTime);

	/** Begin hiding FirstPersonMesh */
	void BeginHideWeapon();

	/** Update mesh transition */
	void UpdateMeshTransition(float DeltaTime);

	/** Switch to MeleeMesh */
	void SwitchToMeleeMesh();

	/** Switch back to FirstPersonMesh */
	void SwitchToFirstPersonMesh();

	/** Update MeleeMesh rotation to match camera */
	void UpdateMeleeMeshRotation();

	/** Play charge animation */
	void PlayChargeAnimation();

	/** Stop charge animation */
	void StopChargeAnimation();

	/** Update montage play rate based on curve */
	void UpdateMontagePlayRate(float DeltaTime);

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
};
