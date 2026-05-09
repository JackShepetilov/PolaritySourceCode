// ChargeAnimationComponent.cpp
// Charge toggle animation + channeling ability implementation

#include "ChargeAnimationComponent.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
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
#include "EMFPhysicsProp.h"
#include "EMFAcceleratorPlate.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Pickups/UpgradePickup.h"
#include "Variant_Shooter/Pickups/AbilityPickup.h"
#include "Variant_Shooter/Pickups/ScriptedPickup.h"
#include "Variant_Shooter/Weapons/RiotShieldPickup.h"
#include "Variant_Shooter/AI/HumanoidNPC.h"
#include "EngineUtils.h" // TActorIterator
#include "Engine/OverlapResult.h"

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

	// Update plate position during channeling states (animation flow matches legacy —
	// the post-capture lockout lives inside Channeling as a timer, not a separate state).
	if (CurrentState == EChargeAnimationState::Channeling || CurrentState == EChargeAnimationState::ReverseChanneling)
	{
		UpdatePlatePosition();
	}

	// Update beam VFX positions (capture/launch beams track plate and target)
	UpdateBeamVFX();
}

// ==================== Input API ====================

void UChargeAnimationComponent::OnChargeButtonPressed()
{
	// In simplified-onboarding mode (bUsePressPressCaptureMode = true) the dedicated
	// charge-toggle button is intentionally retired — the launch is driven by a second
	// channel press. Body kept verbatim for the legacy/alt mode path below.
	if (bUsePressPressCaptureMode)
	{
		return;
	}

	// === Legacy tap-toggle / reverse-channel-via-toggle path (preserved for revert) ===

	// Case 1: Normal activation from Ready state — tap toggle
	if (CurrentState == EChargeAnimationState::Ready && CanStartAnimation())
	{
		bChannelingPath = false;
		bInputLocked = true;

		MeshTransitionProgress = 0.0f;
		MontageTimeElapsed = 0.0f;

		BeginHideWeapon();
		SetState(EChargeAnimationState::HidingWeapon);
		return;
	}

	// Case 2: Pressed during Channeling — trigger reverse channeling launch
	if (CurrentState == EChargeAnimationState::Channeling)
	{
		BeginLaunch();
		return;
	}
}

void UChargeAnimationComponent::OnChargeButtonReleased()
{
	// ToggleCharge release — no action needed (tap toggle is instant on press,
	// and in simplified mode the button is retired entirely).
}

void UChargeAnimationComponent::OnChannelButtonPressed()
{
	// === Legacy hold-to-channel mode (kept for revert) ===
	if (!bUsePressPressCaptureMode)
	{
		if (CurrentState == EChargeAnimationState::Ready && CanStartAnimation())
		{
			bChannelingPath = true;
			bInputLocked = true;

			MeshTransitionProgress = 0.0f;
			MontageTimeElapsed = 0.0f;

			BeginHideWeapon();
			SetState(EChargeAnimationState::HidingWeapon);
		}
		return;
	}

	// === Press-press mode (Void Breaker-style) ===
	// Press 1 from Ready → wind-up, single capture attempt
	// Press 2 from Channeling → launch held target (gated by post-capture lockout timer)
	switch (CurrentState)
	{
	case EChargeAnimationState::Ready:
		if (!CanStartAnimation())
		{
			return;
		}
		bChannelingPath = true;
		bInputLocked = true;
		MeshTransitionProgress = 0.0f;
		MontageTimeElapsed = 0.0f;
		BeginHideWeapon();
		SetState(EChargeAnimationState::HidingWeapon);
		break;

	case EChargeAnimationState::Channeling:
		// Anti-spam: ignore the second press while the post-capture lockout is still running.
		if (CaptureLockoutTimeRemaining > 0.0f)
		{
			break;
		}
		BeginLaunch();
		break;

	default:
		// HidingWeapon, ReverseChanneling, FinishingAnimation, etc. — input ignored
		break;
	}
}

void UChargeAnimationComponent::OnChannelButtonReleased()
{
	// Press-press mode: release is meaningless — capture and launch are press-only.
	if (bUsePressPressCaptureMode)
	{
		return;
	}

	// === Legacy hold-mode behavior (kept for revert) ===
	if (CurrentState == EChargeAnimationState::Channeling)
	{
		ExitChanneling();
		ReleaseCapturedNPC();
		EnterFinishingAnimation();
		return;
	}

	// Cancel if released during transition before channeling started
	if (bChannelingPath && CurrentState == EChargeAnimationState::HidingWeapon)
	{
		CancelAnimation();
		return;
	}
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
		// Legacy: alpha=0 freed left hand for the melee mesh charge montage.
		// In FP-montage mode, the slot drives the left arm directly via Layered Blend Per Bone,
		// so don't fight it with IK alpha changes.
		if (!bUseFPMontages && ShooterCharacter)
		{
			ShooterCharacter->SetLeftHandIKAlpha(0.0f);
		}
		break;

	case EChargeAnimationState::ShowingWeapon:
		StateTimeRemaining = ShowWeaponTime;
		MeshTransitionProgress = 0.0f;
		StopChargeAnimation();
		StopChargeVFX();
		// When bUseFPMontages: FP mesh never left, so skip the swap-back.
		if (!bUseFPMontages)
		{
			SwitchToFirstPersonMesh();
		}
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
		StateTimeRemaining = 0.0f; // No timer — held until channel button released (legacy)
		                           // or second channel press launches (press-press)
		break;

	case EChargeAnimationState::ReverseChanneling:
		StateTimeRemaining = ReverseChargeDuration;
		break;

	case EChargeAnimationState::FinishingAnimation:
		// Timer set in EnterFinishingAnimation() based on remaining montage length
		break;

	// CaptureLockout enum value retained for revert; no longer entered as a state —
	// the post-capture input lockout is now a timer (CaptureLockoutTimeRemaining)
	// running inside Channeling so the animation flow matches the legacy hold-mode timeline.
	}
}

