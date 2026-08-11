// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeOfferSchedule.h"

bool UUpgradeOfferSchedule::TryGetCategoryForLevel(int32 Level, ESkillCategory& OutCategory) const
{
	if (const ESkillCategory* FoundCategory = LevelToCategory.Find(Level))
	{
		OutCategory = *FoundCategory;
		return true;
	}

	if (bUseAllCategoriesWhenLevelMissing)
	{
		return false;
	}

	OutCategory = DefaultCategory;
	return true;
}
