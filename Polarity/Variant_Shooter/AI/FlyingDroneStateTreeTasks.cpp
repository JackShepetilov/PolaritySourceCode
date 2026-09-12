// FlyingDroneStateTreeTasks.cpp
// Implementation of StateTree Tasks and Conditions for FlyingDrone (flight and evasion)

#include "FlyingDroneStateTreeTasks.h"
#include "AI/TacticalSpace.h"
#include "FlyingDrone.h"
#include "KamikazeCarrierDrone.h"
#include "FlyingAIMovementComponent.h"
#include "EnemyCombatProfile.h"
#include "StateTreeExecutionContext.h"
#include "NoFlyZone.h"
#include "../../AI/Coordination/AICombatCoordinator.h"
#include "AI/PolarityTeams.h"

//////////////////////////////////////////////////////////////////
// TASK: Drone Evasive Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneEvasiveDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneEvasiveDashTask: Invalid Drone"));
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if can dash
	if (!Data.Drone->CanPerformEvasiveDash())
	{
		UE_LOG(LogTemp, Verbose, TEXT("DroneEvasiveDashTask: Dash on cooldown"));
		return EStateTreeRunStatus::Failed;
	}

	// Perform the dash
	if (!Data.Drone->PerformRandomEvasiveDash())
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneEvasiveDashTask: Failed to start dash"));
		return EStateTreeRunStatus::Failed;
	}

	// Clear damage flag since we're responding to it
	Data.Drone->ClearDamageTakenFlag();

	UE_LOG(LogTemp, Verbose, TEXT("DroneEvasiveDashTask: Started evasive dash"));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneEvasiveDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if dash is complete
	if (!Data.Drone->IsDashing())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDroneEvasiveDashTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Nothing special needed
}

#if WITH_EDITOR
FText FStateTreeDroneEvasiveDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Perform evasive dash in random direction"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Drone Fly To Random Point
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneFlyToRandomPointTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneFlyToRandomPointTask: Invalid Drone"));
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement();
	if (!FlyingMovement)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneFlyToRandomPointTask: No FlyingMovement component"));
		return EStateTreeRunStatus::Failed;
	}

	FVector TargetPoint;
	bool bFoundPoint = false;
	constexpr int32 MaxAttempts = 10;

	// If we have a target to orbit around, generate point relative to them
	if (Data.TargetToOrbit)
	{
		const FVector TargetLocation = Data.TargetToOrbit->GetActorLocation();

		for (int32 Attempt = 0; Attempt < MaxAttempts && !bFoundPoint; ++Attempt)
		{
			const float RandomAngle = FMath::RandRange(0.0f, 2.0f * PI);
			const float RandomDistance = FMath::RandRange(Data.MinDistanceFromTarget, Data.MaxDistanceFromTarget);

			FVector CandidatePoint = TargetLocation;
			CandidatePoint.X += FMath::Cos(RandomAngle) * RandomDistance;
			CandidatePoint.Y += FMath::Sin(RandomAngle) * RandomDistance;
			CandidatePoint.Z = Data.Drone->GetActorLocation().Z;

			FVector ProjectedPoint;
			if (FlyingMovement->ProjectToNavMesh(CandidatePoint, ProjectedPoint))
			{
				if (!ANoFlyZone::IsPointInNoFlyZone(Data.Drone, ProjectedPoint))
				{
					TargetPoint = ProjectedPoint;
					bFoundPoint = true;
				}
			}
		}
	}

	// Fallback to patrol point generation (with no-fly zone rejection)
	if (!bFoundPoint)
	{
		for (int32 Attempt = 0; Attempt < MaxAttempts && !bFoundPoint; ++Attempt)
		{
			FVector CandidatePoint;
			if (FlyingMovement->GetRandomPatrolPoint(CandidatePoint))
			{
				if (!ANoFlyZone::IsPointInNoFlyZone(Data.Drone, CandidatePoint))
				{
					TargetPoint = CandidatePoint;
					bFoundPoint = true;
				}
			}
		}
	}

	if (!bFoundPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneFlyToRandomPointTask: Failed to find valid point"));
		return EStateTreeRunStatus::Failed;
	}

	// Start flying to the point
	FlyingMovement->FlyToLocation(TargetPoint);

	UE_LOG(LogTemp, Verbose, TEXT("DroneFlyToRandomPointTask: Flying to (%.0f, %.0f, %.0f)"),
		TargetPoint.X, TargetPoint.Y, TargetPoint.Z);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneFlyToRandomPointTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if movement is complete
	if (!Data.Drone->IsFlying())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDroneFlyToRandomPointTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Stop movement if we're exiting early
	if (Data.Drone && Transition.CurrentRunStatus != EStateTreeRunStatus::Succeeded)
	{
		Data.Drone->StopMovement();
	}
}