void UChargeAnimationComponent::UpdateState(float DeltaTime)
{
	if (CurrentState == EChargeAnimationState::Ready)
	{
		return;
	}

	// Channeling state: continuous charge drain, no state timer.
	// Press-press lockout countdown happens here too (timer-only, no separate state).
	if (CurrentState == EChargeAnimationState::Channeling)
	{
		if (CachedEMFModifier && ChannelingChargeCostPerSecond > 0.0f)
		{
			CachedEMFModifier->DeductCharge(ChannelingChargeCostPerSecond * DeltaTime);
		}
		if (CaptureLockoutTimeRemaining > 0.0f)
		{
			CaptureLockoutTimeRemaining = FMath::Max(0.0f, CaptureLockoutTimeRemaining - DeltaTime);
		}
		return;
	}

	// Update timer
	StateTimeRemaining -= DeltaTime;

	if (StateTimeRemaining <= 0.0f)
	{
		switch (CurrentState)
		{
		case EChargeAnimationState::HidingWeapon:
			// Mesh transition complete — switch meshes and start animation.
			// When bUseFPMontages: skip the legacy mesh swap + charge montage entirely.
			// FP weapon stays visible; catch/hold/throw montages handle the left arm.
			if (!bUseFPMontages)
			{
				SwitchToMeleeMesh();
				PlayChargeAnimation();
			}
			PlaySound(ChargeSound);
			OnChargeAnimationStarted.Broadcast();

			if (bChannelingPath)
			{
				// Channel path: immediately enter channeling
				EnterChanneling();
			}
			else
			{
				// Tap path: perform toggle and play animation
				PerformTapToggle();
				SpawnChargeVFX();
				SetState(EChargeAnimationState::Playing);
			}
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

		case EChargeAnimationState::ReverseChanneling:
			// Reverse channeling time expired — release NPC then cleanup
			ReleaseCapturedNPC();
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

bool UChargeAnimationComponent::IsBlockingFiring() const
{
	// Allow firing during Channeling and ReverseChanneling
	if (CurrentState == EChargeAnimationState::Channeling ||
	    CurrentState == EChargeAnimationState::ReverseChanneling)
	{
		return false;
	}

	// All other active phases block firing
	return IsAnimating();
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
	SetState(EChargeAnimationState::Ready);

	return true;
}

// ==================== Channeling ====================

void UChargeAnimationComponent::EnterChanneling()
{
	// Save the player's current charge sign
	if (CachedEMFModifier)
	{
		ChannelingChargeSign = CachedEMFModifier->GetChargeSign();
		if (ChannelingChargeSign == 0)
		{
			ChannelingChargeSign = 1; // Default to positive if neutral
		}
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
	}

	// Spawn the channeling plate with the same charge sign as the player
	SpawnPlate(ChannelingChargeSign);

	// Enable proxy mode on EMFVelocityModifier
	if (CachedEMFModifier)
	{
		CachedEMFModifier->SetChannelingProxyMode(true, ChannelingPlateActor);
	}

	if (bUsePressPressCaptureMode)
	{
		// Press-press mode: one synchronous capture scan. CurrentState is still HidingWeapon
		// at this point, so UpdateCaptureRaycast's press-press scan-skip does NOT apply yet
		// (it gates only when state is Channeling — see that function).
		FVector CamLoc;
		FRotator CamRot;
		if (GetCameraViewPoint(CamLoc, CamRot))
		{
			UpdateCaptureRaycast(CamLoc, CamRot);
		}

		if (CurrentCapturedNPC.IsValid())
		{
			// Self-contained pull targets (dropped weapons, upgrade/scripted pickups) have no
			// hold/throw phase — the pull manages itself in Tick and grants the item on its own.
			// Skip the Channeling hold state and wind down via the same path as "no target", so
			// the player returns to Ready without a second button press. ExitChanneling already
			// resets CurrentCapturedNPC for these types and lets the in-flight pull continue.
			AActor* Captured = CurrentCapturedNPC.Get();
			const bool bSelfContainedPull = Captured && (
				Cast<ADroppedMeleeWeapon>(Captured) ||
				Cast<ADroppedRangedWeapon>(Captured) ||
				Cast<AUpgradePickup>(Captured) ||
				Cast<AAbilityPickup>(Captured) ||
				Cast<AScriptedPickup>(Captured) ||
				Cast<ARiotShieldPickup>(Captured));

			if (bSelfContainedPull)
			{
				ExitChanneling();
				EnterFinishingAnimation();
				return;
			}

			// Capture succeeded — enter the same Channeling state the legacy hold-mode uses,
			// so the animation timeline is identical. The post-capture input lockout runs as
			// a timer inside Channeling (CaptureLockoutTimeRemaining), not a separate state.
			SetState(EChargeAnimationState::Channeling);
			CaptureLockoutTimeRemaining = CaptureToLaunchLockout;
			OnChannelingStarted.Broadcast();

			// FP-montage mode: free left hand IK so the slot/montage drives the left arm
			// (Control Rig sits AFTER the slot in AnimGraph, so it would otherwise yank the
			// hand back to the weapon grip socket and override the catch/hold/throw poses).
			if (bUseFPMontages && ShooterCharacter)
			{
				ShooterCharacter->SetLeftHandIKAlpha(0.0f);
			}

			// Trigger Catch montage on FP mesh — chains into Hold loop on natural end.
			PlayCatchMontage();
		}
		else
		{
			// No target — bail out via the standard finishing-animation cleanup path.
			ExitChanneling();
			EnterFinishingAnimation();
		}
		return;
	}

	// Legacy hold-mode: continuous scan happens via tick.
	SetState(EChargeAnimationState::Channeling);
	OnChannelingStarted.Broadcast();

	// Same IK-disable as press-press path — see comment there.
	if (bUseFPMontages && ShooterCharacter)
	{
		ShooterCharacter->SetLeftHandIKAlpha(0.0f);
	}

	// Legacy mode: kick off Catch on channeling entry; capture confirmation happens in tick.
	PlayCatchMontage();
}

void UChargeAnimationComponent::ExitChanneling()
{
	// Target stays in captured state (knockback) — it will be re-attached
	// to the reverse plate if player taps, or fully released on timeout.
	// Just clear the plate reference so weak ptr doesn't dangle.
	// Exception: AcceleratorPlate is fully released here (no reverse capture).
	if (CurrentCapturedNPC.IsValid())
	{
		if (AEMFAcceleratorPlate* AccelPlate = Cast<AEMFAcceleratorPlate>(CurrentCapturedNPC.Get()))
		{
			// AcceleratorPlate: fully release and freeze in place — no reverse capture
			AccelPlate->StopCapture();
			if (ChannelingPlateActor)
			{
				ChannelingPlateActor->ClearCapturedNPC();
			}
			CurrentCapturedNPC.Reset();
		}
		else if (AShooterNPC* NPC = Cast<AShooterNPC>(CurrentCapturedNPC.Get()))
		{
			if (UEMFVelocityModifier* Mod = NPC->FindComponentByClass<UEMFVelocityModifier>())
			{
				Mod->DetachFromPlate();
			}
		}
		else if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(CurrentCapturedNPC.Get()))
		{
			Prop->DetachFromPlate();
		}
		else if (ADroppedMeleeWeapon* DroppedWeapon = Cast<ADroppedMeleeWeapon>(CurrentCapturedNPC.Get()))
		{
			// DroppedMeleeWeapon pull is self-contained — let it finish on its own
			CurrentCapturedNPC.Reset();
		}
		else if (ADroppedRangedWeapon* DroppedRanged = Cast<ADroppedRangedWeapon>(CurrentCapturedNPC.Get()))
		{
			// DroppedRangedWeapon pull is self-contained — let it finish on its own
			CurrentCapturedNPC.Reset();
		}
		else if (AUpgradePickup* UPickup = Cast<AUpgradePickup>(CurrentCapturedNPC.Get()))
		{
			// UpgradePickup pull is self-contained — let it finish on its own
			CurrentCapturedNPC.Reset();
		}
		else if (AAbilityPickup* APickup = Cast<AAbilityPickup>(CurrentCapturedNPC.Get()))
		{
			// AbilityPickup pull is self-contained — let it finish on its own
			CurrentCapturedNPC.Reset();
		}
		else if (AScriptedPickup* SPickup = Cast<AScriptedPickup>(CurrentCapturedNPC.Get()))
		{
			// ScriptedPickup pull is self-contained — let it finish on its own
			CurrentCapturedNPC.Reset();
		}
		else if (ARiotShieldPickup* ShieldPickup = Cast<ARiotShieldPickup>(CurrentCapturedNPC.Get()))
		{
			// RiotShieldPickup pull is self-contained — let it finish on its own
			CurrentCapturedNPC.Reset();
		}
		if (ChannelingPlateActor)
		{
			ChannelingPlateActor->ClearCapturedNPC();
		}
	}

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
		return;
	}

	FVector CameraLoc = FVector::ZeroVector;
	FRotator CameraRot = FRotator::ZeroRotator;

	if (!GetCameraViewPoint(CameraLoc, CameraRot))
	{
		return;
	}

	ChannelingPlateActor->UpdateTransformFromCamera(CameraLoc, CameraRot, PlateOffset);

	// Update captured accelerator plate position (follows camera offset, not EMF forces)
	if (AEMFAcceleratorPlate* AccelPlate = Cast<AEMFAcceleratorPlate>(CurrentCapturedNPC.Get()))
	{
		AccelPlate->UpdateHoldPosition(CameraLoc, CameraRot);
	}

	// Raycast for capture target
	UpdateCaptureRaycast(CameraLoc, CameraRot);
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

// ==================== Capture ====================

void UChargeAnimationComponent::UpdateCaptureRaycast(const FVector& CameraLoc, const FRotator& CameraRot)
{
	if (!ChannelingPlateActor)
	{
		return;
	}

	// Check if current captured target is still valid and still captured
	if (CurrentCapturedNPC.IsValid())
	{
		// Check NPC
		if (AShooterNPC* NPC = Cast<AShooterNPC>(CurrentCapturedNPC.Get()))
		{
			UEMFVelocityModifier* Mod = NPC->FindComponentByClass<UEMFVelocityModifier>();
			if (Mod && Mod->IsCapturedByPlate())
			{
				return; // Still captured — don't re-search
			}
		}
		// Check Prop
		else if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(CurrentCapturedNPC.Get()))
		{
			if (Prop->IsCapturedByPlate())
			{
				return; // Still captured — don't re-search
			}
		}
		// Check AcceleratorPlate
		else if (AEMFAcceleratorPlate* AccelPlate = Cast<AEMFAcceleratorPlate>(CurrentCapturedNPC.Get()))
		{
			if (AccelPlate->IsCaptured())
			{
				return; // Still captured — don't re-search
			}
		}
		// Check DroppedMeleeWeapon (pull is self-contained — just check if still in progress)
		else if (ADroppedMeleeWeapon* DroppedWeapon = Cast<ADroppedMeleeWeapon>(CurrentCapturedNPC.Get()))
		{
			if (DroppedWeapon->IsBeingPulled())
			{
				return; // Still pulling — don't re-search
			}
		}
		// Check DroppedRangedWeapon (pull is self-contained — just check if still in progress)
		else if (ADroppedRangedWeapon* DroppedRanged = Cast<ADroppedRangedWeapon>(CurrentCapturedNPC.Get()))
		{
			if (DroppedRanged->IsBeingPulled())
			{
				return; // Still pulling — don't re-search
			}
		}
		// Check UpgradePickup (pull is self-contained — just check if still in progress)
		else if (AUpgradePickup* UPickup = Cast<AUpgradePickup>(CurrentCapturedNPC.Get()))
		{
			if (UPickup->IsBeingPulled())
			{
				return; // Still pulling — don't re-search
			}
		}
		// Check AbilityPickup (pull is self-contained — just check if still in progress)
		else if (AAbilityPickup* APickup = Cast<AAbilityPickup>(CurrentCapturedNPC.Get()))
		{
			if (APickup->IsBeingPulled())
			{
				return; // Still pulling — don't re-search
			}
		}
		// Check ScriptedPickup (pull is self-contained — just check if still in progress)
		else if (AScriptedPickup* SPickup = Cast<AScriptedPickup>(CurrentCapturedNPC.Get()))
		{
			if (SPickup->IsBeingPulled())
			{
				return; // Still pulling — don't re-search
			}
		}
		// Check RiotShieldPickup (pull is self-contained — just check if still in progress)
		else if (ARiotShieldPickup* ShieldPickup = Cast<ARiotShieldPickup>(CurrentCapturedNPC.Get()))
		{
			if (ShieldPickup->IsBeingPulled())
			{
				return; // Still pulling — don't re-search
			}
		}
		// Target was auto-released or is invalid — clear and search for new target
		CurrentCapturedNPC.Reset();
		if (ChannelingPlateActor)
		{
			ChannelingPlateActor->ClearCapturedNPC();
		}
	}

	// Press-press mode: skip the new-target scan once we're past the single-shot entry.
	// The initial capture scan happens from EnterChanneling while state is still HidingWeapon,
	// so this gate does NOT apply on entry — it only suppresses replacement scans during
	// the held window (Channeling). Lockout is now a timer inside Channeling, not a separate state.
	if (bUsePressPressCaptureMode && CurrentState == EChargeAnimationState::Channeling)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter)
	{
		return;
	}

	const FVector CameraForward = CameraRot.Vector();
	const float SearchRadiusSq = CaptureSearchRadius * CaptureSearchRadius;
	const FVector PlayerLoc = OwnerCharacter->GetActorLocation();

	// Adaptive cone: close objects get a wider acceptance angle because
	// small height differences create large angles when viewed from nearby.
	// At 0 cm → 90°, at NearFieldRadius → CaptureMaxAngle. Beyond that → CaptureMaxAngle.
	constexpr float NearFieldRadius = 500.0f;
	auto GetMaxAngleCosForDistance = [&](float Dist) -> float
	{
		const float T = FMath::Clamp(Dist / NearFieldRadius, 0.0f, 1.0f);
		const float EffectiveAngle = FMath::Lerp(90.0f, CaptureMaxAngle, T);
		return FMath::Cos(FMath::DegreesToRadians(EffectiveAngle));
	};

	// Find pawns, physics bodies, and world dynamic (for DroppedMeleeWeapon) in radius via overlap
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQuery.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);
	QueryParams.AddIgnoredActor(ChannelingPlateActor);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		PlayerLoc,
		FQuat::Identity,
		ObjectQuery,
		FCollisionShape::MakeSphere(CaptureSearchRadius),
		QueryParams
	);

	// Unified scoring: best target closest to crosshair
	enum class ECaptureTargetType { None, NPC, Prop, DroppedWeapon, DroppedRangedWeapon, UpgradePickup, AbilityPickup, ScriptedPickup, RiotShieldPickup, HumanoidWeapon, HumanoidShield };
	AActor* BestTarget = nullptr;
	float BestAngleCos = -1.0f; // worst possible (cos 180°)
	ECaptureTargetType BestTargetType = ECaptureTargetType::None;

	// Debug: log overlap count periodically
	{
		static int32 FrameCounter = 0;
		if (++FrameCounter % 60 == 0) // every ~1 sec at 60fps
		{
			int32 DroppedWeaponCount = 0;
			for (const FOverlapResult& O : Overlaps)
			{
				if (O.GetActor() && Cast<ADroppedMeleeWeapon>(O.GetActor()))
				{
					DroppedWeaponCount++;
				}
			}
			if (DroppedWeaponCount > 0 || Overlaps.Num() > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] Overlaps=%d, DroppedWeaponsInOverlap=%d, SearchRadius=%.0f"),
					Overlaps.Num(), DroppedWeaponCount, CaptureSearchRadius);
			}
		}
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor)
		{
			continue;
		}

		// Try NPC — HumanoidNPC is handled separately below (yank weapon, not body capture)
		if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
		{
			if (Cast<AHumanoidNPC>(NPC))
			{
				// Skip — handled in HumanoidNPC branch below
			}
			else
			{
				UEMFVelocityModifier* NPCModifier = NPC->FindComponentByClass<UEMFVelocityModifier>();
				if (!NPCModifier || (!NPCModifier->bEnableViscousCapture && !NPC->IsStunnedByExplosion()) || NPCModifier->IsCapturedByPlate())
				{
					continue;
				}

				// Charge validation: only capture NPCs with OPPOSITE charge sign
				// Neutral NPCs can't be captured, same-sign are repelled
				const float NPCCharge = NPCModifier->GetCharge();
				if (FMath::IsNearlyZero(NPCCharge) || NPCCharge * static_cast<float>(ChannelingChargeSign) > 0.0f)
				{
					continue;
				}

				const FVector ToTarget = NPC->GetActorLocation() - CameraLoc;
				const float DistSq = ToTarget.SizeSquared();
				if (DistSq > SearchRadiusSq || DistSq < 1.0f)
				{
					continue;
				}

				const FVector DirToTarget = ToTarget.GetUnsafeNormal();
				const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
				if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
				{
					continue;
				}

				if (AngleCos > BestAngleCos)
				{
					BestAngleCos = AngleCos;
					BestTarget = NPC;
					BestTargetType = ECaptureTargetType::NPC;
				}
				continue;
			}
		}

		// HumanoidNPC — yank shield first (if present), then weapon, instead of body capture
		if (AHumanoidNPC* Humanoid = Cast<AHumanoidNPC>(HitActor))
		{
			const bool bShieldYankable = Humanoid->CanShieldBeYanked();
			const bool bWeaponYankable = !bShieldYankable && Humanoid->CanBeYanked();
			if (!bShieldYankable && !bWeaponYankable) continue;

			// Require opposite charge sign (same rule as regular NPC capture)
			UEMFVelocityModifier* HumanoidMod = Humanoid->FindComponentByClass<UEMFVelocityModifier>();
			if (!HumanoidMod) continue;

			const float HumanoidCharge = HumanoidMod->GetCharge();
			if (FMath::IsNearlyZero(HumanoidCharge) || HumanoidCharge * static_cast<float>(ChannelingChargeSign) > 0.0f)
			{
				continue;
			}

			// Range: logarithmic, same formula as DroppedRangedWeapon (shield uses its own base/norm)
			const FVector ToTarget = Humanoid->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float YankRange = bShieldYankable ? Humanoid->CalculateShieldYankRange() : Humanoid->CalculateWeaponYankRange();
			if (YankRange < 1.0f || DistSq > YankRange * YankRange || DistSq < 1.0f) continue;

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq))) continue;

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = Humanoid;
				BestTargetType = bShieldYankable ? ECaptureTargetType::HumanoidShield : ECaptureTargetType::HumanoidWeapon;
			}
			continue;
		}

		// Try DroppedMeleeWeapon (same priority as props)
		if (ADroppedMeleeWeapon* DroppedWeapon = Cast<ADroppedMeleeWeapon>(HitActor))
		{
			if (!DroppedWeapon->bCanBeCaptured || DroppedWeapon->IsBeingPulled() || DroppedWeapon->IsPullComplete())
			{
				UE_LOG(LogTemp, Verbose, TEXT("[CaptureScan] DroppedWeapon %s skipped: bCanBeCaptured=%d, pulling=%d, pullComplete=%d"),
					*DroppedWeapon->GetName(), DroppedWeapon->bCanBeCaptured, DroppedWeapon->IsBeingPulled(), DroppedWeapon->IsPullComplete());
				continue;
			}

			// Charge validation: only capture weapons with OPPOSITE charge sign
			const float WeaponCharge = DroppedWeapon->GetCharge();
			if (FMath::IsNearlyZero(WeaponCharge) || WeaponCharge * static_cast<float>(ChannelingChargeSign) > 0.0f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedWeapon %s REJECTED charge: WeaponCharge=%.2f, ChannelingSign=%d (need opposite)"),
					*DroppedWeapon->GetName(), WeaponCharge, static_cast<int32>(ChannelingChargeSign));
				continue;
			}

			// Range check using weapon's own logarithmic capture range
			const FVector ToTarget = DroppedWeapon->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float CaptureRange = DroppedWeapon->CalculateCaptureRange();
			if (DistSq > CaptureRange * CaptureRange || DistSq < 1.0f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedWeapon %s OUT OF RANGE: dist=%.0f, captureRange=%.0f"),
					*DroppedWeapon->GetName(), FMath::Sqrt(DistSq), CaptureRange);
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			const float AdaptedMaxAngleCosWeapon = GetMaxAngleCosForDistance(FMath::Sqrt(DistSq));
			if (AngleCos < AdaptedMaxAngleCosWeapon)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedWeapon %s OUT OF ANGLE: cos=%.2f, minCos=%.2f"),
					*DroppedWeapon->GetName(), AngleCos, AdaptedMaxAngleCosWeapon);
				continue;
			}

			UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedWeapon %s VALID TARGET: charge=%.2f, dist=%.0f/%.0f, angle=%.2f"),
				*DroppedWeapon->GetName(), WeaponCharge, FMath::Sqrt(DistSq), CaptureRange, AngleCos);

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = DroppedWeapon;
				BestTargetType = ECaptureTargetType::DroppedWeapon;
			}
			continue;
		}

		// Try DroppedRangedWeapon (same priority as DroppedMeleeWeapon/props)
		if (ADroppedRangedWeapon* DroppedRanged = Cast<ADroppedRangedWeapon>(HitActor))
		{
			if (!DroppedRanged->bCanBeCaptured || DroppedRanged->IsBeingPulled() || DroppedRanged->IsPullComplete())
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedRangedWeapon %s skipped: bCanBeCaptured=%d, pulling=%d, pullComplete=%d"),
					*DroppedRanged->GetName(), DroppedRanged->bCanBeCaptured, DroppedRanged->IsBeingPulled(), DroppedRanged->IsPullComplete());
				continue;
			}

			// Charge validation: only capture weapons with OPPOSITE charge sign
			const float RangedCharge = DroppedRanged->GetCharge();
			if (FMath::IsNearlyZero(RangedCharge) || RangedCharge * static_cast<float>(ChannelingChargeSign) > 0.0f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedRangedWeapon %s REJECTED charge: Charge=%.2f, ChannelingSign=%d (need opposite)"),
					*DroppedRanged->GetName(), RangedCharge, static_cast<int32>(ChannelingChargeSign));
				continue;
			}

			// Range check using weapon's own logarithmic capture range
			const FVector ToTarget = DroppedRanged->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float CaptureRange = DroppedRanged->CalculateCaptureRange();
			if (DistSq > CaptureRange * CaptureRange || DistSq < 1.0f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedRangedWeapon %s OUT OF RANGE: dist=%.0f, captureRange=%.0f"),
					*DroppedRanged->GetName(), FMath::Sqrt(DistSq), CaptureRange);
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			const float AdaptedMaxAngleCosRanged = GetMaxAngleCosForDistance(FMath::Sqrt(DistSq));
			if (AngleCos < AdaptedMaxAngleCosRanged)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedRangedWeapon %s OUT OF ANGLE: cos=%.2f, minCos=%.2f"),
					*DroppedRanged->GetName(), AngleCos, AdaptedMaxAngleCosRanged);
				continue;
			}

			UE_LOG(LogTemp, Warning, TEXT("[CaptureScan] DroppedRangedWeapon %s VALID TARGET: charge=%.2f, dist=%.0f/%.0f, angle=%.2f"),
				*DroppedRanged->GetName(), RangedCharge, FMath::Sqrt(DistSq), CaptureRange, AngleCos);

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = DroppedRanged;
				BestTargetType = ECaptureTargetType::DroppedRangedWeapon;
			}
			continue;
		}

		// Try UpgradePickup (same priority as DroppedWeapon/props)
		if (AUpgradePickup* UPickup = Cast<AUpgradePickup>(HitActor))
		{
			if (!UPickup->bCanBeCaptured || UPickup->IsBeingPulled() || UPickup->IsPullComplete())
			{
				continue;
			}

			// Upgrade pickups can be captured regardless of charge sign,
			// but must have non-zero charge for range calculation
			const float PickupCharge = UPickup->GetCharge();
			if (FMath::IsNearlyZero(PickupCharge))
			{
				continue;
			}

			// Range check using pickup's own logarithmic capture range
			const FVector ToTarget = UPickup->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float CaptureRange = UPickup->CalculateCaptureRange();
			if (DistSq > CaptureRange * CaptureRange || DistSq < 1.0f)
			{
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
			{
				continue;
			}

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = UPickup;
				BestTargetType = ECaptureTargetType::UpgradePickup;
			}
			continue;
		}

		// Try AbilityPickup (same logic as UpgradePickup, grants ability instead of upgrade)
		if (AAbilityPickup* APickup = Cast<AAbilityPickup>(HitActor))
		{
			if (!APickup->bCanBeCaptured || APickup->IsBeingPulled() || APickup->IsPullComplete())
			{
				continue;
			}

			const float PickupCharge = APickup->GetCharge();
			if (FMath::IsNearlyZero(PickupCharge))
			{
				continue;
			}

			const FVector ToTarget = APickup->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float CaptureRange = APickup->CalculateCaptureRange();
			if (DistSq > CaptureRange * CaptureRange || DistSq < 1.0f)
			{
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
			{
				continue;
			}

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = APickup;
				BestTargetType = ECaptureTargetType::AbilityPickup;
			}
			continue;
		}

		// Try ScriptedPickup (same logic as UpgradePickup)
		if (AScriptedPickup* SPickup = Cast<AScriptedPickup>(HitActor))
		{
			if (!SPickup->bCanBeCaptured || SPickup->IsBeingPulled() || SPickup->IsPullComplete())
			{
				continue;
			}

			const float PickupCharge = SPickup->GetCharge();
			if (FMath::IsNearlyZero(PickupCharge))
			{
				continue;
			}

			const FVector ToTarget = SPickup->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float CaptureRange = SPickup->CalculateCaptureRange();
			if (DistSq > CaptureRange * CaptureRange || DistSq < 1.0f)
			{
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
			{
				continue;
			}

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = SPickup;
				BestTargetType = ECaptureTargetType::ScriptedPickup;
			}
			continue;
		}

		// Try RiotShieldPickup — fixed-range capture, no charge-sign requirement (shield can be charge-less).
		if (ARiotShieldPickup* ShieldPickup = Cast<ARiotShieldPickup>(HitActor))
		{
			if (!ShieldPickup->bCanBeCaptured || ShieldPickup->IsBeingPulled())
			{
				continue;
			}

			const FVector ToTarget = ShieldPickup->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			const float ShieldCaptureRange = ShieldPickup->CaptureRange;
			if (DistSq > ShieldCaptureRange * ShieldCaptureRange || DistSq < 1.0f)
			{
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
			{
				continue;
			}

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = ShieldPickup;
				BestTargetType = ECaptureTargetType::RiotShieldPickup;
			}
			continue;
		}

		// Try Physics Prop
		if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(HitActor))
		{
			if (!Prop->bCanBeCaptured || Prop->IsCapturedByPlate() || Prop->IsDead())
			{
				continue;
			}

			const FVector ToTarget = Prop->GetActorLocation() - CameraLoc;
			const float DistSq = ToTarget.SizeSquared();
			if (DistSq > SearchRadiusSq || DistSq < 1.0f)
			{
				continue;
			}

			const FVector DirToTarget = ToTarget.GetUnsafeNormal();
			const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
			if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
			{
				continue;
			}

			if (AngleCos > BestAngleCos)
			{
				BestAngleCos = AngleCos;
				BestTarget = Prop;
				BestTargetType = ECaptureTargetType::Prop;
			}
			continue;
		}
	}

	if (BestTarget)
	{
		switch (BestTargetType)
		{
		case ECaptureTargetType::NPC:
			CaptureNPC(Cast<AShooterNPC>(BestTarget));
			break;
		case ECaptureTargetType::Prop:
			CaptureProp(Cast<AEMFPhysicsProp>(BestTarget));
			break;
		case ECaptureTargetType::DroppedWeapon:
			CaptureDroppedWeapon(Cast<ADroppedMeleeWeapon>(BestTarget));
			break;
		case ECaptureTargetType::DroppedRangedWeapon:
			CaptureDroppedRangedWeapon(Cast<ADroppedRangedWeapon>(BestTarget));
			break;
		case ECaptureTargetType::UpgradePickup:
			CaptureUpgradePickup(Cast<AUpgradePickup>(BestTarget));
			break;
		case ECaptureTargetType::AbilityPickup:
			CaptureAbilityPickup(Cast<AAbilityPickup>(BestTarget));
			break;
		case ECaptureTargetType::ScriptedPickup:
			CaptureScriptedPickup(Cast<AScriptedPickup>(BestTarget));
			break;
		case ECaptureTargetType::RiotShieldPickup:
			CaptureRiotShieldPickup(Cast<ARiotShieldPickup>(BestTarget));
			break;
		case ECaptureTargetType::HumanoidWeapon:
			CaptureHumanoidWeapon(Cast<AHumanoidNPC>(BestTarget));
			break;
		case ECaptureTargetType::HumanoidShield:
			CaptureHumanoidShield(Cast<AHumanoidNPC>(BestTarget));
			break;
		default:
			break;
		}
		return;
	}

	// Lowest priority: scan for AcceleratorPlates only if no NPC/Prop was found.
	// Uses actor iterator since AcceleratorPlates may not have collision primitives.
	// No charge dependency — purely distance + angle based.
	AEMFAcceleratorPlate* BestAccelPlate = nullptr;
	float BestAccelAngleCos = -1.0f;

	for (TActorIterator<AEMFAcceleratorPlate> It(World); It; ++It)
	{
		AEMFAcceleratorPlate* AccelPlate = *It;
		if (!AccelPlate || !AccelPlate->bCanBeCaptured || AccelPlate->IsCaptured())
		{
			continue;
		}

		const FVector ToTarget = AccelPlate->GetActorLocation() - CameraLoc;
		const float DistSq = ToTarget.SizeSquared();
		if (DistSq > SearchRadiusSq || DistSq < 1.0f)
		{
			continue;
		}

		const FVector DirToTarget = ToTarget.GetUnsafeNormal();
		const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
		if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq)))
		{
			continue;
		}

		if (AngleCos > BestAccelAngleCos)
		{
			BestAccelAngleCos = AngleCos;
			BestAccelPlate = AccelPlate;
		}
	}

	if (BestAccelPlate)
	{
		CaptureAcceleratorPlate(BestAccelPlate);
	}
}

