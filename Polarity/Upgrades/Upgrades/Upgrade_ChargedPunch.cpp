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
#include "Engine/DamageEvents.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"

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

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] ACTIVATED — MinHold=%.2fs, MaxHold=%.2fs, Drain=%.1f/s, MaxDist=%.0f, MaxBonus=%.1f, Mgr=%s, MeleeComp=%s"),
		CachedDef->MinHoldTime, CachedDef->MaxHoldTime, CachedDef->PickupsPerSecond,
		CachedDef->MaxDistance, CachedDef->MaxBonusDamage,
		CachedUpgradeManager.IsValid() ? TEXT("OK") : TEXT("NULL"),
		CachedMeleeComp.IsValid() ? TEXT("OK") : TEXT("NULL"));

	Character->OnMeleeChargeHoldStarted.AddDynamic(this, &UUpgrade_ChargedPunch::HandleHoldStarted);
	Character->OnMeleeChargeHoldReleased.AddDynamic(this, &UUpgrade_ChargedPunch::HandleHoldReleased);
}

void UUpgrade_ChargedPunch::OnUpgradeDeactivated()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] DEACTIVATED (bIsCharging=%d, bIsLunging=%d, HoldElapsed=%.2f)"),
		bIsCharging ? 1 : 0, bIsLunging ? 1 : 0, HoldElapsed);

	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		Character->OnMeleeChargeHoldStarted.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleHoldStarted);
		Character->OnMeleeChargeHoldReleased.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleHoldReleased);
	}

	ExitChargingState();

	// If we're deactivated mid-lunge (e.g. on death), restore movement / mesh so
	// the character isn't stuck in Flying mode with hidden weapon mesh.
	if (bIsLunging)
	{
		FinishLunge();
	}

	SetComponentTickEnabled(false);
}

void UUpgrade_ChargedPunch::HandleHoldStarted()
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	const int32 PoolNow = CachedUpgradeManager.IsValid() ? CachedUpgradeManager->GetStoredHealthPickups() : -1;
	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] HOLD_START — pool=%d/%d"),
		PoolNow,
		CachedUpgradeManager.IsValid() ? CachedUpgradeManager->GetMaxStoredHealthPickups() : -1);

	bButtonHeld = true;
	HoldElapsed = 0.0f;
	DrainAccumulator = 0.0f;
	SetComponentTickEnabled(true);
}

void UUpgrade_ChargedPunch::HandleHoldReleased()
{
	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] HOLD_RELEASE — HoldElapsed=%.2fs, bIsCharging=%d"),
		HoldElapsed, bIsCharging ? 1 : 0);

	bButtonHeld = false;

	if (bIsCharging)
	{
		// Fire the punch — pool has already drained whatever it could during hold.
		// ExecutePunch -> StartLunge sets bIsLunging=true and keeps tick running so
		// TickLunge can interpolate the player to Endpoint over LungeDuration.
		ExecutePunch();
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[CHARGED_PUNCH_DEBUG] Release under MinHoldTime — no charge fired (jab path only)"));
	}

	ExitChargingState();

	// Keep ticking while a lunge is in progress; FinishLunge or the lunge-finish path
	// in TickComponent will disable tick once everything's restored.
	if (!bIsLunging)
	{
		SetComponentTickEnabled(false);
	}
}

void UUpgrade_ChargedPunch::EnterChargingState()
{
	if (bIsCharging)
	{
		return;
	}
	bIsCharging = true;

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] ENTER_CHARGE — HoldElapsed=%.2fs crossed MinHold=%.2fs"),
		HoldElapsed, CachedDef->MinHoldTime);

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	// Spawn endpoint-preview VFX (positioned at endpoint each tick via SetWorldLocation).
	if (CachedDef->EndpointPreviewVFX)
	{
		FVector Endpoint;
		float Distance;
		ComputeEndpoint(Endpoint, Distance);
		ActiveEndpointVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), CachedDef->EndpointPreviewVFX, Endpoint,
			FRotator::ZeroRotator, FVector::OneVector,
			/*bAutoDestroy=*/ false, /*bAutoActivate=*/ true,
			ENCPoolMethod::None);
	}

	// Start charge-loop audio.
	if (CachedDef->ChargeLoopSound)
	{
		ActiveChargeLoop = UGameplayStatics::SpawnSoundAttached(
			CachedDef->ChargeLoopSound, Character->GetRootComponent());
	}
}

void UUpgrade_ChargedPunch::ExitChargingState()
{
	if (bIsCharging)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[CHARGED_PUNCH_DEBUG] EXIT_CHARGE — cleared VFX/SFX"));
	}

	bIsCharging = false;
	HoldElapsed = 0.0f;
	DrainAccumulator = 0.0f;

	if (ActiveEndpointVFX)
	{
		ActiveEndpointVFX->DestroyComponent();
		ActiveEndpointVFX = nullptr;
	}
	if (ActiveChargeLoop)
	{
		ActiveChargeLoop->Stop();
		ActiveChargeLoop = nullptr;
	}
}

