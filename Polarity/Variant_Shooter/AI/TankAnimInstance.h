// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "TankAnimInstance.generated.h"

class ATrackedTankNPC;

/**
 *  Animation state for the tracked tank hull.
 *
 *  The tank has no animations: it has two bones that point where it is looking. This reads the
 *  turret angles off the pawn (where they are replicated, because the AI controller that decides
 *  them does not exist on clients) and hands the graph two ready-made rotators.
 *
 *  Both rotators are meant for a Modify Bone node in BONE space with mode "Add to Existing". The
 *  mesh comes out of Blender with every bone rotated 90 degrees in yaw, so absolute angles in
 *  component space would need that offset folded in by hand; a delta in the bone's own space does
 *  not care how the bone was authored. Consequence of that same rest orientation: the barrel lies
 *  along the bone's -Y, so elevating the gun is a ROLL, not a pitch.
 */
UCLASS()
class POLARITY_API UTankAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	/** Turret angle in hull space, degrees */
	UPROPERTY(BlueprintReadOnly, Category = "Tank")
	float TurretYaw = 0.0f;

	/** Gun elevation in hull space, degrees, positive is up */
	UPROPERTY(BlueprintReadOnly, Category = "Tank")
	float GunPitch = 0.0f;

	/** Feed to Modify Bone on `turret_yaw`, bone space, add to existing */
	UPROPERTY(BlueprintReadOnly, Category = "Tank")
	FRotator TurretBoneRotation = FRotator::ZeroRotator;

	/** Feed to Modify Bone on `gun_pitch`, bone space, add to existing */
	UPROPERTY(BlueprintReadOnly, Category = "Tank")
	FRotator GunBoneRotation = FRotator::ZeroRotator;

	/** Hull ground speed, for track and wheel animation later */
	UPROPERTY(BlueprintReadOnly, Category = "Tank")
	float HullSpeed = 0.0f;

	/** True while the running gear is broken, for a stuck-track look later */
	UPROPERTY(BlueprintReadOnly, Category = "Tank")
	bool bImmobilized = false;

	/** Flip if the gun elevates the wrong way on this mesh */
	UPROPERTY(EditDefaultsOnly, Category = "Tank")
	bool bInvertGunPitch = true;

	/** How fast the local copy chases the replicated angles. Replication arrives in steps; without
	 *  this the barrel of a remote tank jumps between updates. */
	UPROPERTY(EditDefaultsOnly, Category = "Tank", meta = (ClampMin = "10.0"))
	float FollowRate = 180.0f;

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:

	TWeakObjectPtr<ATrackedTankNPC> Tank;
};
