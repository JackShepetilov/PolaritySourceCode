// AnimNotify_BossFire.h
// Placed at each shot frame inside the boss FireMontage. Each notify fires exactly one weapon
// shot at the boss's current target, so the animation drives the firing cadence.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_BossFire.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

UCLASS(meta = (DisplayName = "Boss — Fire One Shot"))
class POLARITY_API UAnimNotify_BossFire : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Boss Fire"); }
};
