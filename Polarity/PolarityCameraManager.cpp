// Copyright Epic Games, Inc. All Rights Reserved.

#include "PolarityCameraManager.h"

APolarityCameraManager::APolarityCameraManager()
{
	ViewPitchMin = -70.0f;
	ViewPitchMax = 80.0f;
}

void APolarityCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	// Let base class do all work (CalcCamera, CameraComponent, etc.)
	Super::UpdateViewTarget(OutVT, DeltaTime);

	// Interpolate rotation offset
	CurrentRotationOffset = FMath::RInterpTo(
		CurrentRotationOffset,
		TargetRotationOffset,
		DeltaTime,
		RotationOffsetInterpSpeed
	);

	// Apply rotation offset to POV using quaternion math
	if (!CurrentRotationOffset.IsNearlyZero(0.01f))
	{
		FQuat BaseQuat = OutVT.POV.Rotation.Quaternion();
		FQuat OffsetQuat = CurrentRotationOffset.Quaternion();
		FQuat FinalQuat = BaseQuat * OffsetQuat;
		OutVT.POV.Rotation = FinalQuat.Rotator();
	}
}