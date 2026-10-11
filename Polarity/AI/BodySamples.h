// BodySamples.h
// The points of a body that somebody looking at it may see.
//
// Every "can A see B" in the AI used to ask about the waist and, failing that, the head, both worked
// out from the capsule. A player's eye is not where the capsule says. Measured 2026-10-11: crouched,
// the capsule ends 100 cm above the floor and the camera sits at 117. So a player crouched behind a
// low wall saw the enemy over it and shot him, while every point the enemy asked about was behind
// the wall, and the enemy "did not see" somebody who was plainly looking at it.
//
// The rule now [author, 2026-10-11]: what a player sees FROM can be seen. The camera is one of the
// points. One list for all the askers: the sight sense (APolarityCharacter::CanBeSeenFrom), the
// sense task (SenseSight::SeesBody), the weapon's line (AShooterNPC::HasLineOfSightTo) and the aim
// point. They have to agree, or the sense reports somebody the gun then refuses to shoot at.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "PolarityCharacter.h"

namespace BodySamples
{
	using FPoints = TArray<FVector, TInlineAllocator<4>>;

	/** Waist, the top of the capsule's head, and for a player the camera it is looking out of. */
	inline void Get(const AActor* Target, FPoints& Out)
	{
		Out.Reset();
		if (!Target)
		{
			return;
		}

		const FVector Waist = Target->GetActorLocation();
		Out.Add(Waist);
		Out.Add(Waist + FVector(0.0f, 0.0f, FMath::Max(0.0f, Target->GetSimpleCollisionHalfHeight() - 15.0f)));

		// A player only: an NPC's camera component is not where its eyes are
		// (Docs/Gotchas/Camera_FirstPerson.md).
		const APolarityCharacter* const Body = Cast<APolarityCharacter>(Target);
		if (Body && Body->IsPlayerControlled())
		{
			if (const UCameraComponent* const Camera = Body->GetFirstPersonCameraComponent())
			{
				Out.Add(Camera->GetComponentLocation());
			}
		}
	}
}
