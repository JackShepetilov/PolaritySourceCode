// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterDummyInterface.h"
#include "MovementSettings.h"
#include "CameraShakeComponent.h"
#include "WeaponRecoilComponent.h"
#include "HitMarkerComponent.h"
#include "MeleeAttackComponent.h"
#include "ChargeAnimationComponent.h"
#include "ApexMovementComponent.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Curves/CurveFloat.h"
#include "Polarity/Checkpoint/CheckpointData.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/CapsuleComponent.h"

AShooterCharacter::AShooterCharacter()
{
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// create the recoil component
	RecoilComponent = CreateDefaultSubobject<UWeaponRecoilComponent>(TEXT("Recoil Component"));

	// create the hit marker component
	HitMarkerComponent = CreateDefaultSubobject<UHitMarkerComponent>(TEXT("Hit Marker Component"));

	// create the melee attack component
	MeleeAttackComponent = CreateDefaultSubobject<UMeleeAttackComponent>(TEXT("Melee Attack Component"));

	// create the charge animation component
	ChargeAnimationComponent = CreateDefaultSubobject<UChargeAnimationComponent>(TEXT("Charge Animation Component"));

	// ==================== UE4 Mesh System ====================

	// Create UE4 First Person Mesh (visible, copies pose from FirstPersonMesh)
	UE4_FPMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UE4_FPMesh"));
	UE4_FPMesh->SetupAttachment(GetMesh());
	UE4_FPMesh->SetOnlyOwnerSee(true);
	UE4_FPMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	UE4_FPMesh->SetCollisionProfileName(FName("NoCollision"));
	UE4_FPMesh->SetVisibility(false); // Hidden by default, enabled in BeginPlay if bUseUE4Meshes

	// Create UE4 Melee Mesh (visible, copies pose from MeleeMesh)
	UE4_MeleeMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UE4_MeleeMesh"));
	UE4_MeleeMesh->SetupAttachment(GetMesh());
	UE4_MeleeMesh->SetOnlyOwnerSee(true);
	UE4_MeleeMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	UE4_MeleeMesh->SetCollisionProfileName(FName("NoCollision"));
	UE4_MeleeMesh->SetVisibility(false); // Hidden by default, controlled by MeleeAttackComponent

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
}

USkeletalMeshComponent* AShooterCharacter::GetMeleeMesh() const
{
	if (MeleeAttackComponent)
	{
		return MeleeAttackComponent->MeleeMesh;
	}
	return nullptr;
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// reset HP to max
	CurrentHP = MaxHP;

	// Store base FOV and location values for ADS interpolation
	if (UCameraComponent* Camera = GetFirstPersonCameraComponent())
	{
		BaseCameraFOV = Camera->FieldOfView;
		BaseFirstPersonFOV = Camera->FirstPersonFieldOfView;
		BaseCameraLocation = Camera->GetRelativeLocation();

		UE_LOG(LogTemp, Warning, TEXT("ShooterCharacter: BaseCameraLocation=%s, BaseFOV=%.1f"),
			*BaseCameraLocation.ToString(), BaseCameraFOV);
	}

	// Initialize recoil component
	if (RecoilComponent)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		RecoilComponent->Initialize(PC, GetCharacterMovement(), GetApexMovement());
	}

	// Bind melee hit event to forward to hit marker system
	if (MeleeAttackComponent)
	{
		MeleeAttackComponent->OnMeleeHit.AddDynamic(this, &AShooterCharacter::OnMeleeHit);
	}

	// ==================== Setup UE4 Mesh System ====================
	if (bUseUE4Meshes)
	{
		// Hide original meshes (they become leader meshes)
		if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
		{
			FPMesh->SetVisibility(false);
		}
		if (USkeletalMeshComponent* MeleeMesh = GetMeleeMesh())
		{
			MeleeMesh->SetVisibility(false);
		}

		// Show and configure UE4_FPMesh (follower)
		if (UE4_FPMesh)
		{
			UE4_FPMesh->SetVisibility(true);
			// Animation Blueprint will be set in editor with Copy Pose from Mesh node
			// that references GetFirstPersonMesh() as source and uses FPMeshRetargeter
		}

		// UE4_MeleeMesh visibility is controlled by MeleeAttackComponent
		// We need to update MeleeAttackComponent to use UE4_MeleeMesh instead of MeleeMesh
		if (UE4_MeleeMesh && MeleeAttackComponent)
		{
			// Override MeleeAttackComponent's mesh reference to use UE4_MeleeMesh
			MeleeAttackComponent->MeleeMesh = UE4_MeleeMesh;
		}
	}

	// Configure EMF components if they exist (created in Blueprint)
	if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
	{
		EMFMod->SetOwnerType(EEMSourceOwnerType::Player);
		// Player doesn't react to NPC EM forces
		EMFMod->NPCForceMultiplier = 0.0f;
	}
	if (UEMF_FieldComponent* FieldComp = FindComponentByClass<UEMF_FieldComponent>())
	{
		FieldComp->SetOwnerType(EEMSourceOwnerType::Player);
	}

	// Bind movement SFX delegates
	BindMovementSFXDelegates();

	// update the HUD
	OnDamaged.Broadcast(1.0f);
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	// Unbind movement SFX delegates
	UnbindMovementSFXDelegates();

	// Stop any looping sounds
	StopSlideLoopSound();
	StopWallRunLoopSound();

	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Switch weapon
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoSwitchWeapon);

		// ADS (hold to aim)
		if (ADSAction)
		{
			EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartADS);
			EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopADS);
		}

		// Melee attack
		if (MeleeAction)
		{
			EnhancedInputComponent->BindAction(MeleeAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoMeleeAttack);
		}
	}
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// Call parent implementation
	Super::DoAim(Yaw, Pitch);

	// Track mouse delta for recoil sway
	LastMouseDelta = FVector2D(Yaw, Pitch);

	// Feed mouse input to recoil component for sway
	if (RecoilComponent)
	{
		RecoilComponent->AddMouseInput(Yaw, Pitch);
	}
}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Reset regeneration delay timer
	TimeSinceLastDamage = 0.0f;

	// Calculate damage direction angle relative to player forward
	// Only show damage direction for actual damage (positive value), not healing
	if (DamageCauser && Damage > 0.0f)
	{
		// Get direction from damage source to player
		FVector DamageDirection = (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal();

		// Get player's forward vector (ignore pitch)
		FVector PlayerForward = GetActorForwardVector();
		PlayerForward.Z = 0.0f;
		PlayerForward.Normalize();

		DamageDirection.Z = 0.0f;
		DamageDirection.Normalize();

		// Calculate angle using atan2 for proper signed angle
		// Positive = right side, Negative = left side
		float DotProduct = FVector::DotProduct(PlayerForward, DamageDirection);
		float CrossProduct = FVector::CrossProduct(PlayerForward, DamageDirection).Z;
		float AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(CrossProduct, DotProduct));

		// Broadcast damage direction
		OnDamageDirection.Broadcast(AngleDegrees, Damage);
	}

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	// update the HUD
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	return Damage;
}