void UChargeAnimationComponent::CaptureNPC(AShooterNPC* NPC)
{
	if (!NPC || !ChannelingPlateActor)
	{
		return;
	}

	// Release previous if any
	ReleaseCapturedNPC();

	CurrentCapturedNPC = NPC;
	ChannelingPlateActor->SetCapturedNPC(NPC);

	if (UEMFVelocityModifier* Modifier = NPC->FindComponentByClass<UEMFVelocityModifier>())
	{
		// Enable viscous capture for stunned NPCs that don't normally have it
		if (!Modifier->bEnableViscousCapture && NPC->IsStunnedByExplosion())
		{
			Modifier->bEnableViscousCapture = true;
			NPC->SetCaptureEnabledByStun(true);
		}
		Modifier->SetCapturedByPlate(ChannelingPlateActor);
	}

	// VFX and delegate
	SpawnCaptureVFX(NPC);
	SpawnHoldVFX();
	if (ShooterCharacter)
	{
		ShooterCharacter->OnPropCaptured.Broadcast(NPC);
	}
}

void UChargeAnimationComponent::CaptureHumanoidWeapon(AHumanoidNPC* Humanoid)
{
	if (!Humanoid) return;

	// Yank is a one-shot action — no hold state, so release any previous capture first
	ReleaseCapturedNPC();

	// Match prop-capture VFX feedback: beam from plate to humanoid + hold aura on socket.
	// Beam will freeze at humanoid's spawn location since CurrentCapturedNPC is not set
	// (UpdateBeamVFX only re-targets when CurrentCapturedNPC is valid). Acceptable for yank —
	// reads as a short discharge, and the dropped weapon flies on its own.
	SpawnCaptureVFX(Humanoid);
	SpawnHoldVFX();

	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		// Strict rule: at most one yanked weapon in inventory. If the player already has one,
		// throw it away as a non-capturable physics decoration BEFORE starting the new yank.
		// If the discarded weapon was equipped, switch to a non-yanked replacement instantly
		// so the subsequent BeginWeaponLower has something concrete to lower.
		ShooterChar->ThrowYankedWeaponIfAny();

		// Start lowering player's current weapon NOW (in parallel with the pull flight).
		// Lower (~0.15s) completes well before pull arrival (~0.4s), so mesh waits at the
		// bottom. When the dropped weapon arrives via CompletePull → AddWeaponClassAnimated
		// detects the paused-at-bottom switch and calls FinishWeaponSwitch — instant
		// off-camera swap + raise of new weapon. No-op if player is unarmed.
		ShooterChar->BeginWeaponLower();

		// Broadcast capture event — cancels Upgrade_HealthBlast's empty-capture timer
		// (yank IS a successful capture, so the timer should not fire).
		ShooterChar->OnPropCaptured.Broadcast(Humanoid);

		Humanoid->YankCurrentWeapon(ShooterChar);
	}

	// CurrentCapturedNPC intentionally NOT set — yank has no hold phase
}