void UUpgrade_ChargedPunch::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Lunge phase (after release with charge) takes priority over hold logic.
	if (bIsLunging)
	{
		TickLunge(DeltaTime);
		// If lunge just finished and button is no longer held, disable tick.
		if (!bIsLunging && !bButtonHeld)
		{
			SetComponentTickEnabled(false);
		}
		return;
	}

	if (!bButtonHeld || !CachedDef.IsValid())
	{
		return;
	}

	HoldElapsed = FMath::Min(HoldElapsed + DeltaTime, CachedDef->MaxHoldTime);

	// Cross MinHoldTime threshold for the first time — try to enter charging state.
	if (!bIsCharging && HoldElapsed >= CachedDef->MinHoldTime)
	{
		UUpgradeManagerComponent* Mgr = CachedUpgradeManager.Get();
		if (!Mgr || Mgr->GetStoredHealthPickups() <= 0)
		{
			// No pickups available — do nothing. The regular jab from Triggered already played.
			UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] Threshold crossed but pool empty (pool=%d) — jab-only this press"),
				Mgr ? Mgr->GetStoredHealthPickups() : -1);
			return;
		}
		EnterChargingState();
	}

	if (!bIsCharging)
	{
		return;
	}

	// Drain pickups at PickupsPerSecond. Accumulate fractional and consume whole pickups.
	DrainAccumulator += CachedDef->PickupsPerSecond * DeltaTime;
	if (DrainAccumulator >= 1.0f)
	{
		UUpgradeManagerComponent* Mgr = CachedUpgradeManager.Get();
		if (Mgr)
		{
			const int32 ToDrain = FMath::FloorToInt(DrainAccumulator);
			const int32 Drained = Mgr->ConsumeStoredHealthPickups(ToDrain);
			DrainAccumulator -= static_cast<float>(Drained);

			UE_LOG(LogTemp, Verbose, TEXT("[CHARGED_PUNCH_DEBUG] Drained %d/%d pickups (pool now=%d, acc=%.2f)"),
				Drained, ToDrain, Mgr->GetStoredHealthPickups(), DrainAccumulator);

			// If we drained less than requested, the pool is empty — release the charge early.
			if (Drained < ToDrain)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] Pool depleted mid-hold — auto-firing at HoldElapsed=%.2fs"), HoldElapsed);
				ExecutePunch();
				ExitChargingState();
				return;
			}
		}
	}

	// Update endpoint VFX position each tick (camera-driven).
	if (ActiveEndpointVFX)
	{
		FVector Endpoint;
		float Distance;
		ComputeEndpoint(Endpoint, Distance);
		ActiveEndpointVFX->SetWorldLocation(Endpoint);
	}
}

