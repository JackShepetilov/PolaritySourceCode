// Data-driven biome layout and arena pools for deterministic run assembly.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BiomeRunRegistry.generated.h"

class UWorld;

/** A reusable arena level that can occupy a biome slot. */
USTRUCT(BlueprintType)
struct FBiomeArenaOption
{
	GENERATED_BODY()

	/** Existing arena .umap, loaded as a regular LevelStreamingDynamic sublevel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> ArenaLevel;

	/** Relative probability inside this slot. Zero disables the option without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

};

/** Unreal does not support reflected arrays-of-arrays, so each slot wraps its candidate array. */
USTRUCT(BlueprintType)
struct FBiomeArenaPool
{
	GENERATED_BODY()

	/** Must match one ABiomeArenaAnchor::SlotId in the layout level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	FName SlotId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TArray<FBiomeArenaOption> Arenas;
};

/** A hand-authored island/bridge/heightmap layout available to this biome. */
USTRUCT(BlueprintType)
struct FBiomeLayoutOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> LayoutLevel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

/** Registry for one biome: layout maps plus per-position arena candidate pools. */
UCLASS(BlueprintType)
class POLARITY_API UBiomeRunRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Used by the run-map selector before opening a layout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	TArray<FBiomeLayoutOption> Layouts;

	/** Ordered slots. Array order is part of deterministic selection and should remain stable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	TArray<FBiomeArenaPool> ArenaPools;
};
