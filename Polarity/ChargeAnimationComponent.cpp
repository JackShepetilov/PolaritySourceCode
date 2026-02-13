// ChargeAnimationComponent.cpp
// Charge toggle animation + channeling ability implementation

#include "ChargeAnimationComponent.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "EMFChannelingPlateActor.h"
#include "EMFVelocityModifier.h"
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
		CameraComponent = OwnerCharacter->FindComponentByClass<UCameraComponent>();
		ShooterCharacter = Cast<AShooterCharacter>(OwnerCharacter);

		// Cache EMF references
		CachedEMFModifier = OwnerCharacter->FindComponentByClass<UEMFVelocityModifier>();
		CachedFieldComponent = OwnerCharacter->FindComponentByClass<UEMF_FieldComponent>();
	}

	AutoDetectMeshReferences();
}

void UChargeAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Safety cleanup: ensure no orphaned plate actors or stuck state
	CleanupChanneling();

	Super::EndPlay(EndPlayReason);
}

void UChargeAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateState(DeltaTime);
	UpdateMeshTransition(DeltaTime);
	UpdateMontagePlayRate(DeltaTime);

	// Update plate position during channeling states
	if (CurrentState == EChargeAnimationState::Channeling || CurrentState == EChargeAnimationState::ReverseChanneling)
	{
		UpdatePlatePosition();
	}
}

// ==================== Input API ====================

void UChargeAnimationComponent::OnChargeButtonPressed()
{
	// Case 1: Normal activation from Ready state
	if (CurrentState == EChargeAnimationState::Ready && CanStartAnimation())
	{
		ButtonPressTime = GetWorld()->GetTimeSeconds();
		bButtonHeld = true;
		bCommittedAsHold = false;
		bTapToggleDone = false;
		bInputLocked = true;

		MeshTransitionProgress = 0.0f;
		MontageTimeElapsed = 0.0f;

		BeginHideWeapon();
		SetState(EChargeAnimationState::HidingWeapon);
		return;
	}

	// Case 2: Reverse-charge tap during post-release window
	if (CurrentState == EChargeAnimationState::ChannelingRelease)
	{
		// Spawn plate with OPPOSITE charge sign
		SpawnPlate(-ChannelingChargeSign);

		// Enable proxy mode with the new plate
		if (CachedEMFModifier)
		{
			CachedEMFModifier->SetChannelingProxyMode(true, ChannelingPlateActor);
		}

		SetState(EChargeAnimationState::ReverseChanneling);
		return;
	}
}

void UChargeAnimationComponent::OnChargeButtonReleased()
{
	bButtonHeld = false;

	// If released during channeling, exit to release window
	if (CurrentState == EChargeAnimationState::Channeling)
	{
		ExitChanneling();
		SetState(EChargeAnimationState::ChannelingRelease);
		return;
	}

	// If released during HidingWeapon or Playing and not yet committed as hold,
	// bButtonHeld = false will be picked up by UpdateState to commit as tap
}

// ==================== State Machine ====================

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
		if (ShooterCharacter)
		{
			ShooterCharacter->SetLeftHandIKAlpha(1.0f);
		}
		break;

	case EChargeAnimationState::Cooldown:
		StateTimeRemaining = Cooldown;
		OnChargeAnimationEnded.Broadcast();
		break;

	case EChargeAnimationState::Channeling:
		StateTimeRemaining = 0.0f; // No timer — held indefinitely
		break;

	case EChargeAnimationState::ChannelingRelease:
		StateTimeRemaining = ReverseChargeWindow;
		break;

	case EChargeAnimationState::ReverseChanneling:
		StateTimeRemaining = ReverseChargeDuration;
		break;

	case EChargeAnimationState::FinishingAnimation:
		// Timer set in EnterFinishingAnimation() based on remaining montage length
		break;
	}
}

