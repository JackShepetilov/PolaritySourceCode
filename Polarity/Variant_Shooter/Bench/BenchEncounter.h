// BenchEncounter.h
// A debug console for one fight on the bench level: shoot its button and the enemies it lists appear
// at their points; shoot it again and whatever is left of them is removed. When the last one dies it
// writes how long the fight took, on its label and in the log ([BENCH]).
//
// Hand-placed, with hand-placed spawn points (an actor per enemy), so a fight is changed in the
// Details panel and not in code. Server only: this is a tool for the host, the label does not
// replicate. TODO(COOP): replicate the label text if a client ever needs to read it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/AI/SquadSpawn/Squad.h"
#include "BenchEncounter.generated.h"

class AShooterNPC;
class UShootableButtonComponent;
class UTextRenderComponent;
class UMaterialInterface;

/** One kind of enemy and where each one of them appears. */
USTRUCT(BlueprintType)
struct FBenchEncounterGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	TSubclassOf<AShooterNPC> EnemyClass;

	/** One enemy per point, facing the way the point faces. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Encounter")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	/** The side these fight for (PolarityTeams: 1 and 2 are the two AI factions). 255 leaves the
	 *  side the class comes with. Two groups on different sides make the fight a versus: it is over
	 *  when one side is left, and the time is written then. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	uint8 Team = 255;

	/** Put on every material slot of the body, so the sides of a versus can be told apart by eye.
	 *  Empty leaves the body as its class made it. Set on the server only, like the rest of the bench.
	 *  TODO(COOP): a client sees the class's own materials. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<UMaterialInterface> BodyMaterial;

	/** What these are out to do in the fight. @see AShooterNPC::SetCombatTask */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	ESquadTask Task = ESquadTask::Advance;

	/** Not there at the start: these come ReinforcementDelay seconds after the alarm, and stay
	 *  [author, 2026-10-11: reinforcement comes to outposts only, so that an outpost is taken in one
	 *  go and not by hit and run]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	bool bReinforcement = false;

	/** For a group that holds: the ground it must not leave, this far around the middle of its own
	 *  spawn points (cm). 0: no ground. @see AShooterNPC::SetHoldArea */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.0", Units = "cm"))
	float HoldRadius = 0.0f;

	/** The middle of the held ground. Empty: the middle of the group's own spawn points, which is
	 *  wrong for a yard whose posts all stand along one wall. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<AActor> HoldCentre;
};

UCLASS()
class POLARITY_API ABenchEncounter : public AActor
{
	GENERATED_BODY()

public:

	ABenchEncounter();

	/** Shown on the label and written to the log. Also the name polarity.bench.start takes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	FString EncounterName = TEXT("ENCOUNTER");

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Encounter")
	TArray<FBenchEncounterGroup> Groups;

	/** Nobody's AI counts a player as an enemy while this fight runs (polarity.ai.IgnorePlayers):
	 *  for a fight that is watched and not fought. The switch is the game-wide one, so it is on for
	 *  every enemy on the level until this fight ends or is cleared. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	bool bPlayersOnlyWatch = false;

	/** How long after the alarm the groups marked bReinforcement arrive (s). The alarm is the first
	 *  time anybody of this fight is hurt, shoots or dies. Everybody dead before that: taken, and
	 *  nobody comes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.0", Units = "s"))
	float ReinforcementDelay = 60.0f;

	/** Spawn the fight when nobody of it is alive, clear it when somebody is. What the button does. */
	UFUNCTION(BlueprintCallable, Category = "Encounter")
	void Toggle();

	UFUNCTION(BlueprintCallable, Category = "Encounter")
	void StartEncounter();

	/** Remove every enemy of this fight that is still alive. No time is written. */
	UFUNCTION(BlueprintCallable, Category = "Encounter")
	void ClearEncounter();

	UFUNCTION(BlueprintPure, Category = "Encounter")
	int32 GetAliveCount() const;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<UShootableButtonComponent> Button;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator);

	UFUNCTION()
	void HandleDeath(AShooterNPC* DeadNPC);

	void SetStatus(const FString& Status);

	void SetPlayersIgnored(bool bIgnored);

	/** Spawn one group at its points. Returns how many appeared. */
	int32 SpawnGroup(const FBenchEncounterGroup& Group);

	/** Looks for the alarm, counts down, brings the reinforcement. On a timer while one is owed. */
	void WatchAlarm();

	FTimerHandle AlarmTimer;
	TArray<uint8> SidesSpawned;
	bool bReinforcementOwed = false;
	bool bReinforcementCame = false;
	float AlarmTime = -1.0f;

	/** True when the groups name more than one side. */
	bool bVersus = false;

	TArray<TWeakObjectPtr<AShooterNPC>> Alive;
	int32 SpawnedCount = 0;
	float StartTime = 0.0f;
	bool bRunning = false;
};
