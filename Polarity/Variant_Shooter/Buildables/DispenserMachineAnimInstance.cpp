// DispenserMachineAnimInstance.cpp

#include "DispenserMachineAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "Components/SkeletalMeshComponent.h"
#include "DispenserSlotMachineComponent.h"

FAnimInstanceProxy* UDispenserMachineAnimInstance::CreateAnimInstanceProxy()
{
	return new FDispenserMachineAnimInstanceProxy(this);
}

void FDispenserMachineAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// Game thread: the slot machine has already ticked this frame (it is the mesh's prerequisite).
	Joints.Reset();
	const AActor* const Owner = InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr;
	const UDispenserSlotMachineComponent* const Machine = Owner ? Owner->FindComponentByClass<UDispenserSlotMachineComponent>() : nullptr;
	if (!Machine)
	{
		return;
	}
	USkeletalMeshComponent* const Mesh = InAnimInstance->GetSkelMeshComponent();
	if (!Mesh)
	{
		return;
	}
	// All bones remain root children, preserving the existing skeleton hierarchy.
	const auto AddTurn = [this, Mesh](FName Bone, const FVector& Axis, float Degrees)
	{
		const int32 BoneIndex = Mesh->GetBoneIndex(Bone);
		if (BoneIndex == INDEX_NONE || Axis.IsNearlyZero())
		{
			return;
		}
		const FVector Pivot = Mesh->GetRefPoseTransform(BoneIndex).GetTranslation();
		const FQuat Rotation(Axis.GetSafeNormal(), FMath::DegreesToRadians(Degrees));
		Joints.Add({Bone, FTransform(Rotation, Pivot - Rotation.RotateVector(Pivot))});
	};
	for (int32 Index = 0; Index < Machine->LidBones.Num(); ++Index)
	{
		if (!Machine->HasCassettes())
		{
			AddTurn(Machine->LidBones[Index], Machine->LidHingeAxis, Machine->GetLidAngle(Index));
			continue;
		}
		const float Progress = Machine->GetCassetteAlpha(Index);
		const float ShutterAlpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Progress / 0.36f, 0.0f, 1.0f));
		const float TrayAlpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((Progress - 0.36f) / 0.64f, 0.0f, 1.0f));
		const FName TrayBone(*FString::Printf(TEXT("tray_%d"), Index));
		const FTransform TrayMotion(FVector(20.0f * TrayAlpha, 0.0f, 0.0f));
		Joints.Add({TrayBone, TrayMotion});
		if (Machine->BoxSockets.IsValidIndex(Index))
		{
			Joints.Add({Machine->BoxSockets[Index], TrayMotion});
		}

		const int32 GuideIndex = Mesh->GetBoneIndex(Machine->LidBones[Index]);
		if (GuideIndex == INDEX_NONE)
		{
			continue;
		}
		const float TurnZ = Mesh->GetRefPoseTransform(GuideIndex).GetTranslation().Z;
		// v04 mesh contract, centimetres: 10 rigid slats, 3 cm return radius, 28.6 cm travel.
		constexpr float Radius = 3.0f;
		constexpr float ArcLength = Radius * HALF_PI;
		for (int32 Slat = 0; Slat < 10; ++Slat)
		{
			const FName Bone(*FString::Printf(TEXT("shutter_%d_%02d"), Index, Slat));
			const int32 BoneIndex = Mesh->GetBoneIndex(Bone);
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}
			const FVector Rest = Mesh->GetRefPoseTransform(BoneIndex).GetTranslation();
			const float Distance = Rest.Z + 28.6f * ShutterAlpha - TurnZ;
			FVector Position = Rest + FVector(0.0f, 0.0f, 28.6f * ShutterAlpha);
			float Angle = 0.0f;
			if (Distance > 0.0f && Distance < ArcLength)
			{
				Angle = Distance / Radius;
				Position = FVector(Rest.X - Radius * (1.0f - FMath::Cos(Angle)), Rest.Y, TurnZ + Radius * FMath::Sin(Angle));
			}
			else if (Distance >= ArcLength)
			{
				Angle = HALF_PI;
				Position = FVector(Rest.X - Radius - (Distance - ArcLength), Rest.Y, TurnZ + Radius);
			}
			const FQuat Rotation(FVector::YAxisVector, -Angle);
			Joints.Add({Bone, FTransform(Rotation, Position - Rotation.RotateVector(Rest))});
		}
	}
	AddTurn(Machine->FlapBone, Machine->FlapHingeAxis, Machine->GetFlapAngle());
}

bool FDispenserMachineAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	// The reference pose is the rest one (lids shut, flap shut); everything is a change from it.
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();

	// Which compact bone each joint drives; a bone the mesh lacks, or a joint at rest, drives nothing.
	TArray<FCompactPoseBoneIndex, TInlineAllocator<40>> JointBones;
	bool bAnyMotion = false;
	for (const FJoint& Joint : Joints)
	{
		const int32 MeshIndex = Joint.Bone.IsNone() ? INDEX_NONE : Bones.GetPoseBoneIndexForBoneName(Joint.Bone);
		const bool bDrives = MeshIndex != INDEX_NONE && !Joint.Motion.Equals(FTransform::Identity);
		JointBones.Add(bDrives ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE));
		bAnyMotion |= bDrives;
	}
	if (!bAnyMotion)
	{
		return true;
	}

	// Reference component space. Compact indices run parents first.
	const int32 NumBones = Pose.GetNumBones();
	TArray<FTransform, TInlineAllocator<32>> RefComponent;
	RefComponent.SetNumUninitialized(NumBones);
	for (const FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
		RefComponent[Index.GetInt()] = Parent.IsValid() ? Pose[Index] * RefComponent[Parent.GetInt()] : Pose[Index];
	}

	// Posed component space; a driven bone turns about its axis through its own rest position
	// (x -> R(x - P) + P), the rest follow their parent.
	TArray<FTransform, TInlineAllocator<32>> Posed;
	Posed.SetNumUninitialized(NumBones);
	for (const FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		const int32 I = Index.GetInt();
		const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
		const FTransform ParentPosed = Parent.IsValid() ? Posed[Parent.GetInt()] : FTransform::Identity;

		const int32 JointIndex = JointBones.IndexOfByKey(Index);
		if (JointIndex != INDEX_NONE)
		{
			const FJoint& Joint = Joints[JointIndex];
			Posed[I] = RefComponent[I] * Joint.Motion;
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
