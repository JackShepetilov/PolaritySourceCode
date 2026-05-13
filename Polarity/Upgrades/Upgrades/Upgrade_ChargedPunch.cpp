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
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"

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
		UE_LOG(LogTemp, Error, TEXT("[CHARGEDPUNCH] Failed to cast UpgradeDefinition to ChargedPunch type"));
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	CachedUpgradeManager = Character->GetUpgradeManager();
	CachedMeleeComp = Character->GetMeleeAttackComponent();

	Character->OnMeleeChargeHoldStarted.AddDynamic(this, &UUpgrade_ChargedPunch::HandleHoldStarted);
	Character->OnMeleeChargeHoldReleased.AddDynamic(this, &UUpgrade_ChargedPunch::HandleHoldReleased);
}

void UUpgrade_ChargedPunch::OnUpgradeDeactivated()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		Character->OnMeleeChargeHoldStarted.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleHoldStarted);
		Character->OnMeleeChargeHoldReleased.RemoveDynamic(this, &UUpgrade_ChargedPunch::HandleHoldReleased);
	}

	ExitChargingState();
	SetComponentTickEnabled(false);
}

void UUpgrade_ChargedPunch::HandleHoldStarted()
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	bButtonHeld = true;
	HoldElapsed = 0.0f;
	DrainAccumulator = 0.0f;
	SetComponentTickEnabled(true);
}

void UUpgrade_ChargedPunch::HandleHoldReleased()
{
	bButtonHeld = false;

	if (bIsCharging)
	{
		// Fire the punch — pool has already drained whatever it could during hold.
		ExecutePunch();
	}

	ExitChargingState();
	SetComponentTickEnabled(false);
}

void UUpgrade_ChargedPunch::EnterChargingState()
{
	if (bIsCharging)
	{
		return;
	}
	bIsCharging = true;

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

			// If we drained less than requested, the pool is empty — release the charge early.
			if (Drained < ToDrain)
			{
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

	// ==================== Lunge the player forward to endpoint ====================
	// Use LaunchCharacter with a velocity that covers the distance over a short period.
	// We use 0.15s as a fixed lunge duration — feels snappy and matches air-melee feel.
	const float LungeDuration = 0.15f;
	const FVector LungeVelocity = (Endpoint - StartLoc) / LungeDuration;
	Character->LaunchCharacter(LungeVelocity, /*bXYOverride=*/ true, /*bZOverride=*/ true);

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
			ComboUpg->AddComboHits(KilledCount);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGEDPUNCH] Fired: hold=%.2fs, distance=%.0f, hits=%d, kills=%d, dmg=%.1f"),
		HoldElapsed, Distance, HitCount, KilledCount, TotalDamage);
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
