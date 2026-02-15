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
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Curves/CurveFloat.h"
#include "Polarity/Checkpoint/CheckpointData.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Variant_Shooter/DamageTypes/DamageType_Ranged.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFWeapon.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFProximity.h"

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

	// Initialize first person mesh visibility (hidden if no weapon)
	UpdateFirstPersonMeshVisibility();

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

		// Weapon hotkeys
		for (const auto& Hotkey : WeaponHotkeys)
		{
			if (Hotkey.Key && Hotkey.Value)
			{
				EnhancedInputComponent->BindAction(Hotkey.Key, ETriggerEvent::Triggered, this, &AShooterCharacter::DoWeaponHotkey, Hotkey.Value);
			}
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

	// Get damage type for feedback
	TSubclassOf<UDamageType> DamageTypeClass = DamageEvent.DamageTypeClass;

	// Calculate damage direction angle relative to player forward
	// Only show damage direction for actual damage (positive value), not healing
	FVector DamageDirection = FVector::ZeroVector;
	if (DamageCauser && Damage > 0.0f)
	{
		// Get direction from damage source to player
		DamageDirection = (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal();

		// Get player's forward vector (ignore pitch)
		FVector PlayerForward = GetActorForwardVector();
		PlayerForward.Z = 0.0f;
		PlayerForward.Normalize();

		FVector DamageDir2D = DamageDirection;
		DamageDir2D.Z = 0.0f;
		DamageDir2D.Normalize();

		// Calculate angle using atan2 for proper signed angle
		// Positive = right side, Negative = left side
		float DotProduct = FVector::DotProduct(PlayerForward, DamageDir2D);
		float CrossProduct = FVector::CrossProduct(PlayerForward, DamageDir2D).Z;
		float AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(CrossProduct, DotProduct));

		// Broadcast damage direction
		OnDamageDirection.Broadcast(AngleDegrees, Damage);
	}

	// Play damage feedback (camera shake, impact sound)
	if (Damage > 0.0f)
	{
		PlayDamageFeedback(Damage, DamageTypeClass);
	}

	// Apply knockback for melee damage
	if (bEnableMeleeKnockback && Damage > 0.0f && DamageTypeClass)
	{
		if (DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
		{
			// Knockback direction is away from damage source
			FVector KnockbackDir = -DamageDirection;
			KnockbackDir.Z = 0.0f;
			if (!KnockbackDir.IsNearlyZero())
			{
				KnockbackDir.Normalize();
				ApplyMeleeKnockback(KnockbackDir, MeleeKnockbackDistance, MeleeKnockbackDuration);
			}
		}
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

	// Don't fire if weapon switch in progress
	if (bIsWeaponSwitchInProgress)
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

	// Don't switch if already switching
	if (bIsWeaponSwitchInProgress)
	{
		return;
	}

	// Ensure we have at least two weapons to switch between
	if (OwnedWeapons.Num() > 1)
	{
		// Find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// Is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// Loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else
		{
			// Select the next weapon index
			++WeaponIndex;
		}

		// Start animated switch to the new weapon
		StartWeaponSwitch(OwnedWeapons[WeaponIndex]);
	}
}

void AShooterCharacter::DoWeaponHotkey(TSubclassOf<AShooterWeapon> WeaponClass)
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

	// Don't switch if already switching
	if (bIsWeaponSwitchInProgress)
	{
		return;
	}

	// Find weapon of this class in our inventory
	AShooterWeapon* TargetWeapon = FindWeaponOfType(WeaponClass);

	// Only switch if we own this weapon and it's not already equipped
	if (TargetWeapon && TargetWeapon != CurrentWeapon)
	{
		StartWeaponSwitch(TargetWeapon);
	}
}

void AShooterCharacter::StartWeaponSwitch(AShooterWeapon* NewWeapon)
{
	if (!NewWeapon || NewWeapon == CurrentWeapon)
	{
		return;
	}

	// Stop firing current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	// Store the weapon we're switching to
	PendingWeapon = NewWeapon;

	// Begin switch animation
	bIsWeaponSwitchInProgress = true;
	bIsWeaponLowering = true;
	WeaponSwitchProgress = 0.0f;

	// Store current mesh location for interpolation
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		WeaponSwitchMeshBaseLocation = FPMesh->GetRelativeLocation();
	}

	// Play weapon switch sound
	PlayWeaponSwitchSound();
}

