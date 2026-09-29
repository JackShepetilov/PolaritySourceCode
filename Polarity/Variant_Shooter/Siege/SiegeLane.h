// SiegeLane.h
// One lane of a lane map: the road the enemy's packs walk from their side to the base.
//
// The spline starts where the enemy comes in and ends at the base. Every wave the director puts one
// pack on the start of every open lane, and each creep of it walks the spline (USiegeLaneFollower)
// until something to fight shows: a player, a turret, the core at the end. The lanes are always the
// same on a map (Docs/MOBA_Lanes_Concept_2026-09-29.md): they are placed by hand, not rolled.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeLane.generated.h"

class USplineComponent;

UCLASS()
class POLARITY_API ASiegeLane : public AActor
{
	GENERATED_BODY()

public:

	ASiegeLane();

	/** What packs and logs call this lane (Top, Mid, Bot). A pack kind can be limited to lanes by it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lane")
	FName LaneName = NAME_None;

	/** A closed lane gets no packs. Server only; flip it at runtime to open a lane later in the run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane")
	bool bLaneOpen = true;

	/** A pack comes out in a column along the start of the spline, this far apart (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lane|Spawn", meta = (ClampMin = "100.0", Units = "cm"))
	float PackSpacing = 350.0f;

	/** And up to this far to either side of it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lane|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	float PackSideJitter = 250.0f;

	UFUNCTION(BlueprintPure, Category = "Lane")
	USplineComponent* GetPath() const { return Path; }

	UFUNCTION(BlueprintPure, Category = "Lane")
	float GetLength() const;

	/** World location this far along the lane from its enemy end, clamped to the lane. */
	UFUNCTION(BlueprintPure, Category = "Lane")
	FVector GetLocationAtDistance(float Distance) const;

	/** How far along the lane the point of it nearest to this location is. */
	UFUNCTION(BlueprintPure, Category = "Lane")
	float GetDistanceClosestTo(const FVector& Location) const;

	/** Where member Index of a pack stands at spawn: a column back from the lane's start, jittered
	 *  sideways. Flat XY only, the director finds the ground under it. */
	FVector GetPackSlot(int32 Index) const;

	/** Every open lane of this world. */
	static void GetOpenLanes(const UWorld* World, TArray<ASiegeLane*>& OutLanes);

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USplineComponent> Path;
};
