// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_ChargedPunch.h"
#include "UpgradeDefinition_ChargedPunch.h"
#include "Upgrade_Combo.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Variant_Shooter/HitMarkerComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/DamageEvents.h"
#include "CollisionShape.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimMontage.h"

UUpgrade_ChargedPunch::UUpgrade_ChargedPunch()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_ChargedPunch::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_ChargedPunch>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGED_PUNCH_DEBUG] Failed to cast UpgradeDefinition to ChargedPunch type"));
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHARGED_PUNCH_DEBUG] No owning ShooterCharacter — upgrade can't bind"));
		return;
	}

	CachedUpgradeManager = Character->GetUpgradeManager();
	CachedMeleeComp = Character->GetMeleeAttackComponent();

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] ACTIVATED (rise/slam) — MinHold=%.2fs, MaxHold=%.2fs, Drain=%.1f/s, Rise=%.0f/MaxH=%.0f, Buffer=%.2fs, SlamSpeed=%.0f, SlamR=%.0f"),
		CachedDef->MinHoldTime, CachedDef->MaxHoldTime, CachedDef->PickupsPerSecond,
		CachedDef->RiseSpeed, CachedDef->MaxRiseHeight, CachedDef->ReleaseBufferTime,
		CachedDef->SlamDescentSpeed, CachedDef->SlamRadius);

	Character->OnMeleeChargeHoldStarted.AddDynamic(this, &UUpgrade_ChargedPunch::HandleHoldStarted);
	Character->OnMeleeChargeHoldReleased.AddDynamic(this, &UUpgrade_ChargedPunch::HandleHoldReleased);
}

void UUpgrade_ChargedPunch::OnUpgradeDeactivated()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] DEACTIVATED (phase=%d, HoldElapsed=%.2f)"),
		(int32)Phase, HoldElapsed);

	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		Character->OnMeleeChargeHoldStarted.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleHoldStarted);
		Character->OnMeleeChargeHoldReleased.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleHoldReleased);
	}

	EndSequence();
}

// ==================== Input callbacks ====================

void UUpgrade_ChargedPunch::HandleHoldStarted()
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	// Ignore a fresh press while a sequence is already running (e.g. a dropkick re-press during the
	// hover Buffer) — that press is for the dropkick, not a new charge.
	if (Phase != EChargedPunchPhase::None)
	{
		return;
	}

	const int32 PoolNow = CachedUpgradeManager.IsValid() ? CachedUpgradeManager->GetStoredHealthPickups() : -1;
	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] HOLD_START — pool=%d"), PoolNow);

	bButtonHeld = true;
	HoldElapsed = 0.0f;
	DrainAccumulator = 0.0f;
	SetComponentTickEnabled(true);
}

void UUpgrade_ChargedPunch::HandleHoldReleased()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] HOLD_RELEASE — HoldElapsed=%.2fs, phase=%d"), HoldElapsed, (int32)Phase);

	bButtonHeld = false;

	if (Phase == EChargedPunchPhase::Rising)
	{
		// Capture the charge level for the slam, then hover (the buffer window).
		ChargedBonusDamage = EvaluateBonusDamage(HoldElapsed);
		EnterBufferPhase();
	}
	else if (Phase == EChargedPunchPhase::None)
	{
		// Never charged — the regular jab from the Triggered binding already played.
		StopChargeLoop();
		SetComponentTickEnabled(false);
	}
	// else: already past the hold (Buffer / Slamming / DropkickFollow) — the phase tick owns the
	// sequence now; a physical release here must not interrupt it.
}

void UUpgrade_ChargedPunch::HandleDropKickStarted()
{
	// A dropkick fired during the hover buffer → it replaces the fall. Hand movement control back
	// to the dropkick and switch to the follow phase; the slam AoE will fire on the dropkick's hit.
	if (Phase != EChargedPunchPhase::Buffer)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] Dropkick replaced the slam — entering DropkickFollow"));

	Phase = EChargedPunchPhase::DropkickFollow;
	DropkickWaitElapsed = 0.0f;

	// Restore gravity and drop into Falling so the dropkick dive behaves like a normal airborne kick.
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->GravityScale = SavedGravityScale;
			Movement->SetMovementMode(MOVE_Falling);
		}
	}
	bMovementSaved = false; // consumed

	// Release our mesh view so the dropkick's own animation drives the FP view cleanly.
	if (bOwnsMeleeMeshView)
	{
		if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
		{
			MeleeComp->ExitMeleeMeshView();
		}
		bOwnsMeleeMeshView = false;
	}

	StopChargeLoop();
	SetComponentTickEnabled(true); // keep ticking for the hit-timeout
	// OnDropKickHit stays bound to place the slam on the target.
}

