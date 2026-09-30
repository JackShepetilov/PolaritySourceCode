// SiegeCampGuard.h
// Keeps one guard at its forest camp.
//
// The camp (ASiegeCampSite) hangs this on every guard it spawns. It is the camp's leash, the same idea
// as USiegeLaneFollower's for a lane creep, but around a point instead of along a road
// (Docs/Lane_Camps_Design_2026-09-30.md):
//
//  - a guard takes on what stands within LeashRadius of the camp, or whoever just hurt it;
//  - a guard that got farther than MaxRoamRadius from the camp drops everything, walks back to its post
//    and heals to what it had when it came: a camp cannot be dragged under the turrets;
//  - with nothing to fight it keeps to its post, re-ordering the walk against the StateTree's roam
//    the way the siege march does.
//
// The controller asks IsWithinLeash (AShooterAIController::ResolveTargetIntents) and never falls back to
// the base's core for a guard; the carrier asks it too (AKamikazeCarrierDrone::TickSelfDriven).
//
// Server only, like the camp. Nothing here is replicated.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/DamageType.h"
#include "SiegeCampGuard.generated.h"

class ASiegeCampSite;
class AShooterNPC;
class UDamageType;

UCLASS(ClassGroup = (Siege), meta = (BlueprintSpawnableComponent))
class POLARITY_API USiegeCampGuard : public UActorComponent
{
	GENERATED_BODY()

public:

	USiegeCampGuard();

	/** Targets farther than this from the camp's centre are not the guard's business (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0", Units = "cm"))
	float LeashRadius = 4000.0f;

	/** The guard itself farther than this from the camp's centre = it gives up and goes home (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRoamRadius = 5500.0f;

	/** Whoever hurt the guard stays its business this long, wherever they stand (s): shooting a camp
	 *  from past the leash is answered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0", Units = "s"))
	float RetaliateSeconds = 6.0f;

	/** Idle farther than this from the post = walk back to it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.0", Units = "cm"))
	float PostTolerance = 400.0f;

	/** How often the walk home is re-ordered (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camp", meta = (ClampMin = "0.1", Units = "s"))
	float HomeOrderInterval = 0.75f;

	/** Set by the camp right after the spawn. */
	void Init(ASiegeCampSite* InCamp, const FVector& InPost, float InSpawnHealth);

	ASiegeCampSite* GetCamp() const { return Camp.Get(); }

	/** Where the guard stands when there is nothing to fight. */
	FVector GetPost() const { return Post; }

	/** True if the guard may fight Target: it stands near the camp, or it hurt the guard a moment ago.
	 *  Always false while the guard is walking home after a chase. */
	bool IsWithinLeash(const AActor* Target) const;

	/** True while the guard is walking home after being dragged too far. */
	bool IsReturning() const { return bReturning; }

	/** A camp-mate was hurt by Attacker: take it on (the whole camp aggroes at once). */
	void Alert(AActor* Attacker);

protected:

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	UFUNCTION()
	void OnOwnerDamaged(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser);

	/** The pawn behind a damage causer: the causer itself, its instigator or its owner. */
	static APawn* ResolveAttacker(AActor* DamageCauser);

	TWeakObjectPtr<ASiegeCampSite> Camp;
	FVector Post = FVector::ZeroVector;
	float SpawnHealth = 0.0f;
	bool bReturning = false;
	float HomeOrderTimer = 0.0f;

	TWeakObjectPtr<AActor> LastAttacker;
	float LastAttackedTime = -1000.0f;
};
