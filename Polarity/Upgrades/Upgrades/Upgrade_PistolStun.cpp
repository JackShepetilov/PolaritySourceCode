// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_PistolStun.h"
#include "UpgradeDefinition_PistolStun.h"
#include "ShooterNPC.h"
#include "HumanoidNPC.h"
#include "SniperTurretNPC.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UUpgrade_PistolStun::UUpgrade_PistolStun()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_PistolStun::OnUpgradeActivated()
{
	DefPistolStun = Cast<UUpgradeDefinition_PistolStun>(UpgradeDefinition);
	if (!DefPistolStun.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[PISTOL_STUN] UpgradeDefinition is not UUpgradeDefinition_PistolStun!"));
	}
}

void UUpgrade_PistolStun::OnHitscanIonized(AActor* Target)
{
	if (!DefPistolStun.IsValid() || !Target)
	{
		return;
	}

	// Sniper turret immune per design spec
	if (Target->IsA<ASniperTurretNPC>())
	{
		return;
	}

	AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC)
	{
		return;
	}

	const FPistolStunLevelData& Data = DefPistolStun->GetLevelData(CurrentLevel);

	if (Data.StunDuration <= 0.0f)
	{
		return;
	}

	// Per-target cooldown gate
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	if (const double* LastTime = LastStunTimeByNPC.Find(NPC))
	{
		if ((Now - *LastTime) < Data.StunCooldownPerTarget)
		{
			return;
		}
	}

	NPC->ApplyExplosionStun(Data.StunDuration, Data.StunMontage);
	LastStunTimeByNPC.Add(NPC, Now);

	if (Data.StunSound)
	{
		UGameplayStatics::SpawnSoundAttached(
			Data.StunSound,
			NPC->GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			EAttachLocation::KeepRelativeOffset,
			/*bStopWhenAttachedToDestroyed=*/ true,
			Data.SoundVolume,
			Data.SoundPitch);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[PISTOL_STUN] %s stunned for %.2fs (Lv %d)"),
		*NPC->GetName(), Data.StunDuration, CurrentLevel);
}
