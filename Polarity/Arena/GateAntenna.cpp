// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "GateAntenna.h"
#include "Polarity/Variant_Shooter/Run/RunSubsystem.h"
#include "Engine/GameInstance.h"

void AGateAntenna::BeginPlay()
{
	// Base binds the button, applies initial visuals (default Inactive — beacon off).
	Super::BeginPlay();

	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnAntennaCountChanged.AddDynamic(this, &AGateAntenna::HandleAntennaCountChanged);

		// Seed from the persisted count: antennas may already have been activated on earlier
		// sublevels/biomes this run, so the gate must be able to unlock immediately on spawn.
		EvaluateUnlock(Run->GetActivatedAntennaCount());

		UE_LOG(LogTemp, Warning, TEXT("[GATE_DEBUG] [%s] BeginPlay: Required=%d, current run count=%d, unlocked=%d"),
			*GetName(), RequiredAntennaCount, Run->GetActivatedAntennaCount(), bUnlocked ? 1 : 0);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GATE_DEBUG] [%s] BeginPlay: RunSubsystem NOT FOUND — gate will never unlock"), *GetName());
	}
}

void AGateAntenna::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnAntennaCountChanged.RemoveDynamic(this, &AGateAntenna::HandleAntennaCountChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void AGateAntenna::TryActivate()
{
	if (!bUnlocked)
	{
		const int32 Current = GetRunSubsystem() ? GetRunSubsystem()->GetActivatedAntennaCount() : 0;
		UE_LOG(LogTemp, Warning, TEXT("[GATE_DEBUG] [%s] Pressed while LOCKED (%d/%d) — broadcasting OnGateLockedAttempt"),
			*GetName(), Current, RequiredAntennaCount);
		OnGateLockedAttempt.Broadcast(Current, RequiredAntennaCount);
		return;
	}

	// Unlocked: behave exactly like a normal antenna press (plays dialogue if configured,
	// flips to Activated, fires the inherited OnActivated that Level BP listens to).
	Super::TryActivate();
}

void AGateAntenna::HandleAntennaCountChanged(int32 NewCount)
{
	EvaluateUnlock(NewCount);
}

void AGateAntenna::EvaluateUnlock(int32 Count)
{
	if (bUnlocked)
	{
		return;
	}

	if (Count >= RequiredAntennaCount)
	{
		bUnlocked = true;

		UE_LOG(LogTemp, Warning, TEXT("[GATE_DEBUG] [%s] UNLOCKED (%d/%d) — beacon on, broadcasting OnGateUnlocked"),
			*GetName(), Count, RequiredAntennaCount);

		// Reuse the existing "beacon points you here" visual — same language as post-fight antennas.
		SetState(EAntennaState::AvailablePostFight);

		OnGateUnlocked.Broadcast();
	}
}

URunSubsystem* AGateAntenna::GetRunSubsystem() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<URunSubsystem>() : nullptr;
}
