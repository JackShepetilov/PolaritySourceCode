#include "AnimNotify_YankThrow.h"
#include "ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_YankThrowDiscard::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (AShooterCharacter* Character = Cast<AShooterCharacter>(MeshComp->GetOwner()))
	{
		Character->OnYankThrowDiscardNotify();
	}
}

void UAnimNotify_YankThrowLower::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (AShooterCharacter* Character = Cast<AShooterCharacter>(MeshComp->GetOwner()))
	{
		Character->OnYankThrowLowerNotify();
	}
}