void AShooterCharacter::DoStartFiring()
{
	// Don't fire if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't fire if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	// fire the current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->StartFiring();
	}
}

void AShooterCharacter::DoStopFiring()
{
	// stop firing the current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	// Notify recoil component that firing ended
	if (RecoilComponent)
	{
		RecoilComponent->OnFiringEnded();
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	// Don't switch if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't switch if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1)
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon();

		// Play weapon switch sound
		PlayWeaponSwitchSound();
	}
}

void AShooterCharacter::DoMeleeAttack()
{
	// Don't melee if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	if (MeleeAttackComponent)
	{
		// Stop firing if we're shooting
		if (CurrentWeapon)
		{
			CurrentWeapon->StopFiring();
		}

		MeleeAttackComponent->StartAttack();
	}
}

void AShooterCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateADS(DeltaTime);
	UpdateRegeneration(DeltaTime);
	UpdateLeftHandIK(DeltaTime);
	UpdateLowHealthWarning(DeltaTime);

	// Update recoil component state
	if (RecoilComponent)
	{
		RecoilComponent->SetAiming(bWantsToAim);

		// Check if crouching via ApexMovement or CharacterMovement
		bool bIsCrouching = false;
		if (UApexMovementComponent* Apex = GetApexMovement())
		{
			bIsCrouching = Apex->IsCrouching() || Apex->IsSliding();
		}
		else
		{
			bIsCrouching = GetCharacterMovement()->IsCrouching();
		}
		RecoilComponent->SetCrouching(bIsCrouching);
	}

	// ==================== UI Updates ====================

	// Update Heat UI from current weapon
	if (CurrentWeapon && CurrentWeapon->IsHeatSystemEnabled())
	{
		float HeatPercent = CurrentWeapon->GetCurrentHeat();
		float DamageMult = CurrentWeapon->GetHeatDamageMultiplier();
		OnHeatUpdated.Broadcast(HeatPercent, DamageMult);
	}
	else
	{
		// No heat system - broadcast 0 heat
		OnHeatUpdated.Broadcast(0.0f, 1.0f);
	}

	// Update Speed UI
	float CurrentSpeed = GetVelocity().Size();
	float SpeedPercent = FMath::Clamp(CurrentSpeed / MaxSpeedForUI, 0.0f, 1.0f);
	OnSpeedUpdated.Broadcast(SpeedPercent, CurrentSpeed, MaxSpeedForUI);

	// Update Charge/Polarity UI - get charge from EMFVelocityModifier (not PolarityCharacter::CurrentCharge!)
	float ChargeValue = 0.0f;
	float StableCharge = 0.0f;
	float UnstableCharge = 0.0f;
	float MaxStableCharge = 0.0f;
	float MaxUnstableCharge = 0.0f;

	if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
	{
		ChargeValue = EMFMod->GetCharge();
		StableCharge = EMFMod->GetBaseCharge();
		UnstableCharge = EMFMod->GetBonusCharge();
		MaxStableCharge = EMFMod->MaxBaseCharge;
		MaxUnstableCharge = EMFMod->MaxBonusCharge;
	}

	// Determine current polarity (0=Neutral, 1=Positive, 2=Negative)
	uint8 CurrentPolarity = 0; // Neutral
	if (ChargeValue > KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 1; // Positive
	}
	else if (ChargeValue < -KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 2; // Negative
	}

	// Broadcast charge update every tick
	OnChargeUpdated.Broadcast(ChargeValue, CurrentPolarity);

	// Broadcast extended charge info with stable/unstable breakdown
	float TotalCharge = StableCharge + UnstableCharge;
	OnChargeExtended.Broadcast(TotalCharge, StableCharge, UnstableCharge, MaxStableCharge, MaxUnstableCharge, CurrentPolarity);

	// Check if polarity changed
	if (CurrentPolarity != PreviousPolarity)
	{
		OnPolarityChanged.Broadcast(CurrentPolarity, ChargeValue);
		UpdateChargeOverlay(CurrentPolarity);
		PreviousPolarity = CurrentPolarity;
	}

	/*bool bIsWallRunning = false;
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		bIsWallRunning = Apex->IsWallRunning();
	}


	if (bIsWallRunning)
	{
		SetLeftHandIKAlpha(0.0f);  // ���� ��������
	}
	else
	{
		SetLeftHandIKAlpha(1.0f);  // ���� �� ������

	}*/
	
}