void AShooterCharacter::UpdateWeaponSwitch(float DeltaTime)
{
	if (!bIsWeaponSwitchInProgress)
	{
		return;
	}

	if (bIsWeaponLowering)
	{
		// Lowering phase
		if (WeaponSwitchLowerTime > 0.0f)
		{
			WeaponSwitchProgress += DeltaTime / WeaponSwitchLowerTime;
			WeaponSwitchProgress = FMath::Clamp(WeaponSwitchProgress, 0.0f, 1.0f);

			// Interpolate mesh down
			if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
			{
				float Alpha = FMath::InterpEaseIn(0.0f, 1.0f, WeaponSwitchProgress, 2.0f);
				FVector TargetLocation = WeaponSwitchMeshBaseLocation - FVector(0.0f, 0.0f, 100.0f);
				FVector NewLocation = FMath::Lerp(WeaponSwitchMeshBaseLocation, TargetLocation, Alpha);
				FPMesh->SetRelativeLocation(NewLocation);
			}

			// Lowering complete?
			if (WeaponSwitchProgress >= 1.0f)
			{
				OnWeaponSwitchLowered();
			}
		}
		else
		{
			// No lowering time, switch immediately
			OnWeaponSwitchLowered();
		}
	}
	else
	{
		// Raising phase
		if (WeaponSwitchRaiseTime > 0.0f)
		{
			WeaponSwitchProgress += DeltaTime / WeaponSwitchRaiseTime;
			WeaponSwitchProgress = FMath::Clamp(WeaponSwitchProgress, 0.0f, 1.0f);

			// Interpolate mesh up
			if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
			{
				float Alpha = FMath::InterpEaseOut(0.0f, 1.0f, WeaponSwitchProgress, 2.0f);
				FVector LoweredLocation = WeaponSwitchMeshBaseLocation - FVector(0.0f, 0.0f, 100.0f);
				FVector NewLocation = FMath::Lerp(LoweredLocation, WeaponSwitchMeshBaseLocation, Alpha);
				FPMesh->SetRelativeLocation(NewLocation);
			}

			// Raising complete?
			if (WeaponSwitchProgress >= 1.0f)
			{
				OnWeaponSwitchRaised();
			}
		}
		else
		{
			// No raising time, finish immediately
			OnWeaponSwitchRaised();
		}
	}
}

void AShooterCharacter::OnWeaponSwitchLowered()
{
	// Deactivate old weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// Activate new weapon
	if (PendingWeapon)
	{
		CurrentWeapon = PendingWeapon;
		CurrentWeapon->ActivateWeapon();
		PendingWeapon = nullptr;
	}

	// Start raising phase
	bIsWeaponLowering = false;
	WeaponSwitchProgress = 0.0f;
}

void AShooterCharacter::OnWeaponSwitchRaised()
{
	// Restore mesh to exact base position
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetRelativeLocation(WeaponSwitchMeshBaseLocation);
	}

	// Switch complete
	bIsWeaponSwitchInProgress = false;
	PendingWeapon = nullptr;
}

void AShooterCharacter::DoMeleeAttack()
{
	// Don't melee if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	// Don't melee if weapon switch in progress
	if (bIsWeaponSwitchInProgress)
	{
		return;
	}

	// Check for boss finisher mode
	if (bIsOnBossFinisher && !bBossFinisherActive)
	{
		StartBossFinisher();
		return;
	}

	// Don't allow normal melee during boss finisher
	if (bBossFinisherActive)
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

	// Boss finisher has priority over everything
	if (bBossFinisherActive)
	{
		UpdateBossFinisher(DeltaTime);
		return; // Skip normal updates during finisher
	}

	// Update knockback interpolation if active
	if (bIsInKnockback)
	{
		UpdateKnockbackInterpolation(DeltaTime);
	}

	// Update chromatic aberration effect if active
	if (bChromaticAberrationActive)
	{
		UpdateChromaticAberration(DeltaTime);
	}

	UpdateADS(DeltaTime);
	UpdateRegeneration(DeltaTime);
	UpdateLeftHandIK(DeltaTime);
	UpdateLowHealthWarning(DeltaTime);
	UpdatePostProcessEffects(DeltaTime);
	UpdateWeaponSwitch(DeltaTime);

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

	// Let weapon handle secondary action as ability (e.g. laser's Second Harmonic)
	if (CurrentWeapon && CurrentWeapon->OnSecondaryAction())
	{
		return;
	}

	if (MovementSettings && MovementSettings->bEnableADS)
	{
		bWantsToAim = true;

		// Play ADS in sound
		if (CurrentWeapon)
		{
			CurrentWeapon->PlayADSInSound();

			// Set the weapon as the view target — PlayerCameraManager will blend
			// to it and call Weapon->CalcCamera() which returns sight-socket position
			// with ControlRotation (no recoil visual kick)
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				FViewTargetTransitionParams BlendParams;
				BlendParams.BlendTime = CurrentWeapon->GetADSBlendInTime();
				BlendParams.BlendFunction = EViewTargetBlendFunction::VTBlend_EaseInOut;
				BlendParams.BlendExp = 2.0f;
				PC->SetViewTarget(CurrentWeapon, BlendParams);
			}
		}

		// Tell recoil component we're aiming
		if (RecoilComponent)
		{
			RecoilComponent->SetAiming(true);
		}
	}
}