void UChargeAnimationComponent::CaptureHumanoidShield(AHumanoidNPC* Humanoid)
{
	if (!Humanoid) return;

	// Yank is a one-shot action — no hold state, so release any previous capture first
	ReleaseCapturedNPC();

	// Same VFX feedback as weapon-yank: beam to humanoid + plate aura
	SpawnCaptureVFX(Humanoid);
	SpawnHoldVFX();

	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		// Broadcast capture event so HealthBlast-style empty-capture timers cancel
		ShooterChar->OnPropCaptured.Broadcast(Humanoid);

		Humanoid->YankShield(ShooterChar);
	}

	// CurrentCapturedNPC intentionally NOT set — yank has no hold phase
}

void UChargeAnimationComponent::CaptureProp(AEMFPhysicsProp* Prop)
{
	if (!Prop || !ChannelingPlateActor)
	{
		return;
	}

	// Charge validation: only capture charged props with OPPOSITE sign
	// Neutral props can't be captured (no EM interaction), same-sign are repelled
	const float PropCharge = Prop->GetCharge();
	if (FMath::IsNearlyZero(PropCharge) || PropCharge * static_cast<float>(ChannelingChargeSign) > 0.0f)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	CurrentCapturedNPC = Prop;
	ChannelingPlateActor->SetCapturedNPC(Prop);
	Prop->SetCapturedByPlate(ChannelingPlateActor);

	// VFX and delegate
	SpawnCaptureVFX(Prop);
	SpawnHoldVFX();
	if (ShooterCharacter)
	{
		ShooterCharacter->OnPropCaptured.Broadcast(Prop);
	}
}

