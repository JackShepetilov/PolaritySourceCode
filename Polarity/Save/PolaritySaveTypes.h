// PolaritySaveTypes.h
// Shared value types + schema versions for the Polarity save system.
// Leaf data header — safe to include from gameplay subsystems that need to read saved state.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PolaritySaveTypes.generated.h"

/** Schema versions. Bump LATEST on every NON-additive schema change; chain-migrate on load.
 *  Adding a new UPROPERTY is additive-safe (old saves default it) and does NOT need a bump. */
namespace PolaritySaveVersion
{
	inline constexpr int32 MetaInitial = 1;
	inline constexpr int32 MetaLatest  = MetaInitial; // == 1 today

	inline constexpr int32 RunInitial  = 1;
	inline constexpr int32 RunLatest   = RunInitial;  // == 1 today
}

/** Gamer-chat read-state. Group-based (no row indices), so chat rows can be inserted/reordered
 *  freely — only each row's FChatMessage::Group matters. Group membership lives in the chat
 *  DataTable (content); the save only tracks which groups are unlocked / seen. */
USTRUCT()
struct FChatReadState
{
	GENERATED_BODY()

	/** Which message GROUPS are visible. FName-keyed: "intro", "post_tutorial". */
	UPROPERTY()
	TSet<FName> UnlockedGroups;

	/** Groups already viewed once. In set => later visits dump instantly AND the group no longer
	 *  counts toward the unread badge. */
	UPROPERTY()
	TSet<FName> SeenGroups;
};

/**
 * Future meta-layer ability entry (roguelite headroom).
 * Struct (not a raw TMap value) so it extends additively: add cost/prereq/source later by
 * appending UPROPERTYs without re-typing an existing map value (which would silently reset it).
 */
USTRUCT()
struct FAbilityBankEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag AbilityTag;

	UPROPERTY()
	int32 Level = 1; // L1..L3
};
