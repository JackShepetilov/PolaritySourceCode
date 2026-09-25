// TurretAnimInstance.h
// The pose of an articulated turret, straight from ATurretBuildable's joint angles, no graph.
//
// The turret keeps three numbers (head yaw, head pitch, jaw offset) and this puts them on the
// bones. It works in the mesh's component space against the reference pose: the yaw bone turns
// about the vertical line through its own reference position, the pitch bone about the crosswise
// line through its own, the jaw slides along the head's +Y, and every other bone just follows its
// parent. So the local axes the FBX export gave the bones do not matter, only where the pivots are
// and that the mesh itself is +X forward, +Y across, +Z up.
//
// Native on purpose: ATurretBuildable sets it as TurretMesh's anim class, and there is no
// Blueprint to keep in step with the bone names or the axes. The names come from the turret.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "TurretAnimInstance.generated.h"

/** Worker-thread side: a copy of the turret's numbers, taken on the game thread each update. */
struct FTurretAnimInstanceProxy : public FAnimInstanceProxy
{
	FTurretAnimInstanceProxy() = default;
	explicit FTurretAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	float HeadYaw = 0.0f;
	float HeadPitch = 0.0f;
	float JawOffsetCm = 0.0f;
	FName YawBone;
	FName PitchBone;
	FName JawBone;
};

UCLASS(Transient, NotBlueprintable)
class POLARITY_API UTurretAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
