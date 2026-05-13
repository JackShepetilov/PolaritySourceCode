// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_MeleePhase.generated.h"

class UMeleeAttackComponent;

/**
 * Anim notify classes used by the MeleeAttackComponent state machine.
 *
 * The component runs phases Windup -> Active -> Recovery -> ShowingWeapon -> Cooldown.
 * Without notifies, each phase ends on a fallback timer (Settings.WindupTime,
 * ActiveTime, RecoveryTime). With these notifies placed in the montage, transitions
 * are driven by animation curves so changes to play rate (combo speed up) stay in
 * sync with timing.
 *
 * Semantics: each notify performs a SetState on the component. Once the state
 * transitions, the new phase's own StateTimeRemaining takes over — so notify is
 * always a "skip ahead" relative to the fallback timer of the prior phase, never
 * an interrupt of the new phase. If a notify is missing in a montage, the
 * fallback timer of that phase ends it as before.
 */

// ==================== Active Start ====================

/** Marks the start of the damage window (windup -> active transition). */
UCLASS(meta = (DisplayName = "Melee: Active Start"))
class POLARITY_API UAnimNotify_MeleeActiveStart : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MeleeActiveStart"); }
#endif

private:
	/** Find the owning character's UMeleeAttackComponent (may be null if anim plays on a non-character mesh) */
	static UMeleeAttackComponent* ResolveMeleeComp(USkeletalMeshComponent* MeshComp);
};

// ==================== Active End ====================

/** Marks the end of the damage window (active -> recovery transition). */
UCLASS(meta = (DisplayName = "Melee: Active End"))
class POLARITY_API UAnimNotify_MeleeActiveEnd : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MeleeActiveEnd"); }
#endif

private:
	static UMeleeAttackComponent* ResolveMeleeComp(USkeletalMeshComponent* MeshComp);
};

// ==================== Recovery End ====================

/** Marks the end of recovery (recovery -> showing weapon / cooldown transition). */
UCLASS(meta = (DisplayName = "Melee: Recovery End"))
class POLARITY_API UAnimNotify_MeleeRecoveryEnd : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MeleeRecoveryEnd"); }
#endif

private:
	static UMeleeAttackComponent* ResolveMeleeComp(USkeletalMeshComponent* MeshComp);
};