void AShooterCharacter::DoStopADS()
{
	// Only play sound and transition camera if we were actually aiming
	if (bWantsToAim && CurrentWeapon)
	{
		CurrentWeapon->PlayADSOutSound();

		// Blend camera back to the character (CameraComponent)
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			FViewTargetTransitionParams BlendParams;
			BlendParams.BlendTime = CurrentWeapon->GetADSBlendOutTime();
			BlendParams.BlendFunction = EViewTargetBlendFunction::VTBlend_EaseInOut;
			BlendParams.BlendExp = 2.0f;
			PC->SetViewTarget(this, BlendParams);
		}
	}

	bWantsToAim = false;

	// Tell recoil component we stopped aiming
	if (RecoilComponent)
	{
		RecoilComponent->SetAiming(false);
	}
}

void AShooterCharacter::UpdateADS(float DeltaTime)
{
	if (!MovementSettings || !MovementSettings->bEnableADS)
	{
		return;
	}

	// Determine target alpha
	float TargetAlpha = bWantsToAim ? 1.0f : 0.0f;

	// Interpolate alpha (used by other systems like recoil WeaponFraction)
	CurrentADSAlpha = FMath::FInterpTo(
		CurrentADSAlpha,
		TargetAlpha,
		DeltaTime,
		MovementSettings->ADSInterpSpeed
	);

	// Camera position/rotation is handled by SetViewTarget + CalcCamera blend
	// (PlayerCameraManager blends between character camera and weapon CalcCamera).
	// We still need to apply shake offset to the character's own camera component
	// so it's correct when not in ADS.
	UCameraComponent* Camera = GetFirstPersonCameraComponent();
	if (Camera)
	{
		// Apply shake offset to camera (always, regardless of ADS state)
		FVector ShakeOffset = FVector::ZeroVector;
		if (UCameraShakeComponent* ShakeComp = GetCameraShake())
		{
			ShakeOffset = ShakeComp->GetCameraOffset();
		}
		Camera->SetRelativeLocation(BaseCameraLocation + ShakeOffset);
	}
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
	// Call parent implementation first (sets base position of FP Mesh)
	Super::UpdateFirstPersonView(DeltaTime);

	USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	if (!FPMesh)
	{
		return;
	}

	// Get current relative transform (set by Super — the base hip-fire position)
	FVector CurrentLocation = FPMesh->GetRelativeLocation();
	FRotator CurrentRotation = FPMesh->GetRelativeRotation();

	// === Recoil Visual Kick ===
	if (RecoilComponent)
	{
		FVector RecoilOffset = RecoilComponent->GetWeaponOffset();
		FRotator RecoilRotation = RecoilComponent->GetWeaponRotationOffset();

		if (USceneComponent* Parent = FPMesh->GetAttachParent())
		{
			const FRotator ParentRot = Parent->GetRelativeRotation();
			RecoilOffset = ParentRot.UnrotateVector(RecoilOffset);
			const FVector RotAsVec(RecoilRotation.Roll, RecoilRotation.Pitch, RecoilRotation.Yaw);
			const FVector RotTransformed = ParentRot.UnrotateVector(RotAsVec);
			RecoilRotation = FRotator(RotTransformed.Y, RotTransformed.Z, RotTransformed.X);
		}

		CurrentLocation += RecoilOffset;
		CurrentRotation += RecoilRotation;
	}

	// Apply hip-fire + recoil via relative transform
	FPMesh->SetRelativeLocation(CurrentLocation);
	FPMesh->SetRelativeRotation(CurrentRotation);

	// === ADS Weapon Alignment (via SetWorldRotation/Location) ===
	// After setting relative transform, override the world transform directly
	// when in ADS. Camera goes to weapon via SetViewTarget + CalcCamera.
	// Here we make the weapon VISUALLY follow pitch/aim direction.
	if (CurrentADSAlpha > KINDA_SMALL_NUMBER && CurrentWeapon)
	{
		USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetFirstPersonMesh();
		UCameraComponent* Camera = GetFirstPersonCameraComponent();

		if (WeaponMesh && Camera)
		{
			FName SightSocket = FName("Sight");
			FName RearSocket = FName("SightRear");
			FName BottomSocket = FName("SightBottom");

			if (WeaponMesh->DoesSocketExist(SightSocket) && WeaponMesh->DoesSocketExist(RearSocket))
			{
				// Force world transform update so GetComponent*/GetSocket* return fresh data
				FPMesh->UpdateComponentToWorld();
				WeaponMesh->UpdateComponentToWorld();

				// Read current world state (now guaranteed fresh after UpdateComponentToWorld)
				FQuat CurWorldQuat = FPMesh->GetComponentQuat();
				FVector CurWorldPos = FPMesh->GetComponentLocation();

				// Socket world positions (based on current world transform)
				FVector FrontWorld = WeaponMesh->GetSocketLocation(SightSocket);
				FVector RearWorld = WeaponMesh->GetSocketLocation(RearSocket);

				FVector CamLoc = Camera->GetComponentLocation();
				FVector CamFwd = GetControlRotation().Vector();

				// Step 1: Align Rear→Front with camera forward
				FVector WorldAimDir = (FrontWorld - RearWorld).GetSafeNormal();
				FQuat AimCorrection = FQuat::FindBetweenNormals(WorldAimDir, CamFwd);

				// Step 2: Roll correction
				FQuat RollCorrection = FQuat::Identity;
				if (WeaponMesh->DoesSocketExist(BottomSocket))
				{
					FVector BottomWorld = WeaponMesh->GetSocketLocation(BottomSocket);
					FVector WorldDownDir = (BottomWorld - RearWorld).GetSafeNormal();
					FVector CorrectedDown = AimCorrection.RotateVector(WorldDownDir);

					FVector CurrentDownProj = FVector::VectorPlaneProject(CorrectedDown, CamFwd).GetSafeNormal();
					FVector TargetDownProj = FVector::VectorPlaneProject(-FVector::UpVector, CamFwd).GetSafeNormal();

					if (!CurrentDownProj.IsNearlyZero() && !TargetDownProj.IsNearlyZero())
					{
						RollCorrection = FQuat::FindBetweenNormals(CurrentDownProj, TargetDownProj);
					}
				}

				// Step 3: Target world rotation
				FQuat TargetWorldQuat = RollCorrection * AimCorrection * CurWorldQuat;

				// Step 4: Position — place front socket on camera ray
				FVector FrontOffset = FrontWorld - CurWorldPos;
				FQuat TotalCorrection = RollCorrection * AimCorrection;
				FVector FrontInTarget = TotalCorrection.RotateVector(FrontOffset);

				const float SightDist = 30.0f;
				FVector SightTarget = CamLoc + CamFwd * SightDist;
				FVector TargetWorldPos = SightTarget - FrontInTarget;

				// Blend: lerp between current world and target world
				FVector FinalWorldPos = FMath::Lerp(CurWorldPos, TargetWorldPos, CurrentADSAlpha);
				FQuat FinalWorldQuat = FQuat::Slerp(CurWorldQuat, TargetWorldQuat, CurrentADSAlpha);

				// Apply directly in world space (bypasses parent-relative issues)
				FPMesh->SetWorldLocation(FinalWorldPos);
				FPMesh->SetWorldRotation(FinalWorldQuat);
			}
		}
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
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);

	// If weapon has OptionalGrip socket, offset the mesh so that socket aligns with hand
	static const FName OptionalGripSocket = FName("OptionalGrip");

	if (USkeletalMeshComponent* FPMesh = Weapon->GetFirstPersonMesh())
	{
		if (FPMesh->DoesSocketExist(OptionalGripSocket))
		{
			FTransform SocketTransform = FPMesh->GetSocketTransform(OptionalGripSocket, RTS_Component);
			FPMesh->SetRelativeLocation(-SocketTransform.GetLocation());
			FPMesh->SetRelativeRotation(SocketTransform.GetRotation().Inverse());
		}
	}

	if (USkeletalMeshComponent* TPMesh = Weapon->GetThirdPersonMesh())
	{
		if (TPMesh->DoesSocketExist(OptionalGripSocket))
		{
			FTransform SocketTransform = TPMesh->GetSocketTransform(OptionalGripSocket, RTS_Component);
			TPMesh->SetRelativeLocation(-SocketTransform.GetLocation());
			TPMesh->SetRelativeRotation(SocketTransform.GetRotation().Inverse());
		}
	}
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	// Play on third-person mesh (visible to other players)
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

	// Play on first-person mesh (visible to local player)
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
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

	// Get aim direction from controller (works for both hip fire and ADS)
	// GetFirstPersonCameraComponent() doesn't update rotation when ADS camera is active
	FVector Start = GetFirstPersonCameraComponent()->GetComponentLocation();
	FVector AimDirection;

	if (AController* PC = GetController())
	{
		AimDirection = PC->GetControlRotation().Vector();
	}
	else
	{
		AimDirection = GetFirstPersonCameraComponent()->GetForwardVector();
	}

	const FVector End = Start + (AimDirection * MaxAimDistance);

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
			// Check if this is the first weapon (for visibility update)
			const bool bWasUnarmed = OwnedWeapons.Num() == 0;

			OwnedWeapons.Add(AddedWeapon);

			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon();

			// Update mesh visibility when picking up first weapon
			if (bWasUnarmed)
			{
				UpdateFirstPersonMeshVisibility();
			}
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

void AShooterCharacter::UpdateFirstPersonMeshVisibility()
{
	USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	if (!FPMesh)
	{
		return;
	}

	const bool bHasWeapon = OwnedWeapons.Num() > 0;
	FPMesh->SetVisibility(bHasWeapon, false);
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

// ==================== Damage Feedback ====================

void AShooterCharacter::PlayDamageFeedback(float Damage, TSubclassOf<UDamageType> DamageTypeClass)
{
	// Play camera shake scaled by damage
	if (DamageCameraShake)
	{
		float ShakeScale = 1.0f;
		if (DamageToCameraShakeCurve)
		{
			ShakeScale = DamageToCameraShakeCurve->GetFloatValue(Damage) * MaxCameraShakeScale;
		}
		else
		{
			// Default: linear scale up to MaxCameraShakeScale at 100 damage
			ShakeScale = FMath::Clamp(Damage / 100.0f, 0.1f, 1.0f) * MaxCameraShakeScale;
		}

		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->ClientStartCameraShake(DamageCameraShake, ShakeScale);
		}
	}

	// Play impact sound based on damage type
	USoundBase* ImpactSound = GetImpactSoundForDamageType(DamageTypeClass);
	if (ImpactSound)
	{
		UGameplayStatics::PlaySound2D(this, ImpactSound, DamageImpactSoundVolume);
	}

	// Start chromatic aberration effect
	StartChromaticAberrationEffect(Damage);
}

USoundBase* AShooterCharacter::GetImpactSoundForDamageType(TSubclassOf<UDamageType> DamageTypeClass) const
{
	if (!DamageTypeClass)
	{
		return DefaultImpactSound;
	}

	// Check for specific damage types
	if (DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		return MeleeImpactSound ? MeleeImpactSound : DefaultImpactSound;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_Ranged::StaticClass()))
	{
		return RangedImpactSound ? RangedImpactSound : DefaultImpactSound;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_EMFWeapon::StaticClass()) ||
		DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()))
	{
		return EMFImpactSound ? EMFImpactSound : DefaultImpactSound;
	}

	// Check if it's a radial damage (explosion)
	// UE doesn't have a built-in explosion type, so we use default for now
	// You can add UDamageType_Explosion if needed

	return DefaultImpactSound;
}