void UChargeAnimationComponent::CaptureDroppedWeapon(ADroppedMeleeWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	// Start scripted pull (weapon manages its own interpolation in Tick)
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		Weapon->StartPull(ShooterChar);
	}

	// Track as current target to prevent re-search
	CurrentCapturedNPC = Weapon;
}

void UChargeAnimationComponent::CaptureDroppedRangedWeapon(ADroppedRangedWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	// Start scripted pull (weapon manages its own interpolation in Tick)
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		Weapon->StartPull(ShooterChar);
	}

	// Track as current target to prevent re-search
	CurrentCapturedNPC = Weapon;
}

void UChargeAnimationComponent::CaptureUpgradePickup(AUpgradePickup* Pickup)
{
	if (!Pickup)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	// Start scripted pull (pickup manages its own interpolation in Tick)
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		Pickup->StartPull(ShooterChar);
	}

	// Track as current target to prevent re-search
	CurrentCapturedNPC = Pickup;
}

void UChargeAnimationComponent::CaptureAbilityPickup(AAbilityPickup* Pickup)
{
	if (!Pickup)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	// Start scripted pull (pickup manages its own interpolation in Tick)
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		Pickup->StartPull(ShooterChar);
	}

	// Track as current target to prevent re-search
	CurrentCapturedNPC = Pickup;
}

