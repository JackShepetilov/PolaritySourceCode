// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeTooltipWidget.h"
#include "UpgradeDefinition.h"

void UUpgradeTooltipWidget::InitFromDefinition(UUpgradeDefinition* Definition)
{
	if (!Definition)
	{
		return;
	}

	UpgradeName = Definition->DisplayName;
	UpgradeDescription = Definition->Description;
	UpgradeIcon = Definition->Icon;
	UpgradeTier = Definition->Tier;

	BP_OnTooltipInitialized();
}
