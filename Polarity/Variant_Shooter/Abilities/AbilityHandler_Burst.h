// AbilityHandler_Burst.h
// Reusable pipeline for burst-archetype abilities. Owns the state machine and animation
// orchestration; subclasses just override OnPerShotEffect() to do the actual per-shot action
// (spawn projectile, apply effect, etc.).
//
// Pipeline:
//   Idle → CastStart → Loop × NumProjectiles → CastFinish → Idle (+ cooldown)
//
// Seamless chaining: each Montage_Play schedules a timer at (EffectiveDuration - Overlap)
// that kicks off the NEXT montage WHILE the current is still blending out — UE's slot
// crossfade hides the transition. PlayRate is computed so per-shot rhythm stays correct
// despite the overlap.

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityDefinition_Burst.h"
#include "AbilityHandler_Burst.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class EBurstPhase : uint8
{
	Idle,
	CastStart,
	Loop,
	CastFinish
};

UCLASS(Blueprintable, Abstract)
class POLARITY_API UAbilityHandler_Burst : public UAbilityHandler
{
	GENERATED_BODY()

public:

	// ==================== Tuning ====================

	/** Crossfade overlap (seconds) between consecutive montages. Each montage's overlap timer
	 *  fires this long before its natural end to kick the next montage. UE's Slot Blend In/Out
	 *  produces the crossfade. Tune to match LoopMontage's Blend In/Out times. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Burst|Animation", meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float MontageOverlap = 0.15f;

	// ==================== Subclass extension point ====================

	/** Called once per per-shot AnimNotify. Override to do the per-shot effect. */
	UFUNCTION(BlueprintNativeEvent, Category = "Burst")
	void OnPerShotEffect();
	virtual void OnPerShotEffect_Implementation() {}

	// ==================== Public ====================

	/** Called by UAnimNotify_AbilityFire from inside the LoopMontage. */
	void NotifyPerShotFromAnimNotify();

	// ==================== UAbilityHandler overrides ====================

	virtual void OnActivate_Implementation() override;
	virtual void OnCancelRequested_Implementation() override;

protected:

	// ==================== State ====================

	UPROPERTY(BlueprintReadOnly, Category = "Burst|State")
	EBurstPhase Phase = EBurstPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Burst|State")
	int32 ShotsFired = 0;

	UPROPERTY()
	TObjectPtr<UAbilityDefinition_Burst> CachedBurstDef;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveLoopMontage;

	/** Snapshot of stats at activation start; reused for the whole cast so mid-cast level changes
	 *  don't desync iteration count or play rate. */
	FBurstLevelStats CachedStats;

	FTimerHandle TransitionTimerHandle;

	// ==================== Pipeline ====================

	void EnterCastStart();
	void EnterLoopFirst();
	void PlayNextLoopIteration();
	void EnterCastFinish();
	void ResetToIdle();

	void ScheduleOverlapTimer(float MontageNativeLength, float PlayRate, FName CallbackName);

	UFUNCTION()
	void OnCastStartOverlapTimer();

	UFUNCTION()
	void OnLoopOverlapTimer();

	UFUNCTION()
	void OnCastFinishMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};