void UChargeAnimationComponent::CaptureScriptedPickup(AScriptedPickup* Pickup)
{
	if (!Pickup)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	// Start scripted pull (pickup manages its own interpolation in Tick)
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		Pickup->StartPull(ShooterChar);
	}

	// Track as current target to prevent re-search
	CurrentCapturedNPC = Pickup;
}

void UChargeAnimationComponent::CaptureRiotShieldPickup(ARiotShieldPickup* Pickup)
{
	if (!Pickup)
	{
		return;
	}

	// Release previous target if any
	ReleaseCapturedNPC();

	// Start scripted pull (pickup manages its own interpolation + equip in Tick)
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (ShooterChar)
	{
		Pickup->StartPull(ShooterChar);
	}

	// Track as current target to prevent re-search
	CurrentCapturedNPC = Pickup;
}

void UChargeAnimationComponent::CaptureAcceleratorPlate(AEMFAcceleratorPlate* Plate)
{
	if (!Plate || !ChannelingPlateActor)
	{
		return;
	}

	// No charge validation — AcceleratorPlate capture is charge-independent

	// Release previous target if any
	ReleaseCapturedNPC();

	CurrentCapturedNPC = Plate;
	ChannelingPlateActor->SetCapturedNPC(Plate);
	Plate->StartCapture();
}

void UChargeAnimationComponent::ReleaseCapturedNPC()
{
	StopHoldVFX();

	// Stop beam VFX — target is being released
	if (ActiveCaptureVFX)
	{
		ActiveCaptureVFX->DeactivateImmediate();
		ActiveCaptureVFX = nullptr;
	}

	if (!CurrentCapturedNPC.IsValid())
	{
		return;
	}

	if (AShooterNPC* NPC = Cast<AShooterNPC>(CurrentCapturedNPC.Get()))
	{
		if (UEMFVelocityModifier* Modifier = NPC->FindComponentByClass<UEMFVelocityModifier>())
		{
			Modifier->ReleasedFromCapture();
		}
	}
	else if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(CurrentCapturedNPC.Get()))
	{
		Prop->ReleasedFromCapture();
	}
	else if (AEMFAcceleratorPlate* AccelPlate = Cast<AEMFAcceleratorPlate>(CurrentCapturedNPC.Get()))
	{
		AccelPlate->StopCapture();
	}
	else if (Cast<ADroppedMeleeWeapon>(CurrentCapturedNPC.Get()))
	{
		// DroppedMeleeWeapon pull is self-contained — no release action needed
	}
	else if (Cast<ADroppedRangedWeapon>(CurrentCapturedNPC.Get()))
	{
		// DroppedRangedWeapon pull is self-contained — no release action needed
	}
	else if (Cast<AUpgradePickup>(CurrentCapturedNPC.Get()))
	{
		// UpgradePickup pull is self-contained — no release action needed
	}
	else if (Cast<AAbilityPickup>(CurrentCapturedNPC.Get()))
	{
		// AbilityPickup pull is self-contained — no release action needed
	}
	else if (Cast<AScriptedPickup>(CurrentCapturedNPC.Get()))
	{
		// ScriptedPickup pull is self-contained — no release action needed
	}

	if (ChannelingPlateActor)
	{
		ChannelingPlateActor->ClearCapturedNPC();
	}

	CurrentCapturedNPC.Reset();
}

void UChargeAnimationComponent::PerformTapToggle()
{
	if (CachedEMFModifier)
	{
		CachedEMFModifier->ToggleChargeSign();
	}
}

void UChargeAnimationComponent::BeginLaunch()
{
	// Trigger Throw montage on FP mesh BEFORE plate teardown so the gesture starts immediately
	// when the player presses the launch button (most responsive feel).
	PlayThrowMontage();

	// Tear down the current "hold" plate (proxy mode disabled, plate destroyed).
	ExitChanneling();

	// Deduct fixed charge cost for the reverse channeling burst
	if (CachedEMFModifier && ReverseChannelingChargeCost > 0.0f)
	{
		CachedEMFModifier->DeductCharge(ReverseChannelingChargeCost);
	}

	// Spawn a fresh plate with the OPPOSITE charge sign — this inversion is what produces
	// the same-sign repulsion against the held target and propels it forward.
	SpawnPlate(-ChannelingChargeSign);

	// Flip player's charge sign — gated upstream by EMFVelocityModifier::bAllowPolarityToggle.
	// In simplified mode (player BP sets bAllowPolarityToggle = false) this call no-ops, so
	// the plate inversion alone drives the launch and the player visibly stays positive.
	// In legacy mode the flip still happens, preserving the original behavior.
	if (CachedEMFModifier)
	{
		CachedEMFModifier->ToggleChargeSign();
	}

	// Mark plate as reverse (tangential-only damping) so the held target slides off cleanly.
	if (ChannelingPlateActor)
	{
		ChannelingPlateActor->SetReverseMode(true);
	}

	// Launch VFX + delegate (after plate inversion is in place)
	if (CurrentCapturedNPC.IsValid())
	{
		SpawnLaunchVFX(CurrentCapturedNPC.Get());
		StopHoldVFX();
		if (ShooterCharacter)
		{
			ShooterCharacter->OnPropLaunched.Broadcast(CurrentCapturedNPC.Get());
		}
	}

	// Re-attach the captured target to the new (reverse) plate so the same-sign field
	// pushes it forward instead of letting it fall.
	if (CurrentCapturedNPC.IsValid() && ChannelingPlateActor)
	{
		if (AShooterNPC* NPC = Cast<AShooterNPC>(CurrentCapturedNPC.Get()))
		{
			if (UEMFVelocityModifier* Mod = NPC->FindComponentByClass<UEMFVelocityModifier>())
			{
				Mod->SetCapturedByPlate(ChannelingPlateActor);
			}
			ChannelingPlateActor->SetCapturedNPC(NPC);
		}
		else if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(CurrentCapturedNPC.Get()))
		{
			Prop->SetCapturedByPlate(ChannelingPlateActor);
			ChannelingPlateActor->SetCapturedNPC(Prop);
		}
	}

	// Re-enable proxy mode against the new plate
	if (CachedEMFModifier)
	{
		CachedEMFModifier->SetChannelingProxyMode(true, ChannelingPlateActor);
	}

	SetState(EChargeAnimationState::ReverseChanneling);
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
	// Stop any FP montages (catch/hold/throw) — safety net for cancel/EndPlay paths.
	StopFPMontages();

	// Force FP alpha back to 0 with throw blend-out time — handles cancel paths where
	// OnThrowMontageEnded didn't get to fire.
	if (ShooterCharacter)
	{
		ShooterCharacter->SetFPMontageAlpha(0.0f, ThrowAlphaBlendOut);
	}

	// Release any captured NPC first
	ReleaseCapturedNPC();

	// Safety: stop any lingering beam VFX
	if (ActiveLaunchVFX)
	{
		ActiveLaunchVFX->DeactivateImmediate();
		ActiveLaunchVFX = nullptr;
	}

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

	// Don't restore FP mesh if melee weapon is equipped — MeleeWeaponFPMesh is used instead
	if (ShooterCharacter)
	{
		AShooterWeapon* CurrentWeapon = ShooterCharacter->GetCurrentWeapon();
		if (CurrentWeapon && CurrentWeapon->IsMeleeWeapon())
		{
			return;
		}
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

// ==================== FP Montages (catch/hold/throw) ====================

void UChargeAnimationComponent::PlayCatchMontage()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] PlayCatchMontage CALLED. FirstPersonMesh=%s CatchMontage=%s"),
		FirstPersonMesh ? *FirstPersonMesh->GetName() : TEXT("nullptr"),
		CatchMontage ? *CatchMontage->GetName() : TEXT("nullptr"));

	if (!FirstPersonMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayCatchMontage EARLY RETURN: FirstPersonMesh is null"));
		return;
	}

	// No catch montage assigned — fall through directly to hold so the loop still starts.
	if (!CatchMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] PlayCatchMontage: no CatchMontage, fallback to PlayHoldMontage"));
		PlayHoldMontage();
		return;
	}

	UAnimInstance* AnimInstance = FirstPersonMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayCatchMontage EARLY RETURN: AnimInstance is null on FirstPersonMesh (anim class=%s)"),
			FirstPersonMesh->GetAnimClass() ? *FirstPersonMesh->GetAnimClass()->GetName() : TEXT("none"));
		return;
	}

	float Duration = AnimInstance->Montage_Play(CatchMontage);
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] Montage_Play(CatchMontage) returned duration=%.3f (0=failed). AnimInstance class=%s"),
		Duration, *AnimInstance->GetClass()->GetName());

	// Catch is a left-arm-only animation (Slot 'LeftArm' + LBPB mask on upperarm_l).
	// Does NOT trigger FPMontageAlpha — only two-hand class animations (Throw) do that.

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UChargeAnimationComponent::OnCatchMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, CatchMontage);

	// Overlap catch → hold the same way hold loops itself: schedule hold start
	// HoldLoopOverlap seconds BEFORE catch ends. Reuses the existing HoldLoopTimerHandle
	// (we don't need both timers active at once — catch fires first, then hold takes over).
	// UE crossfades the two via Slot's Blend In/Out → no rest-pose snap between them.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldLoopTimerHandle);
		const float Length = CatchMontage->GetPlayLength();
		const float Overlap = FMath::Min(HoldLoopOverlap, Length * 0.5f);
		const float Delay = FMath::Max(0.05f, Length - Overlap);
		World->GetTimerManager().SetTimer(HoldLoopTimerHandle, this, &UChargeAnimationComponent::OnHoldLoopTimer, Delay, false);
	}
}

