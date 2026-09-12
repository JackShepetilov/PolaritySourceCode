// AimPoints.cpp

#include "AI/AimPoints.h"

#include "AI/AimTargetInterface.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Collision centre and half height, for a target that has not been given an aim point. */
	void ResolveCollisionBox(const AActor* Target, FVector& OutCentre, float& OutHalfHeight)
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Target->GetActorBounds(true, Origin, Extent);

		OutCentre = Origin;
		OutHalfHeight = Extent.Z;
	}

	FVector ResolveInternal(const AActor* Target, bool bWithScatter)
	{
		if (!Target)
		{
			return FVector::ZeroVector;
		}

		if (const IAimTargetInterface* AimTarget = Cast<IAimTargetInterface>(Target))
		{
			// The point is authored in the actor's own space, so a hull that has turned keeps being
			// shot at in the same place on itself rather than at a spot that swings with its yaw.
			const FVector World = Target->GetActorTransform().TransformPosition(AimTarget->GetLocalAimPoint());
			if (!bWithScatter)
			{
				return World;
			}

			const float Jitter = AimTarget->GetAimVerticalJitter();
			return World + FVector(0.0f, 0.0f, FMath::FRandRange(-Jitter, Jitter));
		}

		FVector Centre = FVector::ZeroVector;
		float HalfHeight = 0.0f;
		ResolveCollisionBox(Target, Centre, HalfHeight);

		if (!bWithScatter)
		{
			return Centre;
		}

		// A quarter of the target's own height: enough to look like aimed fire rather than a laser,
		// small enough to stay inside anything worth shooting at.
		const float Jitter = HalfHeight * 0.25f;
		return Centre + FVector(0.0f, 0.0f, FMath::FRandRange(-Jitter, Jitter));
	}
}

namespace PolarityAim
{
	FVector ResolveAimPoint(const AActor* Target)
	{
		return ResolveInternal(Target, /*bWithScatter*/ true);
	}

	FVector ResolveAimPointExact(const AActor* Target)
	{
		return ResolveInternal(Target, /*bWithScatter*/ false);
	}
}