void UUpgrade_ChargedPunch::HandleDropKickHit(AActor* HitActor, const FVector& HitLocation, float Damage)
{
	if (Phase != EChargedPunchPhase::DropkickFollow)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] Dropkick connected on %s — firing slam AoE at target"),
		*GetNameSafe(HitActor));

	// Slam's AoE sphere centered on the dropkick target (dropkick damage already applied by the
	// melee component; this adds the area damage around the victim).
	DoSlamAoE(HitLocation);
	EndSequence();
}

// ==================== Tick ====================

void UUpgrade_ChargedPunch::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Safety: if the player dies mid-sequence (gravity is disabled during rise/buffer/slam),
	// tear everything down so the corpse doesn't float in MOVE_Flying.
	if (Phase != EChargedPunchPhase::None)
	{
		AShooterCharacter* Character = GetShooterCharacter();
		if (!Character || Character->IsDead())
		{
			EndSequence();
			return;
		}
	}

	switch (Phase)
	{
	case EChargedPunchPhase::Buffer:         TickBuffer(DeltaTime); return;
	case EChargedPunchPhase::Slamming:       TickSlam(DeltaTime); return;
	case EChargedPunchPhase::DropkickFollow: TickDropkickFollow(DeltaTime); return;
	default: break; // None or Rising fall through to the hold logic below
	}

	if (!bButtonHeld || !CachedDef.IsValid())
	{
		// Not holding and not in a phase — nothing to do (e.g. a stray tick after EndSequence).
		if (Phase == EChargedPunchPhase::None)
		{
			SetComponentTickEnabled(false);
		}
		return;
	}

	HoldElapsed = FMath::Min(HoldElapsed + DeltaTime, CachedDef->MaxHoldTime);

	// Cross MinHoldTime → enter the rise (if the pool has at least one pickup).
	if (Phase == EChargedPunchPhase::None && HoldElapsed >= CachedDef->MinHoldTime)
	{
		UUpgradeManagerComponent* Mgr = CachedUpgradeManager.Get();
		if (!Mgr || Mgr->GetStoredHealthPickups() <= 0)
		{
			// No pickups — jab-only this press (already fired off Triggered).
			return;
		}
		EnterRisingPhase();
	}

	if (Phase != EChargedPunchPhase::Rising)
	{
		return;
	}

	// Drain pickups at PickupsPerSecond. When the pool runs dry, auto-release into the buffer.
	DrainAccumulator += CachedDef->PickupsPerSecond * DeltaTime;
	if (DrainAccumulator >= 1.0f)
	{
		if (UUpgradeManagerComponent* Mgr = CachedUpgradeManager.Get())
		{
			const int32 ToDrain = FMath::FloorToInt(DrainAccumulator);
			const int32 Drained = Mgr->ConsumeStoredHealthPickups(ToDrain);
			DrainAccumulator -= static_cast<float>(Drained);

			if (Drained < ToDrain)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] Pool depleted mid-rise — auto-releasing into buffer at HoldElapsed=%.2fs"), HoldElapsed);
				bButtonHeld = false;
				ChargedBonusDamage = EvaluateBonusDamage(HoldElapsed);
				EnterBufferPhase();
				return;
			}
		}
	}

	TickRise(DeltaTime);
}

// ==================== Phase logic ====================

void UUpgrade_ChargedPunch::EnterRisingPhase()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || !CachedDef.IsValid())
	{
		return;
	}

	Phase = EChargedPunchPhase::Rising;
	RiseStartZ = Character->GetActorLocation().Z;

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] ENTER_RISE — HoldElapsed=%.2fs, startZ=%.0f"), HoldElapsed, RiseStartZ);

	// MOVE_Falling (not Flying) + zero gravity: we drive the rise via Velocity.Z, but the player
	// keeps normal AIR CONTROL over horizontal movement (X/Y left to the CMC). Don't zero velocity
	// here — preserve the player's momentum into the takeoff.
	CaptureMovement();
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->GravityScale = 0.0f;
		Movement->SetMovementMode(MOVE_Falling);
	}

	// Stop the in-flight jab and show the charge pose on the MeleeMesh.
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->EnterMeleeMeshView();
		bOwnsMeleeMeshView = true;
		if (CachedDef->AirAttackMontage)
		{
			MeleeComp->PlayMontageOnMeleeMesh(CachedDef->AirAttackMontage, 1.0f);
		}
	}

	// Charge-loop audio.
	if (CachedDef->ChargeLoopSound)
	{
		ActiveChargeLoop = UGameplayStatics::SpawnSoundAttached(CachedDef->ChargeLoopSound, Character->GetRootComponent());
	}
}

