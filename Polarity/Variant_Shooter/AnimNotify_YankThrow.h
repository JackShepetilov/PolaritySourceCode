// AnimNotify subclasses for the FP arms ThrowMontage of yanked-weapon discard.
// Two notifies, placed by the animator in the montage's notify track:
//   - UAnimNotify_YankThrowDiscard — fires when the weapon should LEAVE the hand
//     (hides held weapon meshes + spawns the dropped/thrown version at the FP mesh's
//     world position).
//   - UAnimNotify_YankThrowLower — fires when the empty hands should start lowering
//     so the next weapon can be raised (triggers BeginWeaponLower + FinishWeaponSwitch).

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_YankThrow.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

UCLASS(meta = (DisplayName = "Yank Throw — Discard (hide weapon mesh + spawn dropped)"))
class POLARITY_API UAnimNotify_YankThrowDiscard : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("YankThrow Discard"); }
};

UCLASS(meta = (DisplayName = "Yank Throw — Lower hands (begin weapon switch)"))
class POLARITY_API UAnimNotify_YankThrowLower : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("YankThrow Lower"); }
};
