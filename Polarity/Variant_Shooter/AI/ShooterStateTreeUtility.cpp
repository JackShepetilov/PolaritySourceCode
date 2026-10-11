// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterStateTreeUtility.h"
#include "AI/BodySamples.h"
#include "StateTreeExecutionContext.h"
#include "ShooterNPC.h"
#include "Camera/CameraComponent.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "ShooterAIController.h"
#include "AI/SmokeVisionSubsystem.h"
#include "AI/FactionContactMemory.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "Variant_Shooter/AI/SquadSpawn/Squad.h"
#include "StateTreeAsyncExecutionContext.h"
#include "Variant_Shooter/Siege/SiegeCampGuard.h"
#include "Variant_Shooter/Siege/SiegeCampSite.h"
#include "Variant_Shooter/Siege/SiegeLaneFollower.h"

namespace SenseLeash
{
	// The sense task keeps its own copy of the target and an investigate spot straight from perception.
	// A leashed NPC (a lane creep, a camp guard) must not take either from past its leash: the
	// controller already refuses such a target, and without this the tree would still walk to it.

	bool Allows(const AActor* Pawn, const AActor* Target)
	{
		if (!Pawn || !Target)
		{
			return true;
		}
		const USiegeLaneFollower* const LaneFollower = Pawn->FindComponentByClass<USiegeLaneFollower>();
		if (LaneFollower && !LaneFollower->IsWithinLeash(Target))
		{
			return false;
		}
		const USiegeCampGuard* const CampGuard = Pawn->FindComponentByClass<USiegeCampGuard>();
		return !CampGuard || CampGuard->IsWithinLeash(Target);
	}

	/** Camp guards only: an investigate spot must lie inside the camp's leash, and none while walking home. */
	bool AllowsLocation(const AActor* Pawn, const FVector& Where)
	{
		const USiegeCampGuard* const CampGuard = Pawn ? Pawn->FindComponentByClass<USiegeCampGuard>() : nullptr;
		if (!CampGuard)
		{
			return true;
		}
		if (CampGuard->IsReturning())
		{
			return false;
		}
		const ASiegeCampSite* const Camp = CampGuard->GetCamp();
		return !Camp || FVector::DistSquared2D(Where, Camp->GetActorLocation()) <= FMath::Square(CampGuard->LeashRadius);
	}
}

namespace SenseSight
{
	/** Can Observer's eyes see Target's body: the waist, or failing that the head.
	 *
	 *  The sense task used to ask this with one ray from the observer's WAIST to the target's waist,
	 *  so a wall 1.2 m high between them hid everything: an enemy standing at its own low cover never
	 *  acquired anybody, and a player behind one could not be acquired, both with heads and chests in
	 *  plain view [author, 2026-10-10]. Eyes to body is what APolarityCharacter::CanBeSeenFrom answers
	 *  for the sight sense, and the two must agree or the sense reports somebody this task then
	 *  refuses. OutHit is the last blocking hit, for the log. */
	bool SeesBody(const APawn* Observer, const AActor* Target, FHitResult& OutHit)
	{
		if (!Observer || !Target || !Observer->GetWorld())
		{
			return false;
		}

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Observer);
		QueryParams.AddIgnoredActor(Target);

		const FVector Eyes = Observer->GetPawnViewLocation();
		BodySamples::FPoints Points;
		BodySamples::Get(Target, Points);

		for (const FVector& Point : Points)
		{
			if (!Observer->GetWorld()->LineTraceSingleByChannel(OutHit, Eyes, Point, ECC_Visibility, QueryParams))
			{
				return true;
			}
		}
		return false;
	}
}

bool FStateTreeLineOfSightToTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// ensure the target is valid
	if (!IsValid(InstanceData.Target))
	{
		return !InstanceData.bMustHaveLineOfSight;
	}
	
	// check if the character is facing towards the target
	const FVector TargetDir = (InstanceData.Target->GetActorLocation() - InstanceData.Character->GetActorLocation()).GetSafeNormal();

	const float FacingDot = FVector::DotProduct(TargetDir, InstanceData.Character->GetActorForwardVector());
	const float MaxDot = FMath::Cos(FMath::DegreesToRadians(InstanceData.LineOfSightConeAngle));

	// is the facing outside of our cone half angle?
	if (FacingDot <= MaxDot)
	{
		return !InstanceData.bMustHaveLineOfSight;
	}

	// Smoke, which has no collision and therefore cannot be found by the traces below. Checked once
	// for the whole fan rather than per trace: the cloud is metres across and the fan spans one
	// body. @see USmokeVisionSubsystem
	if (USmokeVisionSubsystem::IsSightBlockedInWorld(InstanceData.Character->GetWorld(),
		InstanceData.Character->GetPawnViewLocation(), InstanceData.Target->GetActorLocation()))
	{
		return !InstanceData.bMustHaveLineOfSight;
	}

	// get the target's bounding box
	FVector CenterOfMass, Extent;
	InstanceData.Target->GetActorBounds(true, CenterOfMass, Extent, false);

	// divide the vertical extent by the number of line of sight checks we'll do
	const float ExtentZOffset = Extent.Z * 2.0f / InstanceData.NumberOfVerticalLineOfSightChecks;

	// get the character's camera location as the source for the line checks
	const FVector Start = InstanceData.Character->GetFirstPersonCameraComponent()->GetComponentLocation();

	// ignore the character and target. We want to ensure there's an unobstructed trace not counting them
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(InstanceData.Character);
	QueryParams.AddIgnoredActor(InstanceData.Target);

	FHitResult OutHit;

	// run a number of vertically offset line traces to the target location
	for (int32 i = 0; i < InstanceData.NumberOfVerticalLineOfSightChecks - 1; ++i)
	{
		// calculate the endpoint for the trace
		const FVector End = CenterOfMass + FVector(0.0f, 0.0f, Extent.Z - ExtentZOffset * i);

		InstanceData.Character->GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

		// is the trace unobstructed?
		if (!OutHit.bBlockingHit)
		{
			// we only need one unobstructed trace, so terminate early
			return InstanceData.bMustHaveLineOfSight;
		}
	}

	// no line of sight found
	return !InstanceData.bMustHaveLineOfSight;
}

#if WITH_EDITOR
FText FStateTreeLineOfSightToTargetCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Has Line of Sight</b>");
}
#endif

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeFaceActorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned from another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// set the AI Controller's focus
		InstanceData.Controller->SetFocus(InstanceData.ActorToFaceTowards);
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeFaceActorTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned to another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// clear the AI Controller's focus
		InstanceData.Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

#if WITH_EDITOR
FText FStateTreeFaceActorTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Face Towards Actor</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeFaceLocationTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned from another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// set the AI Controller's focus
		InstanceData.Controller->SetFocalPoint(InstanceData.FaceLocation);
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeFaceLocationTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned to another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// clear the AI Controller's focus
		InstanceData.Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

#if WITH_EDITOR
FText FStateTreeFaceLocationTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Face Towards Location</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeSetRandomFloatTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned to another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// calculate the output value
		InstanceData.OutValue = FMath::RandRange(InstanceData.MinValue, InstanceData.MaxValue);
	}

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FStateTreeSetRandomFloatTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Set Random Float</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeShootAtTargetTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned from another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// tell the character to shoot the target
		InstanceData.Character->StartShooting(InstanceData.Target);
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeShootAtTargetTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned to another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// tell the character to stop shooting
		InstanceData.Character->StopShooting();
	}
}

#if WITH_EDITOR
FText FStateTreeShootAtTargetTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Shoot at Target</b>");
}
#endif // WITH_EDITOR

