// DispenserMachineAnimInstance.h
// The pose of the dispenser's slot machine, straight from UDispenserSlotMachineComponent's angles,
// no graph.
//
// The component keeps each lid's angle and the intake flap's; this puts them on the bones. Same
// recipe as UTurretAnimInstance: in the mesh's component space against the reference pose, each
// driven bone turns about a component-space line through its own reference position, and every
// other bone just follows its parent. So the local axes the FBX gave the bones do not matter.
//
// Native on purpose: the slot machine component sets it as the machine mesh's anim class, and
// there is no Blueprint to keep in step with the bone names. The names and axes come from the
// component.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "DispenserMachineAnimInstance.generated.h"

/** Worker-thread side: a copy of the machine's joints, taken on the game thread each update. */
struct FDispenserMachineAnimInstanceProxy : public FAnimInstanceProxy
{
	FDispenserMachineAnimInstanceProxy() = default;
	explicit FDispenserMachineAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** A rigid component-space delta from this bone's reference pose. */
	struct FJoint
	{
		FName Bone;
		FTransform Motion = FTransform::Identity;
	};

	TArray<FJoint, TInlineAllocator<40>> Joints;
};

UCLASS(Transient, NotBlueprintable)
class POLARITY_API UDispenserMachineAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
