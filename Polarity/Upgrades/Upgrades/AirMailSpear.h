// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"

/**
 * Air Mail "spear" orientation helpers, shared by the bounce-return and the kick paths.
 *
 * Goal: make a thrown/kicked object fly point-first (its longest visual axis aligned with the
 * direction of travel) and spin cleanly around that axis, instead of tumbling.
 *
 * Why this is driven KINEMATICALLY and not via physics angular velocity: an asymmetric body
 * (e.g. a rifle with a stock + magazine) has its principal inertia axes ROTATED relative to its
 * bounding-box axes. Spinning a free rigid body about a NON-principal axis triggers torque-free
 * precession (the Dzhanibekov / tennis-racket effect) — visually a tumble — and it cannot be
 * stabilised by just picking a spin axis. So instead of giving the body angular velocity and
 * hoping, AirMailTickSpear overwrites the orientation every frame (teleport rotation + zeroed
 * angular velocity); a body whose orientation is forced each frame physically cannot precess.
 */

/** Index (0=X, 1=Y, 2=Z) of the mesh's longest LOCAL axis = the spear / spin axis. Read from the
 *  local bounding box (largest half-extent, scaled by the component; GetAbs() guards mirrored scale). */
inline int32 AirMailLongAxisIndex(const UStaticMeshComponent* Mesh)
{
	int32 LongAxis = 0;
	if (Mesh)
	{
		if (const UStaticMesh* SM = Mesh->GetStaticMesh())
		{
			const FVector Ext = (SM->GetBoundingBox().GetExtent() * Mesh->GetComponentScale()).GetAbs();
			float MaxExt = Ext.X;
			if (Ext.Y > MaxExt) { MaxExt = Ext.Y; LongAxis = 1; }
			if (Ext.Z > MaxExt) { MaxExt = Ext.Z; LongAxis = 2; }
		}
	}
	return LongAxis;
}

/** Orientation (no roll) that points the chosen local axis along Dir. */
inline FQuat AirMailSpearBasis(int32 LongAxis, const FVector& Dir)
{
	switch (LongAxis)
	{
	case 1:  return FRotationMatrix::MakeFromY(Dir).ToQuat();
	case 2:  return FRotationMatrix::MakeFromZ(Dir).ToQuat();
	default: return FRotationMatrix::MakeFromX(Dir).ToQuat();
	}
}

/** One-shot: snap the body so its longest local axis points along MoveDir and clear any spin.
 *  Used at launch (bounce / kick) for an immediately correct frame; AirMailTickSpear maintains it. */
inline void AirMailOrientSpear(UStaticMeshComponent* Mesh, const FVector& MoveDir)
{
	if (!Mesh)
	{
		return;
	}
	const FVector Dir = MoveDir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	const int32 LongAxis = AirMailLongAxisIndex(Mesh);
	Mesh->SetWorldRotation(AirMailSpearBasis(LongAxis, Dir), /*bSweep=*/false, /*OutHit=*/nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

	UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] spear orient: longAxis=%s"),
		(LongAxis == 0 ? TEXT("X") : LongAxis == 1 ? TEXT("Y") : TEXT("Z")));
}

/** Per-tick: keep the longest local axis pointing along the CURRENT velocity (nose tracks the arc)
 *  and roll the body around that axis at RollDegPerSec. Kinematic (teleport rotation + zeroed
 *  angular velocity) so the body cannot precess/tumble. No-ops once the body slows below MinSpeed
 *  (landed / stopped), so physics resumes naturally. */
inline void AirMailTickSpear(UStaticMeshComponent* Mesh, float RollDegPerSec, float MinSpeed = 200.0f)
{
	if (!Mesh)
	{
		return;
	}
	const UWorld* World = Mesh->GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Vel = Mesh->GetPhysicsLinearVelocity();
	if (Vel.SizeSquared() < MinSpeed * MinSpeed)
	{
		return; // settled — let physics have the rotation back
	}

	const FVector Dir = Vel.GetSafeNormal();
	const FQuat Base = AirMailSpearBasis(AirMailLongAxisIndex(Mesh), Dir);
	const FQuat Roll = FQuat(Dir, FMath::DegreesToRadians(RollDegPerSec * World->GetTimeSeconds()));
	Mesh->SetWorldRotation(Roll * Base, /*bSweep=*/false, /*OutHit=*/nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}
