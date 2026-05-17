// StreamArenaConfig.h
// PrimaryDataAsset for per-arena stream tweaks. Designer assigns one to each arena's data.
// ArenaMultiplier scales the viewer target (T1 arenas typically <1.0, T4 typically >1.0).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StreamArenaConfig.generated.h"

class UChatScript;

UCLASS(BlueprintType)
class POLARITY_API UStreamArenaConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Multiplier on viewer target for this arena/tier (T1 ≈ 0.3, T4 ≈ 2.0 typical). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Arena")
	float ArenaMultiplier = 1.0f;

	/** Optional per-arena chat script. If null, falls back to UStreamConfig::DefaultChatScript. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Arena")
	TObjectPtr<UChatScript> ChatScript;

	/** Biome tag this arena belongs to (Cartels / Islands / Yachts / OtherDim).
	 *  Consumed by ULoreSubsystem to pick biome-general lore when arena-specific is exhausted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stream|Arena")
	FName Biome;
};
