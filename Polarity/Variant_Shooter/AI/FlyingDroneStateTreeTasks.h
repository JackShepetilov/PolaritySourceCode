// FlyingDroneStateTreeTasks.h
// StateTree Tasks and Conditions specific to FlyingDrone (flight and evasion)

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "FlyingDroneStateTreeTasks.generated.h"

class AFlyingDrone;
class AKamikazeCarrierDrone;

//////////////////////////////////////////////////////////////////
// TASK: Drone Evasive Dash
// Performs an evasive dash in a random direction
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneEvasiveDashInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone that will dash (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(meta = (DisplayName = "Drone Evasive Dash", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneEvasiveDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneEvasiveDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Fly To Random Point
// Flies to a random patrol point within NavMesh bounds
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneFlyToRandomPointInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone that will move (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Optional target to stay near (for combat positioning) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	TObjectPtr<AActor> TargetToOrbit;

	/** Maximum distance from TargetToOrbit (if set), otherwise uses patrol radius */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100", ClampMax = "5000"))
	float MaxDistanceFromTarget = 1500.0f;

	/** Minimum distance from TargetToOrbit (for combat spacing) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0", ClampMax = "2000"))
	float MinDistanceFromTarget = 500.0f;
};

USTRUCT(meta = (DisplayName = "Drone Fly To Random Point", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneFlyToRandomPointTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneFlyToRandomPointInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Took Damage Recently
// Checks if drone took damage within grace period (for evasion trigger)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneTookDamageInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Time window to consider "recent" damage (seconds) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float GracePeriod = 0.5f;
};

USTRUCT(DisplayName = "Drone Took Damage Recently", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneTookDamageCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneTookDamageInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Can Evasive Dash
// Checks if drone can perform evasive dash (cooldown check)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneCanEvasiveDashInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Can Evasive Dash", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneCanEvasiveDashCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneCanEvasiveDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Flying
// Checks if drone is currently moving to a destination
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneIsFlyingInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Is Flying", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneIsFlyingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneIsFlyingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Dashing
// Checks if drone is currently performing a dash
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneIsDashingInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Is Dashing", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneIsDashingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneIsDashingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is In No-Fly Zone
// Checks if drone's current position is inside a NoFlyZone volume
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneIsInNoFlyZoneInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Is In No-Fly Zone", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneIsInNoFlyZoneCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneIsInNoFlyZoneInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Pick Fire Position
// Discrete fire-position selection (replaces the orbit): samples candidates
// on a distance ring around the target, validates LOS / NoFlyZone / clear
// approach path, picks the one least exposed to other enemies, flies there.
// Fire is suppressed for the whole flight.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDronePickFirePositionInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone that will move (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Combat target to position against */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Closest ring distance from the target a fire position may sit at */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "200", ClampMax = "5000"))
	float MinFireDistance = 800.0f;

	/** Farthest ring distance from the target */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "400", ClampMax = "8000"))
	float MaxFireDistance = 2200.0f;

	/** How many ring candidates to sample and score per pick */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1", ClampMax = "12"))
	int32 CandidateSamples = 6;

	/** Intermediate rays used to verify a straight approach exists to a candidate */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0", ClampMax = "4"))
	int32 PathProbeCount = 2;

	/** Acceptance radius passed to the movement component */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "50", ClampMax = "1000"))
	float AcceptanceRadius = 150.0f;

	/** If true, prefer the combat coordinator slot as anti-mixing bias */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUseCoordinator = true;

	/** Pull toward friendly presence when choosing where to fire from. Zero makes the drone a loner
	 *  again; raising it makes the flight hold one piece of sky together. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float AllyCohesionWeight = 1.0f;

	/** Push away from hostile presence, so a firing position is taken in front of the enemy line
	 *  rather than inside it. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float EnemyAvoidWeight = 1.0f;

	/** Weight of each hostile that can see the candidate. Compared against the two above, so a value
	 *  near them means "cover matters as much as company". */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ExposureWeight = 0.75f;
};

