// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_Backstab.h"
#include "UpgradeDefinition_Backstab.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

UUpgrade_Backstab::UUpgrade_Backstab()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_Backstab::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_Backstab>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[BACKSTAB_DEBUG] Failed to cast UpgradeDefinition to Backstab type"));
		return;
	}

	const FBackstabLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	UE_LOG(LogTemp, Warning, TEXT("[BACKSTAB_DEBUG] ACTIVATED Lv%d/%d — DamageMult=%.2fx, BackCone=%.0fdeg, RequireExplosionStun=%d"),
		CurrentLevel, CachedDef->MaxLevel,
		LD.DamageMultiplier, LD.BackConeHalfAngle, LD.bRequireStunnedByExplosion ? 1 : 0);
}

void UUpgrade_Backstab::OnUpgradeDeactivated()
{
	UE_LOG(LogTemp, Warning, TEXT("[BACKSTAB_DEBUG] DEACTIVATED"));
}

void UUpgrade_Backstab::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
	if (!CachedDef.IsValid())
	{
		return;
	}
	const FBackstabLevelData& LD = CachedDef->GetLevelData(NewLevel);
	UE_LOG(LogTemp, Warning, TEXT("[BACKSTAB_DEBUG] LEVEL_UP %d -> %d — DamageMult=%.2fx, BackCone=%.0fdeg, RequireExplosionStun=%d"),
		OldLevel, NewLevel,
		LD.DamageMultiplier, LD.BackConeHalfAngle, LD.bRequireStunnedByExplosion ? 1 : 0);
}

float UUpgrade_Backstab::GetMeleeDamageMultiplier(AActor* Target) const
{
	if (!CachedDef.IsValid() || !Target)
	{
		return 1.0f;
	}

	if (!IsTargetStunned(Target))
	{
		return 1.0f;
	}

	if (!IsPlayerBehindTarget(Target))
	{
		return 1.0f;
	}

	const FBackstabLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	UE_LOG(LogTemp, Warning, TEXT("[BACKSTAB_DEBUG] Conditions met on %s — applying %.2fx multiplier"),
		*Target->GetName(), LD.DamageMultiplier);
	return LD.DamageMultiplier;
}

bool UUpgrade_Backstab::IsTargetStunned(AActor* Target) const
{
	const AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC)
	{
		return false;
	}

	const FBackstabLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	if (LD.bRequireStunnedByExplosion)
	{
		// Narrow: only explosion-driven stuns qualify.
		return NPC->IsStunnedByExplosion();
	}

	// Broad: any knockback state (covers explosion stun, normal knockback, etc.).
	return NPC->IsInKnockback();
}

bool UUpgrade_Backstab::IsPlayerBehindTarget(AActor* Target) const
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || !Target)
	{
		return false;
	}

	const FVector TargetForward = Target->GetActorForwardVector();
	const FVector ToPlayer = (Character->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	if (ToPlayer.IsNearlyZero())
	{
		return false;
	}

	// Player is "behind" if the angle between Target->Forward and Target->ToPlayer
	// exceeds (180° - BackConeHalfAngle). Equivalently:
	//   FwdDot = dot(TargetForward, ToPlayer)
	//   threshold = cos(180° - BackConeHalfAngle) = -cos(BackConeHalfAngle)
	//   player behind iff FwdDot <= threshold
	// BackConeHalfAngle=180° → threshold=-cos(180°)=+1 → impossible match? No:
	//   actually we want the inverted convention. Use the angle between -Forward (Back)
	//   and ToPlayer directly — simpler.
	const FVector BackDir = -TargetForward;
	const float BackDot = FVector::DotProduct(BackDir, ToPlayer);

	const FBackstabLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(LD.BackConeHalfAngle));
	// Player is inside the back cone if the dot between back direction and ToPlayer
	// is >= cos(half-angle). At HalfAngle=90 this is dot >= 0 (entire back hemisphere).
	// At HalfAngle=45 this is dot >= 0.707 (tight cone directly behind).
	return BackDot >= CosHalf;
}