void UChargeAnimationComponent::UpdateState(float DeltaTime)
{
	if (CurrentState == EChargeAnimationState::Ready)
	{
		return;
	}

	// Special logic for Playing state: tap vs hold decision
	if (CurrentState == EChargeAnimationState::Playing && !bCommittedAsHold && !bTapToggleDone)
	{
		float ElapsedSincePress = GetWorld()->GetTimeSeconds() - ButtonPressTime;

		if (bButtonHeld && ElapsedSincePress >= TapThreshold)
		{
			// HOLD committed — enter channeling
			bCommittedAsHold = true;
			EnterChanneling();
			return;
		}

		if (!bButtonHeld)
		{
			// TAP committed — toggle charge
			bTapToggleDone = true;
			PerformTapToggle();
			SpawnChargeVFX();
			// Continue in Playing state — timer will run out and go to ShowingWeapon
		}
	}

	// Channeling state: no timer, just update plate position (done in TickComponent)
	if (CurrentState == EChargeAnimationState::Channeling)
	{
		return;
	}

	// Update timer
	StateTimeRemaining -= DeltaTime;

	if (StateTimeRemaining <= 0.0f)
	{
		switch (CurrentState)
		{
		case EChargeAnimationState::HidingWeapon:
			// Mesh transition complete — switch meshes and start animation
			SwitchToMeleeMesh();
			PlayChargeAnimation();
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

		case EChargeAnimationState::ChannelingRelease:
			// Window expired — no reverse tap. Finish animation.
			EnterFinishingAnimation();
			break;

		case EChargeAnimationState::ReverseChanneling:
			// Reverse channeling time expired
			DestroyPlate();
			if (CachedEMFModifier)
			{
				CachedEMFModifier->SetChannelingProxyMode(false);
			}
			EnterFinishingAnimation();
			break;

		case EChargeAnimationState::FinishingAnimation:
			// Montage finished (or timer expired) — go to ShowingWeapon
			SetState(EChargeAnimationState::ShowingWeapon);
			break;

		default:
			break;
		}
	}
}

// ==================== Query API ====================

bool UChargeAnimationComponent::CanStartAnimation() const
{
	if (CurrentState != EChargeAnimationState::Ready || bInputLocked)
	{
		return false;
	}

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
	return CurrentState != EChargeAnimationState::Ready &&
	       CurrentState != EChargeAnimationState::Cooldown;
}

bool UChargeAnimationComponent::IsChanneling() const
{
	return CurrentState == EChargeAnimationState::Channeling ||
	       CurrentState == EChargeAnimationState::ReverseChanneling;
}

bool UChargeAnimationComponent::CancelAnimation()
{
	if (CurrentState != EChargeAnimationState::HidingWeapon)
	{
		return false;
	}

	StopChargeAnimation();
	StopChargeVFX();
	SwitchToFirstPersonMesh();
	if (ShooterCharacter)
	{
		ShooterCharacter->SetLeftHandIKAlpha(1.0f);
	}
	bInputLocked = false;
	bButtonHeld = false;
	SetState(EChargeAnimationState::Ready);

	return true;
}

// ==================== Channeling ====================

void UChargeAnimationComponent::EnterChanneling()
{
	UE_LOG(LogTemp, Warning, TEXT("=== EnterChanneling START ==="));

	// Save the player's current charge sign
	if (CachedEMFModifier)
	{
		ChannelingChargeSign = CachedEMFModifier->GetChargeSign();
		if (ChannelingChargeSign == 0)
		{
			ChannelingChargeSign = 1; // Default to positive if neutral
		}
		UE_LOG(LogTemp, Warning, TEXT("EnterChanneling: ChargeSign=%d"), ChannelingChargeSign);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("EnterChanneling: CachedEMFModifier is NULL!"));
	}

	// Freeze montage at the specified frame
	if (MeleeMesh && CurrentMontage)
	{
		UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance();
		if (AnimInstance)
		{
			float MontageLength = CurrentMontage->GetPlayLength();
			float FreezePosition = ChannelingFreezeFrame * MontageLength;
			AnimInstance->Montage_SetPosition(CurrentMontage, FreezePosition);
			AnimInstance->Montage_SetPlayRate(CurrentMontage, 0.0f);
		}
	}

	// Disable player's own EMF field (unregister from registry)
	if (CachedFieldComponent)
	{
		bFieldWasRegistered = CachedFieldComponent->IsRegistered();
		if (bFieldWasRegistered)
		{
			CachedFieldComponent->UnregisterFromRegistry();
		}
		UE_LOG(LogTemp, Warning, TEXT("EnterChanneling: FieldWasRegistered=%d"), bFieldWasRegistered);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("EnterChanneling: CachedFieldComponent is NULL!"));
	}

	// Spawn the channeling plate with the same charge sign as the player
	SpawnPlate(ChannelingChargeSign);
	UE_LOG(LogTemp, Warning, TEXT("EnterChanneling: PlateSpawned=%d"), ChannelingPlateActor != nullptr);

	// Enable proxy mode on EMFVelocityModifier
	if (CachedEMFModifier)
	{
		CachedEMFModifier->SetChannelingProxyMode(true, ChannelingPlateActor);
	}

	SetState(EChargeAnimationState::Channeling);
	OnChannelingStarted.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("=== EnterChanneling DONE, state=Channeling ==="));
}

