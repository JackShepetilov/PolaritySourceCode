// UpgradeCardWidget.cpp

#include "UpgradeCardWidget.h"

#include "UpgradeDefinition.h"
#include "UpgradeManagerComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void UUpgradeCardWidget::InitFromDefinition(UUpgradeDefinition* InDefinition, int32 InIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] Card::InitFromDefinition idx=%d def=%s name='%s'"),
		InIndex,
		InDefinition ? *InDefinition->GetName() : TEXT("NULL"),
		InDefinition ? *InDefinition->DisplayName.ToString() : TEXT(""));

	Definition = InDefinition;
	Index = InIndex;

	if (Definition)
	{
		UpgradeName = Definition->DisplayName;
		UpgradeIcon = Definition->Icon;
		UpgradeTier = Definition->Tier;
		UpgradeMaxLevel = Definition->MaxLevel;
		// UpgradeDescription is set further down using GetDescriptionForLevel(DisplayLevel)
	}
	else
	{
		UpgradeName = FText::GetEmpty();
		UpgradeDescription = FText::GetEmpty();
		UpgradeIcon = nullptr;
		UpgradeTier = 0;
		UpgradeMaxLevel = 1;
	}

	// Look up current level on the player's UpgradeManager (0 if not yet owned)
	UpgradeCurrentLevel = 0;
	if (Definition)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				if (UUpgradeManagerComponent* Mgr = Pawn->FindComponentByClass<UUpgradeManagerComponent>())
				{
					UpgradeCurrentLevel = Mgr->GetUpgradeLevel(Definition->UpgradeTag);
				}
			}
		}
	}

	// Choice screen always shows the level the player would have AFTER accepting:
	//  - not yet owned (CurrentLevel == 0) -> display Lv 1
	//  - already at Lv N (< MaxLevel)       -> display Lv N+1
	//  - clamped to MaxLevel (defensive — maxed-out upgrades shouldn't reach here)
	const int32 DisplayLevel = (UpgradeCurrentLevel == 0)
		? 1
		: FMath::Min(UpgradeCurrentLevel + 1, FMath::Max(UpgradeMaxLevel, 1));

	UpgradeDescription = Definition ? Definition->GetDescriptionForLevel(DisplayLevel) : FText::GetEmpty();
	UpgradeStats = Definition ? Definition->GetDisplayedStats(DisplayLevel) : TArray<FUpgradeStat>();

	UE_LOG(LogTemp, Log,
		TEXT("[XP_DEBUG] Card pre-BP: this=%p UpgradeName='%s' Tier=%d Level=%d/%d (display Lv %d, %d stat rows)"),
		this, *UpgradeName.ToString(), UpgradeTier, UpgradeCurrentLevel, UpgradeMaxLevel, DisplayLevel, UpgradeStats.Num());

	BP_OnInitialized(UpgradeName, UpgradeDescription, UpgradeIcon, UpgradeTier, UpgradeCurrentLevel, UpgradeMaxLevel);
	BP_OnStatsAvailable(UpgradeStats);
}

void UUpgradeCardWidget::RequestSelect()
{
	OnSelected.Broadcast(Index);
}
