// KamikazeDroneController.h
// Minimal AI Controller for KamikazeDroneNPC.
// Main behavior logic lives in the drone's state machine, not here.

#pragma once

#include "CoreMinimal.h"
#include "ShooterAIController.h"
#include "KamikazeDroneController.generated.h"

class AKamikazeDroneNPC;
class UFlyingAIMovementComponent;

/**
 * AI Controller for Kamikaze Drone NPCs.
 * Minimal — the drone manages its own movement via internal state machine.
 * Inherits StateTree integration from ShooterAIController.
 */
UCLASS()
class POLARITY_API AKamikazeDroneController : public AShooterAIController
{
	GENERATED_BODY()

public:

	AKamikazeDroneController();

protected:

	/** Cached pointer to the controlled kamikaze drone */
	UPROPERTY()
	TObjectPtr<AKamikazeDroneNPC> ControlledKamikaze;

	/** Cached pointer to flying movement component */
	UPROPERTY()
	TObjectPtr<UFlyingAIMovementComponent> FlyingMovement;

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:

	/** Get the controlled kamikaze drone */
	UFUNCTION(BlueprintPure, Category = "Kamikaze AI")
	AKamikazeDroneNPC* GetControlledKamikaze() const { return ControlledKamikaze; }

	/** Get the flying movement component */
	UFUNCTION(BlueprintPure, Category = "Kamikaze AI")
	UFlyingAIMovementComponent* GetFlyingMovement() const { return FlyingMovement; }
};
