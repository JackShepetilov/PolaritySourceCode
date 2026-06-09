// LoreSubsystem.cpp

#include "LoreSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Save/SaveGameSubsystem.h"
#include "Save/PolarityMetaSave.h"

void ULoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Restore persisted lore progress from the save profile. SaveGameSubsystem depends on nobody,
	// so this dependency is acyclic and guarantees the meta file has loaded before we read it.
	if (USaveGameSubsystem* Save = Cast<USaveGameSubsystem>(
			Collection.InitializeDependency(USaveGameSubsystem::StaticClass())))
	{
		if (const UPolarityMetaSave* M = Save->GetMeta())
		{
			ConsumedLoreIDs = M->ConsumedLoreIDs;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] LoreSubsystem initialized (%d consumed restored)"), ConsumedLoreIDs.Num());
}

void ULoreSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void ULoreSubsystem::SetLoreTables(const TArray<UDataTable*>& InTables)
{
	LoreTables.Reset();
	for (UDataTable* T : InTables)
	{
		if (T) { LoreTables.Add(T); }
	}
	UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] Lore tables set: %d table(s)"), LoreTables.Num());
}

bool ULoreSubsystem::PickAndConsumeLoreForArena(FName ArenaTag, FName Biome, FLoreEntryRow& OutEntry)
{
	TArray<const FLoreEntryRow*> Candidates;

	// Phase 1: arena-specific
	CollectCandidates(ArenaTag, Biome, ELoreScope::Arena, Candidates);
	if (Candidates.Num() == 0)
	{
		// Phase 2: biome-general
		CollectCandidates(ArenaTag, Biome, ELoreScope::Biome, Candidates);
	}
	if (Candidates.Num() == 0)
	{
		// Phase 3: global
		CollectCandidates(ArenaTag, Biome, ELoreScope::Global, Candidates);
	}
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] No eligible lore for arena=%s biome=%s"),
			*ArenaTag.ToString(), *Biome.ToString());
		return false;
	}

	// Sort by Priority desc, then pick uniformly among ties at the top priority.
	Candidates.Sort([](const FLoreEntryRow& A, const FLoreEntryRow& B)
	{
		return A.Priority > B.Priority;
	});

	const int32 TopPriority = Candidates[0]->Priority;
	int32 TiedCount = 1;
	while (TiedCount < Candidates.Num() && Candidates[TiedCount]->Priority == TopPriority)
	{
		++TiedCount;
	}

	const int32 PickedIdx = FMath::RandRange(0, TiedCount - 1);
	OutEntry = *Candidates[PickedIdx];

	MarkLoreConsumed(OutEntry.LoreID);

	UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] Picked %s (scope=%d, priority=%d) for arena=%s biome=%s"),
		*OutEntry.LoreID.ToString(), static_cast<int32>(OutEntry.Scope), OutEntry.Priority,
		*ArenaTag.ToString(), *Biome.ToString());

	return true;
}

bool ULoreSubsystem::IsLoreAvailable(FName LoreID) const
{
	if (IsLoreConsumed(LoreID)) { return false; }
	const FLoreEntryRow* Row = FindRowByLoreID(LoreID);
	if (!Row) { return false; }
	return ArePrerequisitesMet(*Row);
}

bool ULoreSubsystem::IsLoreConsumed(FName LoreID) const
{
	return ConsumedLoreIDs.Contains(LoreID);
}

void ULoreSubsystem::MarkLoreConsumed(FName LoreID)
{
	if (LoreID.IsNone()) { return; }
	if (ConsumedLoreIDs.Contains(LoreID)) { return; }
	ConsumedLoreIDs.Add(LoreID);

	if (const FLoreEntryRow* Row = FindRowByLoreID(LoreID))
	{
		OnLoreConsumed.Broadcast(*Row);
	}

	// Lore progress is meta-persistent — flag the save (debounced).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USaveGameSubsystem* Save = GI->GetSubsystem<USaveGameSubsystem>())
		{
			Save->RequestMetaSave();
		}
	}
}

TArray<FName> ULoreSubsystem::GetUnconsumedInBiome(FName Biome) const
{
	TArray<FName> Out;
	for (const TObjectPtr<UDataTable>& Table : LoreTables)
	{
		if (!Table) { continue; }
		for (const FName& RowName : Table->GetRowNames())
		{
			const FLoreEntryRow* Row = Table->FindRow<FLoreEntryRow>(RowName, TEXT("GetUnconsumedInBiome"));
			if (Row && Row->Biome == Biome && !ConsumedLoreIDs.Contains(Row->LoreID))
			{
				Out.Add(Row->LoreID);
			}
		}
	}
	return Out;
}

TArray<FName> ULoreSubsystem::GetAllLoreIDs() const
{
	TArray<FName> Out;
	for (const TObjectPtr<UDataTable>& Table : LoreTables)
	{
		if (!Table) { continue; }
		for (const FName& RowName : Table->GetRowNames())
		{
			if (const FLoreEntryRow* Row = Table->FindRow<FLoreEntryRow>(RowName, TEXT("GetAllLoreIDs")))
			{
				Out.Add(Row->LoreID);
			}
		}
	}
	return Out;
}

void ULoreSubsystem::DebugReset()
{
	const int32 N = ConsumedLoreIDs.Num();
	ConsumedLoreIDs.Reset();
	UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] Reset — cleared %d consumed entries"), N);
}

// ==================== Internals ====================

const FLoreEntryRow* ULoreSubsystem::FindRowByLoreID(FName LoreID) const
{
	for (const TObjectPtr<UDataTable>& Table : LoreTables)
	{
		if (!Table) { continue; }
		for (const FName& RowName : Table->GetRowNames())
		{
			if (const FLoreEntryRow* Row = Table->FindRow<FLoreEntryRow>(RowName, TEXT("FindRowByLoreID")))
			{
				if (Row->LoreID == LoreID) { return Row; }
			}
		}
	}
	return nullptr;
}

bool ULoreSubsystem::ArePrerequisitesMet(const FLoreEntryRow& Entry) const
{
	for (const FName& Required : Entry.RequiredLoreIDs)
	{
		if (!ConsumedLoreIDs.Contains(Required)) { return false; }
	}
	return true;
}

void ULoreSubsystem::CollectCandidates(FName ArenaTag, FName Biome, ELoreScope FilterScope, TArray<const FLoreEntryRow*>& OutCandidates) const
{
	OutCandidates.Reset();
	for (const TObjectPtr<UDataTable>& Table : LoreTables)
	{
		if (!Table) { continue; }
		for (const FName& RowName : Table->GetRowNames())
		{
			const FLoreEntryRow* Row = Table->FindRow<FLoreEntryRow>(RowName, TEXT("CollectCandidates"));
			if (!Row) { continue; }
			if (Row->Scope != FilterScope) { continue; }
			if (ConsumedLoreIDs.Contains(Row->LoreID)) { continue; }
			if (!ArePrerequisitesMet(*Row)) { continue; }

			// Scope-specific filtering
			if (FilterScope == ELoreScope::Arena)
			{
				if (Row->ArenaTag != ArenaTag) { continue; }
			}
			else if (FilterScope == ELoreScope::Biome)
			{
				if (Row->Biome != Biome) { continue; }
			}
			// Global: no further filter

			OutCandidates.Add(Row);
		}
	}
}