void AShooterCharacter::DoStartADS()
{
	// Don't ADS if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't ADS if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	if (MovementSettings && MovementSettings->bEnableADS)
	{
		bWantsToAim = true;

		// Play ADS in sound on current weapon
		if (CurrentWeapon)
		{
			CurrentWeapon->PlayADSInSound();

			// Attach weapon to camera for proper pitch following
			if (UCameraComponent* Camera = GetFirstPersonCameraComponent())
			{
				CurrentWeapon->AttachToCamera(Camera);
			}

			// Blend to weapon's ADS camera
			if (UCameraComponent* ADSCamera = CurrentWeapon->GetADSCamera())
			{
				if (APlayerController* PC = Cast<APlayerController>(GetController()))
				{
					FViewTargetTransitionParams TransitionParams;
					TransitionParams.BlendTime = CurrentWeapon->GetADSBlendInTime();
					TransitionParams.BlendFunction = EViewTargetBlendFunction::VTBlend_EaseInOut;
					TransitionParams.BlendExp = 2.0f;
					PC->SetViewTarget(CurrentWeapon, TransitionParams);
				}
			}
		}
	}
}

void AShooterCharacter::DoStopADS()
{
	// Only play sound if we were actually aiming
	if (bWantsToAim && CurrentWeapon)
	{
		CurrentWeapon->PlayADSOutSound();

		// Detach weapon from camera, reattach to hands
		if (USkeletalMeshComponent* HandsMesh = GetFirstPersonMesh())
		{
			CurrentWeapon->DetachFromCamera(HandsMesh, FirstPersonWeaponSocket);
		}

		// Blend back to character's camera
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			FViewTargetTransitionParams TransitionParams;
			TransitionParams.BlendTime = CurrentWeapon->GetADSBlendOutTime();
			TransitionParams.BlendFunction = EViewTargetBlendFunction::VTBlend_EaseInOut;
			TransitionParams.BlendExp = 2.0f;
			PC->SetViewTarget(this, TransitionParams);
		}
	}

	bWantsToAim = false;
}

void AShooterCharacter::UpdateADS(float DeltaTime)
{
	if (!MovementSettings || !MovementSettings->bEnableADS)
	{
		return;
	}

	// Determine target alpha
	float TargetAlpha = bWantsToAim ? 1.0f : 0.0f;

	// Interpolate alpha (still used by other systems like recoil)
	CurrentADSAlpha = FMath::FInterpTo(
		CurrentADSAlpha,
		TargetAlpha,
		DeltaTime,
		MovementSettings->ADSInterpSpeed
	);

	// Update weapon position during ADS transition
	if (CurrentWeapon)
	{
		CurrentWeapon->UpdateADSTransition(CurrentADSAlpha, DeltaTime);
	}

	// Note: FOV and camera position are now handled by SetViewTargetWithBlend
	// when switching to weapon's ADS camera

	// Only apply shake offset to main camera when not aiming
	// (ADS camera follows weapon which has its own behavior)
	UCameraComponent* Camera = GetFirstPersonCameraComponent();
	if (!Camera)
	{
		return;
	}

	// Get shake offset from CameraShakeComponent
	FVector ShakeOffset = FVector::ZeroVector;
	if (UCameraShakeComponent* ShakeComp = GetCameraShake())
	{
		ShakeOffset = ShakeComp->GetCameraOffset();
	}

	// Apply only shake offset to main camera (no ADS offset - handled by view target blend)
	Camera->SetRelativeLocation(BaseCameraLocation + ShakeOffset);
}

void AShooterCharacter::UpdateRegeneration(float DeltaTime)
{
	// Check if regeneration is enabled
	if (!bEnableRegeneration)
	{
		return;
	}

	// Don't regenerate if dead
	if (CurrentHP <= 0.0f)
	{
		return;
	}

	// Don't regenerate if already at max HP
	if (CurrentHP >= MaxHP)
	{
		return;
	}

	// Update damage delay timer
	TimeSinceLastDamage += DeltaTime;

	// Check if we're still in the post-damage delay period
	if (TimeSinceLastDamage < RegenDelayAfterDamage)
	{
		return;
	}

	// Calculate current speed ratio (0-1)
	const float CurrentSpeed = GetVelocity().Size();
	const float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeedForRegen, 0.0f, 1.0f);

	// Calculate regen multiplier from speed
	float RegenMultiplier;
	if (SpeedToRegenCurve)
	{
		// Use curve for custom falloff
		RegenMultiplier = FMath::Clamp(SpeedToRegenCurve->GetFloatValue(SpeedRatio), 0.0f, 1.0f);
	}
	else
	{
		// Linear interpolation
		RegenMultiplier = SpeedRatio;
	}

	// Calculate final regen rate
	const float CurrentRegenRate = FMath::Lerp(BaseRegenRate, MaxRegenRate, RegenMultiplier);

	// Apply regeneration
	const float OldHP = CurrentHP;
	CurrentHP = FMath::Min(CurrentHP + CurrentRegenRate * DeltaTime, MaxHP);

	// Update HUD if HP changed
	if (CurrentHP != OldHP)
	{
		OnDamaged.Broadcast(CurrentHP / MaxHP);
	}
}

