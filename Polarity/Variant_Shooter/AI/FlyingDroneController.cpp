// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlyingDroneController.h"
#include "FlyingDrone.h"
#include "FlyingAIMovementComponent.h"

AFlyingDroneController::AFlyingDroneController()
{
	// Flying drones don't use standard pathfinding
	bWantsPlayerState = false;
}

void AFlyingDroneController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Cache drone reference
	ControlledDrone = Cast<AFlyingDrone>(InPawn);

	if (ControlledDrone)
	{
		// Cache flying movement component
		FlyingMovement = ControlledDrone->GetFlyingMovement();

		// Bind to movement events
		if (FlyingMovement)
		{
			FlyingMovement->OnMovementCompleted.AddDynamic(this, &AFlyingDroneController::OnDroneMovementCompleted);
			FlyingMovement->OnDashCompleted.AddDynamic(this, &AFlyingDroneController::OnDroneDashCompleted);
		}
	}
}

void AFlyingDroneController::OnUnPossess()
{
	// Unbind from movement events
	if (FlyingMovement)
	{
		FlyingMovement->OnMovementCompleted.RemoveDynamic(this, &AFlyingDroneController::OnDroneMovementCompleted);
		FlyingMovement->OnDashCompleted.RemoveDynamic(this, &AFlyingDroneController::OnDroneDashCompleted);
	}

	ControlledDrone = nullptr;
	FlyingMovement = nullptr;

	Super::OnUnPossess();
}

// ==================== Navigation Commands ====================

void AFlyingDroneController::FlyToLocation(const FVector& Location, float AcceptanceRadius)
{
	if (FlyingMovement)
	{
		FlyingMovement->FlyToLocation(Location, AcceptanceRadius);
	}
}

void AFlyingDroneController::FlyToActor(AActor* Target, float AcceptanceRadius)
{
	if (FlyingMovement && Target)
	{
		FlyingMovement->FlyToActor(Target, AcceptanceRadius);
	}
}

void AFlyingDroneController::FlyToRandomPatrolPoint()
{
	if (FlyingMovement)
	{
		FVector PatrolPoint;
		if (FlyingMovement->GetRandomPatrolPoint(PatrolPoint))
		{
			FlyingMovement->FlyToLocation(PatrolPoint);
		}
	}
}

void AFlyingDroneController::StopFlying()
{
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}
}

// ==================== Combat Commands ====================

bool AFlyingDroneController::PerformEvasion(const FVector& ThreatLocation)
{
	if (FlyingMovement)
	{
		return FlyingMovement->StartEvasiveDash(ThreatLocation);
	}
	return false;
}

bool AFlyingDroneController::EvadeFromTarget()
{
	if (!FlyingMovement)
	{
		return false;
	}

	// Get current target from base class
	AActor* CurrentTarget = GetCurrentTarget();
	if (CurrentTarget)
	{
		return FlyingMovement->StartEvasiveDash(CurrentTarget->GetActorLocation());
	}

	return false;
}

// ==================== State Queries ====================

bool AFlyingDroneController::IsFlying() const
{
	return FlyingMovement && FlyingMovement->IsMoving();
}

bool AFlyingDroneController::IsDashing() const
{
	return FlyingMovement && FlyingMovement->IsDashing();
}

bool AFlyingDroneController::IsDashOnCooldown() const
{
	return FlyingMovement && FlyingMovement->IsDashOnCooldown();
}

// ==================== Movement Callbacks ====================

void AFlyingDroneController::OnDroneMovementCompleted(bool bSuccess)
{
	// Can be extended for StateTree integration
	// For example, setting blackboard values or triggering events
}

void AFlyingDroneController::OnDroneDashCompleted()
{
	// Can be extended for StateTree integration
}
