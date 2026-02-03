// ChargeAnimationComponent.cpp
// Charge toggle animation system implementation

#include "ChargeAnimationComponent.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "PolarityCharacter.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "EMF_FieldComponent.h"

UChargeAnimationComponent::UChargeAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UChargeAnimationComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache owner references
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter)
	{
		OwnerController = Cast<APlayerController>(OwnerCharacter->GetController());

		// Find camera component
		CameraComponent = OwnerCharacter->FindComponentByClass<UCameraComponent>();

		// Cache ShooterCharacter for LeftHandIK control
		ShooterCharacter = Cast<AShooterCharacter>(OwnerCharacter);
	}

	// Auto-detect mesh references
	AutoDetectMeshReferences();

	// Note: FirstPersonMesh position is not modified during charge animation
	// Note: We don't control MeleeMesh visibility here as MeleeAttackComponent manages it
}

void UChargeAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateState(DeltaTime);
	UpdateMeshTransition(DeltaTime);
	// UpdateMeleeMeshRotation() - no longer needed, MeleeMesh is attached to camera
	UpdateMontagePlayRate(DeltaTime);
}

bool UChargeAnimationComponent::StartChargeAnimation()
{
	if (!CanStartAnimation())
	{
		return false;
	}

	// Lock input immediately
	bInputLocked = true;

	// Reset state
	MeshTransitionProgress = 0.0f;
	MontageTimeElapsed = 0.0f;

	// Start with mesh transition
	BeginHideWeapon();
	SetState(EChargeAnimationState::HidingWeapon);

	return true;
}

bool UChargeAnimationComponent::CancelAnimation()
{
	// Can only cancel during early phases
	if (CurrentState != EChargeAnimationState::HidingWeapon)
	{
		return false;
	}

	StopChargeAnimation();
	StopChargeVFX();
	SwitchToFirstPersonMesh();
	// Restore left hand IK on cancel
	if (ShooterCharacter)
	{
		ShooterCharacter->SetLeftHandIKAlpha(1.0f);
	}
	bInputLocked = false;
	SetState(EChargeAnimationState::Ready);

	return true;
}

bool UChargeAnimationComponent::CanStartAnimation() const
{
	// Must be ready and input not locked
	if (CurrentState != EChargeAnimationState::Ready || bInputLocked)
	{
		return false;
	}

	// Must have valid owner
	if (!OwnerCharacter)
	{
		return false;
	}

	// Don't start if ground or sliding melee attack is in progress (allow air melee)
	if (UMeleeAttackComponent* MeleeComp = OwnerCharacter->FindComponentByClass<UMeleeAttackComponent>())
	{
		if (MeleeComp->IsAttacking())
		{
			EMeleeAttackType AttackType = MeleeComp->GetCurrentAttackType();
			if (AttackType == EMeleeAttackType::Ground || AttackType == EMeleeAttackType::Sliding)
			{
				return false;
			}
		}
	}

	return true;
}

bool UChargeAnimationComponent::IsAnimating() const
{
	return CurrentState == EChargeAnimationState::HidingWeapon ||
		CurrentState == EChargeAnimationState::Playing ||
		CurrentState == EChargeAnimationState::ShowingWeapon;
}

void UChargeAnimationComponent::SetState(EChargeAnimationState NewState)
{
	CurrentState = NewState;

	switch (NewState)
	{
	case EChargeAnimationState::Ready:
		StateTimeRemaining = 0.0f;
		bInputLocked = false;
		break;

	case EChargeAnimationState::HidingWeapon:
		StateTimeRemaining = HideWeaponTime;
		MeshTransitionProgress = 0.0f;
		break;

	case EChargeAnimationState::Playing:
		StateTimeRemaining = AnimationDuration;
		// Detach left hand from weapon during animation
		if (ShooterCharacter)
		{
			ShooterCharacter->SetLeftHandIKAlpha(0.0f);
		}
		break;

	case EChargeAnimationState::ShowingWeapon:
		StateTimeRemaining = ShowWeaponTime;
		MeshTransitionProgress = 0.0f;
		StopChargeAnimation();
		StopChargeVFX();
		SwitchToFirstPersonMesh();
		// Restore left hand to weapon
		if (ShooterCharacter)
		{
			ShooterCharacter->SetLeftHandIKAlpha(1.0f);
		}
		break;

	case EChargeAnimationState::Cooldown:
		StateTimeRemaining = Cooldown;
		OnChargeAnimationEnded.Broadcast();
		break;
	}
}

