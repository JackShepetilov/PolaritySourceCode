// EMFFoliageSettings.cpp

#include "EMFFoliageSettings.h"

UEMFFoliageSettings::UEMFFoliageSettings()
{
	CategoryName = TEXT("Polarity");
	SectionName = TEXT("EMF Foliage Conversion");
}

const FEMFFoliageEntry* UEMFFoliageSettings::FindEntryForFoliageType(const UFoliageType* InType) const
{
	if (!InType)
	{
		return nullptr;
	}

	for (const FEMFFoliageEntry& Entry : Entries)
	{
		// Compare by hard pointer when loaded; fall back to soft path comparison.
		const UFoliageType* EntryType = Entry.FoliageType.Get();
		if (EntryType == InType)
		{
			return &Entry;
		}
		if (!EntryType && Entry.FoliageType.GetUniqueID().GetAssetPath() == FSoftObjectPath(InType).GetAssetPath())
		{
			return &Entry;
		}
	}
	return nullptr;
}
