// AnimNotify_MeleeWindow.h
// Animation notifies for melee attack damage window control
// Allows animators to precisely control when damage is dealt

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotify_MeleeWindow.generated.h"

class UMeleeAttackComponent;

/**
 * Animation Notify to activate the melee damage window.
 * Place this at the moment in the animation where the attack should start dealing damage.
 */
UCLASS(DisplayName = "Melee: Activate Damage Window")
class POLARITY_API UAnimNotify_MeleeActivate : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_MeleeActivate();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

#if WITH_EDITOR
	virtual FLinearColor GetEditorColor() override { return FLinearColor(0.0f, 1.0f, 0.0f, 1.0f); } // Green
#endif
};

/**
 * Animation Notify to deactivate the melee damage window.
 * Place this at the moment in the animation where the attack should stop dealing damage.
 */
UCLASS(DisplayName = "Melee: Deactivate Damage Window")
class POLARITY_API UAnimNotify_MeleeDeactivate : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_MeleeDeactivate();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

#if WITH_EDITOR
	virtual FLinearColor GetEditorColor() override { return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f); } // Red
#endif
};

/**
 * Animation Notify State for melee damage window (alternative to separate activate/deactivate).
 * The damage window is active for the entire duration of this notify state.
 * More convenient for animators as they can visually see the damage window duration.
 */
UCLASS(DisplayName = "Melee: Damage Window State")
class POLARITY_API UAnimNotifyState_MeleeDamageWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_MeleeDamageWindow();

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

#if WITH_EDITOR
	virtual FLinearColor GetEditorColor() override { return FLinearColor(1.0f, 0.5f, 0.0f, 1.0f); } // Orange
#endif
};
