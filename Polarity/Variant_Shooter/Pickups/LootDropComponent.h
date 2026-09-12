// LootDropComponent.h
// Everything an actor drops, as one list.
//
// Replaces the dozen loot fields every enemy used to carry (health class and three counts, armour
// class, melee weapon and its chance, ranged weapon table, scatter settings) and the three copies of
// the code that read them (AShooterNPC::Die, AFlyingDrone::DroneDie, AKamikazeDroneNPC). One line
// of the list is one thing that can come out: a medkit, a pile of metal, a gun, anything.
//
// Every AShooterNPC has one of these built in. Anything else that should drop loot (a crate, a
// scripted event) adds the component in its blueprint and calls DropLootHere.
//
// Which enemies drop what is set per blueprint, never in C++.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LootDropComponent.generated.h"

/** What kind of kill a line needs. These are the rules the old health and armour drops ran on;
 *  the enemy decides which of them are true when it dies (AShooterNPC::MakeLootContext). */
UENUM(BlueprintType)
enum class ELootKillCondition : uint8
{
	/** Any death that drops loot at all. */
	Any,

	/** The enemy was the direct target of a channel (the old armour rule). */
	Channeled,

	/** Killed by a thrown prop, a drone, an explosion, or while stunned by one (the old full
	 *  health drop). */
	PropOrDroneKill,

	/** Killed by another enemy slammed into it, or while stunned by such a hit (the old reduced
	 *  health drop). */
	NPCCollisionKill
};

/** One thing that can drop. */
USTRUCT(BlueprintType)
struct POLARITY_API FLootDropEntry
{
	GENERATED_BODY()

	/** What to spawn. Health, armour and metal pickups, dropped weapons, or any other actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	TSubclassOf<AActor> PickupClass;

	/** 0..1, rolled once per death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 1.0f;

	/** How many actors come out when this line drops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1", ClampMax = "20"))
	int32 Count = 1;

	/** What each one carries: metal in a pile, HP in a medkit. 0 = whatever the pickup class
	 *  says by default. Ignored by actors that carry no amount (weapons, armour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0"))
	int32 Amount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	ELootKillCondition Condition = ELootKillCondition::Any;

	/** Lines with the same group drop at most one between them: they are tried in list order and
	 *  the first whose condition holds and whose chance comes up wins. Empty = rolled on its own.
	 *  This is how "only one gun" and "armour OR health" work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	FName Group;
};

/** What happened, filled by whoever is dying. */
USTRUCT(BlueprintType)
struct POLARITY_API FLootDropContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	bool bChanneled = false;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	bool bPropOrDroneKill = false;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	bool bNPCCollisionKill = false;

	/** EMF charge handed to dropped weapons. 0 leaves their own default alone. */
	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	float Charge = 0.0f;

	/** What landed the killing blow. Used to find the player a dropped gun is credited to. */
	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	TObjectPtr<AActor> KillingDamageCauser = nullptr;

	bool Passes(ELootKillCondition Condition) const;
};

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API ULootDropComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULootDropComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (TitleProperty = "PickupClass"))
	TArray<FLootDropEntry> Loot;

	/** How far pickups (health, metal) scatter from the drop point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0", Units = "cm"))
	float ScatterRadius = 150.0f;

	/** Height above the floor pickups land at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0", Units = "cm"))
	float FloorOffset = 30.0f;

	/** Where things that fall on their own (weapons, armour) appear, relative to the drop point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	FVector PhysicalDropOffset = FVector(0.0f, 0.0f, 50.0f);

	/** Roll the list and spawn what comes up. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Loot")
	void DropLoot(const FLootDropContext& Context);

	/** DropLoot at the owner's location with no kill details: only Any lines can drop. For
	 *  crates and anything else that is not an enemy. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Loot")
	void DropLootHere();

	/** First class in the list that is a BaseClass, or null. For systems that add to an
	 *  enemy's drop in kind (the Tank passive's extra medkits). */
	UClass* FindLootClass(const UClass* BaseClass) const;

	/** Spawn one line, no roll. Static so a console command can drop loot without a component. */
	static void SpawnEntry(UWorld* World, const FLootDropEntry& Entry, const FLootDropContext& Context,
		float InScatterRadius, float InFloorOffset, const FVector& InPhysicalDropOffset, AActor* DroppedBy);

	/** Count landing points on the floor in a circle around Origin, pulled back from walls. */
	static void ComputeScatterPoints(UWorld* World, const FVector& Origin, int32 Count,
		float InScatterRadius, float InFloorOffset, TArray<FVector>& OutPoints);
};
