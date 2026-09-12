// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankAnimInstance.h"

#include "TrackedTankNPC.h"

void UTankAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	Tank = Cast<ATrackedTankNPC>(TryGetPawnOwner());
}

void UTankAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Tank.IsValid())
	{
		Tank = Cast<ATrackedTankNPC>(TryGetPawnOwner());
		if (!Tank.IsValid())
		{
			return;
		}
	}

	ATrackedTankNPC* const Owner = Tank.Get();

	// Chase the replicated angles instead of snapping to them: on the server this is a no-op
	// (the pawn already moves them at the turret's own rate), on a client it turns a handful of
	// updates per second back into a smooth swing.
	const float Step = FollowRate * DeltaSeconds;
	TurretYaw = FMath::FixedTurn(TurretYaw, Owner->GetTurretYaw(), Step);
	GunPitch = FMath::FixedTurn(GunPitch, Owner->GetGunPitch(), Step);

	TurretBoneRotation = FRotator(0.0f, TurretYaw, 0.0f);
	GunBoneRotation = FRotator(0.0f, 0.0f, bInvertGunPitch ? -GunPitch : GunPitch);

	HullSpeed = Owner->GetVelocity().Size2D();
	bImmobilized = Owner->IsImmobilized();
}