#if WITH_EDITOR
FText FStateTreeDroneFlyToRandomPointTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Fly to random point (NavMesh validated)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Took Damage Recently
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneTookDamageCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->TookDamageRecently(Data.GracePeriod);
}

#if WITH_EDITOR
FText FStateTreeDroneTookDamageCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone took damage recently"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Can Evasive Dash
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneCanEvasiveDashCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->CanPerformEvasiveDash();
}

#if WITH_EDITOR
FText FStateTreeDroneCanEvasiveDashCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone can perform evasive dash (off cooldown)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Flying
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneIsFlyingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->IsFlying();
}

#if WITH_EDITOR
FText FStateTreeDroneIsFlyingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone is currently flying to destination"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Dashing
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneIsDashingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->IsDashing();
}

#if WITH_EDITOR
FText FStateTreeDroneIsDashingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone is currently dashing"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is In No-Fly Zone
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneIsInNoFlyZoneCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return ANoFlyZone::IsPointInNoFlyZone(Data.Drone, Data.Drone->GetActorLocation());
}

#if WITH_EDITOR
FText FStateTreeDroneIsInNoFlyZoneCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone is inside a No-Fly Zone"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Drone Pick Fire Position
//////////////////////////////////////////////////////////////////

namespace
{
	/** True when a straight flight from From to To looks clear: the direct ray plus a few
	 *  slightly-offset parallel rays all pass. A single ray threads gaps a real capsule cannot,
	 *  so the offset rays approximate the drone's body sweeping the corridor. */
	bool HasClearApproach(const UWorld* World, AFlyingDrone* Drone, const FVector& From, const FVector& To, int32 OffsetRayCount)
	{
		if (!World || !Drone)
		{
			return false;
		}

		const FVector PathDir = (To - From).GetSafeNormal();
		if (PathDir.IsNearlyZero())
		{
			return true;
		}

		// Perpendicular to the path for offsets (falls back to X when the path is vertical)
		FVector SideDir = FVector::CrossProduct(PathDir, FVector::UpVector).GetSafeNormal();
		if (SideDir.IsNearlyZero())
		{
			SideDir = FVector::CrossProduct(PathDir, FVector(1.0f, 0.0f, 0.0f)).GetSafeNormal();
		}

		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Drone);

		auto IsBlocked = [World, &Params](const FVector& Start, const FVector& End) -> bool
		{
			FHitResult Hit;
			return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
		};

		constexpr float RayOffset = 50.0f;

		for (int32 Ray = 0; Ray <= OffsetRayCount; ++Ray)
		{
			// Ray 0 dead-on, then alternating sides: +/- one body radius each round
			FVector OffsetVec = FVector::ZeroVector;
			if (Ray > 0)
			{
				const float Offset = static_cast<float>(((Ray + 1) / 2)) * RayOffset *
					((Ray % 2 == 1) ? 1.0f : -1.0f);
				OffsetVec = SideDir * Offset;
			}

			if (IsBlocked(From + OffsetVec, To + OffsetVec))
			{
				return false;
			}
		}

		return true;
	}

	/** How many hostile pawns can currently see this point (exposure score for ranking). */
	int32 CountExposureToHostiles(AFlyingDrone* Drone, const FVector& Point)
	{
		TArray<APawn*> Hostiles;
		PolarityTeams::GatherHostilePawns(Drone, Hostiles);

		int32 Exposure = 0;
		for (const APawn* Enemy : Hostiles)
		{
			if (!Enemy)
			{
				continue;
			}

			FHitResult Hit;
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Drone);
			Params.AddIgnoredActor(Enemy);

			if (!Drone->GetWorld()->LineTraceSingleByChannel(
				Hit, Enemy->GetActorLocation(), Point, ECC_Visibility, Params))
			{
				++Exposure;
			}
		}

		return Exposure;
	}
}

