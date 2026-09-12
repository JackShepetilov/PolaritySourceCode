// Copyright Epic Games, Inc. All Rights Reserved.

#include "PolarityCameraManager.h"
#include "PolarityCharacter.h"

APolarityCameraManager::APolarityCameraManager()
{
	ViewPitchMin = -87.0f;
	ViewPitchMax = 87.0f;
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

	// Weapon recoil does NOT arrive here. It is added in AShooterCharacter::GetViewRotation, which
	// is upstream of CalcCamera, so it is already in OutVT.POV by the time this runs, and more to
	// the point it is in the CONTROL rotation, which is what makes the shots follow it: GetAimRay
	// reads the same function. Anything applied at this stage moves the picture and nothing else.
	//
	// (The old reason given here — that the camera component stage lets the first person mesh
	// inherit the kick through attachment — stopped being true when the FPS pack hierarchy went in
	// and the mesh became the camera's PARENT rather than its child.)
	//
	// Everything that must move the picture and NOT the aim is collected here: the wallrun tilt
	// above, plus the pawn's own camera sway and the FPS pack's recoil jolt.
	//
	// This is the same split the pack makes. Their CameraAnimator adds the recoil shake to the
	// control rotation and writes the result onto the camera with SetWorldRotation, leaving the
	// controller untouched, while the aim climb they DO want comes from PRAS pushing
	// AddControllerPitchInput. Ours arrives one stage later but has the same effect: after
	// CalcCamera, so the control rotation never sees it and GetAimRay cannot follow it.
	FRotator ViewOnly = CurrentRotationOffset;

	if (const APolarityCharacter* PolarityPawn = Cast<APolarityCharacter>(OutVT.Target))
	{
		ViewOnly += PolarityPawn->GetViewOnlyRotationOffset();
	}

	// Apply rotation offset to POV using quaternion math
	if (!ViewOnly.IsNearlyZero(0.01f))
	{
		FQuat BaseQuat = OutVT.POV.Rotation.Quaternion();
		FQuat OffsetQuat = ViewOnly.Quaternion();
		FQuat FinalQuat = BaseQuat * OffsetQuat;
		OutVT.POV.Rotation = FinalQuat.Rotator();
	}
}