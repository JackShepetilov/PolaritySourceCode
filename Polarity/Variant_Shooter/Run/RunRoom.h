// RunRoom.h
// One room of the run skeleton on the bench level (Docs/Run_Skeleton_Plan_2026-10-17.md, section 9):
// the glue between a fight and the room after it. The first player into the entry zone starts the
// room's fight; when its last enemy dies, the doors out of the room open. A room with no fight (the
// rest stop, the shop, the casino) is open from the start.
//
// Hand-placed, with hand-placed references: the fight (an ABenchEncounter with its button off), the
// door blocks and the point a team is put down at. The entry zone stands in the passage BEFORE the
// room, so the enemies appear while nobody can see their points.
//
// Server decides; the door state replicates and every machine hides its own copy of the blocks.
// TODO(COOP): the label text does not replicate, a client reads only "ROOM n" on it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunRoom.generated.h"

class ABenchEncounter;
class UBoxComponent;
class UPrimitiveComponent;
class UTextRenderComponent;

UCLASS()
class POLARITY_API ARunRoom : public AActor
{
	GENERATED_BODY()

public:

	ARunRoom();

	/** Order in the run, from 1. polarity.run.room takes it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Room", meta = (ClampMin = "1"))
	int32 RoomIndex = 1;

	/** Shown on the label and in the log. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Room")
	FString RoomName = TEXT("ROOM");

	/** The fight of this room. Empty: a room with no fight, open from the start. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Run Room")
	TObjectPtr<ABenchEncounter> Encounter;

	/** Blocks in the doorways out of the room: hidden and walked through once it is cleared. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Run Room")
	TArray<TObjectPtr<AActor>> ExitDoors;

	/** Where a team sent to this room is put down, and the way it looks. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Run Room")
	TObjectPtr<AActor> StartPoint;

	/** Clearing this room ends the run: the time from RUN START is written. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Room")
	bool bLastRoom = false;

	/** Half size of the entry zone (cm), around the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Room")
	FVector EntryExtent = FVector(500.0f, 500.0f, 300.0f);

	/** Back to before the team came: fight removed, doors closed (open for a room with no fight). */
	void ResetRoom();

	/** Spawn the fight, once until the next reset. */
	void StartFight();

	/** Open the doors out. Also what a stuck fight is ended with (polarity.run.open). */
	void OpenExit();

	bool IsFighting() const;

	/** Every room reset, the ones before Index cleared and open, the team put down at room Index
	 *  and its fight started. What RUN START and polarity.run.room do. Server only. */
	static bool StartRunAt(UWorld* World, int32 Index);

protected:

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Run Room")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Run Room")
	TObjectPtr<UBoxComponent> EntryZone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Run Room")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(ReplicatedUsing = OnRep_ExitOpen)
	bool bExitOpen = false;

	UFUNCTION()
	void OnRep_ExitOpen();

private:

	UFUNCTION()
	void HandleEntryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEncounterDone(ABenchEncounter* Done);

	void ApplyDoors();
	void SetStatus(const FString& Status);

	bool bFightStarted = false;

	/** Server time of RUN START, given to every room by StartRunAt. Below zero: no run. */
	float RunStartTime = -1.0f;
};
