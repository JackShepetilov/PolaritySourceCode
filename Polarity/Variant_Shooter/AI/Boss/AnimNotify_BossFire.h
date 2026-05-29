// AnimNotify_BossFire.h
// Placed once in EACH per-shot montage in ABossCharacter::FireShotMontages. The notify fires that
// montage's shot AND advances the burst (crossfade to the next shot montage). So a burst = a chain
// of montages, one notify per montage, one shot per notify.

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
