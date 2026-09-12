// TrackedTankStateTreeTasks.h
// StateTree Tasks and Conditions specific to ATrackedTankNPC:
// barrel selection, siege targets (buildings), anchor holding, immobilization.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "TrackedTankStateTreeTasks.generated.h"

class ATrackedTankNPC;

//////////////////////////////////////////////////////////////////
// CONDITION: Tank Target Is Building
// True when the current target implements the building marker
// (ATurretBuilding does) - such targets belong to the main gun.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTreeTankTargetIsBuildingInstanceData
{
	GENERATED_BODY()

	/** Candidate target */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Tank Target Is Building", Category = "Tracked Tank")
struct POLARITY_API FSTTreeTankTargetIsBuildingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTreeTankTargetIsBuildingInstanceData;

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
// CONDITION: Tank Target Is Pawn
// Inverse of the above (StateTree has no condition inversion):
// pawns and other non-buildings belong to the machine gun.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTreeTankTargetIsPawnInstanceData
{
	GENERATED_BODY()

	/** Candidate target */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Tank Target Is Pawn", Category = "Tracked Tank")
struct POLARITY_API FSTTreeTankTargetIsPawnCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTreeTankTargetIsPawnInstanceData;

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
// CONDITION: Tank Is Immobilized
// Tracks are broken - the tree should drop movement states and
// fight from where it stands (guns still work).
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTreeTankIsImmobilizedInstanceData
{
	GENERATED_BODY()

	/** The tracked tank */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ATrackedTankNPC> Tank;
};

USTRUCT(DisplayName = "Tank Is Immobilized", Category = "Tracked Tank")
struct POLARITY_API FSTTreeTankIsImmobilizedCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTreeTankIsImmobilizedInstanceData;

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
// TASK: Tank Select Barrel
// Persistent gate task: keeps the requested barrel active while
// the state runs. Use two states ("Main Gun"/"Machine Gun") with
// the Target-Is conditions picking between them.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTask_TankSelectBarrelInstanceData
{
	GENERATED_BODY()

	/** The tracked tank */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ATrackedTankNPC> Tank;

	/** True = main gun (slow, buildings), False = machine gun (pawns) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bSelectMainGun = true;
};

USTRUCT(meta = (DisplayName = "Tank Select Barrel", Category = "Tracked Tank"))
struct POLARITY_API FSTTask_TankSelectBarrel : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_TankSelectBarrelInstanceData;

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
// TASK: Tank Find Siege Target
// Finds the nearest standing building in radius and exposes it as
// an output - bind FoundTarget into the fire task's Target input.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTask_TankFindSiegeTargetInstanceData
{
	GENERATED_BODY()

	/** The tracked tank */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ATrackedTankNPC> Tank;

	/** Search radius around the tank (cm) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "500", ClampMax = "20000"))
	float SearchRadius = 6000.0f;

	/** Output: nearest standing building, null when none found */
	UPROPERTY(EditAnywhere, Category = "Output")
	TObjectPtr<AActor> FoundTarget;
};

USTRUCT(meta = (DisplayName = "Tank Find Siege Target", Category = "Tracked Tank"))
struct POLARITY_API FSTTask_TankFindSiegeTarget : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_TankFindSiegeTargetInstanceData;

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
// TASK: Tank Siege Fire At Building
// Fires the main gun at a building until it falls or the LOS is
// gone. Buildings do not take coordinator attack tokens (those
// budget shots at pawns).
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTask_TankSiegeFireInstanceData
{
	GENERATED_BODY()

	/** The tracked tank */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ATrackedTankNPC> Tank;

	/** The building to shoot at (bind from Find Siege Target's output) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** If true, request coordinator permission before each burst like pawn fire does */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUseCoordinator = false;
};

USTRUCT(meta = (DisplayName = "Tank Siege Fire At Building", Category = "Tracked Tank"))
struct POLARITY_API FSTTask_TankSiegeFire : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_TankSiegeFireInstanceData;

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

	/** Stop firing if a burst is live */
	void EndFiring(FSTTask_TankSiegeFireInstanceData& Data) const;
};

//////////////////////////////////////////////////////////////////
// TASK: Tank Hold Position
// Sector anchor: stand ground; walk back into the leash circle if
// knocked/pushed out. Never pursues beyond it.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTask_TankHoldPositionInstanceData
{
	GENERATED_BODY()

	/** The tracked tank */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ATrackedTankNPC> Tank;

	/** How far from the anchor the tank tolerates being before walking back (cm) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100", ClampMax = "10000"))
	float LeashRadius = 1500.0f;

	// Runtime state
	FVector AnchorLocation = FVector::ZeroVector;
	bool bReturningToAnchor = false;
};

USTRUCT(meta = (DisplayName = "Tank Hold Position", Category = "Tracked Tank"))
struct POLARITY_API FSTTask_TankHoldPosition : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_TankHoldPositionInstanceData;

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