void UUpgrade_ChargedPunch::TickRise(float DeltaTime)
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || !CachedDef.IsValid())
	{
		return;
	}

	const float MaxZ = RiseStartZ + CachedDef->MaxRiseHeight;
	const float CurZ = Character->GetActorLocation().Z;

	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		// Drive ONLY the vertical velocity for the rise; leave X/Y to the CMC's air control so the
		// player can steer while taking off. Stop climbing at the height cap (then it hovers).
		Movement->Velocity.Z = (CurZ < MaxZ) ? CachedDef->RiseSpeed : 0.0f;
	}
}

void UUpgrade_ChargedPunch::EnterBufferPhase()
{
	Phase = EChargedPunchPhase::Buffer;
	BufferElapsed = 0.0f;
	StopChargeLoop();

	// Hover (gravity already disabled in EnterRisingPhase): kill only vertical velocity but keep
	// horizontal — the player retains air control during the buffer too.
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->Velocity.Z = 0.0f;
		}
	}

	// Listen for a dropkick that replaces the slam.
	BindDropkickDelegates();
	SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] ENTER_BUFFER — hover %.2fs (input a dropkick to replace the slam)"), CachedDef.IsValid() ? CachedDef->ReleaseBufferTime : 0.0f);
}

void UUpgrade_ChargedPunch::TickBuffer(float DeltaTime)
{
	if (!CachedDef.IsValid())
	{
		EnterSlamPhase();
		return;
	}

	// Keep hovering (kill only vertical velocity; horizontal stays for air control).
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->Velocity.Z = 0.0f;
		}
	}

	BufferElapsed += DeltaTime;
	if (BufferElapsed >= CachedDef->ReleaseBufferTime)
	{
		EnterSlamPhase();
	}
}

void UUpgrade_ChargedPunch::EnterSlamPhase()
{
	Phase = EChargedPunchPhase::Slamming;

	// The buffer ended without a dropkick — stop listening for one.
	UnbindDropkickDelegates();

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		EndSequence();
		return;
	}

	// Commit to the drop: Flying + zero velocity so the descent is a clean straight slam — no air
	// control nudging it off-line, and the CMC won't fight the SetActorLocation in TickSlam.
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->GravityScale = 0.0f;
		Movement->Velocity = FVector::ZeroVector;
		Movement->SetMovementMode(MOVE_Flying);
	}

	// Capsule half-height — so the descent lands the capsule ON the floor, not centered on it
	// (centering would bury the lower half and the player falls through / physics breaks).
	float HalfHeight = 0.0f;
	if (const UCapsuleComponent* Cap = Character->GetCapsuleComponent())
	{
		HalfHeight = Cap->GetScaledCapsuleHalfHeight();
	}

	// Trace straight down for the ground (the descent target).
	const FVector Origin = Character->GetActorLocation();
	const FVector DownEnd = Origin - FVector(0.0f, 0.0f, 100000.0f);
	FHitResult GroundHit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundHit, Origin, DownEnd, ECC_Visibility, Params))
	{
		// Rest the capsule on the surface (actor location = floor + half-height).
		SlamGroundTarget = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight);
	}
	else
	{
		// No ground below (pit / off the nav) — don't descend into the void. Slam in place at the
		// current location and let normal gravity take over afterward.
		SlamGroundTarget = Origin;
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] ENTER_SLAM — land Z=%.0f (halfH=%.0f)"), SlamGroundTarget.Z, HalfHeight);
}

