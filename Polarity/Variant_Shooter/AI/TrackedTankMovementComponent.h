// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ApexMovementComponent.h"
#include "TrackedTankMovementComponent.generated.h"

/**
 *  Movement for a tracked hull.
 *
 *  A character walks in any direction the moment it is asked to. A tank cannot: it drives along
 *  the hull, and every change of direction costs a turn. That difference is the whole reason the
 *  tank can be flanked, which the design promised and plain character movement never delivered.
 *
 *  How it works: the hull turns toward the requested direction (bOrientRotationToMovement with a
 *  slow RotationRate), and the velocity the base class produced is projected onto the hull's
 *  facing. Sideways motion is dropped instead of being steered, so a 90 degree request stalls the
 *  hull to a stop and it turns on the spot; the closer the hull comes to facing the goal, the more
 *  of the speed survives. No extra state, and nothing to keep in sync over the network: the whole
 *  rule is a function of Acceleration, which already rides in the saved move and replays correctly
 *  on the server.
 *
 *  Reversing is the one exception to "turn toward where you are going": a hull asked to go
 *  backwards a short distance backs up instead of showing its rear to the enemy.
 */
UCLASS()
class POLARITY_API UTrackedTankMovementComponent : public UApexMovementComponent
{
	GENERATED_BODY()

public:

	UTrackedTankMovementComponent();

	/** Fraction of the forward speed available in reverse */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReverseSpeedFactor = 0.5f;

	/** Requested direction further behind the hull than this drives in reverse instead of turning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Movement", meta = (ClampMin = "90.0", ClampMax = "180.0"))
	float ReverseAngle = 135.0f;

	/** True while the hull is backing up rather than turning around */
	UFUNCTION(BlueprintPure, Category = "Tank|Movement")
	bool IsReversing() const;

	/** Where the hull has been asked to go this step, flat, normalised, zero when it has not.
	 *
	 *  NOT simply Acceleration: an AI pawn never accelerates itself. Path following hands the
	 *  movement component a requested velocity, and CalcVelocity turns that into a LOCAL
	 *  RequestedAcceleration; the Acceleration member stays zero the whole way. Reading it is how
	 *  a driving tank kept looking like a parked one. */
	FVector GetRequestedMoveDirection2D() const;

protected:

	/** Own the rotation mode from inside the simulation, see the .cpp for why */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Drop everything that is not along the hull: no strafing, and turns cost speed */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

	/** Hold the heading while reversing, otherwise turn toward the requested direction */
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;
};
