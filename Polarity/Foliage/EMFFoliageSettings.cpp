// EMFFoliageSettings.cpp

#include "EMFFoliageSettings.h"

// Full definition needed: TSoftObjectPtr<UFoliageType>::Get() instantiates
// dynamic_cast<UFoliageType*>(...) which requires the complete type.
#include "FoliageType.h"

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

	// Direct pointer comparison is sufficient: any FoliageType painted on the
	// current level is loaded (Foliage Tool keeps used types resident), so
	// Entry.FoliageType.Get() will resolve. Soft pointer to an asset on a
	// different level can't match an in-flight hit on this level anyway.
	for (const FEMFFoliageEntry& Entry : Entries)
	{
		if (Entry.FoliageType.Get() == InType)
		{
			return &Entry;
		}
	}
	return nullptr;
}
