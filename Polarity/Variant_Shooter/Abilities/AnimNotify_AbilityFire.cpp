// AnimNotify_AbilityFire.cpp

#include "AnimNotify_AbilityFire.h"
#include "AbilityComponent.h"
#include "AbilityHandler_Burst.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UAnimNotify_AbilityFire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}
	UAbilityComponent* AbilityComp = MeshComp->GetOwner()->FindComponentByClass<UAbilityComponent>();
	if (!AbilityComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] AnimNotify: no UAbilityComponent on '%s'"), *MeshComp->GetOwner()->GetName());
		return;
	}
	UAbilityHandler_Burst* BurstHandler = Cast<UAbilityHandler_Burst>(AbilityComp->GetActiveHandler());
	if (!BurstHandler)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] AnimNotify: active handler is not UAbilityHandler_Burst — ignored"));
		return;
	}
	BurstHandler->NotifyPerShotFromAnimNotify();
}
