// LoreSubsystem.h
// GameInstance subsystem managing lore progression across runs.
//
// Responsibilities:
// - Hold persistent state (ConsumedLoreIDs) across runs/levels — SaveGame-backed
// - Aggregate lore entries from one or more DataTables (sharded by biome)
// - Pick the next available lore entry for an antenna activation
// - Enforce prerequisites (sequential puzzle reveals)
//
// Tables are pushed in by UStreamSubsystem::SetConfig() from UStreamConfig.LoreTables.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LoreTypes.h"
#include "LoreSubsystem.generated.h"

class UDataTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoreConsumed, const FLoreEntryRow&, Entry);

UCLASS()
class POLARITY_API ULoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== Configuration ====================

	/** Replace the list of lore tables this subsystem queries. Called by UStreamSubsystem on config. */
	UFUNCTION(BlueprintCallable, Category = "Lore")
	void SetLoreTables(const TArray<UDataTable*>& InTables);

	// ==================== Core API ====================

	/** Picks the highest-priority eligible lore for the given arena context and marks it consumed.
	 *  Priority chain: arena-specific → biome-general → global.
	 *  Returns true if an entry was picked. */
	UFUNCTION(BlueprintCallable, Category = "Lore")
	bool PickAndConsumeLoreForArena(FName ArenaTag, FName Biome, FLoreEntryRow& OutEntry);

	UFUNCTION(BlueprintPure, Category = "Lore")
	bool IsLoreAvailable(FName LoreID) const;

	UFUNCTION(BlueprintPure, Category = "Lore")
	bool IsLoreConsumed(FName LoreID) const;

	/** Force-mark a lore entry as consumed, even without picking it. For scripted gates / debugging. */
	UFUNCTION(BlueprintCallable, Category = "Lore")
	void MarkLoreConsumed(FName LoreID);

	UFUNCTION(BlueprintCallable, Category = "Lore")
	TArray<FName> GetUnconsumedInBiome(FName Biome) const;

	UFUNCTION(BlueprintCallable, Category = "Lore")
	TArray<FName> GetAllLoreIDs() const;

	// ==================== Debug ====================

	UFUNCTION(BlueprintCallable, Category = "Lore|Debug")
	void DebugReset();

	UFUNCTION(BlueprintPure, Category = "Lore|Debug")
	int32 GetConsumedCount() const { return ConsumedLoreIDs.Num(); }

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Lore|Events")
	FOnLoreConsumed OnLoreConsumed;

protected:
	/** Find a row by LoreID across all configured tables. */
	const FLoreEntryRow* FindRowByLoreID(FName LoreID) const;

	/** True iff every entry in Required is in ConsumedLoreIDs. */
	bool ArePrerequisitesMet(const FLoreEntryRow& Entry) const;

	/** Collect rows matching scope filter that are not yet consumed and have prereqs met. */
	void CollectCandidates(FName ArenaTag, FName Biome, ELoreScope FilterScope, TArray<const FLoreEntryRow*>& OutCandidates) const;

	/** Configured DataTables (each holds FLoreEntryRow rows). Sharded by biome typically. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDataTable>> LoreTables;

	/** Set of LoreIDs that have been consumed. Persists across runs via SaveGame
	 *  once a SaveGame class is wired up (deferred; for now lives in memory). */
	UPROPERTY(SaveGame)
	TSet<FName> ConsumedLoreIDs;
};
