// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "PolarityCameraManager.generated.h"

/**
 *  First Person camera manager with wallrun tilt support.
 *
 *  Overrides UpdateViewTarget to add Roll AFTER CalcCamera fills the ViewTarget.POV from the
 *  CameraComponent.
 *
 *  What belongs here and what does not: this stage moves the PICTURE only. The camera component
 *  has already been posed by the time UpdateViewTarget runs, so anything added here is not
 *  inherited by the things parented to that component — most of all the first-person mesh. That is
 *  right for a lean nothing else should follow (wallrun) and wrong for anything the weapon has to
 *  stay welded to. Weapon recoil is therefore added upstream, in
 *  AShooterCharacter::GetViewRotation.
 */
UCLASS()
class APolarityCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:

	APolarityCameraManager();

	/** Target rotation offset to apply (set by character) - Roll is primary for wallrun */
	UPROPERTY(BlueprintReadWrite, Category = "Camera|WallRun")
	FRotator TargetRotationOffset = FRotator::ZeroRotator;

	/** Rotation offset interpolation speed */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|WallRun")
	float RotationOffsetInterpSpeed = 10.0f;


protected:

	/** Override UpdateViewTarget to add rotation offset AFTER CalcCamera */
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

private:

	/** Current interpolated rotation offset */
	FRotator CurrentRotationOffset = FRotator::ZeroRotator;
};