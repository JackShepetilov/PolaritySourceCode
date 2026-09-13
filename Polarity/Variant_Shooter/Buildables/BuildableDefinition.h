// BuildableDefinition.h
// What one kind of building IS, as data: what it costs, how much it can take, how long it takes to
// put up. The TF2 engineer's PDA is a list of these.
//
// The actor class does the behaving (a turret shoots, a dispenser heals); this asset holds the
// numbers every kind shares, so the build menu, the placement ghost and the server's price check
// all read the same values and none of them has to know which building it is looking at.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "BuildableDefinition.generated.h"

class ABuildableActor;
class UTexture2D;

/** Numbers that change with the building's level. One entry = a building that never upgrades. */
USTRUCT(BlueprintType)
struct POLARITY_API FBuildableLevelStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level", meta = (ClampMin = "1.0"))
	float MaxHealth = 150.0f;

	/** Seconds from placement to working, with nobody hitting it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level", meta = (ClampMin = "0.0", Units = "s"))
	float BuildTime = 10.5f;

	/** Metal that has to be hammered in to reach the NEXT level. Ignored on the last level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level", meta = (ClampMin = "0"))
	int32 UpgradeCost = 200;
};

/** Why a spot was refused, for the ghost's colour and, later, a line of text. */
UENUM(BlueprintType)
enum class EBuildablePlacementResult : uint8
{
	Valid,
	NoGround,
	TooSteep,
	Blocked,
	TooFar
};

UCLASS(BlueprintType)
class POLARITY_API UBuildableDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	// ==================== Identity ====================

	/** Buildable.Turret, Buildable.Dispenser, Buildable.Teleporter. What code asks for by kind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (Categories = "Buildable"))
	FGameplayTag BuildableTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TObjectPtr<UTexture2D> Icon;

	// ==================== What gets built ====================

	/** The actor that is spawned. Its Mesh component is also what the placement ghost copies, so the
	 *  ghost and the building can never disagree about size or shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TSubclassOf<ABuildableActor> ActorClass;

	/** Metal taken from the builder at placement. TF2: sentry 130, dispenser 100, teleporter 50. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "0"))
	int32 MetalCost = 100;

	/** Level 1 first. Health, build time and the price of the next level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TArray<FBuildableLevelStats> Levels;

	/** How many of these one player may have standing. 1 for a sentry or a dispenser, 2 for a
	 *  teleporter whose two ends are the same asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1"))
	int32 MaxCountPerPlayer = 1;

	// ==================== Placement ====================

	/** How far in front of the player the ghost can go. The server allows a little more, to cover
	 *  the ground both parties gave up during the round trip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "50.0", Units = "cm"))
	float MaxPlaceDistance = 350.0f;

	/** Steepest ground the building accepts. TF2 asks for "reasonably flat". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	float MaxSlopeDegrees = 30.0f;

	/** Clearance added around the mesh when checking that nothing is in the way. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", Units = "cm"))
	float PlacementPadding = 10.0f;

	// ==================== The wrench ====================

	/** Health one hit puts back, before the metal cap. TF2: 102. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wrench", meta = (ClampMin = "0.0"))
	float WrenchRepairHealth = 102.0f;

	/** Health bought by one metal. TF2: 3 for a sentry or dispenser, 5 for a teleporter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wrench", meta = (ClampMin = "0.1"))
	float RepairHealthPerMetal = 3.0f;

	/** Metal one hit moves from the hitter into the upgrade. A hit with less than this does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wrench", meta = (ClampMin = "1"))
	int32 WrenchUpgradeMetal = 25;

	/** Extra build speed while being hit, as a multiple of the base rate: 1.5 makes it 2.5x, TF2's
	 *  number since 2015. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wrench", meta = (ClampMin = "0.0"))
	float WrenchBuildBoost = 1.5f;

	/** How long one hit keeps the boost going. Matched to the swing interval, so a player hitting
	 *  continuously never sees it lapse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wrench", meta = (ClampMin = "0.1", Units = "s"))
	float WrenchBoostDuration = 0.9f;

	// ==================== Queries ====================

	UFUNCTION(BlueprintPure, Category = "Buildable")
	int32 GetMaxLevel() const { return FMath::Max(1, Levels.Num()); }

	/** Stats at a 1-based level, clamped into the authored range. A definition with no levels at
	 *  all answers with the struct's defaults rather than crashing a building. */
	UFUNCTION(BlueprintPure, Category = "Buildable")
	FBuildableLevelStats GetLevelStats(int32 Level) const;

	/** The building's footprint in its own space, read off the actor class's mesh (asset, relative
	 *  transform, scale) plus PlacementPadding. False when the class has no mesh to measure; the box
	 *  is then a half-metre cube so placement still works on a placeholder. */
	bool GetLocalFootprint(FBox& OutBox) const;

	/** The one answer to "can this stand here", asked by the ghost every frame and by the server
	 *  once before it spawns. Transform is where the building's origin would go; the footprint is
	 *  swept for anything solid, with Ignore left out (the builder and the ghost). Ground and slope
	 *  are checked under the origin. Builder, when given, is also checked for reach: the server
	 *  passes it, the ghost already clamps its own distance. */
	EBuildablePlacementResult ValidatePlacement(const UWorld* World, const FTransform& Transform,
		const TArray<AActor*>& Ignore, const AActor* Builder = nullptr) const;
};
