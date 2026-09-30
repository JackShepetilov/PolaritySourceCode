// SiegeLaneFollower.h
// Keeps one creep on its lane.
//
// The director hangs this on every creep it puts on a lane. It does not move anybody: it answers
// "where next" for the walkers' march (AShooterNPC::TickSiegeMarch) and for the carriers'
// (AKamikazeCarrierDrone::TickSelfDriven), a point a little ahead along the lane. Whatever pulls a
// creep off the road (a player, a turret) keeps its own logic; when it is over the creep asks again
// and is sent on from the nearest part of the lane ahead of where it last got to. Progress only
// grows, so a creep that chased somebody back towards its own side does not walk the lane twice.
//
// Server only, like everything else the director hands out. Nothing here is replicated.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SiegeLaneFollower.generated.h"

class ASiegeLane;

UCLASS(ClassGroup = (Siege), meta = (BlueprintSpawnableComponent))
class POLARITY_API USiegeLaneFollower : public UActorComponent
{
	GENERATED_BODY()

public:

	USiegeLaneFollower();

	/** The march goal runs this far ahead of the creep along the lane (cm). Long enough that the move
	 *  never arrives between two re-issues, short enough that it keeps to the road on a bend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane", meta = (ClampMin = "200.0", Units = "cm"))
	float Lookahead = 1500.0f;

	/** Closer than this to the lane's base end = the lane is walked, the march goes to the core (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane", meta = (ClampMin = "0.0", Units = "cm"))
	float EndDistance = 2500.0f;

	/** A carrier on a lane only takes on what is this near it (cm); everything farther is left to the
	 *  creeps of that lane. Walkers use their own perception. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane", meta = (ClampMin = "0.0", Units = "cm"))
	float CarrierAggroRadius = 6000.0f;

	/** The leash of a walker (author 2026-09-30): it takes on only what stands within this distance of
	 *  its lane (cm). A player in a camp behind the lane is not its business, and a target that walks
	 *  off past it is dropped, so the creep goes back to the march. 0 = no leash. Not applied once the
	 *  lane is walked: at the base everything is its business. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane", meta = (ClampMin = "0.0", Units = "cm"))
	float WalkerLeashRadius = 4000.0f;

	/** True if Target stands close enough to the lane for this creep to take it on. The answer for one
	 *  target is kept a quarter of a second: the controller asks every frame. */
	bool IsWithinLeash(const AActor* Target) const;

	void SetLane(ASiegeLane* InLane);

	UFUNCTION(BlueprintPure, Category = "Lane")
	ASiegeLane* GetLane() const { return Lane.Get(); }

	/** Where to walk next from here: a point Lookahead ahead of the progress along the lane, on the
	 *  navmesh when there is one near. False once the lane is walked (or there is no lane): go for the
	 *  core as a laneless creep would. Moves the progress forward, never back. */
	bool GetMarchGoal(const FVector& From, FVector& OutGoal);

	/** The same point for a flyer: the lane under it, no navmesh. False once the lane is walked. */
	bool GetFlightGoal(const FVector& From, FVector& OutGoal);

	/** True once the creep has been within EndDistance of the lane's base end. */
	UFUNCTION(BlueprintPure, Category = "Lane")
	bool IsLaneWalked() const { return bLaneWalked; }

	/** How far along its lane this creep has got (cm). */
	UFUNCTION(BlueprintPure, Category = "Lane")
	float GetProgress() const { return Progress; }

private:

	/** Update the progress from where the creep is; false if the lane is gone or walked. */
	bool Advance(const FVector& From);

	TWeakObjectPtr<ASiegeLane> Lane;
	float Progress = 0.0f;
	bool bLaneWalked = false;

	// Leash answers (IsWithinLeash) per target: the controller weighs several intents a frame.
	struct FLeashAnswer
	{
		TWeakObjectPtr<const AActor> Target;
		float CheckedAt = -1.0f;
		bool bInside = true;
	};
	mutable TArray<FLeashAnswer, TInlineAllocator<4>> LeashAnswers;
};
