// TurretAnimInstance.cpp

#include "TurretAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "TurretBuildable.h"

namespace TurretPose
{
	static FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, FName Name)
	{
		if (Name.IsNone())
		{
			return FCompactPoseBoneIndex(INDEX_NONE);
		}
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	}

	/** Rotation by Rotation about an axis through Pivot, as a transform: x -> R(x - P) + P. */
	static FTransform RotateAbout(const FQuat& Rotation, const FVector& Pivot)
	{
		return FTransform(Rotation, Pivot - Rotation.RotateVector(Pivot));
	}
}

FAnimInstanceProxy* UTurretAnimInstance::CreateAnimInstanceProxy()
{
	return new FTurretAnimInstanceProxy(this);
}

void FTurretAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// Game thread: the turret has already ticked this frame (TurretMesh ticks after its actor).
	const ATurretBuildable* const Turret = InAnimInstance ? Cast<ATurretBuildable>(InAnimInstance->GetOwningActor()) : nullptr;
	if (!Turret || !Turret->HasArticulatedHead())
	{
		YawBone = NAME_None;
		PitchBone = NAME_None;
		JawBone = NAME_None;
		return;
	}
	HeadYaw = Turret->GetHeadYaw();
	HeadPitch = Turret->GetHeadPitch();
	JawOffsetCm = Turret->GetJawOffsetCm();
	YawBone = Turret->YawBoneName;
	PitchBone = Turret->PitchBoneName;
	JawBone = Turret->JawBoneName;
}

bool FTurretAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	// The reference pose is the neutral one (level, looking down +X, vice open); everything is a
	// change from it.
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();

	const FCompactPoseBoneIndex YawIndex = TurretPose::FindBone(Bones, YawBone);
	const FCompactPoseBoneIndex PitchIndex = TurretPose::FindBone(Bones, PitchBone);
	const FCompactPoseBoneIndex JawIndex = TurretPose::FindBone(Bones, JawBone);
	if (!YawIndex.IsValid() || !PitchIndex.IsValid())
	{
		return true;
	}

	// Reference component space. Compact indices run parents first.
	const int32 NumBones = Pose.GetNumBones();
	TArray<FTransform, TInlineAllocator<16>> RefComponent;
	RefComponent.SetNumUninitialized(NumBones);
	for (const FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
		RefComponent[Index.GetInt()] = Parent.IsValid() ? Pose[Index] * RefComponent[Parent.GetInt()] : Pose[Index];
	}

	// The head's motion in component space. Pitch first, about the crosswise line through the pitch
	// pivot as it stands at rest; then yaw about the vertical line through the yaw pivot, which
	// carries the pitch pivot round with it. FRotator's own signs: positive pitch lifts +X toward
	// +Z, positive yaw turns +X toward +Y, the same as FVector::Rotation() on the aim side.
	const FTransform YawMotion = TurretPose::RotateAbout(FRotator(0.0f, HeadYaw, 0.0f).Quaternion(), RefComponent[YawIndex.GetInt()].GetLocation());
	const FTransform PitchMotion = TurretPose::RotateAbout(FRotator(HeadPitch, 0.0f, 0.0f).Quaternion(), RefComponent[PitchIndex.GetInt()].GetLocation());
	const FTransform HeadMotion = PitchMotion * YawMotion;
	// The jaw slides in the head's frame at rest, before the head moves it.
	const FTransform JawMotion = FTransform(FVector(0.0f, JawOffsetCm, 0.0f)) * HeadMotion;

	// Posed component space; the three driven bones get their motion, the rest follow their parent.
	TArray<FTransform, TInlineAllocator<16>> Posed;
	Posed.SetNumUninitialized(NumBones);
	for (const FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		const int32 I = Index.GetInt();
		const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
		const FTransform ParentPosed = Parent.IsValid() ? Posed[Parent.GetInt()] : FTransform::Identity;

		const FTransform* Motion = nullptr;
		if (Index == YawIndex)
		{
			Motion = &YawMotion;
		}
		else if (Index == PitchIndex)
		{
			Motion = &HeadMotion;
		}
		else if (Index == JawIndex)
		{
			Motion = &JawMotion;
		}

		if (Motion)
		{
			Posed[I] = RefComponent[I] * *Motion;
			FTransform Local = Posed[I].GetRelativeTransform(ParentPosed);
			Local.NormalizeRotation();
			Pose[Index] = Local;
		}
		else
		{
			Posed[I] = Pose[Index] * ParentPosed;
		}
	}
	return true;
}
