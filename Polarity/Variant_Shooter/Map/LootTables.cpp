// LootTables.cpp

#include "Variant_Shooter/Map/LootTables.h"

// ==================== ULootPool ====================

bool ULootPool::PickEntry(float Value, FPoiLootEntry& OutEntry) const
{
	if (Entries.IsEmpty())
	{
		return false;
	}

	// Closeness as a bell rather than as a cutoff. A hard window makes a place hand out exactly one
	// band of things and nothing either side of it, which is the bucket system wearing a float; a
	// bell lets a good roll occasionally reach past itself and a poor one occasionally get lucky,
	// which is what a continuous axis is for.
	const float Sigma = FMath::Max(0.01f, MatchTolerance);
	const float TwoSigmaSq = 2.0f * Sigma * Sigma;

	float Total = 0.0f;
	for (const FPoiLootEntry& Entry : Entries)
	{
		if (!Entry.PickupClass || Entry.Weight <= 0.0f)
		{
			continue;
		}
		const float D = Entry.Value - Value;
		Total += Entry.Weight * FMath::Exp(-(D * D) / TwoSigmaSq);
	}

	if (Total > 0.0f)
	{
		float Roll = FMath::FRandRange(0.0f, Total);
		for (const FPoiLootEntry& Entry : Entries)
		{
			if (!Entry.PickupClass || Entry.Weight <= 0.0f)
			{
				continue;
			}
			const float D = Entry.Value - Value;
			Roll -= Entry.Weight * FMath::Exp(-(D * D) / TwoSigmaSq);
			if (Roll <= 0.0f)
			{
				OutEntry = Entry;
				OutEntry.Category = Category;
				return true;
			}
		}
	}

	// Everything was too far away for the bell to survive the exponent - a pool with a hole in it,
	// or a roll off the end of its range. Nearest line wins rather than nothing at all.
	const FPoiLootEntry* Nearest = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FPoiLootEntry& Entry : Entries)
	{
		if (!Entry.PickupClass)
		{
			continue;
		}
		const float D = FMath::Abs(Entry.Value - Value);
		if (D < BestDistance)
		{
			BestDistance = D;
			Nearest = &Entry;
		}
	}

	if (!Nearest)
	{
		return false;
	}

	OutEntry = *Nearest;
	OutEntry.Category = Category;
	return true;
}

// ==================== ULootRegistry ====================

ULootPool* ULootRegistry::GetPool(ELootCategory Category) const
{
	for (const TObjectPtr<ULootPool>& Pool : Pools)
	{
		if (Pool && Pool->Category == Category)
		{
			return Pool.Get();
		}
	}
	return nullptr;
}

int32 ULootRegistry::RollSlotCount(float Quality) const
{
	const float Q = FMath::Clamp(Quality, 0.0f, 1.0f);
	const int32 Base = FMath::RoundToInt(FMath::Lerp(
		static_cast<float>(SlotsAtZeroQuality), static_cast<float>(SlotsAtFullQuality), Q));

	const int32 Jitter = FMath::Max(0, SlotJitter);
	return FMath::Max(0, Base + FMath::RandRange(-Jitter, Jitter));
}

bool ULootRegistry::PickCategory(ELootCategory& OutCategory) const
{
	float Total = 0.0f;
	for (const TPair<ELootCategory, float>& Pair : CategoryWeights)
	{
		Total += FMath::Max(0.0f, Pair.Value);
	}

	if (Total <= 0.0f)
	{
		return false;
	}

	float Roll = FMath::FRandRange(0.0f, Total);
	for (const TPair<ELootCategory, float>& Pair : CategoryWeights)
	{
		Roll -= FMath::Max(0.0f, Pair.Value);
		if (Roll <= 0.0f)
		{
			OutCategory = Pair.Key;
			return true;
		}
	}

	return false;
}

void ULootRegistry::LogGaps() const
{
	static const UEnum* CategoryEnum = StaticEnum<ELootCategory>();

	for (const TPair<ELootCategory, float>& Pair : CategoryWeights)
	{
		if (Pair.Value <= 0.0f)
		{
			continue;
		}

		const FString CategoryName = CategoryEnum->GetNameStringByValue(static_cast<int64>(Pair.Key));
		const ULootPool* Pool = GetPool(Pair.Key);

		// The quiet failure this catches: the roll picks a category, finds no pool, and the slot
		// comes up empty for no visible reason. A thin sheet is indistinguishable from bad luck.
		if (!Pool)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LOOT] %s weights category %s at %.2f, but no pool provides it."),
				*GetName(), *CategoryName, Pair.Value);
			continue;
		}

		if (Pool->Entries.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[LOOT] pool %s (category %s) has no entries."),
				*Pool->GetName(), *CategoryName);
			continue;
		}

		// A pool whose lines all sit at one end of the axis makes the quality of a place stop
		// meaning anything for that category: rich and poor points hand out the same thing.
		float Low = 1.0f;
		float High = 0.0f;
		for (const FPoiLootEntry& Entry : Pool->Entries)
		{
			if (!Entry.PickupClass)
			{
				continue;
			}
			Low = FMath::Min(Low, Entry.Value);
			High = FMath::Max(High, Entry.Value);
		}

		if (High - Low < 0.2f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LOOT] pool %s (category %s) spans only %.2f..%.2f: "
				"quality will barely change what it hands out."), *Pool->GetName(), *CategoryName, Low, High);
		}
	}
}
