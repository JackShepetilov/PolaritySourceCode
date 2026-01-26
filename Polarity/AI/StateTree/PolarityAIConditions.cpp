// PolarityAIConditions.cpp

#include "PolarityAIConditions.h"
#include "StateTreeExecutionContext.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../Coordination/AICombatCoordinator.h"
#include "../Components/MeleeRetreatComponent.h"
#include "../../ApexMovementComponent.h"
#include "../../Variant_Shooter/AI/ShooterNPC.h"

// ============================================================================
// IsPlayerMovingFast
// ============================================================================

bool FSTCondition_IsPlayerMovingFast::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Target)
	{
		return !Data.bWantMovingFast;
	}

	float Speed = 0.0f;

	if (const ACharacter* Character = Cast<ACharacter>(Data.Target))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Speed = Movement->Velocity.Size();
		}
	}
	else
	{
		Speed = Data.Target->GetVelocity().Size();
	}

	const bool bIsMovingFast = Speed >= Data.SpeedThreshold;
	return bIsMovingFast == Data.bWantMovingFast;
}

#if WITH_EDITOR
FText FSTCondition_IsPlayerMovingFast::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::Format(NSLOCTEXT("PolarityAI", "IsPlayerMovingFastDesc",
		"Target {0} moving fast (>{1} cm/s)"),
		Data->bWantMovingFast ? FText::FromString("IS") : FText::FromString("is NOT"),
		FText::AsNumber(Data->SpeedThreshold));
}
#endif

// ============================================================================
// IsPlayerWallRunning
// ============================================================================

bool FSTCondition_IsPlayerWallRunning::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Target)
	{
		return !Data.bWantWallRunning;
	}

	bool bIsWallRunning = false;

	if (const ACharacter* Character = Cast<ACharacter>(Data.Target))
	{
		if (const UApexMovementComponent* ApexMovement = Character->FindComponentByClass<UApexMovementComponent>())
		{
			bIsWallRunning = ApexMovement->IsWallRunning();
		}
	}

	return bIsWallRunning == Data.bWantWallRunning;
}

#if WITH_EDITOR
FText FSTCondition_IsPlayerWallRunning::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::Format(NSLOCTEXT("PolarityAI", "IsPlayerWallRunningDesc",
		"Target {0} wall running"),
		Data->bWantWallRunning ? FText::FromString("IS") : FText::FromString("is NOT"));
}
#endif

// ============================================================================
// HasAttackPermission
// ============================================================================

bool FSTCondition_HasAttackPermission::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return false;
	}

	AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
	if (!Coordinator)
	{
		return true; // No coordinator = always allowed
	}

	return Coordinator->HasAttackPermission(Data.NPC);
}

#if WITH_EDITOR
FText FSTCondition_HasAttackPermission::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "HasAttackPermissionDesc", "Has attack permission from coordinator");
}
#endif

// ============================================================================
// ShouldRetreat
// ============================================================================

bool FSTCondition_ShouldRetreat::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return false;
	}

	if (UMeleeRetreatComponent* RetreatComp = Data.NPC->FindComponentByClass<UMeleeRetreatComponent>())
	{
		return RetreatComp->IsRetreating();
	}

	return false;
}

#if WITH_EDITOR
FText FSTCondition_ShouldRetreat::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "ShouldRetreatDesc", "Should retreat (after melee hit)");
}
#endif

// ============================================================================
// IsInCombatRange
// ============================================================================

bool FSTCondition_IsInCombatRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Target)
	{
		return false;
	}

	const float Distance = FVector::Dist(Data.NPC->GetActorLocation(), Data.Target->GetActorLocation());
	return Distance >= Data.MinRange && Distance <= Data.MaxRange;
}

#if WITH_EDITOR
FText FSTCondition_IsInCombatRange::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::Format(NSLOCTEXT("PolarityAI", "IsInCombatRangeDesc",
		"Target in range ({0}-{1} cm)"),
		FText::AsNumber(Data->MinRange),
		FText::AsNumber(Data->MaxRange));
}
#endif

// ============================================================================
// CanRetreat
// ============================================================================

bool FSTCondition_CanRetreat::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return false;
	}

	if (UMeleeRetreatComponent* RetreatComp = Data.NPC->FindComponentByClass<UMeleeRetreatComponent>())
	{
		return RetreatComp->CanRetreat();
	}

	return false;
}

#if WITH_EDITOR
FText FSTCondition_CanRetreat::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "CanRetreatDesc", "Can retreat (off cooldown)");
}
#endif

// ============================================================================
// CanShoot
// ============================================================================

bool FSTCondition_CanShoot::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return Data.bInvert;
	}

	// Check if dead
	if (Data.NPC->IsDead())
	{
		return Data.bInvert;
	}

	// Check if already shooting
	if (Data.NPC->IsCurrentlyShooting())
	{
		return Data.bInvert;
	}

	// Check if in burst cooldown
	if (Data.NPC->IsInBurstCooldown())
	{
		return Data.bInvert;
	}

	// Check line of sight if required
	if (Data.bRequireLineOfSight && Data.Target)
	{
		if (!Data.NPC->HasLineOfSightTo(Data.Target))
		{
			return Data.bInvert;
		}
	}

	// Can shoot!
	return !Data.bInvert;
}

#if WITH_EDITOR
FText FSTCondition_CanShoot::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data->bInvert)
	{
		return NSLOCTEXT("PolarityAI", "CanShootDescInverted", "Cannot shoot (dead, in cooldown, or no LOS)");
	}
	return NSLOCTEXT("PolarityAI", "CanShootDesc", "Can shoot (not dead, off cooldown, has LOS)");
}
#endif