EStateTreeRunStatus FStateTreeSenseEnemiesTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned from another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// Capture Controller and Character by value (safe - they're pointers/simple types)
		// Capture WeakContext to access InstanceData only when context is valid
		AShooterAIController* Controller = InstanceData.Controller;
		AShooterNPC* Character = InstanceData.Character;

		// bind the perception updated delegate on the controller
		InstanceData.Controller->OnShooterPerceptionUpdated.BindLambda(
			[WeakContext = Context.MakeWeakExecutionContext(), Controller, Character](AActor* SensedActor, const FAIStimulus& Stimulus)
			{
					// Verify captured objects are still valid
				if (!IsValid(Controller) || !IsValid(Character))
				{
					return;
				}

				// Try to get InstanceData through WeakContext (may fail if StateTree already transitioned)
				const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
				FInstanceDataType* InstanceData = StrongContext.IsValid() ? StrongContext.GetInstanceDataPtr<FInstanceDataType>() : nullptr;

				if (Controller->IsHostileTo(SensedActor) && SenseLeash::Allows(Character, SensedActor))
				{
					// Run a line trace between the character and the sensed actor
					FHitResult OutHit;
					const bool bHit = !SenseSight::SeesBody(Character, SensedActor, OutHit);
					bool bDirectLOS = !bHit;

					if (bDirectLOS)
					{
						// Always update the controller's target (works even if StateTree transitioned)
						Controller->SetCurrentTarget(SensedActor);

						// Update InstanceData only if still in this state
						if (InstanceData)
						{
							InstanceData->TargetActor = SensedActor;
							InstanceData->bHasTarget = true;
							InstanceData->bHasInvestigateLocation = false;
						}
						else
						{
							// InstanceData is NULL - StateTree already transitioned, controller target was still set above
						}

					// no direct line of sight to target
					} else {

						// Only handle partial senses if we have access to InstanceData
						if (InstanceData)
						{
							// if we already have a target, ignore the partial sense and keep on them
							if (!IsValid(InstanceData->TargetActor))
							{
								// is this stimulus stronger than the last one we had?
								if (Stimulus.Strength > InstanceData->LastStimulusStrength)
								{
									// update the stimulus strength
									InstanceData->LastStimulusStrength = Stimulus.Strength;

									// set the investigate location
									InstanceData->InvestigateLocation = Stimulus.StimulusLocation;

									// set the investigate flag
									InstanceData->bHasInvestigateLocation = true;
								}
							}
						}
					}
				}
			}
		);

		// bind the perception forgotten delegate on the controller
		InstanceData.Controller->OnShooterPerceptionForgotten.BindLambda(
			[WeakContext = Context.MakeWeakExecutionContext(), Controller](AActor* SensedActor)
			{
				if (!IsValid(Controller))
				{
					return;
				}

				// Try to get InstanceData through WeakContext (may fail if StateTree already transitioned)
				const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
				FInstanceDataType* InstanceData = StrongContext.IsValid() ? StrongContext.GetInstanceDataPtr<FInstanceDataType>() : nullptr;

				// Perception "forgetting" an actor only means the sensory stimulus expired.
				// It does NOT mean the target is gone — the NPC should keep pursuing.
				// Clearing the controller target here would undo targets set by ArenaManager
				// or other external systems, causing NPCs to stand in place.
				//
				// Instead: only reset secondary perception state (investigate location, stimulus strength).
				// The target persists until the actor is destroyed/invalid (detected in Tick via IsValid)
				// or until a higher-level system explicitly clears it.

				if (InstanceData)
				{
					if (SensedActor == InstanceData->TargetActor || !IsValid(InstanceData->TargetActor))
					{
						InstanceData->bHasInvestigateLocation = false;
						InstanceData->LastStimulusStrength = 0.0f;
					}
				}

			}
		);

		// ВАЖНО: Проверить уже известных актёров из AIPerception
		// Это нужно потому что PerceptionUpdated может вызваться ДО того как мы забиндили делегаты
		// В таком случае NPC не получит событие и не установит цель
		// Force perception system to update immediately
		InstanceData.Controller->ForcePerceptionUpdate();

		if (UAIPerceptionComponent* PerceptionComp = InstanceData.Controller->GetPerceptionComponent())
		{
			TArray<AActor*> KnownActors;
			PerceptionComp->GetKnownPerceivedActors(nullptr, KnownActors);

			for (AActor* KnownActor : KnownActors)
			{
				if (!KnownActor)
				{
					continue;
				}

				// Проверяем сторону, а не тег
				if (!InstanceData.Controller->IsHostileTo(KnownActor) || !SenseLeash::Allows(InstanceData.Character, KnownActor))
				{
					continue;
				}

				// Проверяем Line of Sight
				FHitResult OutHit;
					const bool bHit = !SenseSight::SeesBody(InstanceData.Character, KnownActor, OutHit);

				bool bDirectLOS = !bHit;

				if (bDirectLOS)
				{
					// Устанавливаем цель
					InstanceData.Controller->SetCurrentTarget(KnownActor);
					InstanceData.TargetActor = KnownActor;
					InstanceData.bHasTarget = true;
					InstanceData.bHasInvestigateLocation = false;

					// Нашли валидную цель - можно выходить
					break;
				}
			}
		}
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeSenseEnemiesTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	// Get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Sync InstanceData with Controller's current target
	// This handles the case where delegates update Controller but can't update InstanceData (due to WeakContext invalidation)
	AActor* ControllerTarget = InstanceData.Controller->GetCurrentTarget();

	const bool bWasHasTarget = InstanceData.bHasTarget;
	AActor* PrevInstanceTarget = InstanceData.TargetActor;

	if (ControllerTarget != InstanceData.TargetActor)
	{
		InstanceData.TargetActor = ControllerTarget;
		InstanceData.bHasTarget = IsValid(ControllerTarget);

		UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s SYNC: prev=%s -> new=%s (bHasTarget=%d)"),
			*GetNameSafe(InstanceData.Character),
			*GetNameSafe(PrevInstanceTarget),
			*GetNameSafe(ControllerTarget),
			InstanceData.bHasTarget ? 1 : 0);
	}

	// Safety: clear target if the actor was destroyed (GC may not have cleared the pointer yet)
	if (InstanceData.bHasTarget && !IsValid(InstanceData.TargetActor))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s SAFETY-CLEAR: TargetActor invalid, calling ClearCurrentTarget"),
			*GetNameSafe(InstanceData.Character));

		InstanceData.TargetActor = nullptr;
		InstanceData.bHasTarget = false;
		InstanceData.bHasInvestigateLocation = false;
		InstanceData.LastStimulusStrength = 0.0f;
		InstanceData.Controller->ClearCurrentTarget();
	}

	// A camp guard does not go looking past its leash, nor while it walks home.
	if (InstanceData.bHasInvestigateLocation && !SenseLeash::AllowsLocation(InstanceData.Character, InstanceData.InvestigateLocation))
	{
		InstanceData.bHasInvestigateLocation = false;
		InstanceData.LastStimulusStrength = 0.0f;
	}

	// Periodic perception polling - backup mechanism in case delegates don't fire reliably
	InstanceData.TimeSinceLastPoll += DeltaTime;
	if (InstanceData.TimeSinceLastPoll >= InstanceData.PerceptionPollInterval)
	{
		InstanceData.TimeSinceLastPoll = 0.0f;

		UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s HEARTBEAT: bHasTarget=%d ControllerTarget=%s InstanceTarget=%s"),
			*GetNameSafe(InstanceData.Character),
			InstanceData.bHasTarget ? 1 : 0,
			*GetNameSafe(InstanceData.Controller->GetCurrentTarget()),
			*GetNameSafe(InstanceData.TargetActor));

		// Only poll if we don't have a target yet
		if (!InstanceData.bHasTarget)
		{
			if (UAIPerceptionComponent* PerceptionComp = InstanceData.Controller->GetPerceptionComponent())
			{
				TArray<AActor*> KnownActors;
				PerceptionComp->GetKnownPerceivedActors(nullptr, KnownActors);

				UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s POLL: KnownActors.Num=%d (looking for team %d hostiles)"),
					*GetNameSafe(InstanceData.Character),
					KnownActors.Num(),
					(int32)InstanceData.Controller->GetGenericTeamId().GetId());

				for (AActor* KnownActor : KnownActors)
				{
					if (!KnownActor || !InstanceData.Controller->IsHostileTo(KnownActor)
						|| !SenseLeash::Allows(InstanceData.Character, KnownActor))
					{
						continue;
					}

					// Check Line of Sight
					FHitResult OutHit;
					const bool bHit = !SenseSight::SeesBody(InstanceData.Character, KnownActor, OutHit);

					UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s POLL-LOS: candidate=%s LOSBlocked=%d (blocker=%s)"),
						*GetNameSafe(InstanceData.Character),
						*GetNameSafe(KnownActor),
						bHit ? 1 : 0,
						bHit ? *GetNameSafe(OutHit.GetActor()) : TEXT("none"));

					if (!bHit) // Direct LOS
					{
						InstanceData.Controller->SetCurrentTarget(KnownActor);
						InstanceData.TargetActor = KnownActor;
						InstanceData.bHasTarget = true;
						InstanceData.bHasInvestigateLocation = false;
						UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s POLL-ACQUIRED: %s"),
							*GetNameSafe(InstanceData.Character), *GetNameSafe(KnownActor));
						break;
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Verbose, TEXT("[SENSE_DEBUG] %s POLL: PerceptionComponent NULL!"),
					*GetNameSafe(InstanceData.Character));
			}

			// Nobody in view and nothing to look into: go where the side last saw an enemy. An advancing
			// squad only; one that holds stays where it was put. This is what an enemy with no target
			// does instead of standing where it spawned [author, 2026-10-11].
			InstanceData.TimeWithoutTarget = InstanceData.bHasTarget
				? 0.0f : InstanceData.TimeWithoutTarget + InstanceData.PerceptionPollInterval;

			// A squad on the defensive does not go looking: it was outnumbered when it last had somebody
			// to fight, and losing sight of them has not changed that.
			const AAICombatCoordinator* const PostureCoordinator = AAICombatCoordinator::GetCoordinator(InstanceData.Character);
			const bool bDefending = PostureCoordinator
				&& PostureCoordinator->GetPostureFor(InstanceData.Character) == ESquadPosture::Defense;

			// Anybody with no target, whatever its squad is out to do.
			if (!InstanceData.bHasTarget)
			{
				FFactionContact RadioContact;
				const UFactionContactMemory* const RadioMemory = UFactionContactMemory::Get(InstanceData.Character);
				if (RadioMemory && RadioMemory->FindNearestContact(InstanceData.Controller->GetGenericTeamId().GetId(),
					InstanceData.Character->GetActorLocation(), RadioContact))
				{
					// Told on the radio where he is, and from here there is a clear line to him: that is
					// seeing him. The sight sense alone would not: its radius is 35 m (the controller's
					// blueprint), and an outpost is fought at 30 to 60. Recorded 2026-10-11: the soldier
					// on the roof, 56 m from a player standing in the open, with nothing between them,
					// had no target for the whole of three fights.
					AActor* const Known = RadioContact.Enemy.Get();
					FHitResult RadioHit;
					if (Known && RadioContact.bCurrentlySeen && InstanceData.Controller->IsHostileTo(Known)
						&& SenseLeash::Allows(InstanceData.Character, Known)
						&& SenseSight::SeesBody(InstanceData.Character, Known, RadioHit))
					{
						InstanceData.Controller->SetCurrentTarget(Known);
						InstanceData.TargetActor = Known;
						InstanceData.bHasTarget = true;
						InstanceData.bHasInvestigateLocation = false;
						InstanceData.Character->SayTactic(TEXT("ON THE RADIO: in view from here"));
						return EStateTreeRunStatus::Running;
					}
				}
			}

			if (!InstanceData.bHasTarget && !bDefending && InstanceData.Character->GetCombatTask() == ESquadTask::Advance)
			{
				// Where to look, refreshed on every poll rather than set once: the place the side saw an
				// enemy last while that is fresh, and after HuntAfterSeconds of nothing, the nearest enemy
				// alive. The second is not a sense, it is the squad knowing a fight is still on; the NPC
				// still has to SEE somebody to get a target, this only walks it to where it can.
				FVector Where = FVector::ZeroVector;
				bool bHasWhere = false;
				bool bHunting = false;

				FFactionContact Contact;
				const UFactionContactMemory* const Memory = UFactionContactMemory::Get(InstanceData.Character);
				if (Memory && Memory->FindNearestContact(InstanceData.Controller->GetGenericTeamId().GetId(),
					InstanceData.Character->GetActorLocation(), Contact))
				{
					Where = Contact.LastKnownLocation;
					bHasWhere = true;
				}

				const bool bAtWhere = bHasWhere
					&& FVector::Dist2D(Where, InstanceData.Character->GetActorLocation()) < 300.0f;
				if ((!bHasWhere || bAtWhere) && InstanceData.TimeWithoutTarget >= InstanceData.HuntAfterSeconds)
				{
					const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(InstanceData.Character);
					if (const APawn* const Nearest = Coordinator ? Coordinator->FindNearestHostile(InstanceData.Character) : nullptr)
					{
						Where = Nearest->GetActorLocation();
						bHasWhere = true;
						bHunting = true;
					}
				}

				if (bHasWhere && SenseLeash::AllowsLocation(InstanceData.Character, Where)
					&& (!InstanceData.bHasInvestigateLocation || FVector::Dist2D(Where, InstanceData.InvestigateLocation) > 200.0f))
				{
					InstanceData.InvestigateLocation = Where;
					InstanceData.bHasInvestigateLocation = true;
					InstanceData.Character->SayTactic(bHunting
						? TEXT("NOBODY IN SIGHT: going to the nearest enemy")
						: TEXT("NO TARGET, going to the last contact"));
				}

				// And walk there from here, not only through the tree. Its Investigate branch moves once
				// and then sits in its Wait state whatever the location becomes: recorded 2026-10-11, a
				// soldier said "going to the last contact" and stood for 191 s of a fight going on 20 m
				// away. Only while nothing else is moving the pawn, and only when there is somewhere to
				// go: an order to the spot it stands on makes it spin.
				if (bHasWhere && SenseLeash::AllowsLocation(InstanceData.Character, Where)
					&& FVector::Dist2D(Where, InstanceData.Character->GetActorLocation()) > 300.0f
					&& InstanceData.Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
				{
					InstanceData.Controller->MoveToLocation(Where, 150.0f);
				}
			}

			// One told to hold that sees nobody while its side knows where the enemy is. It does not
			// go to him; it goes to the edge of its own ground on his side, and along that edge a
			// few metres at a time, until it has a line. Recorded 2026-10-11: the fifth man of an
			// outpost stood behind a block in the middle of the yard for 23 to 37 s of each fight and
			// never fired, so the five were four.
			if (!InstanceData.bHasTarget && InstanceData.Character->GetCombatTask() == ESquadTask::Hold
				&& InstanceData.Character->HasHoldArea() && InstanceData.TimeWithoutTarget >= 2.0f
				&& InstanceData.Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
			{
				FFactionContact Contact;
				const UFactionContactMemory* const Memory = UFactionContactMemory::Get(InstanceData.Character);
				if (Memory && Memory->FindNearestContact(InstanceData.Controller->GetGenericTeamId().GetId(),
					InstanceData.Character->GetActorLocation(), Contact))
				{
					// Eight seconds of that and still nobody: its own ground has no line to the fight at
					// all, and the whole place becomes its ground. Recorded 2026-10-11: with the attack
					// from the south the three at the house said this once, went to the edge of their
					// yard and stood there for 49 s each, a 7 man garrison fighting as 4.
					// @see AShooterNPC::SetWideHoldArea
					if (InstanceData.TimeWithoutTarget >= 8.0f && InstanceData.Character->CanWidenHoldArea())
					{
						InstanceData.Character->WidenHoldArea();
						InstanceData.Character->SayTactic(TEXT("HOLD: my ground sees nobody, taking the whole place"));
					}

					// Left of the line, then right of it, four seconds each.
					const float Side = FMath::Fmod(InstanceData.TimeWithoutTarget, 8.0f) < 4.0f ? 350.0f : -350.0f;
					const FVector Where = InstanceData.Character->GetHoldPointToward(Contact.LastKnownLocation, Side);
					if (FVector::Dist2D(Where, InstanceData.Character->GetActorLocation()) > 200.0f)
					{
						InstanceData.Controller->MoveToLocation(Where, 80.0f);
						InstanceData.Character->SayTactic(TEXT("HOLD: no line from here, moving to the side the enemy is on"));
					}
				}
			}
		}
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeSenseEnemiesTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// have we transitioned to another state?
	if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
	{
		// get the instance data
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		// unbind the perception delegates
		InstanceData.Controller->OnShooterPerceptionUpdated.Unbind();
		InstanceData.Controller->OnShooterPerceptionForgotten.Unbind();
	}
}

#if WITH_EDITOR
FText FStateTreeSenseEnemiesTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Sense Enemies</b>");
}
#endif // WITH_EDITOR