void AShooterCharacter::UpdateChargeOverlay(uint8 NewPolarity)
{
	// Don't update if feature is disabled
	if (!bUseChargeOverlay)
	{
		return;
	}

	// Select appropriate material based on polarity
	UMaterialInterface* TargetMaterial = nullptr;

	switch (NewPolarity)
	{
	case 0: // Neutral
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	case 1: // Positive
		TargetMaterial = PositiveChargeOverlayMaterial;
		break;
	case 2: // Negative
		TargetMaterial = NegativeChargeOverlayMaterial;
		break;
	default:
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	}

	// Apply overlay material to third person mesh
	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		TPMesh->SetOverlayMaterial(TargetMaterial);
	}

	// Apply overlay material to first person mesh
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetOverlayMaterial(TargetMaterial);
	}

	// Apply overlay material to UE4 meshes if using them
	if (bUseUE4Meshes)
	{
		if (UE4_FPMesh)
		{
			UE4_FPMesh->SetOverlayMaterial(TargetMaterial);
		}
		if (UE4_MeleeMesh)
		{
			UE4_MeleeMesh->SetOverlayMaterial(TargetMaterial);
		}
	}
}

void AShooterCharacter::UpdateFirstPersonView(float DeltaTime)
{
	// Call parent implementation first
	Super::UpdateFirstPersonView(DeltaTime);

	// Apply recoil visual offsets to first person mesh
	if (RecoilComponent && GetFirstPersonMesh())
	{
		FVector RecoilOffset = RecoilComponent->GetWeaponOffset();
		FRotator RecoilRotation = RecoilComponent->GetWeaponRotationOffset();

		// Get current relative transform
		FVector CurrentLocation = GetFirstPersonMesh()->GetRelativeLocation();
		FRotator CurrentRotation = GetFirstPersonMesh()->GetRelativeRotation();

		// Add recoil offset (in local space)
		FVector FinalLocation = CurrentLocation + RecoilOffset;
		FRotator FinalRotation = CurrentRotation + RecoilRotation;

		GetFirstPersonMesh()->SetRelativeLocation(FinalLocation);
		GetFirstPersonMesh()->SetRelativeRotation(FinalRotation);
	}
}

void AShooterCharacter::OnMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	UE_LOG(LogTemp, Warning, TEXT("[MeleeHit] %s hit %s - Damage=%.1f, Headshot=%d"),
		*GetName(),
		HitActor ? *HitActor->GetName() : TEXT("NULL"),
		Damage, bHeadshot);

	bool bKilled = false;
	bool bIsDummyTarget = HitActor && HitActor->Implements<UShooterDummyTarget>();

	// Forward melee hits to the hit marker system
	if (HitMarkerComponent)
	{
		// Try to get remaining health from hit actor
		APawn* HitPawn = Cast<APawn>(HitActor);
		if (HitPawn)
		{
			// For ShooterCharacter targets, check their HP
			AShooterCharacter* HitCharacter = Cast<AShooterCharacter>(HitPawn);
			if (HitCharacter && HitCharacter->CurrentHP <= 0.0f)
			{
				bKilled = true;
			}
		}

		// Check for dummy death via interface
		if (bIsDummyTarget)
		{
			bKilled = IShooterDummyTarget::Execute_IsDummyDead(HitActor);
		}

		// Calculate hit direction
		FVector HitDirection = (HitLocation - GetActorLocation()).GetSafeNormal();

		// Register hit with hit marker component using actual damage dealt
		HitMarkerComponent->RegisterHit(
			HitLocation,
			HitDirection,
			Damage,
			bHeadshot,
			bKilled
		);
	}

	// Handle charge based on target type
	if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
	{
		// Check if target implements IShooterDummyTarget for stable charge
		if (bIsDummyTarget)
		{
			bool bGrantsStable = IShooterDummyTarget::Execute_GrantsStableCharge(HitActor);

			if (bGrantsStable)
			{
				float StableAmount = IShooterDummyTarget::Execute_GetStableChargeAmount(HitActor);
				if (StableAmount > 0.0f)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MeleeCharge] Dummy stable charge: +%.2f to %s"),
						StableAmount, *GetName());
					EMFMod->AddPermanentCharge(StableAmount);
				}

				// Add kill bonus if we killed the dummy
				if (bKilled)
				{
					float KillBonus = IShooterDummyTarget::Execute_GetKillChargeBonus(HitActor);
					if (KillBonus > 0.0f)
					{
						UE_LOG(LogTemp, Warning, TEXT("[MeleeCharge] Dummy kill bonus: +%.2f to %s"),
							KillBonus, *GetName());
						EMFMod->AddPermanentCharge(KillBonus);
					}
				}
				return; // Don't add bonus charge for dummy targets
			}
		}

		// Default: add decaying bonus charge for regular enemies
		float OldCharge = EMFMod->GetCharge();
		EMFMod->AddBonusCharge(EMFMod->ChargePerMeleeHit);
		float NewCharge = EMFMod->GetCharge();

		UE_LOG(LogTemp, Warning, TEXT("[MeleeCharge] Hit %s - Charge: %.2f -> %.2f (added %.2f bonus)"),
			HitActor ? *HitActor->GetName() : TEXT("NULL"),
			OldCharge, NewCharge, EMFMod->ChargePerMeleeHit);
	}
}

