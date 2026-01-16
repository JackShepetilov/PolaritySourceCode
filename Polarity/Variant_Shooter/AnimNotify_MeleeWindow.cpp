// AnimNotify_MeleeWindow.cpp
// Animation notifies for melee attack damage window control

#include "AnimNotify_MeleeWindow.h"
#include "MeleeAttackComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

// ==================== Helper Function ====================

static UMeleeAttackComponent* GetMeleeComponentFromMesh(USkeletalMeshComponent* MeshComp)
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

// ==================== UAnimNotify_MeleeActivate ====================

UAnimNotify_MeleeActivate::UAnimNotify_MeleeActivate()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(0, 255, 0); // Green
#endif
}

void UAnimNotify_MeleeActivate::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (UMeleeAttackComponent* MeleeComp = GetMeleeComponentFromMesh(MeshComp))
	{
		MeleeComp->ActivateDamageWindowFromNotify();
	}
}

FString UAnimNotify_MeleeActivate::GetNotifyName_Implementation() const
{
	return TEXT("Melee: Activate");
}

// ==================== UAnimNotify_MeleeDeactivate ====================

UAnimNotify_MeleeDeactivate::UAnimNotify_MeleeDeactivate()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 0, 0); // Red
#endif
}

void UAnimNotify_MeleeDeactivate::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (UMeleeAttackComponent* MeleeComp = GetMeleeComponentFromMesh(MeshComp))
	{
		MeleeComp->DeactivateDamageWindowFromNotify();
	}
}

FString UAnimNotify_MeleeDeactivate::GetNotifyName_Implementation() const
{
	return TEXT("Melee: Deactivate");
}

// ==================== UAnimNotifyState_MeleeDamageWindow ====================

UAnimNotifyState_MeleeDamageWindow::UAnimNotifyState_MeleeDamageWindow()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 128, 0); // Orange
#endif
}

void UAnimNotifyState_MeleeDamageWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (UMeleeAttackComponent* MeleeComp = GetMeleeComponentFromMesh(MeshComp))
	{
		MeleeComp->ActivateDamageWindowFromNotify();
	}
}

void UAnimNotifyState_MeleeDamageWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (UMeleeAttackComponent* MeleeComp = GetMeleeComponentFromMesh(MeshComp))
	{
		MeleeComp->DeactivateDamageWindowFromNotify();
	}
}

FString UAnimNotifyState_MeleeDamageWindow::GetNotifyName_Implementation() const
{
	return TEXT("Melee Damage Window");
}