void UUpgrade_ChargedPunch::ExecutePunch()
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
	if (!PC)
	{
		return;
	}

	// ==================== Compute trajectory ====================
	FVector Endpoint;
	float Distance;
	ComputeEndpoint(Endpoint, Distance);

	const FVector StartLoc = Character->GetActorLocation();
	const FVector LungeDir = (Endpoint - StartLoc).GetSafeNormal();

	// ==================== Capsule sweep along trajectory ====================
	// We treat the punch as a swept capsule from StartLoc to Endpoint with CapsuleRadius.
	// SweepMultiByChannel returns ALL overlapping actors on the path — pierce-through is
	// "process every hit, never stop at first".

	TArray<FHitResult> Hits;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);
	QueryParams.bReturnPhysicalMaterial = true;

	GetWorld()->SweepMultiByChannel(
		Hits,
		StartLoc,
		Endpoint,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(CachedDef->CapsuleRadius),
		QueryParams);

	// ==================== Apply damage to each unique pawn hit ====================
	const float BaseDamage = (CachedMeleeComp.IsValid()) ? CachedMeleeComp->Settings.BaseDamage : 50.0f;
	const float BonusDamage = EvaluateBonusDamage(HoldElapsed);
	const float TotalDamage = BaseDamage + BonusDamage;

	TSet<AActor*> Processed;
	int32 KilledCount = 0;
	int32 HitCount = 0;

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || Processed.Contains(HitActor) || HitActor == Character)
		{
			continue;
		}
		if (!Cast<APawn>(HitActor) && !HitActor->ActorHasTag(TEXT("MeleeDestructible")))
		{
			continue;
		}
		Processed.Add(HitActor);

		// Apply damage with DamageType from def (defaults to DamageType_Melee in BP, which
		// routes XP to Melee and triggers electrification on AShooterNPC::TakeDamage).
		FPointDamageEvent DamageEvent(
			TotalDamage,
			Hit,
			LungeDir,
			CachedDef->DamageType);

		HitActor->TakeDamage(TotalDamage, DamageEvent, PC, Character);
		HitCount++;

		// Knockback: use distance based on player's regular melee BaseKnockbackDistance.
		if (ACharacter* HitChar = Cast<ACharacter>(HitActor))
		{
			const float KnockbackDistance = (CachedMeleeComp.IsValid())
				? CachedMeleeComp->Settings.BaseKnockbackDistance : 200.0f;
			HitChar->LaunchCharacter(LungeDir * KnockbackDistance * 5.0f, false, false);
		}

		// Per-hit VFX/sound.
		if (CachedDef->HitVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), CachedDef->HitVFX, Hit.ImpactPoint,
				FRotator::ZeroRotator, FVector::OneVector,
				true, true, ENCPoolMethod::None);
		}
		if (CachedDef->HitSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, CachedDef->HitSound, Hit.ImpactPoint);
		}

		// Hit marker on the player.
		if (UHitMarkerComponent* HitMarker = Character->GetHitMarkerComponent())
		{
			const bool bKilled = HitActor->IsActorBeingDestroyed() ||
				(Cast<AShooterNPC>(HitActor) && Cast<AShooterNPC>(HitActor)->IsDead());
			HitMarker->RegisterHit(Hit.ImpactPoint, LungeDir, TotalDamage, false, bKilled);
			if (bKilled)
			{
				KilledCount++;
			}
		}
	}

	// ==================== Start the lunge phase ====================
	// Tick-driven motion via SetActorLocation between LungeStart and LungeEnd over
	// LungeTotalDuration. StartLunge handles: mesh swap, montage play, vertical lift,
	// gravity disable. FinishLunge (called from TickLunge when elapsed >= duration)
	// restores everything.
	StartLunge(StartLoc, Endpoint);

	// ==================== Player-side feedback ====================
	if (CachedDef->FireVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), CachedDef->FireVFX, StartLoc,
			LungeDir.Rotation(), FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}
	if (CachedDef->FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CachedDef->FireSound, StartLoc);
	}
	if (CachedDef->FireCameraShake && PC)
	{
		PC->ClientStartCameraShake(CachedDef->FireCameraShake, CachedDef->FireCameraShakeScale);
	}

	// ==================== Feed combo with kills ====================
	if (KilledCount > 0)
	{
		if (UUpgrade_Combo* ComboUpg = FindComboUpgrade())
		{
			UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] Feeding %d kills to Combo"), KilledCount);
			ComboUpg->AddComboHits(KilledCount);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[CHARGED_PUNCH_DEBUG] %d kills, but no Combo upgrade present"), KilledCount);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] FIRED: hold=%.2fs, distance=%.0f, capsule_r=%.0f, hits=%d, kills=%d, dmg=%.1f (base=%.1f + bonus=%.1f), poolRemaining=%d"),
		HoldElapsed, Distance, CachedDef->CapsuleRadius, HitCount, KilledCount, TotalDamage, BaseDamage, BonusDamage,
		CachedUpgradeManager.IsValid() ? CachedUpgradeManager->GetStoredHealthPickups() : -1);
}

void UUpgrade_ChargedPunch::ComputeEndpoint(FVector& OutEndpoint, float& OutDistance) const
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		OutEndpoint = FVector::ZeroVector;
		OutDistance = 0.0f;
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC)
	{
		OutEndpoint = Character->GetActorLocation();
		OutDistance = 0.0f;
		return;
	}

	FVector CameraLoc;
	FRotator CameraRot;
	PC->GetPlayerViewPoint(CameraLoc, CameraRot);
	const FVector ForwardDir = CameraRot.Vector();

	const float DesiredDistance = EvaluateDistance(HoldElapsed);

	// Trace forward from the player's actor location (NOT camera — we don't want to
	// punch into walls behind the player). Clamp at first geometry hit.
	const FVector StartLoc = Character->GetActorLocation();
	const FVector DesiredEnd = StartLoc + ForwardDir * DesiredDistance;

	FHitResult WallHit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);

	if (GetWorld()->LineTraceSingleByChannel(WallHit, StartLoc, DesiredEnd, ECC_Visibility, QueryParams))
	{
		// Hit geometry — clamp endpoint a bit short of the wall.
		const float WallDistance = (WallHit.ImpactPoint - StartLoc).Size();
		const float Clamped = FMath::Max(0.0f, WallDistance - CachedDef->CapsuleRadius);
		OutEndpoint = StartLoc + ForwardDir * Clamped;
		OutDistance = Clamped;
		UE_LOG(LogTemp, Verbose, TEXT("[CHARGED_PUNCH_DEBUG] Endpoint clamped by wall (%s) — desired=%.0f, clamped=%.0f"),
			WallHit.GetActor() ? *WallHit.GetActor()->GetName() : TEXT("WorldStatic"), DesiredDistance, Clamped);
	}
	else
	{
		OutEndpoint = DesiredEnd;
		OutDistance = DesiredDistance;
	}
}

