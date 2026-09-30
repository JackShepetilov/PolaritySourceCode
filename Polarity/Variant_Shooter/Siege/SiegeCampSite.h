// SiegeCampSite.h
// One forest camp on the lanes map: a landmark, guards around it, a mech part inside.
//
// Design: Docs/Lane_Camps_Design_2026-09-30.md. The author's rules this class carries:
//
//  - no markers: the camp is found by its landmark (smoke, a lamp, a searchlight), which the build step
//    puts next to it and lists in LandmarkActors; the landmark goes dark when the camp is taken;
//  - the guards appear when a player comes near (ActivationRadius), their number read off the siege
//    clock at that moment, so a camp left for later is a harder camp;
//  - the guards keep to the camp (USiegeCampGuard), the part is only taken once they are all dead, and
//    it goes straight into the inventory of the player who walks up to it;
//  - one use: once the part is taken the camp stays empty.
//
// Which part a camp holds is decided at the start of the run (USiegeCampSubsystem::AssignParts): each
// side of the forest gets all five slots, so either side alone can build the mech.
//
// Server decides everything. The part and the state replicate so the crate shows the right shape and
// empties on every screen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeCampSite.generated.h"

class AShooterNPC;
class APawn;
class UMechPartDefinition;
class USceneComponent;
class UStaticMeshComponent;
class USiegeCampGuard;

UENUM(BlueprintType)
enum class ESiegeCampTier : uint8
{
	Easy,
	Medium,
	Hard
};

UENUM(BlueprintType)
enum class ESiegeCampState : uint8
{
	/** Nobody near, no guards in the world. */
	Dormant,
	/** Guards are up. */
	Guarded,
	/** Every guard is dead, the part waits to be taken. */
	Cleared,
	/** The part is taken. Nothing more happens here. */
	Looted
};

/** One kind of guard in a camp and how its number grows with the siege clock. */
USTRUCT(BlueprintType)
struct FSiegeCampGuardKind
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp")
	TSubclassOf<AShooterNPC> NPCClass;

	/** How many at the first minute this kind comes in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0"))
	int32 BaseCount = 1;

	/** One more every this many minutes after FirstMinute. 0 = no growth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0", Units = "min"))
	float AddOneEveryMinutes = 0.0f;

	/** Siege clock from which this kind is in the camp at all (min). A second tankette from 25 min is a
	 *  row of its own with FirstMinute 25. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0", Units = "min"))
	float FirstMinute = 0.0f;

	/** Ceiling. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0"))
	int32 MaxCount = 0;

	/** Added for every human player past the first (rounded down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0"))
	float CountPerExtraPlayer = 0.0f;

	int32 CountAt(float SiegeMinutes, int32 NumPlayers) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSiegeCampStateChanged, ESiegeCampState, NewState);

UCLASS()
class POLARITY_API ASiegeCampSite : public AActor
{
	GENERATED_BODY()

public:

	ASiegeCampSite();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== Layout ====================

	/** The slot name in Tools/Lanes/layout.json (BehindTop_1, JungleBot_4, ...). For the log. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Camp")
	FName SlotName;

	/** Which side of the forest this camp is on (Top or Bot). Each side gets all five part slots. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Camp")
	FName ForestSide;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camp")
	ESiegeCampTier Tier = ESiegeCampTier::Easy;

	/** The camp's own landmark pieces: smoke, fire, lamp, searchlight. Their Niagara, particle and light
	 *  components are switched off when the part is taken, so a dead camp reads as dead from afar. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Camp")
	TArray<TObjectPtr<AActor>> LandmarkActors;

	/** Where the guards stand, relative to the camp. Empty = a ring of GuardRingRadius around it. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Camp", meta = (MakeEditWidget))
	TArray<FVector> GuardPosts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camp", meta = (ClampMin = "100.0", Units = "cm"))
	float GuardRingRadius = 900.0f;

	// ==================== Guards ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards")
	TArray<FSiegeCampGuardKind> Guards;

	/** A player this near wakes the camp (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards", meta = (ClampMin = "0.0", Units = "cm"))
	float ActivationRadius = 15000.0f;

	/** No player this near for SleepSeconds, and no guard lost yet = the guards go away and the camp
	 *  sleeps again; the next visit counts them afresh (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards", meta = (ClampMin = "0.0", Units = "cm"))
	float SleepRadius = 25000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards", meta = (ClampMin = "0.0", Units = "s"))
	float SleepSeconds = 20.0f;

	/** Copied onto every guard (USiegeCampGuard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards", meta = (ClampMin = "0.0", Units = "cm"))
	float LeashRadius = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRoamRadius = 5500.0f;

	/** A flying guard (a carrier) hangs this high over its post (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Guards", meta = (ClampMin = "0.0", Units = "cm"))
	float FlyerHeight = 1500.0f;

	// ==================== Part ====================

	/** Every part the run may hand out. The start of the run picks one from here by slot and by this
	 *  camp's tier (easy Common, medium Rare, hard Epic). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camp|Part")
	TArray<TObjectPtr<UMechPartDefinition>> PartPool;

	/** A player standing this near the crate takes the part once the camp is cleared (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp|Part", meta = (ClampMin = "0.0", Units = "cm"))
	float PickupRadius = 250.0f;

	UFUNCTION(BlueprintPure, Category = "Camp")
	UMechPartDefinition* GetPart() const { return Part; }

	UFUNCTION(BlueprintPure, Category = "Camp")
	ESiegeCampState GetState() const { return State; }

	/** Server: hand this camp its part for the run. */
	void SetPart(UMechPartDefinition* InPart);

	/** Rarity this camp's parts have. */
	uint8 GetPartRarity() const;

	/** Server: every live guard takes on Attacker. */
	void AlertGuards(AActor* Attacker);

	/** Server: wake the camp now, whoever is near. Console: polarity.camp.wake. */
	void Wake();

	UPROPERTY(BlueprintAssignable, Category = "Camp")
	FOnSiegeCampStateChanged OnStateChanged;

	/** For the Blueprint: dress the camp for its new state (on every machine). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Camp", meta = (DisplayName = "On Camp State Changed"))
	void BP_OnCampStateChanged(ESiegeCampState NewState);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Where the part lies (on a crate the build step puts under it). Shows the part's DisplayMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PartDisplay;

private:

	void SpawnGuards();
	bool SpawnGuard(TSubclassOf<AShooterNPC> NPCClass, const FVector& PostLocal, float HealthMultiplier);
	void DismissGuards();
	void TickGuarded(float DeltaTime);
	void TickCleared();
	void SetState(ESiegeCampState NewState);
	void ApplyStateVisuals();
	void ApplyPartVisuals();
	void SetLandmarksLit(bool bLit);

	/** Where guard number Index stands, relative to the camp. */
	FVector GetPostLocal(int32 Index, int32 Total) const;

	/** Nearest human player's 2D distance, or a huge number. */
	float NearestPlayerDistance2D(APawn** OutPawn = nullptr) const;

	UFUNCTION()
	void OnGuardDied(AShooterNPC* DeadNPC);

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_Part();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ESiegeCampState State = ESiegeCampState::Dormant;

	UPROPERTY(ReplicatedUsing = OnRep_Part)
	TObjectPtr<UMechPartDefinition> Part;

	TArray<TWeakObjectPtr<AShooterNPC>> LiveGuards;
	int32 GuardsLost = 0;
	float NobodyNearSeconds = 0.0f;

	/** Start angle of the guard ring, random per wake. */
	float RingPhase = 0.0f;

	float LastFullBagLogTime = -1000.0f;
};
