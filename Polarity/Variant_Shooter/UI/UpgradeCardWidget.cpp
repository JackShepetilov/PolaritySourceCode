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
		UpgradeDescription = Definition->Description;
		UpgradeIcon = Definition->Icon;
		UpgradeTier = Definition->Tier;
		UpgradeMaxLevel = Definition->MaxLevel;
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

	UE_LOG(LogTemp, Log,
		TEXT("[XP_DEBUG] Card pre-BP: this=%p UpgradeName='%s' Tier=%d Level=%d/%d"),
		this, *UpgradeName.ToString(), UpgradeTier, UpgradeCurrentLevel, UpgradeMaxLevel);

	BP_OnInitialized(UpgradeName, UpgradeDescription, UpgradeIcon, UpgradeTier, UpgradeCurrentLevel, UpgradeMaxLevel);
}

void UUpgradeCardWidget::RequestSelect()
{
	OnSelected.Broadcast(Index);
}