void UUpgrade_ChargedPunch::TickSlam(float DeltaTime)
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || !CachedDef.IsValid())
	{
		EndSequence();
		return;
	}

	FVector Pos = Character->GetActorLocation();
	Pos.Z -= CachedDef->SlamDescentSpeed * DeltaTime;

	if (Pos.Z <= SlamGroundTarget.Z)
	{
		// Landed — settle the capsule on the ground, restore movement, and slam.
		Pos.Z = SlamGroundTarget.Z;
		Character->SetActorLocation(Pos, /*bSweep=*/false);
		RestoreMovement();

		// Slam origin at the player's FEET (floor), not the capsule center — so the AoE + VFX sit
		// on the ground where the slam landed.
		FVector SlamOrigin = Character->GetActorLocation();
		if (const UCapsuleComponent* Cap = Character->GetCapsuleComponent())
		{
			SlamOrigin.Z -= Cap->GetScaledCapsuleHalfHeight();
		}
		DoSlamAoE(SlamOrigin);
		EndSequence();
		return;
	}

	Character->SetActorLocation(Pos, /*bSweep=*/false);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->Velocity = FVector::ZeroVector;
	}
}

void UUpgrade_ChargedPunch::TickDropkickFollow(float DeltaTime)
{
	if (!CachedDef.IsValid())
	{
		EndSequence();
		return;
	}

	DropkickWaitElapsed += DeltaTime;
	if (DropkickWaitElapsed >= CachedDef->DropkickHitTimeout)
	{
		// The dropkick never connected — give up (no slam, since there's no target to center on).
		UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] DropkickFollow timed out — no slam"));
		EndSequence();
	}
}

// ==================== Slam AoE ====================

void UUpgrade_ChargedPunch::DoSlamAoE(const FVector& Origin)
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Character->GetController());

	const float BaseDamage = CachedMeleeComp.IsValid() ? CachedMeleeComp->Settings.BaseDamage : 50.0f;
	const float TotalDamage = BaseDamage + ChargedBonusDamage;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);
	if (GetWorld())
	{
		GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Origin,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeSphere(CachedDef->SlamRadius),
			Params);
	}

	TSet<AActor*> Processed;
	int32 HitCount = 0;
	int32 KilledCount = 0;

	for (const FOverlapResult& Ov : Overlaps)
	{
		AActor* HitActor = Ov.GetActor();
		if (!HitActor || HitActor == Character || Processed.Contains(HitActor))
		{
			continue;
		}
		if (!Cast<APawn>(HitActor) && !HitActor->ActorHasTag(TEXT("MeleeDestructible")))
		{
			continue;
		}
		Processed.Add(HitActor);

		FVector Dir = (HitActor->GetActorLocation() - Origin).GetSafeNormal2D();
		if (Dir.IsNearlyZero())
		{
			Dir = Character->GetActorForwardVector();
		}

		FHitResult SynthHit;
		SynthHit.ImpactPoint = HitActor->GetActorLocation();
		SynthHit.Location = SynthHit.ImpactPoint;

		FPointDamageEvent DamageEvent(TotalDamage, SynthHit, Dir, CachedDef->DamageType);
		HitActor->TakeDamage(TotalDamage, DamageEvent, PC, Character);
		HitCount++;

		// Knockback: punch targets up and outward from the slam origin.
		if (ACharacter* HitChar = Cast<ACharacter>(HitActor))
		{
			const float KnockbackDistance = CachedMeleeComp.IsValid() ? CachedMeleeComp->Settings.BaseKnockbackDistance : 200.0f;
			const FVector Launch = Dir * (KnockbackDistance * 3.0f) + FVector(0.0f, 0.0f, KnockbackDistance * 2.0f);
			HitChar->LaunchCharacter(Launch, false, false);
		}

		// No per-target VFX/sound — the slam plays a SINGLE VFX + HitSound at the landing point below.

		if (UHitMarkerComponent* HitMarker = Character->GetHitMarkerComponent())
		{
			const bool bKilled = HitActor->IsActorBeingDestroyed() ||
				(Cast<AShooterNPC>(HitActor) && Cast<AShooterNPC>(HitActor)->IsDead());
			HitMarker->RegisterHit(SynthHit.ImpactPoint, Dir, TotalDamage, false, bKilled);
			if (bKilled)
			{
				KilledCount++;
			}
		}
	}

	// Slam impact feedback at the origin (reuses the upgrade's "fire" feedback slots).
	if (CachedDef->FireVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), CachedDef->FireVFX, Origin,
			FRotator::ZeroRotator, FVector::OneVector, true, true, ENCPoolMethod::None);
	}
	if (CachedDef->FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CachedDef->FireSound, Origin);
	}
	if (CachedDef->HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CachedDef->HitSound, Origin);
	}
	if (CachedDef->FireCameraShake && PC)
	{
		PC->ClientStartCameraShake(CachedDef->FireCameraShake, CachedDef->FireCameraShakeScale);
	}

	if (KilledCount > 0)
	{
		if (UUpgrade_Combo* ComboUpg = FindComboUpgrade())
		{
			ComboUpg->AddComboHits(KilledCount);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] SLAM at (%.0f,%.0f,%.0f) R=%.0f — hits=%d, kills=%d, dmg=%.1f (base=%.1f + bonus=%.1f)"),
		Origin.X, Origin.Y, Origin.Z, CachedDef->SlamRadius, HitCount, KilledCount, TotalDamage, BaseDamage, ChargedBonusDamage);
}