// ==================== Melee Knockback ====================

void AShooterCharacter::ApplyMeleeKnockback(const FVector& KnockbackDirection, float Distance, float Duration)
{
	if (Distance < 1.0f || Duration < 0.01f)
	{
		return;
	}

	bIsInKnockback = true;
	KnockbackStartPosition = GetActorLocation();
	KnockbackTargetPosition = KnockbackStartPosition + KnockbackDirection * Distance;
	KnockbackTotalDuration = Duration;
	KnockbackElapsedTime = 0.0f;
}

void AShooterCharacter::UpdateKnockbackInterpolation(float DeltaTime)
{
	if (!bIsInKnockback)
	{
		return;
	}

	KnockbackElapsedTime += DeltaTime;
	float Alpha = FMath::Clamp(KnockbackElapsedTime / KnockbackTotalDuration, 0.0f, 1.0f);

	// Use smooth step for more natural feel
	Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

	FVector NewPosition = FMath::Lerp(KnockbackStartPosition, KnockbackTargetPosition, Alpha);

	// Simple collision check - sweep to new position
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	float CapsuleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	bool bHit = GetWorld()->SweepSingleByChannel(
		Hit,
		GetActorLocation(),
		NewPosition,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
		QueryParams
	);

	if (bHit)
	{
		// Stop at wall with small offset
		NewPosition = Hit.Location + Hit.ImpactNormal * 2.0f;
		bIsInKnockback = false; // End knockback on wall hit
	}

	SetActorLocation(NewPosition, false);

	// End knockback when duration complete
	if (KnockbackElapsedTime >= KnockbackTotalDuration)
	{
		bIsInKnockback = false;
	}
}

