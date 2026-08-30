// ExtractionRoute.h
// The way out, and the door at the end of it.
//
// Two small actors, one idea: the run does not end where the team is standing. The final is taken
// and held, and only then does a route get announced and the chase start. Two different skills back
// to back - hold still and shoot, then move - and the second one is what the whole movement kit was
// written for (seminar, section 11).
//
// Several routes are placed and one is drawn at random after the hold. A route the team could have
// walked during the hold is a route they walk; a route announced with the clock already running is a
// decision made under fire.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionRoute.generated.h"

class USphereComponent;
class USplineComponent;
class UArrowComponent;

/**
 * The door. Standing in it for BoardSeconds ends the run for whoever is inside.
 */
UCLASS()
class POLARITY_API AExtractionPoint : public AActor
{
	GENERATED_BODY()

public:

	AExtractionPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	FName ExitTag = NAME_None;

	/** How close a player has to be to count as boarding (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction", meta = (ClampMin = "100.0"))
	float BoardRadius = 600.0f;

	/** Seconds of standing in it before it lifts. Long enough that the chase catches up to a team
	 *  that arrives together with it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction", meta = (ClampMin = "0.0"))
	float BoardSeconds = 8.0f;

	/** How far the boarding has got, 0..1. Zero whenever nobody is standing in it. */
	UFUNCTION(BlueprintPure, Category = "Extraction")
	float GetBoardProgress() const;

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Boarding resets the moment the last player steps out.
	 *
	 *  Not a paused bar: the run out is the one place in the run where standing still has to cost
	 *  something, and a bar that remembers turns the last fight into a game of tag around the pad. */
	float BoardedSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> BoardGizmo;
};

/**
 * One authored way from the final to an exit.
 *
 * The spline is the shape of the run out: what the chase follows, where the choke points are and
 * what the level around it has to provide. It is not a rail - nothing forces a player onto it - it
 * is the line the director hands to the team and to whoever is chasing them.
 */
UCLASS()
class POLARITY_API AExtractionRoute : public AActor
{
	GENERATED_BODY()

public:

	AExtractionRoute();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FName RouteTag = NAME_None;

	/** Where this route ends. A route without an exit is never announced. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	TObjectPtr<AExtractionPoint> Exit = nullptr;

	/** Relative chance of being drawn. Zero takes the route out of the pool without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Seconds after the announcement before the chase sets off. The head start, and the only thing
	 *  the team gets for free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	float ChaseLeadSeconds = 5.0f;

	/** The line itself. Add points in the editor from the final to the exit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Route")
	TObjectPtr<USplineComponent> Path;

	/** World position along the route, 0 at the final and 1 at the exit. Used by the chase and by
	 *  anything that wants to know how far the team has got. */
	UFUNCTION(BlueprintPure, Category = "Route")
	FVector GetPointAlongRoute(float Alpha) const;

	UFUNCTION(BlueprintPure, Category = "Route")
	float GetRouteLength() const;

protected:

	/** Routes register with the director so it can draw one when the hold ends. Like everything else
	 *  in this layer they may sit in a streamed sublevel, so registration is per-load, not per-run. */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