EStateTreeRunStatus FStateTreeDronePickFirePositionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || !Data.Target || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement();
	if (!FlyingMovement)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Fire is suppressed for the whole relocation flight
	Data.Drone->StopShooting();

	FVector ChosenPoint = FVector::ZeroVector;

	// Coordinator slot first: keeps drones of one side from mixing into the opposite side's air
	if (Data.bUseCoordinator)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
		{
			FVector SlotPosition;
			if (Coordinator->GetAssignedSlotPosition(Data.Drone, SlotPosition) &&
				!ANoFlyZone::IsPointInNoFlyZone(Data.Drone, SlotPosition) &&
				HasClearApproach(Data.Drone->GetWorld(), Data.Drone, Data.Drone->GetActorLocation(), SlotPosition, Data.PathProbeCount))
			{
				ChosenPoint = SlotPosition;
				FlyingMovement->FlyToLocation(ChosenPoint, Data.AcceptanceRadius);
				return EStateTreeRunStatus::Running;
			}
		}
	}

	if (!PickBestCandidate(Data, ChosenPoint))
	{
		UE_LOG(LogTemp, Warning, TEXT("DronePickFirePosition: no valid candidate found"));
		return EStateTreeRunStatus::Failed;
	}

	FlyingMovement->FlyToLocation(ChosenPoint, Data.AcceptanceRadius);
	return EStateTreeRunStatus::Running;
}

bool FStateTreeDronePickFirePositionTask::PickBestCandidate(FInstanceDataType& Data, FVector& OutPoint) const
{
	UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement();
	if (!FlyingMovement)
	{
		return false;
	}

	const FVector TargetLocation = Data.Target->GetActorLocation();
	const FVector CurrentLocation = Data.Drone->GetActorLocation();

	bool bFoundAny = false;
	int32 BestExposure = MAX_int32;

	// Who is around, so a candidate can be judged as ground and not only as a firing angle. Built
	// once for the whole pick.
	TacticalSpace::FSpaceContext SpaceContext;
	TacticalSpace::BuildContext(Data.Drone, SpaceContext);

	TacticalSpace::FSpaceWeights SpaceWeights;
	SpaceWeights.AllyWeight = Data.AllyCohesionWeight;
	SpaceWeights.EnemyWeight = Data.EnemyAvoidWeight;

	float BestScore = -TNumericLimits<float>::Max();

	for (int32 Attempt = 0; Attempt < Data.CandidateSamples * 2; ++Attempt)
	{
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Distance = FMath::FRandRange(Data.MinFireDistance, Data.MaxFireDistance);

		FVector Candidate = TargetLocation;
		Candidate.X += FMath::Cos(Angle) * Distance;
		Candidate.Y += FMath::Sin(Angle) * Distance;
		Candidate.Z = CurrentLocation.Z + FMath::FRandRange(-150.0f, 150.0f);

		FVector ProjectedPoint;
		if (!FlyingMovement->ProjectToNavMesh(Candidate, ProjectedPoint))
		{
			continue;
		}

		if (ANoFlyZone::IsPointInNoFlyZone(Data.Drone, ProjectedPoint))
		{
			continue;
		}

		// LOS to target from the candidate (drone eye height is its center)
		{
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(Data.Drone);
			LOSParams.AddIgnoredActor(Data.Target);

			if (Data.Drone->GetWorld()->LineTraceSingleByChannel(
				LOSHit, ProjectedPoint, TargetLocation, ECC_Visibility, LOSParams))
			{
				continue;
			}
		}

		if (!HasClearApproach(Data.Drone->GetWorld(), Data.Drone, CurrentLocation, ProjectedPoint, Data.PathProbeCount))
		{
			continue;
		}

		// One number out of three things that all matter: how many hostiles can shoot this spot, how
		// close it is to friends, how deep it sits in enemy ground. Scoring them separately is what
		// produced a drone that took a perfectly covered position alone in the middle of the enemy
		// squad, because nothing it measured said that was insane.
		const int32 Exposure = CountExposureToHostiles(Data.Drone, ProjectedPoint);
		const float Score = TacticalSpace::ScorePosition(SpaceContext, ProjectedPoint, SpaceWeights)
			- Data.ExposureWeight * static_cast<float>(Exposure);

		if (!bFoundAny || Score > BestScore)
		{
			BestScore = Score;
			BestExposure = Exposure;
			OutPoint = ProjectedPoint;
			bFoundAny = true;
		}
	}

	return bFoundAny;
}

