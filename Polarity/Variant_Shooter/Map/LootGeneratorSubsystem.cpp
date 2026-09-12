// LootGeneratorSubsystem.cpp

#include "Variant_Shooter/Map/LootGeneratorSubsystem.h"

#include "Variant_Shooter/Map/LootTables.h"
#include "Engine/World.h"

ULootGeneratorSubsystem* ULootGeneratorSubsystem::GetLootGenerator(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<ULootGeneratorSubsystem>() : nullptr;
}

ULootRegistry* ULootGeneratorSubsystem::GetRegistry()
{
	if (Registry)
	{
		return Registry;
	}

	if (RegistryPath.IsNull())
	{
		return nullptr;
	}

	// Synchronous, once, at the first roll of the run. An async load would finish after the first
	// point has already decided it has nothing.
	Registry = Cast<ULootRegistry>(RegistryPath.TryLoad());

	if (Registry && !bGapsLogged)
	{
		bGapsLogged = true;
		Registry->LogGaps();
	}

	return Registry;
}

float ULootGeneratorSubsystem::DrawValue(float Quality, float Spread)
{
	// Three uniforms averaged: a bell without a library. Box-Muller would be exact and would also
	// hand back the occasional value four deviations out, which on a 0..1 axis means a starter
	// point handing out the best thing in the game. This one cannot leave +/- Spread, so what a
	// place is worth stays a promise rather than a suggestion.
	const float Bell = (FMath::FRandRange(-1.0f, 1.0f)
		+ FMath::FRandRange(-1.0f, 1.0f)
		+ FMath::FRandRange(-1.0f, 1.0f)) / 3.0f;

	return FMath::Clamp(Quality + Spread * Bell, 0.0f, 1.0f);
}

bool ULootGeneratorSubsystem::RollOne(float Quality, float Spread, FLootRoll& OutRoll)
{
	ULootRegistry* Reg = GetRegistry();
	if (!Reg)
	{
		return false;
	}

	ELootCategory Category = ELootCategory::Ammo;
	if (!Reg->PickCategory(Category))
	{
		return false;
	}

	const ULootPool* Pool = Reg->GetPool(Category);
	if (!Pool)
	{
		return false;
	}

	const float Value = DrawValue(Quality, Spread);

	FPoiLootEntry Entry;
	if (!Pool->PickEntry(Value, Entry))
	{
		return false;
	}

	OutRoll.Entry = Entry;
	OutRoll.Value = Value;

	// The roll decides HOW MUCH as well as what. This is the half of continuous rarity that buckets
	// could never express: a good roll on an ammo line is not a different line, it is more rounds.
	OutRoll.Count = FMath::Clamp(
		FMath::RoundToInt(FMath::Lerp(static_cast<float>(Entry.CountMin),
			static_cast<float>(FMath::Max(Entry.CountMin, Entry.CountMax)), Value)),
		1, 20);

	OutRoll.AmountScale = FMath::Lerp(Entry.AmountScaleMin,
		FMath::Max(Entry.AmountScaleMin, Entry.AmountScaleMax), Value);

	return true;
}

bool ULootGeneratorSubsystem::RollSheet(float Quality, float Spread, TArray<FLootRoll>& OutRolls)
{
	OutRolls.Reset();

	ULootRegistry* Reg = GetRegistry();
	if (!Reg)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LOOT] No loot registry configured (RegistryPath is empty). "
			"Set it in DefaultGame.ini under [/Script/Polarity.LootGeneratorSubsystem]."));
		return false;
	}

	const int32 Slots = Reg->RollSlotCount(Quality);

	for (int32 i = 0; i < Slots; ++i)
	{
		FLootRoll Roll;
		if (RollOne(Quality, Spread, Roll))
		{
			OutRolls.Add(Roll);
		}
	}

	return !OutRolls.IsEmpty();
}