void UChargeAnimationComponent::ExitChanneling()
{
	// Destroy the plate
	DestroyPlate();

	// Disable proxy mode
	if (CachedEMFModifier)
	{
		CachedEMFModifier->SetChannelingProxyMode(false);
	}
}

void UChargeAnimationComponent::SpawnPlate(int32 ChargeSign)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Determine class to spawn
	TSubclassOf<AEMFChannelingPlateActor> ClassToSpawn = PlateActorClass;
	if (!ClassToSpawn)
	{
		ClassToSpawn = AEMFChannelingPlateActor::StaticClass();
	}

	// Get initial position from camera
	FVector CameraLoc = FVector::ZeroVector;
	FRotator CameraRot = FRotator::ZeroRotator;
	GetCameraViewPoint(CameraLoc, CameraRot);

	FVector WorldOffset = CameraRot.RotateVector(PlateOffset);
	FVector SpawnLocation = CameraLoc + WorldOffset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UE_LOG(LogTemp, Warning, TEXT("SpawnPlate: CameraLoc=%s CameraRot=%s SpawnLoc=%s"),
		*CameraLoc.ToString(), *CameraRot.ToString(), *SpawnLocation.ToString());

	ChannelingPlateActor = World->SpawnActor<AEMFChannelingPlateActor>(
		ClassToSpawn, SpawnLocation, CameraRot, SpawnParams);

	if (ChannelingPlateActor)
	{
		// Configure plate
		float ChargeMagnitude = 1.0f;
		if (CachedEMFModifier)
		{
			ChargeMagnitude = FMath::Abs(CachedEMFModifier->GetCharge());
		}
		float Density = ChargeMagnitude * PlateChargeDensityMultiplier * static_cast<float>(ChargeSign);

		ChannelingPlateActor->SetPlateChargeDensity(Density);
		ChannelingPlateActor->SetPlateDimensions(PlateDimensions);
		ChannelingPlateActor->bDrawDebugPlate = bDrawDebugPlate;

		UE_LOG(LogTemp, Warning, TEXT("SpawnPlate: SUCCESS! ActorLoc=%s Density=%.2f"),
			*ChannelingPlateActor->GetActorLocation().ToString(), Density);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnPlate: SpawnActor FAILED!"));
	}
}

void UChargeAnimationComponent::DestroyPlate()
{
	if (ChannelingPlateActor)
	{
		ChannelingPlateActor->Destroy();
		ChannelingPlateActor = nullptr;
	}
}

void UChargeAnimationComponent::UpdatePlatePosition()
{
	if (!ChannelingPlateActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdatePlatePosition: ChannelingPlateActor is null!"));
		return;
	}

	FVector CameraLoc = FVector::ZeroVector;
	FRotator CameraRot = FRotator::ZeroRotator;

	if (!GetCameraViewPoint(CameraLoc, CameraRot))
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdatePlatePosition: GetCameraViewPoint FAILED!"));
		return; // No valid camera — skip this frame
	}

	UE_LOG(LogTemp, Log, TEXT("UpdatePlatePosition: CameraLoc=%s CameraRot=%s PlateOffset=%s"),
		*CameraLoc.ToString(), *CameraRot.ToString(), *PlateOffset.ToString());

	ChannelingPlateActor->UpdateTransformFromCamera(CameraLoc, CameraRot, PlateOffset);

	UE_LOG(LogTemp, Log, TEXT("UpdatePlatePosition: PlateActorLoc=%s"),
		*ChannelingPlateActor->GetActorLocation().ToString());
}

