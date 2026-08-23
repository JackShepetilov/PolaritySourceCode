// AnimNotify for the holster half of a weapon swap: the single frame where the old weapon leaves
// the hand and the new one appears in it.
//
// Placed by the animator in the holster montage's notify track. Nothing else in the swap needs a
// notify: the draw half only has to report that it ended, which its own length already says.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_WeaponSwitch.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

UCLASS(meta = (DisplayName = "Weapon Switch - Swap Point (hide old weapon, show new)"))
class POLARITY_API UAnimNotify_WeaponSwitchSwapPoint : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Weapon Switch Swap Point"); }
};