void UChargeAnimationComponent::PlayHoldMontage()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] PlayHoldMontage CALLED. FirstPersonMesh=%s HoldMontage=%s"),
		FirstPersonMesh ? *FirstPersonMesh->GetName() : TEXT("nullptr"),
		HoldMontage ? *HoldMontage->GetName() : TEXT("nullptr"));

	if (!FirstPersonMesh || !HoldMontage)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayHoldMontage EARLY RETURN: missing FP mesh or HoldMontage"));
		return;
	}

	UAnimInstance* AnimInstance = FirstPersonMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayHoldMontage EARLY RETURN: AnimInstance is null"));
		return;
	}

	float Duration = AnimInstance->Montage_Play(HoldMontage);
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] Montage_Play(HoldMontage) returned duration=%.3f"), Duration);

	// Hold is a left-arm-only animation, same class as Catch — does NOT trigger FPMontageAlpha.

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UChargeAnimationComponent::OnHoldMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, HoldMontage);

	// Schedule overlap-restart: timer fires HoldLoopOverlap seconds BEFORE the montage ends,
	// triggering another Montage_Play. UE's natural Blend In on the new instance + Blend Out
	// on the old one produces a smooth crossfade — no rest-pose snap between iterations.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldLoopTimerHandle);
		const float Length = HoldMontage->GetPlayLength();
		const float Overlap = FMath::Min(HoldLoopOverlap, Length * 0.5f);
		const float Delay = FMath::Max(0.05f, Length - Overlap);
		World->GetTimerManager().SetTimer(HoldLoopTimerHandle, this, &UChargeAnimationComponent::OnHoldLoopTimer, Delay, false);
	}
}

void UChargeAnimationComponent::OnHoldLoopTimer()
{
	// Continue looping if we're still channeling a captured prop, OR if an external system
	// (RiotShield) has the FP montages reserved. Throw / cancel clears one of these flags.
	if (CurrentState == EChargeAnimationState::Channeling || bExternalHoldActive)
	{
		PlayHoldMontage();
	}
}

// ==================== External hold (RiotShield) ====================

void UChargeAnimationComponent::PlayShieldCatchAndHold()
{
	bExternalHoldActive = true;
	// PlayCatchMontage already handles the catch→hold-loop crossfade and timer scheduling.
	// If CatchMontage is null it falls back to PlayHoldMontage directly, which also schedules the loop.
	PlayCatchMontage();
}

void UChargeAnimationComponent::PlayShieldThrow()
{
	bExternalHoldActive = false;
	// PlayThrowMontage already kills HoldLoopTimerHandle and stops Catch/Hold if playing.
	PlayThrowMontage();
}

void UChargeAnimationComponent::StopShieldFPMontages()
{
	bExternalHoldActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldLoopTimerHandle);
	}
	StopFPMontages();
}

void UChargeAnimationComponent::PlayThrowMontage()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] PlayThrowMontage CALLED. FirstPersonMesh=%s ThrowMontage=%s"),
		FirstPersonMesh ? *FirstPersonMesh->GetName() : TEXT("nullptr"),
		ThrowMontage ? *ThrowMontage->GetName() : TEXT("nullptr"));

	// Throw interrupts the hold loop — kill the overlap timer so it doesn't re-kick hold.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldLoopTimerHandle);
	}

	if (!FirstPersonMesh || !ThrowMontage)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayThrowMontage EARLY RETURN: missing FP mesh or ThrowMontage"));
		return;
	}

	UAnimInstance* AnimInstance = FirstPersonMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayThrowMontage EARLY RETURN: AnimInstance is null"));
		return;
	}

	// Interrupt catch/hold if either is currently playing — throw replaces them.
	if (CatchMontage && AnimInstance->Montage_IsPlaying(CatchMontage))
	{
		AnimInstance->Montage_Stop(0.0f, CatchMontage);
	}
	if (HoldMontage && AnimInstance->Montage_IsPlaying(HoldMontage))
	{
		AnimInstance->Montage_Stop(0.0f, HoldMontage);
	}

	float Duration = AnimInstance->Montage_Play(ThrowMontage);
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] Montage_Play(ThrowMontage) returned duration=%.3f"), Duration);

	// Drive alpha → 1 with throw blend-in. Bind end callback so we can blend out → 0
	// when throw ends (yank-throw flow doesn't go through ShowingWeapon/CleanupChanneling).
	if (ShooterCharacter)
	{
		ShooterCharacter->SetFPMontageAlpha(1.0f, ThrowAlphaBlendIn);
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UChargeAnimationComponent::OnThrowMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, ThrowMontage);
}

void UChargeAnimationComponent::OnThrowMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] OnThrowMontageEnded: bInterrupted=%d"), bInterrupted ? 1 : 0);

	// Blend alpha back to 0 with throw's blend-out time.
	// This is the channeling-throw montage end (BeginLaunch flow).
	if (ShooterCharacter)
	{
		ShooterCharacter->SetFPMontageAlpha(0.0f, ThrowAlphaBlendOut);
	}
}

