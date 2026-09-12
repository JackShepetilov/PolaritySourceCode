// BattleLog.h
// What happened in the fight, in numbers.
//
// Until now the only instrument for judging combat was watching it. That is enough to spot a tank
// that will not move; it is useless for "is the drone escort worth its cost" or "did that change
// make grenadiers better or just louder". Both questions need the same thing: who killed whom, when,
// from how far, and how the sides stood at the end.
//
// Deliberately a record and a summary, not a balance opinion. It reports; the reading is yours.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BattleLog.generated.h"

class AShooterNPC;
class UDamageType;

/** One thing that happened, flattened for printing. */
USTRUCT()
struct FBattleEvent
{
	GENERATED_BODY()

	/** Seconds since the first event of the fight */
	UPROPERTY()
	float Time = 0.0f;

	UPROPERTY()
	FString Kind;

	UPROPERTY()
	FString Subject;

	UPROPERTY()
	uint8 SubjectTeam = 0;

	UPROPERTY()
	FString Other;

	UPROPERTY()
	uint8 OtherTeam = 0;

	/** Distance between the two, cm. Zero when the event has only one side. */
	UPROPERTY()
	float Distance = 0.0f;

	UPROPERTY()
	FString Detail;
};

/** Totals for one side. */
USTRUCT()
struct FBattleSideStats
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Spawned = 0;

	UPROPERTY()
	int32 Lost = 0;

	UPROPERTY()
	int32 Kills = 0;

	/** Sum of engagement distances of its kills, for the average */
	UPROPERTY()
	float KillDistanceSum = 0.0f;
};

/**
 * Per-world record of one fight. Cleared when a scenario starts, dumped when it ends or on demand
 * (`squad.BattleLog`).
 */
UCLASS()
class POLARITY_API UBattleLogSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	static UBattleLogSubsystem* Get(const UObject* WorldContext);

	/** Throw away the previous fight and start timing a new one */
	void BeginFight(const FString& ScenarioName);

	/** A pawn joined the fight. Subscribes to its death so the log does not need a tick. */
	void RecordSpawn(APawn* Pawn, uint8 TeamId);

	/** Anything worth a line that is not a kill: a squad losing its nerve, a squad regrouping. */
	void RecordNote(const FString& Kind, const FString& Subject, uint8 TeamId, const FString& Detail);

	/** Print the whole fight and the totals. Safe to call twice; the second call still prints. */
	void Dump() const;

	/** True once BeginFight has been called and the fight has not been dumped as finished */
	bool IsFightRunning() const { return bFightRunning; }

	/** Called by the squad system when one side has nobody left standing */
	void FinishFight(const FString& Reason);

protected:

	UFUNCTION()
	void OnMemberDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingDamageCauser);

	float NowRelative() const;

	UPROPERTY()
	TArray<FBattleEvent> Events;

	/** Side totals, keyed by team id */
	TMap<uint8, FBattleSideStats> Sides;

	FString Scenario;
	float StartTime = 0.0f;
	bool bFightRunning = false;
};