float UUpgrade_ChargedPunch::EvaluateDistance(float HoldTime) const
{
	if (!CachedDef.IsValid())
	{
		return 0.0f;
	}
	if (CachedDef->HoldTimeToDistance)
	{
		return FMath::Max(0.0f, CachedDef->HoldTimeToDistance->GetFloatValue(HoldTime));
	}
	// Fallback linear: 0 at HoldTime=0, MaxDistance at HoldTime=MaxHoldTime.
	const float Alpha = (CachedDef->MaxHoldTime > 0.0f) ? FMath::Clamp(HoldTime / CachedDef->MaxHoldTime, 0.0f, 1.0f) : 0.0f;
	return Alpha * CachedDef->MaxDistance;
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
	// FindComponentByClass walks all components — Upgrade_Combo is added dynamically by
	// UUpgradeManagerComponent when the combo upgrade is granted.
	return Character->FindComponentByClass<UUpgrade_Combo>();
}

// ==================== Lunge Lifecycle ====================

void UUpgrade_ChargedPunch::StartLunge(const FVector& StartPos, const FVector& EndPos)
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || !CachedDef.IsValid())
	{
		return;
	}

	LungeStart = StartPos;
	LungeEnd = EndPos;
	LungeElapsed = 0.0f;
	LungeTotalDuration = FMath::Max(0.05f, CachedDef->LungeDuration);
	bIsLunging = true;

	// 1. Lift the player slightly so SetActorLocation doesn't collide with the floor.
	if (CachedDef->VerticalLiftAmount > 0.0f)
	{
		FVector Lifted = Character->GetActorLocation() + FVector(0.0f, 0.0f, CachedDef->VerticalLiftAmount);
		Character->SetActorLocation(Lifted, /*bSweep=*/ false);
		LungeStart.Z = Lifted.Z;
		LungeEnd.Z = Lifted.Z; // keep flight perfectly horizontal at the lifted height
	}

	// 2. Disable gravity + put movement into Flying so the player doesn't fall mid-lunge.
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		SavedMovementMode = Movement->MovementMode;
		SavedGravityScale = Movement->GravityScale;

		Movement->GravityScale = 0.0f;
		Movement->Velocity = FVector::ZeroVector;
		Movement->SetMovementMode(MOVE_Flying);
	}

	// 3. Switch to MeleeMesh view (hides weapon + FPMesh, attaches MeleeMesh to camera).
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->EnterMeleeMeshView();

		// 4. Play the single configured air-attack montage on MeleeMesh.
		if (CachedDef->AirAttackMontage)
		{
			MeleeComp->PlayMontageOnMeleeMesh(CachedDef->AirAttackMontage, 1.0f);
		}
	}

	// Ensure tick is running for interpolation.
	SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] StartLunge — start=(%.0f,%.0f,%.0f), end=(%.0f,%.0f,%.0f), duration=%.2fs"),
		LungeStart.X, LungeStart.Y, LungeStart.Z, LungeEnd.X, LungeEnd.Y, LungeEnd.Z, LungeTotalDuration);
}

void UUpgrade_ChargedPunch::TickLunge(float DeltaTime)
{
	if (!bIsLunging)
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		FinishLunge();
		return;
	}

	LungeElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(LungeElapsed / LungeTotalDuration, 0.0f, 1.0f);
	const FVector NewPos = FMath::Lerp(LungeStart, LungeEnd, Alpha);

	// SetActorLocation with sweep so we collide with walls / actors on the way.
	Character->SetActorLocation(NewPos, /*bSweep=*/ true);

	if (Alpha >= 1.0f)
	{
		FinishLunge();
	}
}

void UUpgrade_ChargedPunch::FinishLunge()
{
	if (!bIsLunging)
	{
		return;
	}
	bIsLunging = false;

	AShooterCharacter* Character = GetShooterCharacter();
	if (Character)
	{
		// Restore movement state.
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->GravityScale = SavedGravityScale;
			Movement->SetMovementMode(SavedMovementMode);
			Movement->Velocity = FVector::ZeroVector;
		}
	}

	// Restore mesh view back to FirstPersonMesh + weapon.
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->ExitMeleeMeshView();
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_DEBUG] FinishLunge — restored MovementMode=%d, Gravity=%.2f"),
		(int32)SavedMovementMode, SavedGravityScale);
}
