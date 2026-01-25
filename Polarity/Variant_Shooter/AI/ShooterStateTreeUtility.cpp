// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterStateTreeUtility.h"
#include "StateTreeExecutionContext.h"
#include "ShooterNPC.h"
#include "Camera/CameraComponent.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "ShooterAIController.h"
#include "StateTreeAsyncExecutionContext.h"

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

		UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: EnterState - binding delegates"));

		// Capture necessary data by value to avoid WeakContext invalidation issues
		AShooterAIController* Controller = InstanceData.Controller;
		AShooterNPC* Character = InstanceData.Character;
		FName SenseTag = InstanceData.SenseTag;
		AActor** TargetActorPtr = &InstanceData.TargetActor;
		bool* bHasTargetPtr = &InstanceData.bHasTarget;
		bool* bHasInvestigateLocationPtr = &InstanceData.bHasInvestigateLocation;
		FVector* InvestigateLocationPtr = &InstanceData.InvestigateLocation;
		float* LastStimulusStrengthPtr = &InstanceData.LastStimulusStrength;

		// bind the perception updated delegate on the controller
		InstanceData.Controller->OnShooterPerceptionUpdated.BindLambda(
			[Controller, Character, SenseTag, TargetActorPtr, bHasTargetPtr, bHasInvestigateLocationPtr,
			 InvestigateLocationPtr, LastStimulusStrengthPtr](AActor* SensedActor, const FAIStimulus& Stimulus)
			{
				UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: PerceptionUpdated called for %s"), *SensedActor->GetName());

				// Verify captured pointers are still valid
				if (!IsValid(Controller) || !IsValid(Character))
				{
					UE_LOG(LogTemp, Error, TEXT("SenseEnemies: Controller or Character invalid in lambda"));
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Checking tag '%s' on %s - HasTag: %s"),
					*SenseTag.ToString(),
					*SensedActor->GetName(),
					SensedActor->ActorHasTag(SenseTag) ? TEXT("YES") : TEXT("NO"));

				if (SensedActor->ActorHasTag(SenseTag))
					{
						bool bDirectLOS = false;

						// Run a line trace between the character and the sensed actor
						// (removed cone check - AIPerception already handles that)
						FCollisionQueryParams QueryParams;
						QueryParams.AddIgnoredActor(Character);
						QueryParams.AddIgnoredActor(SensedActor);

						FHitResult OutHit;

						bool bHit = Character->GetWorld()->LineTraceSingleByChannel(
							OutHit,
							Character->GetActorLocation(),
							SensedActor->GetActorLocation(),
							ECC_Visibility,
							QueryParams
						);
						bDirectLOS = !bHit;

						UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: LineTrace to %s - hit=%s, blocker=%s, DirectLOS=%s"),
							*SensedActor->GetName(),
							bHit ? TEXT("YES") : TEXT("NO"),
							bHit && OutHit.GetActor() ? *OutHit.GetActor()->GetName() : TEXT("none"),
							bDirectLOS ? TEXT("YES") : TEXT("NO"));

						// check if we have a direct line of sight to the stimulus
						UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: DirectLOS to %s = %s"), *SensedActor->GetName(), bDirectLOS ? TEXT("YES") : TEXT("NO"));

						if (bDirectLOS)
						{
							UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Setting target to %s"), *SensedActor->GetName());

							// set the controller's target
							Controller->SetCurrentTarget(SensedActor);

							// set the task output
							(*TargetActorPtr) = SensedActor;

							// set the flags
							(*bHasTargetPtr) = true;
							(*bHasInvestigateLocationPtr) = false;

						// no direct line of sight to target
						} else {

							// if we already have a target, ignore the partial sense and keep on them
							if (!IsValid((*TargetActorPtr)))
							{
								// is this stimulus stronger than the last one we had?
								if (Stimulus.Strength > (*LastStimulusStrengthPtr))
								{
									// update the stimulus strength
									(*LastStimulusStrengthPtr) = Stimulus.Strength;

									// set the investigate location
									(*InvestigateLocationPtr) = Stimulus.StimulusLocation;

									// set the investigate flag
									(*bHasInvestigateLocationPtr) = true;
								}
							}
						}
					}
				}
			}
		);

		// bind the perception forgotten delegate on the controller
		InstanceData.Controller->OnShooterPerceptionForgotten.BindLambda(
			[Controller, TargetActorPtr, bHasTargetPtr, bHasInvestigateLocationPtr, LastStimulusStrengthPtr](AActor* SensedActor)
			{
				if (!IsValid(Controller))
				{
					return;
				}

				bool bForget = false;

				// are we forgetting the current target?
				if (SensedActor == (*TargetActorPtr))
				{
					bForget = true;

				} else {

					// are we forgetting about a partial sense?
					if (!IsValid((*TargetActorPtr)))
					{
						bForget = true;
					}
				}

				if (bForget)
				{
					// clear the target
					(*TargetActorPtr) = nullptr;

					// clear the flags
					(*bHasInvestigateLocationPtr) = false;
					(*bHasTargetPtr) = false;

					// reset the stimulus strength
					(*LastStimulusStrengthPtr) = 0.0f;

					// clear the target on the controller
					Controller->ClearCurrentTarget();
					Controller->ClearFocus(EAIFocusPriority::Gameplay);
				}

			}
		);

		// ВАЖНО: Проверить уже известных актёров из AIPerception
		// Это нужно потому что PerceptionUpdated может вызваться ДО того как мы забиндили делегаты
		// В таком случае NPC не получит событие и не установит цель
		UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Checking already known actors"));

		if (UAIPerceptionComponent* PerceptionComp = InstanceData.Controller->GetPerceptionComponent())
		{
			TArray<AActor*> KnownActors;
			PerceptionComp->GetKnownPerceivedActors(nullptr, KnownActors);

			UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Found %d known actors"), KnownActors.Num());

			for (AActor* KnownActor : KnownActors)
			{
				if (!KnownActor)
				{
					continue;
				}

				UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Processing known actor %s"), *KnownActor->GetName());

				// Проверяем тег
				if (!KnownActor->ActorHasTag(InstanceData.SenseTag))
				{
					UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Known actor %s doesn't have tag '%s'"),
						*KnownActor->GetName(), *InstanceData.SenseTag.ToString());
					continue;
				}

				// Проверяем Line of Sight
				FCollisionQueryParams QueryParams;
				QueryParams.AddIgnoredActor(InstanceData.Character);
				QueryParams.AddIgnoredActor(KnownActor);

				FHitResult OutHit;
				bool bHit = InstanceData.Character->GetWorld()->LineTraceSingleByChannel(
					OutHit,
					InstanceData.Character->GetActorLocation(),
					KnownActor->GetActorLocation(),
					ECC_Visibility,
					QueryParams
				);

				bool bDirectLOS = !bHit;

				UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Known actor %s - DirectLOS=%s"),
					*KnownActor->GetName(), bDirectLOS ? TEXT("YES") : TEXT("NO"));

				if (bDirectLOS)
				{
					UE_LOG(LogTemp, Warning, TEXT("SenseEnemies: Setting target to known actor %s"), *KnownActor->GetName());

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