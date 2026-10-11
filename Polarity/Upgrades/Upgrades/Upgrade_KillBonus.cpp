// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_KillBonus.h"
#include "UpgradeDefinition_KillBonus.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/UI/ScreenEdgeComponent.h"
#include "UpgradeManagerComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UUpgrade_KillBonus::UUpgrade_KillBonus()
{
	// Only the regen kind ticks, and only while its clock runs.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_KillBonus::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_KillBonus>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[KILLBONUS_DEBUG] Failed to cast UpgradeDefinition to KillBonus type"));
	}
}

void UUpgrade_KillBonus::OnUpgradeDeactivated()
{
	BonusEndTime = -1.0f;
	SetComponentTickEnabled(false);
}

bool UUpgrade_KillBonus::IsBonusActive() const
{
	const UWorld* const World = GetWorld();
	return World && BonusEndTime > World->GetTimeSeconds();
}

void UUpgrade_KillBonus::OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled)
{
	// A kill of something that fights, not of a crate.
	const UWorld* const World = GetWorld();
	if (!bKilled || !World || !CachedDef.IsValid() || !Cast<APawn>(Target))
	{
		return;
	}

	// Within a second: an enemy that is reused later is a new kill.
	const float Now = World->GetTimeSeconds();
	if (LastPaidKill.Get() == Target && Now - LastPaidKillTime < 1.0f)
	{
		return;
	}
	LastPaidKill = Target;
	LastPaidKillTime = Now;

	const FKillBonusLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	AShooterCharacter* const Character = GetShooterCharacter();

	switch (CachedDef->Kind)
	{
	case EKillBonusKind::AbilityCooldown:
		// The cooldown is the authority's; a client's copy of this upgrade has nothing to cut.
		if (Character && Character->HasAuthority())
		{
			if (UAbilityComponent* const Abilities = Character->FindComponentByClass<UAbilityComponent>())
			{
				Abilities->ReduceCooldownByFraction(LD.Magnitude);
			}
		}
		ShowFeedback(CachedDef->InstantFeedbackTime);
		break;

	case EKillBonusKind::HealthRegen:
		BonusEndTime = World->GetTimeSeconds() + LD.Duration;
		// Health is the authority's to write, so only its copy ticks.
		SetComponentTickEnabled(Character && Character->HasAuthority());
		ShowFeedback(LD.Duration);
		break;

	case EKillBonusKind::ReloadSpeed:
	default:
		BonusEndTime = World->GetTimeSeconds() + LD.Duration;
		ShowFeedback(LD.Duration);
		break;
	}

	UE_LOG(LogTemp, Log, TEXT("[KILLBONUS_DEBUG] %s: kind %d on kill of %s (magnitude %.2f, %.1fs)"),
		*GetNameSafe(UpgradeDefinition), (int32)CachedDef->Kind, *GetNameSafe(Target), LD.Magnitude, LD.Duration);
}

void UUpgrade_KillBonus::OnOwnerTookDamage(float Damage, AActor* DamageCauser)
{
	// Getting hit ends the regen, and only the regen.
	if (Damage > 0.0f && CachedDef.IsValid() && CachedDef->Kind == EKillBonusKind::HealthRegen && IsBonusActive())
	{
		BonusEndTime = -1.0f;
		SetComponentTickEnabled(false);
		HideFeedback();
	}
}

void UUpgrade_KillBonus::ShowFeedback(float Seconds)
{
	AShooterCharacter* const Character = GetShooterCharacter();
	if (!CachedDef.IsValid() || !Character || !Character->IsLocallyControlled() || Seconds <= 0.0f)
	{
		return;
	}

	// Keyed by the asset, so two kill bonuses running at once are two lines and two glows.
	const FName Key = CachedDef->GetFName();
	if (!CachedDef->NoticeText.IsEmpty())
	{
		if (UUpgradeManagerComponent* const Manager = Character->GetUpgradeManager())
		{
			Manager->RaiseNotice(Key, CachedDef->NoticeText, CachedDef->NoticeColorTag, Seconds);
		}
	}
	if (CachedDef->ScreenEdgeMaterial)
	{
		if (UScreenEdgeComponent* const Edge = UScreenEdgeComponent::GetOrCreate(Character))
		{
			Edge->Play(Key, CachedDef->ScreenEdgeMaterial, CachedDef->ScreenEdgeColor, Seconds);
		}
	}
}

void UUpgrade_KillBonus::HideFeedback()
{
	AShooterCharacter* const Character = GetShooterCharacter();
	if (!CachedDef.IsValid() || !Character || !Character->IsLocallyControlled())
	{
		return;
	}

	const FName Key = CachedDef->GetFName();
	if (UUpgradeManagerComponent* const Manager = Character->GetUpgradeManager())
	{
		Manager->RaiseNotice(Key, FText::GetEmpty(), FGameplayTag(), 0.0f);
	}
	if (UScreenEdgeComponent* const Edge = Character->FindComponentByClass<UScreenEdgeComponent>())
	{
		Edge->Stop(Key);
	}
}

float UUpgrade_KillBonus::GetReloadSpeedMultiplier() const
{
	if (!CachedDef.IsValid() || CachedDef->Kind != EKillBonusKind::ReloadSpeed || !IsBonusActive())
	{
		return 1.0f;
	}
	return FMath::Max(0.1f, CachedDef->GetLevelData(CurrentLevel).Magnitude);
}

void UUpgrade_KillBonus::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AShooterCharacter* const Character = GetShooterCharacter();
	if (!IsBonusActive() || !CachedDef.IsValid() || !Character || Character->IsDead())
	{
		SetComponentTickEnabled(false);
		return;
	}

	Character->RestoreHealth(CachedDef->GetLevelData(CurrentLevel).Magnitude * DeltaTime);
}