bool UChargeAnimationComponent::GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	// Try controller first (lazy resolve — don't rely on cached pointer from BeginPlay)
	if (OwnerCharacter)
	{
		if (APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController()))
		{
			PC->GetPlayerViewPoint(OutLocation, OutRotation);
			return true;
		}
	}

	// Fallback to camera component
	if (CameraComponent)
	{
		OutLocation = CameraComponent->GetComponentLocation();
		OutRotation = CameraComponent->GetComponentRotation();
		return true;
	}

	return false;
}

void UChargeAnimationComponent::PerformTapToggle()
{
	if (CachedEMFModifier)
	{
		CachedEMFModifier->ToggleChargeSign();
	}
}

void UChargeAnimationComponent::EnterFinishingAnimation()
{
	// Re-enable player's EMF field
	if (CachedFieldComponent && bFieldWasRegistered)
	{
		CachedFieldComponent->RegisterWithRegistry();
		bFieldWasRegistered = false;
	}

	// Resume montage playback
	if (MeleeMesh && CurrentMontage)
	{
		UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance();
		if (AnimInstance && AnimInstance->Montage_IsPlaying(CurrentMontage))
		{
			// Calculate remaining duration
			float CurrentPos = AnimInstance->Montage_GetPosition(CurrentMontage);
			float TotalLength = CurrentMontage->GetPlayLength();
			float RemainingFraction = FMath::Max(0.0f, 1.0f - (CurrentPos / FMath::Max(TotalLength, 0.001f)));

			// Resume at base play rate
			float PlayRate = AnimationData.BasePlayRate;
			if (TotalLength > 0.0f && AnimationDuration > 0.0f)
			{
				PlayRate = TotalLength / AnimationDuration * AnimationData.BasePlayRate;
			}
			AnimInstance->Montage_SetPlayRate(CurrentMontage, PlayRate);

			// Set timer for remaining duration
			float RemainingTime = (RemainingFraction * TotalLength) / FMath::Max(PlayRate, 0.01f);
			StateTimeRemaining = RemainingTime;
		}
		else
		{
			// Montage not playing — go straight to ShowingWeapon
			StateTimeRemaining = 0.01f;
		}
	}
	else
	{
		StateTimeRemaining = 0.01f;
	}

	SetState(EChargeAnimationState::FinishingAnimation);
	OnChannelingEnded.Broadcast();
}

void UChargeAnimationComponent::CleanupChanneling()
{
	// Destroy any lingering plate
	DestroyPlate();

	// Disable proxy mode
	if (CachedEMFModifier)
	{
		CachedEMFModifier->SetChannelingProxyMode(false);
	}

	// Re-register player field if it was unregistered
	if (CachedFieldComponent && bFieldWasRegistered)
	{
		CachedFieldComponent->RegisterWithRegistry();
		bFieldWasRegistered = false;
	}

	// Restore left hand IK
	if (ShooterCharacter)
	{
		ShooterCharacter->SetLeftHandIKAlpha(1.0f);
	}

	// Restore mesh visibility
	StopChargeAnimation();
	StopChargeVFX();

	if (MeleeMesh && MeleeMesh->IsVisible())
	{
		SwitchToFirstPersonMesh();
	}
}

// ==================== Mesh Transition ====================

void UChargeAnimationComponent::BeginHideWeapon()
{
	MeshTransitionProgress = 0.0f;
}

