#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ReloadReady.generated.h"

/** Author placed at the bolt click where the magazine becomes usable. */
UCLASS(DisplayName = "Weapon: Reload Ready")
class POLARITY_API UAnimNotify_ReloadReady : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_ReloadReady();
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
#if WITH_EDITOR
	virtual FLinearColor GetEditorColor() override { return FLinearColor(1.0f, 0.75f, 0.1f, 1.0f); }
#endif
};