void UChargeAnimationComponent::UpdateState(float DeltaTime)
{
	if (CurrentState == EChargeAnimationState::Ready)
	{
		return;
	}

	// Update timer
	StateTimeRemaining -= DeltaTime;

	if (StateTimeRemaining <= 0.0f)
	{
		// Transition to next state
		switch (CurrentState)
		{
		case EChargeAnimationState::HidingWeapon:
			// Mesh transition complete - switch meshes and start animation
			SwitchToMeleeMesh();
			PlayChargeAnimation();
			SpawnChargeVFX();
			PlaySound(ChargeSound);
			OnChargeAnimationStarted.Broadcast();
			SetState(EChargeAnimationState::Playing);
			break;

		case EChargeAnimationState::Playing:
			SetState(EChargeAnimationState::ShowingWeapon);
			break;

		case EChargeAnimationState::ShowingWeapon:
			SetState(EChargeAnimationState::Cooldown);
			break;

		case EChargeAnimationState::Cooldown:
			SetState(EChargeAnimationState::Ready);
			break;

		default:
			break;
		}
	}
}

void UChargeAnimationComponent::BeginHideWeapon()
{
	MeshTransitionProgress = 0.0f;
	// Note: FirstPersonMesh position is not changed during charge animation
}

void UChargeAnimationComponent::UpdateMeshTransition(float DeltaTime)
{
	// Note: FirstPersonMesh is not moved during charge animation (remains at original position)
	// Only update transition progress for state timing
	if (CurrentState == EChargeAnimationState::HidingWeapon)
	{
		if (HideWeaponTime > 0.0f)
		{
			MeshTransitionProgress += DeltaTime / HideWeaponTime;
			MeshTransitionProgress = FMath::Clamp(MeshTransitionProgress, 0.0f, 1.0f);
		}
	}
	else if (CurrentState == EChargeAnimationState::ShowingWeapon)
	{
		if (ShowWeaponTime > 0.0f)
		{
			MeshTransitionProgress += DeltaTime / ShowWeaponTime;
			MeshTransitionProgress = FMath::Clamp(MeshTransitionProgress, 0.0f, 1.0f);
		}
	}
}

void UChargeAnimationComponent::SwitchToMeleeMesh()
{
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(false);
	}

	// Note: Weapon is NOT hidden during charge animation (only FirstPersonMesh is hidden)

	if (MeleeMesh)
	{
		MeleeMesh->SetVisibility(true);

		// Hide specified bones
		CurrentlyHiddenBones = AnimationData.HiddenBones;
		for (const FName& BoneName : CurrentlyHiddenBones)
		{
			MeleeMesh->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		}

		// Attach to camera for automatic synchronization
		if (CameraComponent)
		{
			MeleeMesh->AttachToComponent(
				CameraComponent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale
			);

			// Set relative transform (offset from camera)
			MeleeMesh->SetRelativeLocation(AnimationData.MeshLocationOffset);

			// Combine global and per-animation rotation offsets
			FRotator FinalRelativeRotation = MeleeMeshRotationOffset + AnimationData.MeshRotationOffset;
			MeleeMesh->SetRelativeRotation(FinalRelativeRotation);
		}
	}
}

void UChargeAnimationComponent::SwitchToFirstPersonMesh()
{
	if (MeleeMesh)
	{
		// Detach from camera
		MeleeMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

		MeleeMesh->SetVisibility(false);

		// Unhide bones
		for (const FName& BoneName : CurrentlyHiddenBones)
		{
			MeleeMesh->UnHideBoneByName(BoneName);
		}
		CurrentlyHiddenBones.Empty();
	}

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(true);
	}

	// Note: Weapon visibility is not changed (remains visible throughout)
}

void UChargeAnimationComponent::UpdateMeleeMeshRotation()
{
	// Only update during active phases
	if (CurrentState != EChargeAnimationState::Playing)
	{
		return;
	}

	if (!MeleeMesh || !OwnerController)
	{
		return;
	}

	// Get camera transform
	FVector CameraLocation;
	FRotator CameraRotation;
	OwnerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// Combine rotations
	FQuat CameraQuat = CameraRotation.Quaternion();
	FQuat GlobalOffsetQuat = MeleeMeshRotationOffset.Quaternion();
	FQuat AnimOffsetQuat = AnimationData.MeshRotationOffset.Quaternion();
	FQuat FinalQuat = CameraQuat * GlobalOffsetQuat * AnimOffsetQuat;

	FRotator FinalRotation = FinalQuat.Rotator();

	// Calculate location
	FVector LocalOffset = AnimationData.MeshLocationOffset;
	FVector WorldOffset = CameraRotation.RotateVector(LocalOffset);
	FVector FinalLocation = CameraLocation + WorldOffset;

	// Apply final transform
	MeleeMesh->SetWorldLocationAndRotation(FinalLocation, FinalRotation);
}

