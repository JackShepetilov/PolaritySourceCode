// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_LowHealthDefense.h"
#include "UpgradeDefinition_LowHealthDefense.h"
#include "ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"

namespace
{
	// How often the (heavier) nearby-enemy overlap scan runs while the effect is active.
	constexpr float LowHPScanInterval = 0.15f;
}

UUpgrade_LowHealthDefense::UUpgrade_LowHealthDefense()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_LowHealthDefense::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_LowHealthDefense>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[LOWHP_DEBUG] Activation FAILED — UpgradeDefinition is not UUpgradeDefinition_LowHealthDefense!"));
		return;
	}

	const FLowHealthDefenseLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	UE_LOG(LogTemp, Warning, TEXT("[LOWHP_DEBUG] ACTIVATED Lv%d — Threshold=%.0f%% SlowRadius=%.0f MinDilation=%.2f"),
		CurrentLevel, LD.HealthThreshold * 100.0f, LD.SlowRadius, LD.MinEnemyTimeDilation);

	// Tick is always on (cheap when inactive — just an HP read); the overlap scan is throttled
	// and only runs while the effect is active.
	SetComponentTickEnabled(true);
}

void UUpgrade_LowHealthDefense::OnUpgradeDeactivated()
{
	SetComponentTickEnabled(false);
	RestoreAllSlowedEnemies();

	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		Character->SetLowHealthDefenseActive(false);
		Character->SetEnemyBoltSlowMultiplier(1.0f);
	}
	bActiveState = false;
}

void UUpgrade_LowHealthDefense::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedDef.IsValid())
	{
		return;
	}

	const float Strength = ComputeStrength();
	const bool bShouldBeActive = Strength > 0.0f;

	// State transition: flip the player-side flag (read by enemy weapons) on change.
	if (bShouldBeActive != bActiveState)
	{
		bActiveState = bShouldBeActive;
		if (AShooterCharacter* Character = GetShooterCharacter())
		{
			Character->SetLowHealthDefenseActive(bActiveState);
		}
		UE_LOG(LogTemp, Warning, TEXT("[LOWHP_DEBUG] Low-HP defense %s (strength=%.2f)"),
			bActiveState ? TEXT("ACTIVE") : TEXT("inactive"), Strength);

		if (!bActiveState)
		{
			RestoreAllSlowedEnemies();
			if (AShooterCharacter* Character = GetShooterCharacter())
			{
				Character->SetEnemyBoltSlowMultiplier(1.0f);
			}
		}
	}

	if (!bActiveState)
	{
		return;
	}

	// Slow enemy bolts: none at the threshold, ramping to MinBoltSpeedMultiplier toward 0 HP (curve-scaled).
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		const FLowHealthDefenseLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
		Character->SetEnemyBoltSlowMultiplier(FMath::Lerp(1.0f, LD.MinBoltSpeedMultiplier, Strength));
	}

	// Throttle the overlap scan; update slow strength each scan (it scales with HP depth).
	ScanAccumulator += DeltaTime;
	if (ScanAccumulator >= LowHPScanInterval)
	{
		ScanAccumulator = 0.0f;
		RefreshSlowedEnemies(Strength);
	}
}

float UUpgrade_LowHealthDefense::ComputeStrength() const
{
	if (!CachedDef.IsValid())
	{
		return 0.0f;
	}

	const AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return 0.0f;
	}

	const float MaxHP = Character->GetMaxHP();
	if (MaxHP <= 0.0f)
	{
		return 0.0f;
	}

	const float Pct = Character->GetCurrentHP() / MaxHP;
	const FLowHealthDefenseLevelData& LD = CachedDef->GetLevelData(CurrentLevel);

	// Dead, at/above threshold, or degenerate threshold → inactive.
	if (Pct <= 0.0f || Pct >= LD.HealthThreshold || LD.HealthThreshold <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// t = 0 at the threshold, 1 at 0 HP.
	const float T = FMath::Clamp((LD.HealthThreshold - Pct) / LD.HealthThreshold, 0.0f, 1.0f);
	return CachedDef->ScalingCurve
		? FMath::Clamp(CachedDef->ScalingCurve->GetFloatValue(T), 0.0f, 1.0f)
		: T;
}

void UUpgrade_LowHealthDefense::RefreshSlowedEnemies(float Strength)
{
	const FLowHealthDefenseLevelData& LD = CachedDef->GetLevelData(CurrentLevel);

	AShooterCharacter* Character = GetShooterCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return;
	}

	const float TargetDilation = FMath::Lerp(1.0f, LD.MinEnemyTimeDilation, FMath::Clamp(Strength, 0.0f, 1.0f));
	const FVector Origin = Character->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);
	World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(LD.SlowRadius),
		Params);

	TSet<TWeakObjectPtr<AShooterNPC>> NowInRange;
	for (const FOverlapResult& Ov : Overlaps)
	{
		if (AShooterNPC* NPC = Cast<AShooterNPC>(Ov.GetActor()))
		{
			if (NPC->IsDead())
			{
				continue;
			}
			NPC->CustomTimeDilation = TargetDilation;
			NowInRange.Add(NPC);
		}
	}

	// Restore enemies that were slowed but are no longer in range.
	for (const TWeakObjectPtr<AShooterNPC>& WeakNPC : SlowedEnemies)
	{
		if (!NowInRange.Contains(WeakNPC))
		{
			if (AShooterNPC* NPC = WeakNPC.Get())
			{
				NPC->CustomTimeDilation = 1.0f;
			}
		}
	}

	SlowedEnemies = MoveTemp(NowInRange);
}

void UUpgrade_LowHealthDefense::RestoreAllSlowedEnemies()
{
	for (const TWeakObjectPtr<AShooterNPC>& WeakNPC : SlowedEnemies)
	{
		if (AShooterNPC* NPC = WeakNPC.Get())
		{
			NPC->CustomTimeDilation = 1.0f;
		}
	}
	SlowedEnemies.Empty();
}
