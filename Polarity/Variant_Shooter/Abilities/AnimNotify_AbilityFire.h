// AnimNotify_AbilityFire.h
// Placed inside an AbilityDefinition::LoopMontage at the frame where the per-shot action
// should occur (projectile spawn for EMFBurst, beam tick for channeled, etc.).
// The notify finds the UAbilityComponent on the mesh's owner and dispatches.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_AbilityFire.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

UCLASS(meta = (DisplayName = "Ability — Per-Shot Fire"))
class POLARITY_API UAnimNotify_AbilityFire : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Ability Fire"); }
};