// ==================== SFX Functions ====================

void AShooterCharacter::PlayFootstepSound()
{
	if (!FootstepSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(FootstepPitchMin, FootstepPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		FootstepSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		FootstepVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlayCrouchFootstepSound()
{
	if (!CrouchFootstepSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(CrouchFootstepPitchMin, CrouchFootstepPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		CrouchFootstepSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		CrouchFootstepVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlaySlideStartSound()
{
	if (!SlideStartSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(SlideSoundPitchMin, SlideSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		SlideStartSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		SlideSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlaySlideEndSound()
{
	if (!SlideEndSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(SlideSoundPitchMin, SlideSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		SlideEndSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		SlideSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::StartSlideLoopSound()
{
	if (!SlideLoopSound)
	{
		return;
	}

	// Stop existing loop if any
	StopSlideLoopSound();

	// Create and play looping sound attached to character
	SlideLoopAudioComponent = UGameplayStatics::SpawnSoundAttached(
		SlideLoopSound,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		false,
		SlideSoundVolume,
		FMath::RandRange(SlideSoundPitchMin, SlideSoundPitchMax),
		0.0f,
		nullptr,
		nullptr,
		true
	);
}

void AShooterCharacter::StopSlideLoopSound()
{
	if (SlideLoopAudioComponent && SlideLoopAudioComponent->IsPlaying())
	{
		SlideLoopAudioComponent->Stop();
		SlideLoopAudioComponent = nullptr;
	}
}

void AShooterCharacter::PlayWallRunStartSound()
{
	if (!WallRunStartSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(WallRunSoundPitchMin, WallRunSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		WallRunStartSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		WallRunSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlayWallRunEndSound()
{
	if (!WallRunEndSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(WallRunSoundPitchMin, WallRunSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		WallRunEndSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		WallRunSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::StartWallRunLoopSound()
{
	if (!WallRunLoopSound)
	{
		return;
	}

	// Stop existing loop if any
	StopWallRunLoopSound();

	// Create and play looping sound attached to character
	WallRunLoopAudioComponent = UGameplayStatics::SpawnSoundAttached(
		WallRunLoopSound,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		false,
		WallRunSoundVolume,
		FMath::RandRange(WallRunSoundPitchMin, WallRunSoundPitchMax),
		0.0f,
		nullptr,
		nullptr,
		true
	);
}

void AShooterCharacter::StopWallRunLoopSound()
{
	if (WallRunLoopAudioComponent && WallRunLoopAudioComponent->IsPlaying())
	{
		WallRunLoopAudioComponent->Stop();
		WallRunLoopAudioComponent = nullptr;
	}
}

void AShooterCharacter::PlayJumpSound(bool bIsDoubleJump)
{
	USoundBase* SoundToPlay = bIsDoubleJump ? DoubleJumpSound : JumpSound;

	if (!SoundToPlay)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(JumpSoundPitchMin, JumpSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		SoundToPlay,
		GetActorLocation(),
		FRotator::ZeroRotator,
		JumpSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlayLandSound(float FallSpeed)
{
	if (!LandSound)
	{
		return;
	}

	// Only play if fall speed exceeds minimum threshold
	if (FallSpeed < LandSoundMinFallSpeed)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(LandSoundPitchMin, LandSoundPitchMax);

	// Scale volume based on fall speed (louder for harder landings)
	const float SpeedRatio = FMath::Clamp(FallSpeed / 1000.0f, 0.5f, 1.5f);
	const float AdjustedVolume = LandSoundVolume * SpeedRatio;

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		LandSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		AdjustedVolume,
		RandomPitch
	);
}

// ==================== SFX Delegate Handlers ====================

void AShooterCharacter::OnSlideStarted_SFX()
{
	PlaySlideStartSound();
	StartSlideLoopSound();
}

void AShooterCharacter::OnSlideEnded_SFX()
{
	StopSlideLoopSound();
	PlaySlideEndSound();
}

void AShooterCharacter::OnWallRunStarted_SFX(EWallSide Side)
{
	PlayWallRunStartSound();
	StartWallRunLoopSound();
}

void AShooterCharacter::OnWallRunEnded_SFX()
{
	StopWallRunLoopSound();
	PlayWallRunEndSound();
}

void AShooterCharacter::OnLanded_SFX(const FHitResult& Hit)
{
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		PlayLandSound(Apex->LastFallVelocity);
	}
}

void AShooterCharacter::BindMovementSFXDelegates()
{
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->OnSlideStarted.AddDynamic(this, &AShooterCharacter::OnSlideStarted_SFX);
		Apex->OnSlideEnded.AddDynamic(this, &AShooterCharacter::OnSlideEnded_SFX);
		Apex->OnWallrunStarted.AddDynamic(this, &AShooterCharacter::OnWallRunStarted_SFX);
		Apex->OnWallrunEnded.AddDynamic(this, &AShooterCharacter::OnWallRunEnded_SFX);
		Apex->OnLanded_Movement.AddDynamic(this, &AShooterCharacter::OnLanded_SFX);

		// New movement event delegates
		Apex->OnJumpPerformed.AddDynamic(this, &AShooterCharacter::OnJumpPerformed_Handler);
		Apex->OnMantleStarted.AddDynamic(this, &AShooterCharacter::OnMantleStarted_Handler);
		Apex->OnAirDashStarted.AddDynamic(this, &AShooterCharacter::OnAirDashStarted_Handler);
		Apex->OnAirDashEnded.AddDynamic(this, &AShooterCharacter::OnAirDashEnded_Handler);
	}
}

void AShooterCharacter::UnbindMovementSFXDelegates()
{
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->OnSlideStarted.RemoveDynamic(this, &AShooterCharacter::OnSlideStarted_SFX);
		Apex->OnSlideEnded.RemoveDynamic(this, &AShooterCharacter::OnSlideEnded_SFX);
		Apex->OnWallrunStarted.RemoveDynamic(this, &AShooterCharacter::OnWallRunStarted_SFX);
		Apex->OnWallrunEnded.RemoveDynamic(this, &AShooterCharacter::OnWallRunEnded_SFX);
		Apex->OnLanded_Movement.RemoveDynamic(this, &AShooterCharacter::OnLanded_SFX);

		// New movement event delegates
		Apex->OnJumpPerformed.RemoveDynamic(this, &AShooterCharacter::OnJumpPerformed_Handler);
		Apex->OnMantleStarted.RemoveDynamic(this, &AShooterCharacter::OnMantleStarted_Handler);
		Apex->OnAirDashStarted.RemoveDynamic(this, &AShooterCharacter::OnAirDashStarted_Handler);
		Apex->OnAirDashEnded.RemoveDynamic(this, &AShooterCharacter::OnAirDashEnded_Handler);
	}
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, FirstPersonWeaponSocket);
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
		{
			if (!AnimInstance->Montage_IsPlaying(Montage))
			{
				AnimInstance->Montage_Play(Montage);
			}
		}
	}
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	if (CurrentWeapon && CurrentWeapon->UsesAdvancedRecoil() && RecoilComponent)
	{
		RecoilComponent->OnWeaponFired();
	}
	else
	{
		AddControllerPitchInput(Recoil);
	}
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	FHitResult OutHit;

	const FVector Start = GetFirstPersonCameraComponent()->GetComponentLocation();
	const FVector End = Start + (GetFirstPersonCameraComponent()->GetForwardVector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			OwnedWeapons.Add(AddedWeapon);

			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon();
		}
	}
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	TSubclassOf<UAnimInstance> FPAnimClass = Weapon->GetFirstPersonAnimInstanceClass();
	TSubclassOf<UAnimInstance> TPAnimClass = Weapon->GetThirdPersonAnimInstanceClass();

	if (FPAnimClass)
	{
		GetFirstPersonMesh()->SetAnimInstanceClass(FPAnimClass);
	}

	if (TPAnimClass)
	{
		GetMesh()->SetAnimInstanceClass(TPAnimClass);
	}

	if (RecoilComponent && Weapon->UsesAdvancedRecoil())
	{
		RecoilComponent->SetRecoilSettings(Weapon->GetRecoilSettings());
		RecoilComponent->ResetRecoil();
	}
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	if (RecoilComponent)
	{
		RecoilComponent->ResetRecoil();
	}
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

void AShooterCharacter::OnWeaponHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled)
{
	if (HitMarkerComponent)
	{
		HitMarkerComponent->RegisterHit(HitLocation, HitDirection, Damage, bHeadshot, bKilled);
	}
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	return nullptr;
}

void AShooterCharacter::Die()
{
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	GetCharacterMovement()->StopMovementImmediately();
	DisableInput(nullptr);
	OnBulletCountUpdated.Broadcast(0, 0);

	// Stop any looping sounds
	StopSlideLoopSound();
	StopWallRunLoopSound();

	BP_OnDeath();

	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, RespawnTime, false);
}

void AShooterCharacter::OnRespawn()
{
	// Try to respawn at checkpoint first
	if (UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		if (CheckpointSubsystem->HasActiveCheckpoint())
		{
			if (CheckpointSubsystem->RespawnAtCheckpoint(this))
			{
				return; // Successfully respawned at checkpoint
			}
		}
	}

	// No checkpoint or respawn failed - destroy and let GameMode handle it
	Destroy();
}

bool AShooterCharacter::SaveToCheckpoint(FCheckpointData& OutData)
{
	// Health
	OutData.Health = CurrentHP;

	// EMF - save base charge (0 for neutral, not bonus charge)
	// Per requirements: reset bonus charge, keep base
	OutData.BaseEMFCharge = 0.0f; // Player spawns neutral

	// Weapon state
	int32 CurrentWeaponIdx = OwnedWeapons.IndexOfByKey(CurrentWeapon);
	OutData.CurrentWeaponIndex = (CurrentWeaponIdx != INDEX_NONE) ? CurrentWeaponIdx : 0;

	// Save ammo for all weapons
	OutData.WeaponAmmo.Empty();
	for (int32 i = 0; i < OwnedWeapons.Num(); ++i)
	{
		if (AShooterWeapon* Weapon = OwnedWeapons[i])
		{
			OutData.WeaponAmmo.Add(i, Weapon->GetBulletCount());
		}
	}

	return true;
}

bool AShooterCharacter::RestoreFromCheckpoint(const FCheckpointData& Data)
{
	if (!Data.bIsValid)
	{
		return false;
	}

	// Reset character state first
	ResetCharacterState();

	// Teleport to spawn point and set view rotation
	SetActorTransform(Data.SpawnTransform);

	// Set controller rotation to match checkpoint direction (add 180 to face forward from checkpoint)
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FRotator SpawnRotation = Data.SpawnTransform.GetRotation().Rotator();
		SpawnRotation.Yaw += 180.0f;
		PC->SetControlRotation(SpawnRotation);
	}

	// Restore health (per requirements: restore HP on respawn)
	CurrentHP = Data.Health;
	OnDamaged.Broadcast(CurrentHP / MaxHP);

	// Restore EMF charge (reset to base/neutral)
	CurrentCharge = Data.BaseEMFCharge;
	// Calculate polarity byte: 0=neutral, 1=positive, 2=negative
	uint8 RestoredPolarity = 0;
	if (CurrentCharge > 0.01f)
	{
		RestoredPolarity = 1; // Positive
	}
	else if (CurrentCharge < -0.01f)
	{
		RestoredPolarity = 2; // Negative
	}
	OnChargeUpdated.Broadcast(CurrentCharge, RestoredPolarity);

	// Restore weapon
	if (OwnedWeapons.IsValidIndex(Data.CurrentWeaponIndex))
	{
		// Deactivate current weapon if different
		if (CurrentWeapon && CurrentWeapon != OwnedWeapons[Data.CurrentWeaponIndex])
		{
			CurrentWeapon->DeactivateWeapon();
		}

		CurrentWeapon = OwnedWeapons[Data.CurrentWeaponIndex];
		if (CurrentWeapon)
		{
			CurrentWeapon->ActivateWeapon();
		}
	}

	// Restore ammo
	for (const auto& AmmoPair : Data.WeaponAmmo)
	{
		if (OwnedWeapons.IsValidIndex(AmmoPair.Key))
		{
			if (AShooterWeapon* Weapon = OwnedWeapons[AmmoPair.Key])
			{
				Weapon->SetBulletCount(AmmoPair.Value);
			}
		}
	}

	// Re-enable input and reset camera
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		EnableInput(PC);

		// Reset view target back to this character (in case death camera was active)
		PC->SetViewTarget(this);
	}

	// Update UI
	if (CurrentWeapon)
	{
		OnBulletCountUpdated.Broadcast(CurrentWeapon->GetMagazineSize(), CurrentWeapon->GetBulletCount());
	}

	// Blueprint event (use this to reset any death-related visual effects)
	BP_OnRespawnAtCheckpoint();

	return true;
}

void AShooterCharacter::ResetCharacterState()
{
	// Stop all movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->StopMovementImmediately();
		MovementComp->Velocity = FVector::ZeroVector;

		// Reset movement mode to walking (in case we died mid-air or in weird state)
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	// Reset apex movement state
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->ResetMovementState();
	}

	// Clear respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);

	// Reset regen delay (allow immediate regeneration)
	TimeSinceLastDamage = RegenDelayAfterDamage;

	// Stop looping sounds
	StopSlideLoopSound();
	StopWallRunLoopSound();

	// Reset mesh visibility and transforms (in case death animation modified them)
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(true);
		FPMesh->SetRelativeLocation(FirstPersonMeshBaseLocation);
		FPMesh->SetRelativeRotation(FirstPersonMeshBaseRotation);
	}

	// Reset third person mesh if visible
	if (GetMesh())
	{
		GetMesh()->SetVisibility(true);
		GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Reactivate weapon if needed
	if (CurrentWeapon)
	{
		CurrentWeapon->ActivateWeapon();
	}
}

void AShooterCharacter::UpdateLeftHandIK(float DeltaTime)
{
	// Determine target alpha based on state
	bool bIsWallRunning = false;
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		bIsWallRunning = Apex->IsWallRunning();
	}

	TargetLeftHandIKAlpha = bIsWallRunning ? 0.0f : 1.0f;

	// Interpolate alpha
	CurrentLeftHandIKAlpha = FMath::FInterpTo(
		CurrentLeftHandIKAlpha,
		TargetLeftHandIKAlpha,
		DeltaTime,
		LeftHandIKAlphaInterpSpeed
	);

	// Get socket transform from weapon mesh (if available)
	FTransform FinalTransform = FTransform::Identity;

	if (CurrentWeapon)
	{
		if (USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetFirstPersonMesh())
		{
			if (WeaponMesh->DoesSocketExist(LeftHandGripSocket))
			{
				FTransform SocketTransform = WeaponMesh->GetSocketTransform(LeftHandGripSocket, ERelativeTransformSpace::RTS_World);
				FinalTransform = LeftHandIKOffset * SocketTransform;
			}
		}
	}

	// Always pass the interpolated alpha value
	SetAnimInstanceLeftHandIK(FinalTransform, CurrentLeftHandIKAlpha);
}

void AShooterCharacter::SetAnimInstanceLeftHandIK(const FTransform& Transform, float Alpha)
{
	USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	if (!FPMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: No FPMesh!"));
		return;
	}

	UAnimInstance* AnimInstance = FPMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: No AnimInstance!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("LeftHandIK: AnimInstance=%s, Alpha=%.2f"), *AnimInstance->GetClass()->GetName(), Alpha);

	// Set LeftHandIKTransform property via reflection
	static FName LeftHandIKTransformName(TEXT("LeftHandIKTransform"));
	FProperty* TransformProperty = AnimInstance->GetClass()->FindPropertyByName(LeftHandIKTransformName);

	if (TransformProperty)
	{
		FStructProperty* StructProp = CastField<FStructProperty>(TransformProperty);
		if (StructProp && StructProp->Struct == TBaseStructure<FTransform>::Get())
		{
			void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(AnimInstance);
			if (ValuePtr)
			{
				*static_cast<FTransform*>(ValuePtr) = Transform;
			}
		}
	}

	// Set LeftHandIKAlpha property via reflection
	static FName LeftHandIKAlphaName(TEXT("LeftHandIKAlpha"));
	FProperty* AlphaProperty = AnimInstance->GetClass()->FindPropertyByName(LeftHandIKAlphaName);

	if (!AlphaProperty)
	{
		UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: Property 'LeftHandIKAlpha' NOT FOUND in %s"), *AnimInstance->GetClass()->GetName());
		return;
	}

	// Try as FFloatProperty first (UE4 style)
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(AlphaProperty))
	{
		void* ValuePtr = FloatProp->ContainerPtrToValuePtr<void>(AnimInstance);
		if (ValuePtr)
		{
			*static_cast<float*>(ValuePtr) = Alpha;
			UE_LOG(LogTemp, Log, TEXT("LeftHandIK: Set as float = %.2f"), Alpha);
		}
	}
	// Try as FDoubleProperty (UE5 may use double for Blueprint floats)
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(AlphaProperty))
	{
		void* ValuePtr = DoubleProp->ContainerPtrToValuePtr<void>(AnimInstance);
		if (ValuePtr)
		{
			*static_cast<double*>(ValuePtr) = static_cast<double>(Alpha);
			UE_LOG(LogTemp, Log, TEXT("LeftHandIK: Set as double = %.2f"), Alpha);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: Property found but wrong type!"));
	}
}

// ==================== New Movement SFX/VFX Handlers ====================

void AShooterCharacter::OnJumpPerformed_Handler(bool bIsDoubleJump)
{
	// Play jump sound
	PlayJumpSound(bIsDoubleJump);

	// Spawn double jump VFX if this is a double jump
	if (bIsDoubleJump)
	{
		SpawnDoubleJumpVFX();
	}
}

void AShooterCharacter::OnMantleStarted_Handler()
{
	PlayMantleSound();
}

void AShooterCharacter::OnAirDashStarted_Handler()
{
	PlayAirDashSound();
	StartAirDashTrailVFX();
}

void AShooterCharacter::OnAirDashEnded_Handler()
{
	StopAirDashTrailVFX();
}

void AShooterCharacter::PlayAirDashSound()
{
	if (AirDashSound)
	{
		const float Pitch = FMath::RandRange(AirDashSoundPitchMin, AirDashSoundPitchMax);
		UGameplayStatics::PlaySoundAtLocation(
			this,
			AirDashSound,
			GetActorLocation(),
			AirDashSoundVolume,
			Pitch
		);
	}
}

void AShooterCharacter::PlayMantleSound()
{
	if (MantleSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			MantleSound,
			GetActorLocation(),
			MantleSoundVolume
		);
	}
}

void AShooterCharacter::PlayWeaponSwitchSound()
{
	if (WeaponSwitchSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			WeaponSwitchSound,
			GetActorLocation(),
			WeaponSwitchSoundVolume
		);
	}
}

void AShooterCharacter::UpdateLowHealthWarning(float DeltaTime)
{
	const float HealthPercent = CurrentHP / MaxHP;
	const bool bShouldBeInLowHealth = HealthPercent < LowHealthThreshold && HealthPercent > 0.0f;

	if (bShouldBeInLowHealth)
	{
		if (!bIsLowHealth)
		{
			// Just entered low health state - play warning immediately
			bIsLowHealth = true;
			LowHealthWarningTimer = 0.0f;

			if (LowHealthWarningSound)
			{
				UGameplayStatics::PlaySound2D(this, LowHealthWarningSound, LowHealthWarningVolume);
			}
		}
		else
		{
			// Already in low health - update timer
			LowHealthWarningTimer += DeltaTime;

			if (LowHealthWarningTimer >= LowHealthWarningInterval)
			{
				LowHealthWarningTimer = 0.0f;

				if (LowHealthWarningSound)
				{
					UGameplayStatics::PlaySound2D(this, LowHealthWarningSound, LowHealthWarningVolume);
				}
			}
		}
	}
	else
	{
		// Reset low health state
		bIsLowHealth = false;
		LowHealthWarningTimer = 0.0f;
	}
}

void AShooterCharacter::SpawnDoubleJumpVFX()
{
	if (DoubleJumpFX)
	{
		const FVector SpawnLocation = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DoubleJumpFX,
			SpawnLocation,
			GetActorRotation(),
			FVector(DoubleJumpFXScale),
			true,
			true,
			ENCPoolMethod::AutoRelease
		);
	}
}

void AShooterCharacter::StartAirDashTrailVFX()
{
	if (AirDashTrailFX && !ActiveAirDashTrailComponent)
	{
		ActiveAirDashTrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			AirDashTrailFX,
			GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			true
		);
	}
}

void AShooterCharacter::StopAirDashTrailVFX()
{
	if (ActiveAirDashTrailComponent)
	{
		ActiveAirDashTrailComponent->Deactivate();
		ActiveAirDashTrailComponent = nullptr;
	}
}