// ==================== Helpers / lifecycle ====================

void UUpgrade_ChargedPunch::EndSequence()
{
	StopChargeLoop();
	UnbindDropkickDelegates();
	RestoreMovement();

	if (bOwnsMeleeMeshView)
	{
		if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
		{
			MeleeComp->ExitMeleeMeshView();
		}
		bOwnsMeleeMeshView = false;
	}

	Phase = EChargedPunchPhase::None;
	bButtonHeld = false;
	HoldElapsed = 0.0f;
	DrainAccumulator = 0.0f;
	BufferElapsed = 0.0f;
	DropkickWaitElapsed = 0.0f;
	ChargedBonusDamage = 0.0f;

	SetComponentTickEnabled(false);
}

void UUpgrade_ChargedPunch::StopChargeLoop()
{
	if (ActiveChargeLoop)
	{
		ActiveChargeLoop->Stop();
		ActiveChargeLoop = nullptr;
	}
}

void UUpgrade_ChargedPunch::CaptureMovement()
{
	if (bMovementSaved)
	{
		return;
	}
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			SavedMovementMode = Movement->MovementMode;
			SavedGravityScale = Movement->GravityScale;
			bMovementSaved = true;
		}
	}
}

void UUpgrade_ChargedPunch::RestoreMovement()
{
	if (!bMovementSaved)
	{
		return;
	}
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->GravityScale = SavedGravityScale;
			Movement->SetMovementMode(SavedMovementMode);
			Movement->Velocity = FVector::ZeroVector;
		}
	}
	bMovementSaved = false;
}

void UUpgrade_ChargedPunch::BindDropkickDelegates()
{
	if (bDropkickDelegatesBound)
	{
		return;
	}
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->OnDropKickStarted.AddDynamic(this, &UUpgrade_ChargedPunch::HandleDropKickStarted);
		MeleeComp->OnDropKickHit.AddDynamic(this, &UUpgrade_ChargedPunch::HandleDropKickHit);
		bDropkickDelegatesBound = true;
	}
}

void UUpgrade_ChargedPunch::UnbindDropkickDelegates()
{
	if (!bDropkickDelegatesBound)
	{
		return;
	}
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->OnDropKickStarted.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleDropKickStarted);
		MeleeComp->OnDropKickHit.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleDropKickHit);
	}
	bDropkickDelegatesBound = false;
}

float UUpgrade_ChargedPunch::EvaluateBonusDamage(float HoldTime) const
{
	if (!CachedDef.IsValid())
	{
		return 0.0f;
	}
	if (CachedDef->HoldTimeToBonusDamage)
	{
		return FMath::Max(0.0f, CachedDef->HoldTimeToBonusDamage->GetFloatValue(HoldTime));
	}
	const float Alpha = (CachedDef->MaxHoldTime > 0.0f) ? FMath::Clamp(HoldTime / CachedDef->MaxHoldTime, 0.0f, 1.0f) : 0.0f;
	return Alpha * CachedDef->MaxBonusDamage;
}

UUpgrade_Combo* UUpgrade_ChargedPunch::FindComboUpgrade() const
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return nullptr;
	}
	return Character->FindComponentByClass<UUpgrade_Combo>();
}

bool UUpgrade_ChargedPunch::IsBusy() const
{
	// Suppress the regular swing while rising / slamming / dropkick-following. Deliberately FALSE
	// during the hover Buffer so the player's dropkick re-press reaches MeleeAttackComponent.
	return Phase == EChargedPunchPhase::Rising
		|| Phase == EChargedPunchPhase::Slamming
		|| Phase == EChargedPunchPhase::DropkickFollow;
}
