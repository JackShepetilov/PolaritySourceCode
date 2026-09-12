// LootGeneratorSubsystem.h
// Rolls loot. The only thing in the project that decides what a place hands out.
//
// It is a subsystem rather than a function on the sheet because the registry is loaded once per
// world and the holes in it are worth reporting once, not once per crate. Everything here runs on
// the server: a sheet lays out actors, and actors replicate.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Variant_Shooter/Map/LootTypes.h"
#include "LootGeneratorSubsystem.generated.h"

class ULootRegistry;

UCLASS(Config = Game)
class POLARITY_API ULootGeneratorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** The generator for this world. */
	UFUNCTION(BlueprintPure, Category = "Loot", meta = (WorldContext = "WorldContextObject"))
	static ULootGeneratorSubsystem* GetLootGenerator(const UObject* WorldContextObject);

	/**
	 * One sheet's worth of loot for a place worth Quality, give or take Spread.
	 *
	 * How many slots comes from the quality; each slot then draws its own value around that quality
	 * and finds a line near it. Nothing is guaranteed and nothing is bucketed: a poor place can turn
	 * up something good, a rich one can turn up junk, and how often is exactly Spread.
	 */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	bool RollSheet(float Quality, float Spread, TArray<FLootRoll>& OutRolls);

	/** One slot, for anything that wants a single thing rather than a sheet of them. */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	bool RollOne(float Quality, float Spread, FLootRoll& OutRoll);

	/** The registry this world is using, loading it on first use. Null when nothing is configured,
	 *  which is a project that has not been set up rather than an error to swallow. */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	ULootRegistry* GetRegistry();

	/** Which registry to use. Set in DefaultGame.ini under this class, so the project has one
	 *  answer and a map that needs another overrides it there rather than per point. */
	UPROPERTY(EditAnywhere, Config, Category = "Loot")
	FSoftObjectPath RegistryPath;

private:

	/** A value around Quality, clamped to 0..1. */
	static float DrawValue(float Quality, float Spread);

	UPROPERTY(Transient)
	TObjectPtr<ULootRegistry> Registry;

	/** Holes are reported once per world, not once per crate. */
	bool bGapsLogged = false;
};
