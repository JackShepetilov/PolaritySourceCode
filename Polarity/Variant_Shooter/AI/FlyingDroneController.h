// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterAIController.h"
#include "FlyingDroneController.generated.h"

class AFlyingDrone;
class UFlyingAIMovementComponent;

/**
 * AI Controller for Flying Drone enemies.
 * Extends ShooterAIController with flying-specific navigation commands.
 * Works with StateTree for behavior logic.
 */
UCLASS()
class POLARITY_API AFlyingDroneController : public AShooterAIController
{
	GENERATED_BODY()

public:

	AFlyingDroneController();

protected:

	// ==================== Cached References ====================

	/** Cached pointer to the controlled drone */
	UPROPERTY()
	TObjectPtr<AFlyingDrone> ControlledDrone;

	/** Cached pointer to flying movement component */
	UPROPERTY()
	TObjectPtr<UFlyingAIMovementComponent> FlyingMovement;

protected:

	// ==================== Controller Lifecycle ====================

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:

	// ==================== Navigation Commands ====================

	/**
	 * Command the drone to fly to a location
	 * @param Location - Target location in world space
	 * @param AcceptanceRadius - How close to get before considering arrived (-1 for default)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying AI|Navigation")
	void FlyToLocation(const FVector& Location, float AcceptanceRadius = -1.0f);

	/**
	 * Command the drone to fly to an actor
	 * @param Target - Actor to fly towards
	 * @param AcceptanceRadius - How close to get before considering arrived (-1 for default)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying AI|Navigation")
	void FlyToActor(AActor* Target, float AcceptanceRadius = -1.0f);

	/**
	 * Command drone to fly to a random patrol point
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying AI|Navigation")
	void FlyToRandomPatrolPoint();

	/**
	 * Stop current movement
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying AI|Navigation")
	void StopFlying();

	// ==================== Combat Commands ====================

	/**
	 * Perform evasive dash maneuver away from threat
	 * @param ThreatLocation - Location to evade from
	 * @return True if evasion started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying AI|Combat")
	bool PerformEvasion(const FVector& ThreatLocation);

	/**
	 * Perform evasive dash away from current target
	 * @return True if evasion started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying AI|Combat")
	bool EvadeFromTarget();

	// ==================== State Queries ====================

	/** Returns true if drone is currently flying to a destination */
	UFUNCTION(BlueprintPure, Category = "Flying AI|State")
	bool IsFlying() const;

	/** Returns true if drone is currently dashing */
	UFUNCTION(BlueprintPure, Category = "Flying AI|State")
	bool IsDashing() const;

	/** Returns true if dash is on cooldown */
	UFUNCTION(BlueprintPure, Category = "Flying AI|State")
	bool IsDashOnCooldown() const;

	/** Get the controlled drone */
	UFUNCTION(BlueprintPure, Category = "Flying AI")
	AFlyingDrone* GetControlledDrone() const { return ControlledDrone; }

	/** Get the flying movement component */
	UFUNCTION(BlueprintPure, Category = "Flying AI")
	UFlyingAIMovementComponent* GetFlyingMovement() const { return FlyingMovement; }

protected:

	// ==================== Movement Callbacks ====================

	/** Called when drone completes movement to target */
	UFUNCTION()
	void OnDroneMovementCompleted(bool bSuccess);

	/** Called when drone completes a dash */
	UFUNCTION()
	void OnDroneDashCompleted();
};