void AShooterCharacter::CancelKnockback()
{
	if (bIsInKnockback && bKnockbackCancellableByPlayer)
	{
		bIsInKnockback = false;
	}
}

// ==================== Chromatic Aberration ====================

void AShooterCharacter::StartChromaticAberrationEffect(float Damage)
{
	// Calculate base intensity from damage (linear, clamped to 0-1)
	ChromaticAberrationBaseIntensity = FMath::Clamp(Damage / MaxDamageForFullChromaticAberration, 0.0f, 1.0f);
	ChromaticAberrationElapsedTime = 0.0f;
	bChromaticAberrationActive = true;
}

void AShooterCharacter::UpdateChromaticAberration(float DeltaTime)
{
	if (!bChromaticAberrationActive)
	{
		return;
	}

	ChromaticAberrationElapsedTime += DeltaTime;

	// Check if effect has finished
	if (ChromaticAberrationElapsedTime >= ChromaticAberrationDuration)
	{
		bChromaticAberrationActive = false;
		// Broadcast final zero intensity
		OnDamageChromaticAberration.Broadcast(0.0f);
		return;
	}

	// Calculate intensity using half sine wave (0 → 1 → 0)
	// sin(t * PI / Duration) where t goes from 0 to Duration
	float Alpha = ChromaticAberrationElapsedTime / ChromaticAberrationDuration;
	float SineMultiplier = FMath::Sin(Alpha * PI);
	float FinalIntensity = ChromaticAberrationBaseIntensity * SineMultiplier;

	// Broadcast current intensity
	OnDamageChromaticAberration.Broadcast(FinalIntensity);
}

