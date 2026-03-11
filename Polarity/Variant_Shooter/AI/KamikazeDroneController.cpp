// KamikazeDroneController.cpp

#include "KamikazeDroneController.h"
#include "KamikazeDroneNPC.h"
#include "FlyingAIMovementComponent.h"

AKamikazeDroneController::AKamikazeDroneController()
{
	// Kamikaze drones don't use standard pathfinding
	bWantsPlayerState = false;
}

void AKamikazeDroneController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ControlledKamikaze = Cast<AKamikazeDroneNPC>(InPawn);

	if (ControlledKamikaze)
	{
		FlyingMovement = ControlledKamikaze->GetFlyingMovement();
	}
}

void AKamikazeDroneController::OnUnPossess()
{
	ControlledKamikaze = nullptr;
	FlyingMovement = nullptr;

	Super::OnUnPossess();
}
