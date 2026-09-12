// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackedTankMovementComponent.h"

#include "GameFramework/Character.h"

UTrackedTankMovementComponent::UTrackedTankMovementComponent()
{
	// The hull follows where it is going, not where the controller is looking: aiming is the
	// turret's job now, and a hull that snapped to the target could never be flanked.
	bOrientRotationToMovement = true;
	bUseControllerDesiredRotation = false;

	// Slow enough to read as a tracked vehicle turning, fast enough not to look stuck
	RotationRate = FRotator(0.0f, 60.0f, 0.0f);

	MaxWalkSpeed = 250.0f;
}

void UTrackedTankMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (CharacterOwner)
	{
		// Snapping yaw to the control rotation would spin a 3.8 metre hull like a turret.
		// AShooterNPC re-asserts the ground-NPC facing mode on BeginPlay and after every stun, so
		// the tank takes its flags back here, inside the simulation, every step.
		CharacterOwner->bUseControllerRotationYaw = false;
	}

	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

FVector UTrackedTankMovementComponent::GetRequestedMoveDirection2D() const
{
	// Path following hands its move over as a requested velocity (RequestDirectMove), and
	// CalcVelocity turns that into a local RequestedAcceleration inside the engine: the
	// Acceleration member never sees it. Player-style input does the opposite. Ask both, in the
	// order that makes an AI pawn work, because that is the one that was broken.
	if (bHasRequestedVelocity)
	{
		const FVector Requested = RequestedVelocity.GetSafeNormal2D();
		if (!Requested.IsNearlyZero())
		{
			return Requested;
		}
	}

	return Acceleration.GetSafeNormal2D();
}

bool UTrackedTankMovementComponent::IsReversing() const
{
	if (!UpdatedComponent)
	{
		return false;
	}

	const FVector WantDir = GetRequestedMoveDirection2D();
	if (WantDir.IsNearlyZero())
	{
		return false;
	}

	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(ReverseAngle));

	return FVector::DotProduct(WantDir, Forward) < CosLimit;
}

void UTrackedTankMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// Decided before Super, because Super consumes the request: bHasRequestedVelocity is cleared
	// later in the same update, and the whole point is to catch the frame where the AI IS asking to
	// move. PhysicsRotation runs after CalcVelocity, so flags set here still land this frame.
	const bool bWantsToMove = !GetRequestedMoveDirection2D().IsNearlyZero();

	// Driving: the hull turns toward where it is going. Standing: it keeps facing whatever the
	// controller is looking at, which for a tank is the enemy, front armour first.
	bOrientRotationToMovement = bWantsToMove;
	bUseControllerDesiredRotation = !bWantsToMove;

	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);

	if (!UpdatedComponent || !IsMovingOnGround())
	{
		return;
	}

	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	// Whatever the base class decided, only the part along the hull survives. A request at 90
	// degrees leaves nothing, which is exactly the turn-on-the-spot we want, and it falls out of
	// the projection instead of needing a state to sit in.
	float Speed = FVector::DotProduct(Velocity, Forward);
	if (Speed < 0.0f)
	{
		Speed *= ReverseSpeedFactor;
	}

	Velocity = FVector(Forward.X * Speed, Forward.Y * Speed, Velocity.Z);
}

FRotator UTrackedTankMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const
{
	if (IsReversing())
	{
		// Backing away from something keeps the front armour and both barrels pointed at it
		DeltaRotation = FRotator::ZeroRotator;
		return CurrentRotation;
	}

	return Super::ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRotation);
}
