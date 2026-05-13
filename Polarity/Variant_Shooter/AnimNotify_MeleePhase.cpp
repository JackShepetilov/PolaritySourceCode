// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "AnimNotify_MeleePhase.h"
#include "MeleeAttackComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

// ==================== ResolveMeleeComp helpers ====================
// All three notifies look up the component the same way; keep the impl identical
// per-class so each can stay loose (no shared base).

namespace
{
	UMeleeAttackComponent* ResolveImpl(USkeletalMeshComponent* MeshComp)
	{
		if (!MeshComp)
		{
			return nullptr;
		}
		AActor* Owner = MeshComp->GetOwner();
		if (!Owner)
		{
			return nullptr;
		}
		return Owner->FindComponentByClass<UMeleeAttackComponent>();
	}
}

UMeleeAttackComponent* UAnimNotify_MeleeActiveStart::ResolveMeleeComp(USkeletalMeshComponent* MeshComp)
{
	return ResolveImpl(MeshComp);
}

UMeleeAttackComponent* UAnimNotify_MeleeActiveEnd::ResolveMeleeComp(USkeletalMeshComponent* MeshComp)
{
	return ResolveImpl(MeshComp);
}

UMeleeAttackComponent* UAnimNotify_MeleeRecoveryEnd::ResolveMeleeComp(USkeletalMeshComponent* MeshComp)
{
	return ResolveImpl(MeshComp);
}

// ==================== Notify impls ====================

void UAnimNotify_MeleeActiveStart::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (UMeleeAttackComponent* Comp = ResolveMeleeComp(MeshComp))
	{
		Comp->ActivateDamageWindowFromNotify();
	}
}

void UAnimNotify_MeleeActiveEnd::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (UMeleeAttackComponent* Comp = ResolveMeleeComp(MeshComp))
	{
		Comp->DeactivateDamageWindowFromNotify();
	}
}

void UAnimNotify_MeleeRecoveryEnd::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (UMeleeAttackComponent* Comp = ResolveMeleeComp(MeshComp))
	{
		Comp->EndRecoveryFromNotify();
	}
}