// ==================== Death ====================

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

	// Start fade to black
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(0.0f, 1.0f, DeathFadeOutDuration, DeathFadeColor, false, true);
		}
	}

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

		// Fade in from black
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, RespawnFadeInDuration, DeathFadeColor, false, false);
		}
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
		//UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: No FPMesh!"));
		return;
	}

	UAnimInstance* AnimInstance = FPMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		//UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: No AnimInstance!"));
		return;
	}

	//UE_LOG(LogTemp, Log, TEXT("LeftHandIK: AnimInstance=%s, Alpha=%.2f"), *AnimInstance->GetClass()->GetName(), Alpha);

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
		//UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: Property 'LeftHandIKAlpha' NOT FOUND in %s"), *AnimInstance->GetClass()->GetName());
		return;
	}

	// Try as FFloatProperty first (UE4 style)
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(AlphaProperty))
	{
		void* ValuePtr = FloatProp->ContainerPtrToValuePtr<void>(AnimInstance);
		if (ValuePtr)
		{
			*static_cast<float*>(ValuePtr) = Alpha;
			//UE_LOG(LogTemp, Log, TEXT("LeftHandIK: Set as float = %.2f"), Alpha);
		}
	}
	// Try as FDoubleProperty (UE5 may use double for Blueprint floats)
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(AlphaProperty))
	{
		void* ValuePtr = DoubleProp->ContainerPtrToValuePtr<void>(AnimInstance);
		if (ValuePtr)
		{
			*static_cast<double*>(ValuePtr) = static_cast<double>(Alpha);
			//UE_LOG(LogTemp, Log, TEXT("LeftHandIK: Set as double = %.2f"), Alpha);
		}
	}
	else
	{
		//UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: Property found but wrong type!"));
	}
}

// ==================== New Movement SFX/VFX Handlers ====================

void AShooterCharacter::OnJumpPerformed_Handler(bool bIsDoubleJump)
{
	CancelKnockback(); // Player action cancels knockback

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
	CancelKnockback(); // Player action cancels knockback
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

void AShooterCharacter::UpdatePostProcessEffects(float DeltaTime)
{
	// Calculate target intensities
	const float HealthPercent = CurrentHP / MaxHP;
	const float TargetLowHealthIntensity = (HealthPercent < LowHealthThreshold && HealthPercent > 0.0f)
		? FMath::GetMappedRangeValueClamped(FVector2D(0.0f, LowHealthThreshold), FVector2D(1.0f, 0.0f), HealthPercent)
		: 0.0f;

	const float CurrentSpeed = GetVelocity().Size();
	const float TargetHighSpeedIntensity = (CurrentSpeed > HighSpeedThreshold)
		? FMath::GetMappedRangeValueClamped(FVector2D(HighSpeedThreshold, HighSpeedMaxThreshold), FVector2D(0.0f, 1.0f), CurrentSpeed)
		: 0.0f;

	// Interpolate current values
	CurrentLowHealthPPIntensity = FMath::FInterpTo(CurrentLowHealthPPIntensity, TargetLowHealthIntensity, DeltaTime, PPInterpSpeed);
	CurrentHighSpeedPPIntensity = FMath::FInterpTo(CurrentHighSpeedPPIntensity, TargetHighSpeedIntensity, DeltaTime, PPInterpSpeed);

	// Apply to materials
	if (LowHealthPPMaterial)
	{
		LowHealthPPMaterial->SetScalarParameterValue(PPIntensityParameterName, CurrentLowHealthPPIntensity);
	}

	if (HighSpeedPPMaterial)
	{
		HighSpeedPPMaterial->SetScalarParameterValue(PPIntensityParameterName, CurrentHighSpeedPPIntensity);
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

// ==================== Boss Finisher Implementation ====================

void AShooterCharacter::StartBossFinisher()
{
	if (bBossFinisherActive)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Starting finisher sequence"));

	bBossFinisherActive = true;
	BossFinisherPhase = EBossFinisherPhase::CurveMovement;
	BossFinisherElapsedTime = 0.0f;
	BossFinisherStartPosition = GetActorLocation();

	// Setup Bezier curve
	SetupBezierCurve();

	// Stop any current weapon firing
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	// Lower weapon immediately (will skip lowering phase when attack starts later)
	if (MeleeAttackComponent)
	{
		MeleeAttackComponent->LowerWeapon();
	}

	// Disable gravity and movement input
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GravityScale = 0.0f;
		Movement->Velocity = FVector::ZeroVector;
		Movement->SetMovementMode(MOVE_Flying);
	}

	// Disable player input (movement)
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// We don't disable input completely - we still want camera control during most phases
		// But movement is handled by the finisher system
	}

	// Broadcast start event
	OnBossFinisherStarted.Broadcast();
}

void AShooterCharacter::StopBossFinisher()
{
	if (!bBossFinisherActive)
	{
		return;
	}

	EndBossFinisher();
}

void AShooterCharacter::SetupBezierCurve()
{
	// P0 = Start position (player current location)
	BezierP0 = BossFinisherStartPosition;

	// P3 = Target position
	BezierP3 = BossFinisherSettings.TargetPoint;

	// Calculate approach point (where the "straight line" phase begins)
	// ApproachOffset is relative to target - we want player to come FROM this direction
	FVector ApproachPoint = BezierP3 + BossFinisherSettings.ApproachOffset;

	// P1 = Control point near start - creates initial curve away from direct path
	// Place it roughly 1/3 of the way, but offset to create the curve shape
	FVector StartToApproach = ApproachPoint - BezierP0;
	FVector StartToTarget = BezierP3 - BezierP0;

	// P1 creates the "swing out" at the beginning
	// Cross product gives us perpendicular direction for the curve
	FVector CurveDirection = FVector::CrossProduct(StartToTarget.GetSafeNormal(), FVector::UpVector);
	if (CurveDirection.IsNearlyZero())
	{
		CurveDirection = FVector::RightVector;
	}
	CurveDirection.Normalize();

	// Add some height and lateral offset for dramatic curve
	BezierP1 = BezierP0 + StartToTarget * 0.33f + CurveDirection * StartToTarget.Size() * 0.3f + FVector(0, 0, 200.0f);

	// P2 = Control point near approach point - creates the "diving in" feel
	// This should be near the approach point but pulled toward P3
	BezierP2 = ApproachPoint + (BezierP3 - ApproachPoint) * 0.3f;

	UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Bezier curve setup - P0: %s, P1: %s, P2: %s, P3: %s"),
		*BezierP0.ToString(), *BezierP1.ToString(), *BezierP2.ToString(), *BezierP3.ToString());
}

