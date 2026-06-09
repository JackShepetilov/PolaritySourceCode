// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AdsTimeDilation.h"
#include "UpgradeDefinition_AdsTimeDilation.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/WeaponRecoilComponent.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"

UUpgrade_AdsTimeDilation::UUpgrade_AdsTimeDilation()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

const UUpgradeDefinition_AdsTimeDilation* UUpgrade_AdsTimeDilation::GetDef() const
{
	return Cast<UUpgradeDefinition_AdsTimeDilation>(UpgradeDefinition);
}

void UUpgrade_AdsTimeDilation::OnUpgradeActivated()
{
	const UUpgradeDefinition_AdsTimeDilation* Def = GetDef();
	if (!Def)
	{
		UE_LOG(LogTemp, Error, TEXT("[ADS_SLOWMO] UpgradeDefinition is not UUpgradeDefinition_AdsTimeDilation"));
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		UE_LOG(LogTemp, Error, TEXT("[ADS_SLOWMO] No owning ShooterCharacter — upgrade can't run"));
		return;
	}

	CachedUpgradeManager = Character->GetUpgradeManager();
	CachedRecoilComp = Character->GetRecoilComponent();

	bEngaged = false;
	BlendAlpha = 0.0f;
	bTimeApplied = false;
	DrainAccumulator = 0.0f;

	// No ADS delegate exists on the character — poll IsAiming() && IsFalling() each frame.
	SetComponentTickEnabled(true);

	const FAdsTimeDilationLevelData& Data = Def->GetLevelData(CurrentLevel);
	UE_LOG(LogTemp, Warning, TEXT("[ADS_SLOWMO] ACTIVATED (Lv %d) — Drain=%.1f/s, GlobalDilation=%.2f, PlayerComp=%.2f, Recoil=%.2f"),
		CurrentLevel, Data.PickupsPerSecond, Data.GlobalTimeDilation, Data.PlayerTimeCompensation, Data.RecoilMultiplier);
}

void UUpgrade_AdsTimeDilation::OnUpgradeDeactivated()
{
	RestoreNeutral();
	StopCosmetics();
	bEngaged = false;
	BlendAlpha = 0.0f;
	DrainAccumulator = 0.0f;
	SetComponentTickEnabled(false);
}

void UUpgrade_AdsTimeDilation::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	// Critical: if the component is torn down (level end, actor destroyed) without a clean
	// OnUpgradeDeactivated, never leave global time dilation altered.
	RestoreNeutral();
	StopCosmetics();
	Super::EndPlay(EndPlayReason);
}

void UUpgrade_AdsTimeDilation::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UUpgradeDefinition_AdsTimeDilation* Def = GetDef();
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Def || !Character)
	{
		if (bTimeApplied)
		{
			RestoreNeutral();
			StopCosmetics();
		}
		return;
	}

	const FAdsTimeDilationLevelData& Data = Def->GetLevelData(CurrentLevel);

	// Convert the (dilated) tick delta back to wall-clock seconds so drain & blend run in real time
	// regardless of how slow the world currently is.
	const float ActorDilation = FMath::Max(Character->GetActorTimeDilation(), 0.001f);
	const float RealDelta = DeltaTime / ActorDilation;

	UUpgradeManagerComponent* Mgr = CachedUpgradeManager.Get();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	const int32 PoolNow = Mgr ? Mgr->GetStoredHealthPickups() : 0;
	const bool bConditionsHold =
		!Character->IsDead() &&
		Character->IsAiming() &&
		Movement && Movement->IsFalling();

	if (!bEngaged)
	{
		// Try to engage: conditions hold AND enough pool to start.
		if (bConditionsHold && PoolNow >= Data.MinPickupsToActivate)
		{
			bEngaged = true;
			DrainAccumulator = 0.0f;
			StartCosmetics();
			if (Def->ActivateSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, Def->ActivateSound, Character->GetActorLocation());
			}
			UE_LOG(LogTemp, Warning, TEXT("[ADS_SLOWMO] ENGAGE — pool=%d"), PoolNow);
		}
	}
	else
	{
		if (!bConditionsHold || PoolNow <= 0)
		{
			// Lost a condition (landed / lowered sights / died) or pool empty — disengage.
			bEngaged = false;
			if (Def->DeactivateSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, Def->DeactivateSound, Character->GetActorLocation());
			}
			UE_LOG(LogTemp, Warning, TEXT("[ADS_SLOWMO] DISENGAGE — conditions=%d, pool=%d"), bConditionsHold ? 1 : 0, PoolNow);
		}
		else
		{
			// Drain the shared pool in real seconds.
			DrainAccumulator += Data.PickupsPerSecond * RealDelta;
			if (DrainAccumulator >= 1.0f && Mgr)
			{
				const int32 ToDrain = FMath::FloorToInt(DrainAccumulator);
				const int32 Drained = Mgr->ConsumeStoredHealthPickups(ToDrain);
				DrainAccumulator -= static_cast<float>(Drained);
				if (Drained < ToDrain)
				{
					// Pool emptied mid-drain — disengage.
					bEngaged = false;
					if (Def->DeactivateSound)
					{
						UGameplayStatics::PlaySoundAtLocation(this, Def->DeactivateSound, Character->GetActorLocation());
					}
					UE_LOG(LogTemp, Warning, TEXT("[ADS_SLOWMO] Pool depleted — disengage"));
				}
			}
		}
	}

	// Ramp the blend toward the target in real seconds.
	const float TargetAlpha = bEngaged ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(BlendAlpha, TargetAlpha))
	{
		if (BlendAlpha < TargetAlpha)
		{
			const float Rate = (Def->BlendInTime > KINDA_SMALL_NUMBER) ? (RealDelta / Def->BlendInTime) : 1.0f;
			BlendAlpha = FMath::Min(TargetAlpha, BlendAlpha + Rate);
		}
		else
		{
			const float Rate = (Def->BlendOutTime > KINDA_SMALL_NUMBER) ? (RealDelta / Def->BlendOutTime) : 1.0f;
			BlendAlpha = FMath::Max(TargetAlpha, BlendAlpha - Rate);
		}
	}

	if (BlendAlpha > 0.0f)
	{
		ApplyForAlpha(BlendAlpha);
	}
	else if (bTimeApplied)
	{
		// Fully blended out — restore once and stop touching global time so we don't fight other
		// time-dilation systems (e.g. kill slow-mo) while idle.
		RestoreNeutral();
		StopCosmetics();
	}
}