void UChargeAnimationComponent::UpdateMeshTransition(float DeltaTime)
{
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

	if (MeleeMesh)
	{
		MeleeMesh->SetVisibility(true);

		// Hide specified bones
		CurrentlyHiddenBones = AnimationData.HiddenBones;
		for (const FName& BoneName : CurrentlyHiddenBones)
		{
			MeleeMesh->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		}

		// Attach to camera
		if (CameraComponent)
		{
			MeleeMesh->AttachToComponent(
				CameraComponent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale
			);

			MeleeMesh->SetRelativeLocation(AnimationData.MeshLocationOffset);
			FRotator FinalRelativeRotation = MeleeMeshRotationOffset + AnimationData.MeshRotationOffset;
			MeleeMesh->SetRelativeRotation(FinalRelativeRotation);
		}
	}
}

void UChargeAnimationComponent::SwitchToFirstPersonMesh()
{
	if (MeleeMesh)
	{
		MeleeMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		MeleeMesh->SetVisibility(false);

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
}

void UChargeAnimationComponent::UpdateMeleeMeshRotation()
{
	if (CurrentState != EChargeAnimationState::Playing)
	{
		return;
	}

	if (!MeleeMesh || !OwnerController)
	{
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	OwnerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	FQuat CameraQuat = CameraRotation.Quaternion();
	FQuat GlobalOffsetQuat = MeleeMeshRotationOffset.Quaternion();
	FQuat AnimOffsetQuat = AnimationData.MeshRotationOffset.Quaternion();
	FQuat FinalQuat = CameraQuat * GlobalOffsetQuat * AnimOffsetQuat;

	FRotator FinalRotation = FinalQuat.Rotator();

	FVector LocalOffset = AnimationData.MeshLocationOffset;
	FVector WorldOffset = CameraRotation.RotateVector(LocalOffset);
	FVector FinalLocation = CameraLocation + WorldOffset;

	MeleeMesh->SetWorldLocationAndRotation(FinalLocation, FinalRotation);
}

// ==================== Animation ====================

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

	float MontageLength = AnimationData.ChargeMontage->GetPlayLength();
	float AdjustedPlayRate = AnimationData.BasePlayRate;

	if (MontageLength > 0.0f && AnimationDuration > 0.0f)
	{
		AdjustedPlayRate = MontageLength / AnimationDuration * AnimationData.BasePlayRate;
	}

	float Duration = AnimInstance->Montage_Play(AnimationData.ChargeMontage, AdjustedPlayRate);

	if (Duration > 0.0f)
	{
		CurrentMontage = AnimationData.ChargeMontage;
		MontageTotalDuration = Duration;
		MontageTimeElapsed = 0.0f;

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

	// Don't override play rate during channeling freeze
	if (CurrentState == EChargeAnimationState::Channeling ||
	    CurrentState == EChargeAnimationState::ChannelingRelease ||
	    CurrentState == EChargeAnimationState::ReverseChanneling)
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

	MontageTimeElapsed += DeltaTime;

	float NormalizedProgress = FMath::Clamp(MontageTimeElapsed / MontageTotalDuration, 0.0f, 1.0f);
	float CurveValue = AnimationData.PlayRateCurve->GetFloatValue(NormalizedProgress);
	float NewPlayRate = AnimationData.BasePlayRate * CurveValue;

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

	float CurrentCharge = FieldComp->GetSourceDescription().PointChargeParams.Charge;
	return -CurrentCharge;
}

// ==================== VFX ====================

void UChargeAnimationComponent::SpawnChargeVFX()
{
	if (!MeleeMesh)
	{
		return;
	}

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
		VFXToSpawn = ChargeVFX;
	}

	if (!VFXToSpawn)
	{
		return;
	}

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

// ==================== Audio ====================

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

// ==================== Callbacks ====================

void UChargeAnimationComponent::OnMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == CurrentMontage)
	{
		// If in FinishingAnimation state, the montage finishing triggers next transition
		if (CurrentState == EChargeAnimationState::FinishingAnimation)
		{
			StateTimeRemaining = 0.0f; // Force immediate transition
		}
		CurrentMontage = nullptr;
	}
}

// ==================== Mesh Detection ====================

void UChargeAnimationComponent::AutoDetectMeshReferences()
{
	if (!OwnerCharacter)
	{
		return;
	}

	if (!FirstPersonMesh)
	{
		if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
		{
			FirstPersonMesh = PolarityChar->GetFirstPersonMesh();
		}
	}

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