FVector AShooterCharacter::EvaluateBezierCurve(float T) const
{
	// Cubic Bezier: B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
	float OneMinusT = 1.0f - T;
	float OneMinusT2 = OneMinusT * OneMinusT;
	float OneMinusT3 = OneMinusT2 * OneMinusT;
	float T2 = T * T;
	float T3 = T2 * T;

	return OneMinusT3 * BezierP0 +
		   3.0f * OneMinusT2 * T * BezierP1 +
		   3.0f * OneMinusT * T2 * BezierP2 +
		   T3 * BezierP3;
}

void AShooterCharacter::UpdateBossFinisher(float DeltaTime)
{
	BossFinisherElapsedTime += DeltaTime;

	const float TotalTime = BossFinisherSettings.TotalTravelTime;
	const float StraightenTime = BossFinisherSettings.StraightenTime;
	const float AnimStartTime = BossFinisherSettings.AnimationStartTime;
	const float HangTime = BossFinisherSettings.HangTime;

	// Calculate time remaining until reaching target
	float TimeRemaining = TotalTime - BossFinisherElapsedTime;

	// Always focus camera on target point during finisher
	// Add 150 unit offset along approach direction to prevent 180 flip when passing through target
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Calculate camera focus point with offset
		// Offset is 150 units from TargetPoint in the direction from ApproachOffset toward TargetPoint
		FVector ApproachPoint = BossFinisherSettings.TargetPoint + BossFinisherSettings.ApproachOffset;
		FVector ApproachDirection = (BossFinisherSettings.TargetPoint - ApproachPoint).GetSafeNormal();
		FVector CameraFocusPoint = BossFinisherSettings.TargetPoint + ApproachDirection * 150.0f;

		FVector ToTarget = CameraFocusPoint - GetActorLocation();
		FRotator TargetRotation = ToTarget.Rotation();
		FRotator CurrentRotation = PC->GetControlRotation();

		// Smooth interpolation to target
		FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, 10.0f);
		PC->SetControlRotation(NewRotation);
	}

	switch (BossFinisherPhase)
	{
	case EBossFinisherPhase::CurveMovement:
		{
			// Check if we should transition to linear movement
			if (TimeRemaining <= StraightenTime)
			{
				BossFinisherPhase = EBossFinisherPhase::LinearMovement;
				LinearStartPosition = GetActorLocation();
				LinearStartTime = BossFinisherElapsedTime;
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Transitioning to LinearMovement"));
				break;
			}

			// Calculate T parameter for Bezier (0 to 1 during curve phase)
			// Curve phase runs from 0 to (TotalTime - StraightenTime)
			float CurvePhaseTime = TotalTime - StraightenTime;
			float LinearT = FMath::Clamp(BossFinisherElapsedTime / CurvePhaseTime, 0.0f, 1.0f);

			// Apply EaseIn (quadratic) - slow start, accelerating toward end
			// T^2 gives nice acceleration curve
			float T = LinearT * LinearT;

			FVector NewPosition = EvaluateBezierCurve(T);
			SetActorLocation(NewPosition);

			// Rotate character to face movement direction
			FVector Velocity = EvaluateBezierCurve(FMath::Min(T + 0.01f, 1.0f)) - NewPosition;
			if (!Velocity.IsNearlyZero())
			{
				SetActorRotation(FRotator(0, Velocity.Rotation().Yaw, 0));
			}
		}
		break;

	case EBossFinisherPhase::LinearMovement:
		{
			// Check if we should start animation
			if (TimeRemaining <= AnimStartTime && BossFinisherPhase != EBossFinisherPhase::Animation)
			{
				BossFinisherPhase = EBossFinisherPhase::Animation;
				StartBossFinisherAnimation();
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Starting animation phase"));
				// Fall through to animation phase
			}
			else if (TimeRemaining <= 0)
			{
				// Reached target - start hanging
				BossFinisherPhase = EBossFinisherPhase::Hanging;
				BossFinisherElapsedTime = 0.0f; // Reset for hang timer
				SetActorLocation(BossFinisherSettings.TargetPoint);
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Reached target, starting hang phase"));
				break;
			}
			else
			{
				// Linear interpolation to target with EaseIn for acceleration effect
				float LinearPhaseTime = StraightenTime;
				float LinearElapsed = BossFinisherElapsedTime - LinearStartTime;
				float LinearAlpha = FMath::Clamp(LinearElapsed / LinearPhaseTime, 0.0f, 1.0f);

				// EaseIn (quadratic) - continues the acceleration from curve phase
				float Alpha = LinearAlpha * LinearAlpha;

				FVector NewPosition = FMath::Lerp(LinearStartPosition, BossFinisherSettings.TargetPoint, Alpha);
				SetActorLocation(NewPosition);

				// Face target
				FVector ToTarget = BossFinisherSettings.TargetPoint - NewPosition;
				if (!ToTarget.IsNearlyZero())
				{
					SetActorRotation(FRotator(0, ToTarget.Rotation().Yaw, 0));
				}
			}
		}
		break;

	case EBossFinisherPhase::Animation:
		{
			// Continue moving to target while animating
			if (TimeRemaining <= 0)
			{
				// Reached target - start hanging
				BossFinisherPhase = EBossFinisherPhase::Hanging;
				BossFinisherElapsedTime = 0.0f; // Reset for hang timer
				SetActorLocation(BossFinisherSettings.TargetPoint);
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Reached target during animation, starting hang phase"));
			}
			else
			{
				// Continue linear movement with EaseIn acceleration
				float LinearPhaseTime = StraightenTime;
				float LinearElapsed = BossFinisherElapsedTime - LinearStartTime;
				float LinearAlpha = FMath::Clamp(LinearElapsed / LinearPhaseTime, 0.0f, 1.0f);

				// EaseIn (quadratic) - accelerates toward target
				float Alpha = LinearAlpha * LinearAlpha;

				FVector NewPosition = FMath::Lerp(LinearStartPosition, BossFinisherSettings.TargetPoint, Alpha);
				SetActorLocation(NewPosition);
			}
		}
		break;

	case EBossFinisherPhase::Hanging:
		{
			// Stay at target point
			SetActorLocation(BossFinisherSettings.TargetPoint);

			if (BossFinisherElapsedTime >= HangTime)
			{
				BossFinisherPhase = EBossFinisherPhase::Falling;

				// Re-enable gravity
				if (UCharacterMovementComponent* Movement = GetCharacterMovement())
				{
					Movement->GravityScale = 1.0f;
					Movement->SetMovementMode(MOVE_Falling);
				}

				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Hang complete, starting fall"));
			}
		}
		break;

	case EBossFinisherPhase::Falling:
		{
			// Check if landed
			if (UCharacterMovementComponent* Movement = GetCharacterMovement())
			{
				if (Movement->IsMovingOnGround())
				{
					EndBossFinisher();
					UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Landed, finisher complete"));
				}
			}
		}
		break;

	default:
		break;
	}
}

void AShooterCharacter::StartBossFinisherAnimation()
{
	// Use MeleeAttackComponent's air attack animation
	if (MeleeAttackComponent)
	{
		// Temporarily set movement mode to Falling so MeleeAttackComponent
		// uses AirborneAttack animation instead of Ground
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Falling);
		}

		// Trigger the air attack animation through melee component
		// This will apply all the mesh offsets, hidden bones, etc. from AirborneAttack settings
		MeleeAttackComponent->StartAttack();

		// Return to Flying for controlled movement
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Flying);
		}
	}
}

void AShooterCharacter::EndBossFinisher()
{
	UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Ending finisher sequence"));

	bBossFinisherActive = false;
	BossFinisherPhase = EBossFinisherPhase::None;
	bIsOnBossFinisher = false; // Reset flag so it needs to be set again for next finisher

	// Restore normal movement
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GravityScale = 1.0f;
		if (!Movement->IsMovingOnGround())
		{
			Movement->SetMovementMode(MOVE_Falling);
		}
		else
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}

	// Broadcast end event
	OnBossFinisherEnded.Broadcast();
}