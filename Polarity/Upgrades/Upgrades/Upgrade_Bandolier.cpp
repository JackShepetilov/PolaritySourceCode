// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_Bandolier.h"
#include "UpgradeDefinition_Bandolier.h"

UUpgrade_Bandolier::UUpgrade_Bandolier()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_Bandolier::OnUpgradeActivated()
{
	DefBandolier = Cast<UUpgradeDefinition_Bandolier>(UpgradeDefinition);
	if (!DefBandolier.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[BANDOLIER] UpgradeDefinition is not UUpgradeDefinition_Bandolier!"));
	}
}

int32 UUpgrade_Bandolier::GetMaxCopiesForCurrentLevel() const
{
	if (!DefBandolier.IsValid())
	{
		return 1;
	}
	const FBandolierLevelData& Data = DefBandolier->GetLevelData(CurrentLevel);
	return FMath::Max(1, Data.MaxCopies);
}
