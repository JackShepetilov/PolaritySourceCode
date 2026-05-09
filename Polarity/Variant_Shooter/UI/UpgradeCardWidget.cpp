// UpgradeCardWidget.cpp

#include "UpgradeCardWidget.h"

#include "UpgradeDefinition.h"

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
	}
	else
	{
		UpgradeName = FText::GetEmpty();
		UpgradeDescription = FText::GetEmpty();
		UpgradeIcon = nullptr;
		UpgradeTier = 0;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[XP_DEBUG] Card pre-BP: this=%p UpgradeName='%s' UpgradeDescription='%s' Tier=%d"),
		this, *UpgradeName.ToString(), *UpgradeDescription.ToString(), UpgradeTier);

	BP_OnInitialized(UpgradeName, UpgradeDescription, UpgradeIcon, UpgradeTier);
}

void UUpgradeCardWidget::RequestSelect()
{
	OnSelected.Broadcast(Index);
}