void UUpgrade_AdsTimeDilation::ApplyForAlpha(float Alpha)
{
	const UUpgradeDefinition_AdsTimeDilation* Def = GetDef();
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Def || !Character)
	{
		return;
	}
	const FAdsTimeDilationLevelData& Data = Def->GetLevelData(CurrentLevel);

	// Global world slow (ramped from 1.0 -> GlobalTimeDilation by Alpha).
	const float GlobalNow = FMath::Lerp(1.0f, Data.GlobalTimeDilation, Alpha);
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, GlobalNow);
	}

	// Compensate the player back up: effective player time = Lerp(GlobalNow, 1.0, Comp).
	// Player CustomTimeDilation = effective / GlobalNow (the engine multiplies the two back together).
	const float SafeGlobal = FMath::Max(GlobalNow, 0.001f);
	const float EffectivePlayerScale = FMath::Lerp(GlobalNow, 1.0f, Data.PlayerTimeCompensation);
	Character->CustomTimeDilation = EffectivePlayerScale / SafeGlobal;

	// Recoil reduction (ramped from 1.0 -> RecoilMultiplier by Alpha).
	if (UWeaponRecoilComponent* Recoil = CachedRecoilComp.Get())
	{
		const float RecoilNow = FMath::Lerp(1.0f, Data.RecoilMultiplier, Alpha);
		Recoil->SetExternalRecoilMultiplier(RecoilNow);
	}

	bTimeApplied = true;
}

void UUpgrade_AdsTimeDilation::RestoreNeutral()
{
	// Only reset global / player time if WE are the ones who changed it — avoids stomping other
	// systems' global time dilation when this upgrade is idle.
	if (bTimeApplied)
	{
		if (UWorld* World = GetWorld())
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
		}
		if (AShooterCharacter* Character = GetShooterCharacter())
		{
			Character->CustomTimeDilation = 1.0f;
		}
	}

	// The recoil scalar is owned solely by us — always safe to clear.
	if (UWeaponRecoilComponent* Recoil = CachedRecoilComp.Get())
	{
		Recoil->SetExternalRecoilMultiplier(1.0f);
	}

	bTimeApplied = false;
}

void UUpgrade_AdsTimeDilation::StartCosmetics()
{
	const UUpgradeDefinition_AdsTimeDilation* Def = GetDef();
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Def || !Character)
	{
		return;
	}
	USceneComponent* Root = Character->GetRootComponent();

	if (Def->LoopSound && !ActiveLoopSound)
	{
		ActiveLoopSound = UGameplayStatics::SpawnSoundAttached(Def->LoopSound, Root);
	}

	if (Def->ActiveVFX && !ActiveVFXComp)
	{
		ActiveVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Def->ActiveVFX, Root, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, /*bAutoDestroy=*/true, /*bAutoActivate=*/true);
	}
}

void UUpgrade_AdsTimeDilation::StopCosmetics()
{
	if (ActiveLoopSound)
	{
		ActiveLoopSound->Stop();
		ActiveLoopSound = nullptr;
	}

	if (ActiveVFXComp)
	{
		// Deactivate stops spawning; existing particles finish, then bAutoDestroy cleans it up.
		ActiveVFXComp->Deactivate();
		ActiveVFXComp = nullptr;
	}
}
