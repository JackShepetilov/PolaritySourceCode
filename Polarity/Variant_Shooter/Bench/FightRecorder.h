// FightRecorder.h
// Writes a fight down so it can be looked at afterwards instead of described in words [author,
// 2026-10-11]: which scenario, who stood where and when, who fired how much, who hit whom, who lived
// how long.
//
// One folder per fight under Saved/BenchTrack/<date>_<scenario>/:
//   track.csv     a row per soldier and per player every half second (place, health, what it is doing)
//   events.csv    a row per shot, per hit, per death, per tactic note
//   geometry.csv  the boxes of the level around the fight, for the picture from above
//   summary.txt   the table per soldier and per player
// Pictures and the report are made from these by Source/Tools/CombatBench/analyze_fight.py.
//
// Started and stopped by ABenchEncounter. Server only, like the rest of the bench: positions and
// damage are read where they are decided. Costs nothing while no fight is being written.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FightRecorder.generated.h"

class AActor;
class AController;

UCLASS()
class POLARITY_API UFightRecorderSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	/** Null when the world has none (a client, the editor world). */
	static UFightRecorderSubsystem* Get(const UObject* WorldContext);

	/** A fight began. A second fight begun while one is being written joins the same record. */
	void BeginFight(const FString& Scenario);

	/** A fight ended. The record is closed when the last fight that joined it has ended. */
	void EndFight(const FString& Scenario, const FString& Outcome);

	bool IsRecording() const { return bRecording; }

	// The notes below are free to call from anywhere on the authority: each one returns at once when
	// nothing is being written.

	/** One round left Shooter's barrel. bPinFire: fired to keep a head down, not to hit. */
	static void NoteShot(const AActor* Shooter, const AActor* Target, bool bPinFire);

	/** Victim lost Damage. The one behind it is worked out from the instigator and the causer. */
	static void NoteDamage(const AActor* Victim, const AController* InstigatedBy, const AActor* Causer, float Damage, float HealthAfter);

	static void NoteDeath(const AActor* Victim, const AActor* Killer);

	static void NoteTactic(const AActor* Who, FName Note);

	/** Something that happened to the fight as a whole: the alarm, the reinforcement. */
	static void NoteMark(const UObject* WorldContext, const FString& What);

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

private:

	/** What is counted for one soldier or one player over the fight. */
	struct FTally
	{
		FString Name;
		uint8 Team = 255;
		bool bPlayer = false;

		float FirstSeenTime = -1.0f;
		float LastAliveTime = -1.0f;
		float DeathTime = -1.0f;
		FString KilledBy;

		/** Whoever hurt it last. A player's death arrives without a killer, and this is the answer. */
		FString LastHurtBy;

		int32 Shots = 0;
		int32 PinShots = 0;
		int32 HitsLanded = 0;
		int32 HitsTaken = 0;
		float DamageDealt = 0.0f;
		float DamageTaken = 0.0f;
		int32 Kills = 0;

		int32 Samples = 0;
		int32 ShootingSamples = 0;
		int32 SightSamples = 0;
		int32 IdleSamples = 0;

		/** Fight time of the first sample that had a line to its target, of the first step after it
		 *  and of the first round after it. What "stood there at the start" is read from. */
		float FirstSightTime = -1.0f;
		float FirstMoveTime = -1.0f;
		float FirstShotTime = -1.0f;

		float PathLength = 0.0f;
		FVector LastLocation = FVector::ZeroVector;
		bool bHasLastLocation = false;
	};

	void Sample();
	void Flush();
	void Close(const FString& Outcome);
	void WriteGeometry() const;
	void WriteSummary(const FString& Outcome) const;

	/** The name an actor goes by in the files, and its tally. Players are PLAYER, PLAYER_2 and on. */
	FString NameOf(const AActor* Actor);
	FTally& TallyOf(const AActor* Actor);
	float FightTime() const;

	static const AActor* ResolveAttacker(const AController* InstigatedBy, const AActor* Causer);

	bool bRecording = false;
	int32 OpenFights = 0;
	FString Folder;
	FString Scenarios;
	float StartTime = 0.0f;
	float LastSampleTime = -1.0f;
	float LastFlushTime = 0.0f;

	FString PendingTrack;
	FString PendingEvents;

	TArray<TWeakObjectPtr<const AActor>> Players;
	TMap<FString, FTally> Tallies;

	/** Everything that was ever sampled, to cut the level's boxes down to the ones around the fight. */
	FBox Extent = FBox(ForceInit);
};
