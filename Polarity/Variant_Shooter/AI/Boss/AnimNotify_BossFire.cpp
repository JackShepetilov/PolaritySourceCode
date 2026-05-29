// AnimNotify_BossFire.cpp

#include "AnimNotify_BossFire.h"
#include "BossCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UAnimNotify_BossFire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	if (ABossCharacter* Boss = Cast<ABossCharacter>(MeshComp->GetOwner()))
	{
		Boss->FireOneShotFromNotify();
	}
}