EStateTreeRunStatus FStateTreeDronePickFirePositionTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Arrival: movement component completed or aborted (stuck detection loops us back here)
	if (!Data.Drone->IsFlying())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDronePickFirePositionTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Drone && Transition.CurrentRunStatus != EStateTreeRunStatus::Succeeded)
	{
		Data.Drone->StopMovement();
	}
}

#if WITH_EDITOR
FText FStateTreeDronePickFirePositionTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Pick least-exposed fire position and fly there (fire off)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Drone Hold Fire Position
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneHoldFirePositionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || !Data.Target || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Per-class rhythm numbers override the tree defaults when the profile carries them
	if (UEnemyCombatProfile* Profile = Data.Drone->GetCombatProfile())
	{
		if (Profile->bOverrideDroneTuning)
		{
			Data.AimDuration = Profile->DroneAimTelegraphDuration;
			Data.OverheatDuration = Profile->DroneOverheatDuration;
		}
	}

	Data.AnchorPosition = Data.Drone->GetActorLocation();
	Data.DriftTime = FMath::FRandRange(0.0f, 10.0f); // desync drift phases across drones
	Data.PhaseTime = 0.0f;
	Data.Phase = EDroneFireHoldPhase::Aiming;
	Data.bIsShooting = false;
	Data.LastLOSTime = Data.Drone->GetWorld()->GetTimeSeconds();

	// Damage taken on the way here must not instantly relocate the fresh position
	Data.Drone->ClearDamageTakenFlag();

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneHoldFirePositionTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead() || !Data.Target)
	{
		EndFiring(Data);
		return EStateTreeRunStatus::Failed;
	}

	const bool bHasLOS = Data.Drone->HasLineOfSightTo(Data.Target);
	const float Now = Data.Drone->GetWorld()->GetTimeSeconds();
	if (bHasLOS)
	{
		Data.LastLOSTime = Now;
	}

	// Honest dash pause: while dashing nothing advances - timers freeze and fire stays off
	if (Data.Drone->IsDashing())
	{
		EndFiring(Data);
		return EStateTreeRunStatus::Running;
	}

	// Hit while holding = hit from an uncontrolled position: give the spot up
	if (Data.bRelocateOnDamaged && Data.Drone->TookDamageRecently(0.25f))
	{
		EndFiring(Data);
		return EStateTreeRunStatus::Succeeded;
	}

	// Hold with slow lateral drift + bob: capturable by aim assist, never a sitting target
	if (UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement())
	{
		Data.DriftTime += DeltaTime;

		FVector ToTargetHorizontal = (Data.Target->GetActorLocation() - Data.AnchorPosition);
		ToTargetHorizontal.Z = 0.0f;
		const FVector SideDir = FVector::CrossProduct(FVector::UpVector, ToTargetHorizontal.GetSafeNormal());

		const float LateralOffset = FMath::Sin(Data.DriftTime * Data.DriftFrequency * 2.0f * PI) * Data.DriftAmplitude;
		const float BobOffset = FMath::Sin(Data.DriftTime * Data.DriftFrequency * 2.0f * PI * 0.7f + 1.3f) * Data.BobAmplitude;

		FlyingMovement->FlyToLocation(Data.AnchorPosition + SideDir * LateralOffset + FVector(0.0f, 0.0f, BobOffset));
	}

	switch (Data.Phase)
	{
	case EDroneFireHoldPhase::Aiming:
	{
		Data.PhaseTime += DeltaTime;
		// TODO(VFX): pre-fire telegraph - charge VFX at muzzle + thin beam gaining brightness,
		// needs Niagara assets assigned in the editor session.

		if (!bHasLOS && (Now - Data.LastLOSTime) > Data.LostLOSGrace)
		{
			return EStateTreeRunStatus::Succeeded; // relocate to regain sight
		}

		bool bCanOpenFire = Data.PhaseTime >= Data.AimDuration &&
			bHasLOS &&
			!Data.Drone->IsInBurstCooldown() &&
			!Data.Drone->IsCurrentlyShooting();

		if (bCanOpenFire && Data.bUseCoordinator)
		{
			if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
			{
				bCanOpenFire = Coordinator->RequestAttackPermission(Data.Drone);
			}
		}

		if (bCanOpenFire)
		{
			Data.Drone->StartShooting(Data.Target, true);
			Data.bIsShooting = true;
			Data.PhaseTime = 0.0f;
			Data.Phase = EDroneFireHoldPhase::Firing;

			if (Data.bUseCoordinator)
			{
				if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
				{
					Coordinator->NotifyAttackStarted(Data.Drone);
				}
			}
		}
		break;
	}

	case EDroneFireHoldPhase::Firing:
	{
		if (!bHasLOS)
		{
			// Never fire through walls: drop the burst and relocate
			EndFiring(Data);
			return EStateTreeRunStatus::Succeeded;
		}

		// Burst finished -> overheat window. StopShooting clears bWantsToShoot so OnBurstCooldownEnd
		// cannot auto-resume behind the task's back.
		if (Data.Drone->IsInBurstCooldown() || !Data.Drone->IsCurrentlyShooting())
		{
			EndFiring(Data);
			Data.PhaseTime = 0.0f;
			Data.Phase = EDroneFireHoldPhase::Overheating;
		}
		break;
	}

	case EDroneFireHoldPhase::Overheating:
	{
		Data.PhaseTime += DeltaTime;
		// TODO(VFX): smoke wisp from the muzzle during the window.
		// Critical stage overheats faster (shorter window between bursts).

		if (Data.PhaseTime >= Data.OverheatDuration * Data.Drone->GetStageOverheatScale())
		{
			// Cycle complete: the public rhythm promises a relocation after every overheat
			return EStateTreeRunStatus::Succeeded;
		}
		break;
	}
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDroneHoldFirePositionTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	EndFiring(Data);

	if (Data.Drone && Transition.CurrentRunStatus != EStateTreeRunStatus::Succeeded)
	{
		Data.Drone->StopMovement();
	}
}