void UChargeAnimationComponent::PlayChargeAnimation()
{
	if (!MeleeMesh || !AnimationData.ChargeMontage)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	// Calculate adjusted play rate to match AnimationDuration
	float MontageLength = AnimationData.ChargeMontage->GetPlayLength();
	float AdjustedPlayRate = AnimationData.BasePlayRate;
	
	if (MontageLength > 0.0f && AnimationDuration > 0.0f)
	{
		AdjustedPlayRate = MontageLength / AnimationDuration * AnimationData.BasePlayRate;
	}

	// Play montage
	float Duration = AnimInstance->Montage_Play(AnimationData.ChargeMontage, AdjustedPlayRate);

	if (Duration > 0.0f)
	{
		CurrentMontage = AnimationData.ChargeMontage;
		MontageTotalDuration = Duration;
		MontageTimeElapsed = 0.0f;

		// Bind to montage end
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &UChargeAnimationComponent::OnMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, AnimationData.ChargeMontage);
	}
}

void UChargeAnimationComponent::StopChargeAnimation()
{
	if (!MeleeMesh || !CurrentMontage)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->Montage_Stop(0.1f, CurrentMontage);
	}

	CurrentMontage = nullptr;
}

void UChargeAnimationComponent::UpdateMontagePlayRate(float DeltaTime)
{
	if (!CurrentMontage || !MeleeMesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance();
	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(CurrentMontage))
	{
		return;
	}

	if (!AnimationData.PlayRateCurve || MontageTotalDuration <= 0.0f)
	{
		return;
	}

	// Update elapsed time
	MontageTimeElapsed += DeltaTime;

	// Calculate normalized progress
	float NormalizedProgress = FMath::Clamp(MontageTimeElapsed / MontageTotalDuration, 0.0f, 1.0f);

	// Sample the curve
	float CurveValue = AnimationData.PlayRateCurve->GetFloatValue(NormalizedProgress);
	float NewPlayRate = AnimationData.BasePlayRate * CurveValue;

	// Apply new play rate
	AnimInstance->Montage_SetPlayRate(CurrentMontage, NewPlayRate);
}

float UChargeAnimationComponent::GetNewChargeAfterToggle() const
{
	if (!OwnerCharacter)
	{
		return 0.0f;
	}

	UEMF_FieldComponent* FieldComp = OwnerCharacter->FindComponentByClass<UEMF_FieldComponent>();
	if (!FieldComp)
	{
		return 0.0f;
	}

	// Get current charge and invert it (since we're about to toggle)
	float CurrentCharge = FieldComp->GetSourceDescription().PointChargeParams.Charge;
	return -CurrentCharge;
}

void UChargeAnimationComponent::SpawnChargeVFX()
{
	if (!MeleeMesh)
	{
		return;
	}

	// Determine which VFX to use based on the NEW charge (after toggle)
	UNiagaraSystem* VFXToSpawn = nullptr;
	float NewCharge = GetNewChargeAfterToggle();

	if (NewCharge > 0.0f && PositiveChargeVFX)
	{
		VFXToSpawn = PositiveChargeVFX;
	}
	else if (NewCharge < 0.0f && NegativeChargeVFX)
	{
		VFXToSpawn = NegativeChargeVFX;
	}
	else
	{
		// Fallback to legacy ChargeVFX
		VFXToSpawn = ChargeVFX;
	}

	if (!VFXToSpawn)
	{
		return;
	}

	// Spawn attached to socket
	ActiveChargeFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
		VFXToSpawn,
		MeleeMesh,
		ChargeVFXSocket,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		true
	);
}

void UChargeAnimationComponent::StopChargeVFX()
{
	if (ActiveChargeFX)
	{
		ActiveChargeFX->DeactivateImmediate();
		ActiveChargeFX = nullptr;
	}
}

void UChargeAnimationComponent::PlaySound(USoundBase* Sound)
{
	if (!Sound || !OwnerCharacter)
	{
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(
		this,
		Sound,
		OwnerCharacter->GetActorLocation()
	);
}

void UChargeAnimationComponent::OnMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == CurrentMontage)
	{
		CurrentMontage = nullptr;
	}
}

void UChargeAnimationComponent::AutoDetectMeshReferences()
{
	if (!OwnerCharacter)
	{
		return;
	}

	// Try to get FirstPersonMesh from PolarityCharacter
	if (!FirstPersonMesh)
	{
		if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
		{
			FirstPersonMesh = PolarityChar->GetFirstPersonMesh();
		}
	}

	// If still not found, try to find by component name
	if (!FirstPersonMesh)
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshes;
		OwnerCharacter->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);

		for (USkeletalMeshComponent* Mesh : SkeletalMeshes)
		{
			if (Mesh != OwnerCharacter->GetMesh())
			{
				if (Mesh->GetName().Contains(TEXT("FirstPerson")))
				{
					FirstPersonMesh = Mesh;
					break;
				}
			}
		}
	}

	// MeleeMesh - find by tag
	if (!MeleeMesh)
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshes;
		OwnerCharacter->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);

		for (USkeletalMeshComponent* Mesh : SkeletalMeshes)
		{
			if (Mesh->ComponentHasTag(TEXT("MeleeMesh")))
			{
				MeleeMesh = Mesh;
				break;
			}
		}
	}
}