USTRUCT(meta = (DisplayName = "Drone Pick Fire Position", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDronePickFirePositionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDronePickFirePositionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

private:

	/** Sample and score ring candidates; writes the winner into OutPoint. Returns false when no
	 *  candidate passes validation. */
	bool PickBestCandidate(FInstanceDataType& Data, FVector& OutPoint) const;
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Hold Fire Position
// Holds a reached fire position with slow drift + bob (target stays capturable,
// never static) and runs the public fire rhythm: aim telegraph -> burst ->
// overheat -> done (tree relocates). Honest pauses: no tracking or fire while
// dashing; damage taken while holding ends the hold early.
//////////////////////////////////////////////////////////////////

/** Phases of the hold: telegraph, burst, visible overheat window */
UENUM(BlueprintType)
enum class EDroneFireHoldPhase : uint8
{
	Aiming UMETA(DisplayName = "Aiming"),
	Firing UMETA(DisplayName = "Firing"),
	Overheating UMETA(DisplayName = "Overheating")
};

USTRUCT()
struct FStateTreeDroneHoldFirePositionInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone holding the position (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Combat target to shoot at */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Lateral drift amplitude around the anchor (cm) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0", ClampMax = "600"))
	float DriftAmplitude = 150.0f;

	/** Drift frequency (Hz) - deliberately slow so aim assist can follow */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DriftFrequency = 0.15f;

	/** Vertical bob amplitude around the anchor (cm) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0", ClampMax = "300"))
	float BobAmplitude = 40.0f;

	/** Seconds of pre-fire telegraph before the burst */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float AimDuration = 0.45f;

	/** Seconds of visible overheat after the burst (counter-play window) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float OverheatDuration = 2.5f;

	/** How long LOS may be lost before the hold gives up and the tree relocates */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float LostLOSGrace = 1.0f;

	/** Damage taken while holding ends the hold early (hit from an uncontrolled position) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bRelocateOnDamaged = true;

	/** If true, use combat coordinator for attack permission */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUseCoordinator = true;

	// Runtime state
	FVector AnchorPosition = FVector::ZeroVector;
	float DriftTime = 0.0f;
	float PhaseTime = 0.0f;
	float LastLOSTime = 0.0f;
	bool bIsShooting = false;
	EDroneFireHoldPhase Phase = EDroneFireHoldPhase::Aiming;
};

USTRUCT(meta = (DisplayName = "Drone Hold Fire Position", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneHoldFirePositionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneHoldFirePositionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

private:

	/** Stop firing and release the coordinator attack slot */
	void EndFiring(FInstanceDataType& Data) const;
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Wants Repair
// True when the repair retreat should trigger (Critical stage or accumulated
// damage past threshold, cooldown elapsed)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneWantsRepairInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Wants Repair", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneWantsRepairCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneWantsRepairInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Repair Retreat
// Climbs vertically to RepairAltitude (~100 m), hovers and heals until full HP.
// Interrupted by a heavy single hit inside the pawn (EndRepairRetreat(false)).
// Succeeds when the retreat is over either way; the tree then picks the next
// fire position from up high.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneRepairRetreatInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone that will retreat (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(meta = (DisplayName = "Drone Repair Retreat", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneRepairRetreatTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneRepairRetreatInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Deploy Kamikaze
// Releases one salvo of kamikaze munitions from a carrier drone.
// Running while the salvo drops, Succeeded when the bay doors close,
// Failed when the carrier cannot deploy (cooldown, empty bay, dead).
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneDeployKamikazeInstanceData
{
	GENERATED_BODY()

	/** Carrier that will drop the munitions (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeCarrierDrone> Carrier;
};

USTRUCT(meta = (DisplayName = "Drone Deploy Kamikaze", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneDeployKamikazeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneDeployKamikazeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Can Deploy Kamikaze
// True when a salvo would actually start now (loaded, off cooldown, alive).
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneCanDeployKamikazeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeCarrierDrone> Carrier;
};

USTRUCT(DisplayName = "Drone Can Deploy Kamikaze", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneCanDeployKamikazeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneCanDeployKamikazeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