void FStateTreeDroneHoldFirePositionTask::EndFiring(FInstanceDataType& Data) const
{
	if (!Data.Drone)
	{
		return;
	}

	if (Data.bIsShooting)
	{
		Data.Drone->StopShooting();
		Data.bIsShooting = false;

		if (Data.bUseCoordinator)
		{
			if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
			{
				Coordinator->NotifyAttackComplete(Data.Drone);
			}
		}
	}
}

#if WITH_EDITOR
FText FStateTreeDroneHoldFirePositionTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Hold fire position with drift: telegraph -> burst -> overheat, then relocate"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Wants Repair
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneWantsRepairCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->ShouldBeginRepair();
}

#if WITH_EDITOR
FText FStateTreeDroneWantsRepairCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone should break off and repair (stage/threshold/cooldown)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Drone Repair Retreat
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneRepairRetreatTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!Data.Drone->BeginRepairRetreat())
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneRepairRetreatTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!Data.Drone->IsRepairing())
	{
		// Interrupted by damage inside the pawn, or healed up completely
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDroneRepairRetreatTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Safety net: leaving the state mid-retreat (transition override, death cleanup) ends it
	if (Data.Drone && Data.Drone->IsRepairing())
	{
		Data.Drone->EndRepairRetreat(false);
	}
}

#if WITH_EDITOR
FText FStateTreeDroneRepairRetreatTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Climb to repair altitude, hover and heal until full HP"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Drone Deploy Kamikaze
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneDeployKamikazeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Carrier || Data.Carrier->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Cooldown, an empty bay and a salvo already in the air all answer here, so the state can be
	// entered optimistically and simply fail back to whatever the drone was doing.
	if (!Data.Carrier->DeploySalvo())
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneDeployKamikazeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Carrier || Data.Carrier->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	return Data.Carrier->IsSalvoInProgress()
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeDroneDeployKamikazeTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drop a salvo of homing kamikaze munitions"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Can Deploy Kamikaze
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneCanDeployKamikazeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	return Data.Carrier && Data.Carrier->CanDeploySalvo();
}

#if WITH_EDITOR
FText FStateTreeDroneCanDeployKamikazeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Carrier is loaded and off salvo cooldown"));
}
#endif