void UChargeAnimationComponent::PlayYankThrowMontage()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] PlayYankThrowMontage CALLED. FirstPersonMesh=%s YankThrowMontage=%s"),
		FirstPersonMesh ? *FirstPersonMesh->GetName() : TEXT("nullptr"),
		YankThrowMontage ? *YankThrowMontage->GetName() : TEXT("nullptr"));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldLoopTimerHandle);
	}

	if (!FirstPersonMesh || !YankThrowMontage)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayYankThrowMontage EARLY RETURN: missing FP mesh or YankThrowMontage"));
		return;
	}

	UAnimInstance* AnimInstance = FirstPersonMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGE_ANIM] PlayYankThrowMontage EARLY RETURN: AnimInstance is null"));
		return;
	}

	// Interrupt catch/hold if playing — yank-throw takes priority on the FP arms slot.
	if (CatchMontage && AnimInstance->Montage_IsPlaying(CatchMontage))
	{
		AnimInstance->Montage_Stop(0.0f, CatchMontage);
	}
	if (HoldMontage && AnimInstance->Montage_IsPlaying(HoldMontage))
	{
		AnimInstance->Montage_Stop(0.0f, HoldMontage);
	}

	float Duration = AnimInstance->Montage_Play(YankThrowMontage);
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] Montage_Play(YankThrowMontage) returned duration=%.3f"), Duration);

	// Drive FPMontageAlpha → 1 with yank-throw's own blend-in time.
	if (ShooterCharacter)
	{
		ShooterCharacter->SetFPMontageAlpha(1.0f, YankThrowAlphaBlendIn);
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UChargeAnimationComponent::OnYankThrowMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, YankThrowMontage);
}

void UChargeAnimationComponent::OnYankThrowMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] OnYankThrowMontageEnded: bInterrupted=%d"), bInterrupted ? 1 : 0);

	// Reset FPMontageAlpha → 0 using yank-throw's own blend-out time.
	if (ShooterCharacter)
	{
		ShooterCharacter->SetFPMontageAlpha(0.0f, YankThrowAlphaBlendOut);
	}
}

void UChargeAnimationComponent::StopFPMontages()
{
	// Always cancel the hold-loop overlap timer — even if mesh/anim is gone, we don't want
	// a stale timer firing later.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldLoopTimerHandle);
	}

	if (!FirstPersonMesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = FirstPersonMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	if (CatchMontage && AnimInstance->Montage_IsPlaying(CatchMontage))
	{
		AnimInstance->Montage_Stop(0.1f, CatchMontage);
	}
	if (HoldMontage && AnimInstance->Montage_IsPlaying(HoldMontage))
	{
		AnimInstance->Montage_Stop(0.1f, HoldMontage);
	}
	if (ThrowMontage && AnimInstance->Montage_IsPlaying(ThrowMontage))
	{
		AnimInstance->Montage_Stop(0.1f, ThrowMontage);
	}
}

void UChargeAnimationComponent::OnCatchMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] OnCatchMontageEnded: bInterrupted=%d state=%d"),
		bInterrupted ? 1 : 0, (int32)CurrentState);

	// Chain into hold loop only if catch ended naturally (not interrupted by throw / cleanup)
	// AND we're still in Channeling state (otherwise the player has already moved on).
	if (!bInterrupted && CurrentState == EChargeAnimationState::Channeling)
	{
		PlayHoldMontage();
	}
}

void UChargeAnimationComponent::OnHoldMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_ANIM] OnHoldMontageEnded: bInterrupted=%d state=%d"),
		bInterrupted ? 1 : 0, (int32)CurrentState);

	// Re-play hold for loop continuation. Same gating as catch: state must still be Channeling.
	if (!bInterrupted && CurrentState == EChargeAnimationState::Channeling)
	{
		PlayHoldMontage();
	}
}

void UChargeAnimationComponent::UpdateMontagePlayRate(float DeltaTime)
{
	if (!CurrentMontage || !MeleeMesh)
	{
		return;
	}

	// Don't override play rate during channeling freeze
	if (CurrentState == EChargeAnimationState::Channeling ||
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

// ==================== Channeling VFX ====================

void UChargeAnimationComponent::SpawnCaptureVFX(AActor* CapturedTarget)
{
	if (!CapturedTarget || !ChannelingPlateActor)
	{
		return;
	}

	// Stop previous capture VFX if still active
	if (ActiveCaptureVFX)
	{
		ActiveCaptureVFX->DeactivateImmediate();
		ActiveCaptureVFX = nullptr;
	}

	// Select VFX based on current polarity (before flip)
	UNiagaraSystem* VFXToSpawn = (ChannelingChargeSign > 0) ? PositiveCaptureVFX : NegativeCaptureVFX;
	if (!VFXToSpawn)
	{
		return;
	}

	const FVector SpawnLocation = ChannelingPlateActor->GetActorLocation() + CaptureVFXOffset;

	ActiveCaptureVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		VFXToSpawn,
		SpawnLocation,
		FRotator::ZeroRotator,
		CaptureVFXScale,
		false  // bAutoDestroy — we track and clean up manually
	);

	if (ActiveCaptureVFX)
	{
		ActiveCaptureVFX->SetVariableVec3(FName("BeamEndPoint"), CapturedTarget->GetActorLocation());
	}
}

void UChargeAnimationComponent::SpawnLaunchVFX(AActor* LaunchedTarget)
{
	if (!LaunchedTarget || !ChannelingPlateActor)
	{
		return;
	}

	// Stop previous launch VFX if still active
	if (ActiveLaunchVFX)
	{
		ActiveLaunchVFX->DeactivateImmediate();
		ActiveLaunchVFX = nullptr;
	}

	// Select VFX based on NEW polarity (after flip: -ChannelingChargeSign)
	const int32 NewSign = -ChannelingChargeSign;
	UNiagaraSystem* VFXToSpawn = (NewSign > 0) ? PositiveLaunchVFX : NegativeLaunchVFX;
	if (!VFXToSpawn)
	{
		return;
	}

	const FVector SpawnLocation = ChannelingPlateActor->GetActorLocation() + LaunchVFXOffset;

	ActiveLaunchVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		VFXToSpawn,
		SpawnLocation,
		FRotator::ZeroRotator,
		LaunchVFXScale,
		false  // bAutoDestroy — we track and clean up manually
	);

	if (ActiveLaunchVFX)
	{
		ActiveLaunchVFX->SetVariableVec3(FName("BeamEndPoint"), LaunchedTarget->GetActorLocation());
	}
}

void UChargeAnimationComponent::UpdateBeamVFX()
{
	// Update capture beam: plate → captured target
	if (ActiveCaptureVFX)
	{
		if (!ActiveCaptureVFX->IsActive())
		{
			// Niagara system finished playing — clean up ref
			ActiveCaptureVFX = nullptr;
		}
		else if (ChannelingPlateActor && CurrentCapturedNPC.IsValid())
		{
			ActiveCaptureVFX->SetWorldLocation(ChannelingPlateActor->GetActorLocation() + CaptureVFXOffset);
			ActiveCaptureVFX->SetVariableVec3(FName("BeamEndPoint"), CurrentCapturedNPC->GetActorLocation());
		}
	}

	// Update launch beam: plate → launched target
	if (ActiveLaunchVFX)
	{
		if (!ActiveLaunchVFX->IsActive())
		{
			ActiveLaunchVFX = nullptr;
		}
		else if (ChannelingPlateActor && CurrentCapturedNPC.IsValid())
		{
			ActiveLaunchVFX->SetWorldLocation(ChannelingPlateActor->GetActorLocation() + LaunchVFXOffset);
			ActiveLaunchVFX->SetVariableVec3(FName("BeamEndPoint"), CurrentCapturedNPC->GetActorLocation());
		}
	}
}

void UChargeAnimationComponent::SpawnHoldVFX()
{
	// Stop existing hold VFX first
	StopHoldVFX();

	if (!MeleeMesh)
	{
		return;
	}

	UNiagaraSystem* VFXToSpawn = (ChannelingChargeSign > 0) ? PositiveHoldVFX : NegativeHoldVFX;
	if (!VFXToSpawn)
	{
		return;
	}

	ActiveHoldVFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
		VFXToSpawn,
		MeleeMesh,
		HoldVFXSocket,
		HoldVFXOffset,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		false  // bAutoDestroy — we manage lifetime manually
	);

	if (ActiveHoldVFX)
	{
		ActiveHoldVFX->SetWorldScale3D(HoldVFXScale);
	}
}

void UChargeAnimationComponent::StopHoldVFX()
{
	if (ActiveHoldVFX)
	{
		ActiveHoldVFX->DeactivateImmediate();
		ActiveHoldVFX = nullptr;
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
