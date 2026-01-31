// BossStateTreeTasks.h
// StateTree Tasks and Conditions for BossCharacter

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "BossCharacter.h"
#include "BossStateTreeTasks.generated.h"

class ABossCharacter;
class AActor;

//////////////////////////////////////////////////////////////////
// TASK: Boss Approach Dash
// Dashes TOWARDS the player to close distance
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossApproachDashInstanceData
{
	GENERATED_BODY()

	/** Boss performing the dash */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to dash towards */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Approach Dash", Category = "Boss"))
struct POLARITY_API FStateTreeBossApproachDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossApproachDashInstanceData;

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
// TASK: Boss Circle Dash
// Dashes AROUND the player at current distance
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossCircleDashInstanceData
{
	GENERATED_BODY()

	/** Boss performing the dash */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to circle around */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Circle Dash", Category = "Boss"))
struct POLARITY_API FStateTreeBossCircleDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossCircleDashInstanceData;

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
// TASK: Boss Melee Attack
// Executes single melee attack after dash
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossMeleeAttackInstanceData
{
	GENERATED_BODY()

	/** Boss performing the attack */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to attack */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Melee Attack", Category = "Boss"))
struct POLARITY_API FStateTreeBossMeleeAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossMeleeAttackInstanceData;

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
// TASK: Boss Start Hovering
// Transitions boss to aerial phase hovering state
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossStartHoveringInstanceData
{
	GENERATED_BODY()

	/** Boss to start hovering */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(meta = (DisplayName = "Boss Start Hovering", Category = "Boss"))
struct POLARITY_API FStateTreeBossStartHoveringTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossStartHoveringInstanceData;

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
// TASK: Boss Stop Hovering
// Returns boss to ground movement
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossStopHoveringInstanceData
{
	GENERATED_BODY()

	/** Boss to stop hovering */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(meta = (DisplayName = "Boss Stop Hovering", Category = "Boss"))
struct POLARITY_API FStateTreeBossStopHoveringTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossStopHoveringInstanceData;

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
// TASK: Boss Aerial Strafe
// Performs slow strafe movement in aerial phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossAerialStrafeInstanceData
{
	GENERATED_BODY()

	/** Boss performing strafe */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Duration to strafe (seconds) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1"))
	float StrafeDuration = 1.0f;

	/** Internal timer */
	float ElapsedTime = 0.0f;

	/** Current strafe direction */
	FVector StrafeDirection = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Boss Aerial Strafe", Category = "Boss"))
struct POLARITY_API FStateTreeBossAerialStrafeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossAerialStrafeInstanceData;

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
// TASK: Boss Aerial Dash
// Performs evasive dash in aerial phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossAerialDashInstanceData
{
	GENERATED_BODY()

	/** Boss performing dash */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(meta = (DisplayName = "Boss Aerial Dash", Category = "Boss"))
struct POLARITY_API FStateTreeBossAerialDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossAerialDashInstanceData;

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
// TASK: Boss Match Opposite Polarity
// Changes boss polarity to opposite of target
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossMatchPolarityInstanceData
{
	GENERATED_BODY()

	/** Boss to change polarity */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target whose polarity to oppose */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Match Opposite Polarity", Category = "Boss"))
struct POLARITY_API FStateTreeBossMatchPolarityTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossMatchPolarityInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Shoot EMF Projectile
// Fires EMF projectile at target (requires weapon setup)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossShootInstanceData
{
	GENERATED_BODY()

	/** Boss shooting */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to shoot at */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Shoot", Category = "Boss"))
struct POLARITY_API FStateTreeBossShootTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossShootInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Enter Finisher Phase
// Transitions boss to finisher phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossEnterFinisherInstanceData
{
	GENERATED_BODY()

	/** Boss entering finisher phase */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(meta = (DisplayName = "Boss Enter Finisher Phase", Category = "Boss"))
struct POLARITY_API FStateTreeBossEnterFinisherTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossEnterFinisherInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Set Phase
// Manually sets boss phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossSetPhaseInstanceData
{
	GENERATED_BODY()

	/** Boss to change phase */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Phase to set */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	EBossPhase NewPhase = EBossPhase::Ground;
};

USTRUCT(meta = (DisplayName = "Boss Set Phase", Category = "Boss"))
struct POLARITY_API FStateTreeBossSetPhaseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossSetPhaseInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITIONS
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Phase Is
// Checks if boss is in specified phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossPhaseIsInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Phase to check for */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	EBossPhase ExpectedPhase = EBossPhase::Ground;
};

USTRUCT(DisplayName = "Boss Phase Is", Category = "Boss")
struct POLARITY_API FStateTreeBossPhaseIsCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossPhaseIsInstanceData;

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
// CONDITION: Boss Should Transition To Aerial
// Checks if boss should enter aerial phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossShouldGoAerialInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Should Transition To Aerial", Category = "Boss")
struct POLARITY_API FStateTreeBossShouldGoAerialCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossShouldGoAerialInstanceData;

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
// CONDITION: Boss Should Transition To Ground
// Checks if boss should return to ground phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossShouldGoGroundInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Should Transition To Ground", Category = "Boss")
struct POLARITY_API FStateTreeBossShouldGoGroundCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossShouldGoGroundInstanceData;

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
// CONDITION: Boss Can Dash
// Checks if boss can perform dash
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossCanDashInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Can Dash", Category = "Boss")
struct POLARITY_API FStateTreeBossCanDashCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossCanDashInstanceData;

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
// CONDITION: Boss Can Melee Attack
// Checks if boss can perform melee attack
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossCanMeleeInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Can Melee Attack", Category = "Boss")
struct POLARITY_API FStateTreeBossCanMeleeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossCanMeleeInstanceData;

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
// CONDITION: Boss Is Dashing
// Checks if boss is currently dashing
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossIsDashingInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is Dashing", Category = "Boss")
struct POLARITY_API FStateTreeBossIsDashingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossIsDashingInstanceData;

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
// CONDITION: Boss Is In Melee Range
// Checks if target is within boss melee range
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossInMeleeRangeInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to check range to */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Boss Is In Melee Range", Category = "Boss")
struct POLARITY_API FStateTreeBossInMeleeRangeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossInMeleeRangeInstanceData;

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
// CONDITION: Boss Target Is Far
// Checks if target is far (needs approach dash)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossTargetIsFarInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to check distance to */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Boss Target Is Far", Category = "Boss")
struct POLARITY_API FStateTreeBossTargetIsFarCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossTargetIsFarInstanceData;

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
// CONDITION: Boss Target Is Close
// Checks if target is close (no approach needed, can circle/attack)
// NOTE: StateTree does NOT support condition inversion, so we need both Far and Close
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossTargetIsCloseInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	/** Target to check distance to */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Boss Target Is Close", Category = "Boss")
struct POLARITY_API FStateTreeBossTargetIsCloseCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossTargetIsCloseInstanceData;

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
// CONDITION: Boss Is In Finisher Phase
// Checks if boss is in finisher phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossInFinisherInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is In Finisher Phase", Category = "Boss")
struct POLARITY_API FStateTreeBossInFinisherCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossInFinisherInstanceData;

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
// CONDITION: Boss Is On Ground (Walking)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossIsOnGroundInstanceData
{
	GENERATED_BODY()

	/** Boss to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is On Ground", Category = "Boss")
struct POLARITY_API FStateTreeBossIsOnGroundCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossIsOnGroundInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
