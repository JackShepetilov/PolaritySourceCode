// LoreTypes.h
// Shared types for the lore system — DataTable row struct and scope enum.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LoreTypes.generated.h"

UENUM(BlueprintType)
enum class ELoreScope : uint8
{
	/** Specific to a single arena tag — picked first when matching arena. */
	Arena    UMETA(DisplayName = "Arena-Specific"),
	/** Available anywhere in the biome — picked when no arena-specific lore is available. */
	Biome    UMETA(DisplayName = "Biome-General"),
	/** Available anywhere in the game — last-resort fallback. */
	Global   UMETA(DisplayName = "Global")
};

USTRUCT(BlueprintType)
struct FLoreEntryRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Unique identifier across all biomes. Used for prerequisites and SaveGame tracking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	FName LoreID;

	/** Biome tag this entry belongs to (Cartels / Islands / Yachts / OtherDim). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	FName Biome;

	/** Specific arena tag — only used when Scope == Arena. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	FName ArenaTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	ELoreScope Scope = ELoreScope::Arena;

	/** LoreIDs that must be consumed before this entry becomes available. Empty = always available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	TArray<FName> RequiredLoreIDs;

	/** SequenceID in DT_ChatScripted — broker plays this sequence when the lore fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	FName ChatScriptedSequenceID;

	/** Friend voiceline identifier — reserved for when voice integration lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore")
	FName VoiceLineID;

	/** Higher = picked first when multiple entries are eligible. Ties broken at random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore", meta = (ClampMin = "0"))
	int32 Priority = 0;

	/** Editor-only note: brief description of the lore beat for the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lore", meta = (MultiLine = true))
	FText DesignerNotes;
};
