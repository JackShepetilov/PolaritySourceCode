// TierBenchComponent.h
// The infantry tier bench: one enemy at a time against a player who shoots back on his own.
//
// Docs/Enemy_Tiers_Plan_2026-10-04.md. To tell a skilled enemy from a clumsy one with numbers, the
// opponent has to be the same every time, so the player is driven by code: he stands still, sees
// what a person standing there would see, waits a human reaction time after the enemy shows, turns
// at a human rate and fires while on target. He takes no lasting damage (health is put back every
// frame and the loss is counted). The enemies come one at a time from the level's spawn marker
// (a TargetPoint tagged TierBenchSpawn), classes taking turns, each until it dies or times out.
//
// Per enemy the log gets one [TIER_BENCH] line; at the end one SUMMARY line per class:
//   life       seconds alive (a timeout counts the full time)
//   exposed    share of its life a player could see it (head or chest)
//   toPlayer   damage it did to the player, per minute alive
//   perExposed damage it did per second it was seen: what showing itself bought it
//   taken      damage it took
// Source/Tools/TierBench/run_tier_bench.sh runs it end to end.
//
// Console: polarity.bench.tier <class>[,<class>...] [rounds] [timeout] ; polarity.bench.tier stop

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TierBenchComponent.generated.h"

class AShooterCharacter;
class AShooterNPC;

UCLASS(ClassGroup = (Debug))
class POLARITY_API UTierBenchComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UTierBenchComponent();

	/** Start a run on the pawn's server copy. Classes take turns, Rounds of each. */
	void StartRun(const TArray<TSubclassOf<AShooterNPC>>& InClasses, int32 InRounds, float InTimeoutSeconds);

	/** End now: the current enemy goes, the summary is printed. */
	void StopRun();

	/** Seconds after an enemy shows before the player reacts. */
	float ReactionSeconds = 0.35f;

	/** How fast the player turns, degrees per second. */
	float TurnRateDegrees = 300.0f;

	/** On target within this many degrees: the trigger goes down. */
	float FireConeDegrees = 2.5f;

	/** Pause between enemies. */
	float GapSeconds = 2.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	struct FEnemyStats
	{
		float Life = 0.0f;
		float Exposed = 0.0f;
		float ToPlayer = 0.0f;
		float Taken = 0.0f;
		bool bTimedOut = false;
	};

	struct FClassTotals
	{
		int32 Count = 0;
		int32 TimedOut = 0;
		float Life = 0.0f;
		float Exposed = 0.0f;
		float ToPlayer = 0.0f;
		float Taken = 0.0f;
	};

	TArray<TSubclassOf<AShooterNPC>> Classes;
	int32 Rounds = 0;
	float TimeoutSeconds = 60.0f;

	/** Which enemy of the run is up next (classes interleaved). */
	int32 NextIndex = 0;

	TWeakObjectPtr<AShooterNPC> Current;
	TSubclassOf<AShooterNPC> CurrentClass;
	FEnemyStats Stats;
	TMap<FString, FClassTotals> Totals;

	float GapLeft = 0.0f;
	float SeenFor = 0.0f;
	float LastHealth = -1.0f;
	bool bFiring = false;
	bool bRunning = false;

	UFUNCTION()
	void HandleEnemyDamaged(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser);

	AShooterCharacter* GetPlayer() const;
	bool SpawnNext();
	void FinishEnemy(bool bTimedOut);
	void PrintSummary() const;
	void SetFiring(bool bFire);

	/** What a person at the camera would see of the enemy: the point to aim at, if any. */
	bool FindVisiblePoint(const AShooterNPC* Enemy, FVector& OutPoint) const;

	/** Health lost since the last frame, then put back. Armour lost counts too and is not refilled. */
	float DrainPlayerDamage();
};
