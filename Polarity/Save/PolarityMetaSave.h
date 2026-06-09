// PolarityMetaSave.h
// Durable profile save object. Slot "Polarity_Meta". Persists forever (never auto-deleted).
// Every field is an explicit, inspectable UPROPERTY so the save is debuggable and migratable.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "PolaritySaveTypes.h"
#include "PolarityMetaSave.generated.h"

UCLASS()
class POLARITY_API UPolarityMetaSave : public USaveGame
{
	GENERATED_BODY()

public:
	// ---- Schema header ----
	UPROPERTY()
	int32 SaveVersion = PolaritySaveVersion::MetaInitial;

	UPROPERTY()
	FDateTime SavedAtUtc = FDateTime(0);

	// ---- UStreamSubsystem meta (mirrored; authority is the live subsystem at runtime) ----
	UPROPERTY()
	int64 MetaCurrency = 0;

	UPROPERTY()
	int32 CompletedRuns = 0;

	/** Empty == "not chosen yet" -> the desktop prompts the player for a handle on first launch. */
	UPROPERTY()
	FString PlayerStreamerName;

	// ---- ULoreSubsystem ----
	UPROPERTY()
	TSet<FName> ConsumedLoreIDs;

	// ---- Tutorial + desktop (owned HERE; set by the tutorial LEVEL event, not UTutorialSubsystem) ----
	UPROPERTY()
	bool bTutorialCompleted = false;

	/** Desktop app icons the player has unlocked, e.g. "chat", "settings", "raid". */
	UPROPERTY()
	TSet<FName> UnlockedApps;

	// ---- Gamer-chat read-state ----
	UPROPERTY()
	FChatReadState Chat;

	// ---- Future roguelite meta layer (reserved homes; empty today; populating later is .cpp-only) ----
	UPROPERTY()
	TArray<FAbilityBankEntry> AbilityBank;        // ability -> L1..L3

	UPROPERTY()
	TSet<FGameplayTag> UnlockedUpgrades;          // pool additions (stable tag keys)

	UPROPERTY()
	TSet<FName> UnlockedCosmetics;

	UPROPERTY()
	TMap<FName, int32> StartingSkillBoosts;
};
