// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_AdsTimeDilation.generated.h"

class USoundBase;
class UNiagaraSystem;

/**
 * Per-level tuning for the "ADS Time Dilation" upgrade.
 * All balance numbers live here so a designer can scale them per level.
 */
USTRUCT(BlueprintType)
struct FAdsTimeDilationLevelData
{
	GENERATED_BODY()

	/** Stored health pickups drained per REAL second while the state is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float PickupsPerSecond = 2.0f;

	/** Global time dilation while active (1.0 = normal, 0.4 = 40% world speed). The whole world slows;
	 *  the player is sped back up by PlayerTimeCompensation so aiming stays responsive. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float GlobalTimeDilation = 0.4f;

	/** How much the player is compensated against the global slow.
	 *  1.0 = player runs at real time (only the world is slow), 0.0 = player as slow as the world. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerTimeCompensation = 0.9f;

	/** Recoil multiplier applied to the equipped weapon while active (1.0 = normal, 0.3 = 70% less). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoilMultiplier = 0.3f;

	/** Minimum stored pickups required to ENTER the state (it then stays until the pool empties). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation", meta = (ClampMin = "1", ClampMax = "99"))
	int32 MinPickupsToActivate = 1;
};

/**
 * Data asset for the "ADS Time Dilation" upgrade (technical placeholder name — rename later).
 *
 * Activates while the player aims down sights (IsAiming) WHILE airborne (IsFalling). While active:
 *   - global time is dilated (the whole world slows), the player compensated back toward real time,
 *   - the equipped weapon's recoil is reduced,
 *   - the shared stored-health-pickup pool drains at PickupsPerSecond (real seconds).
 * Ends when the player lands, stops aiming, dies, or the pool empties.
 *
 * Shares the stored-health-pickup pool with HealthBlast / ChargedPunch — set MutuallyExclusiveWith
 * to their tags in the editor so only one pool consumer can be owned at a time.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AdsTimeDilation : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	UUpgradeDefinition_AdsTimeDilation();

	/** Per-level tuning. Index 0 = Lv 1, index 1 = Lv 2, ... Length = MaxLevel (auto-synced). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation")
	TArray<FAdsTimeDilationLevelData> LevelData;

	// ==================== Feel (shared across levels) ====================

	/** Seconds to ramp INTO the slow-mo (smooths time/recoil/fx). 0 = instant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation|Feel", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BlendInTime = 0.15f;

	/** Seconds to ramp OUT of the slow-mo. 0 = instant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation|Feel", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BlendOutTime = 0.25f;

	// ==================== Cosmetic ====================

	/** One-shot sound on entering the state. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation|FX")
	TObjectPtr<USoundBase> ActivateSound;

	/** Looping sound played while active (attached to the player, stopped on exit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation|FX")
	TObjectPtr<USoundBase> LoopSound;

	/** One-shot sound on leaving the state. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation|FX")
	TObjectPtr<USoundBase> DeactivateSound;

	/** VFX attached to the player while active (e.g. a screen-edge / aura effect). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AdsTimeDilation|FX")
	TObjectPtr<UNiagaraSystem> ActiveVFX;

	/** Per-level tuning lookup (clamps to the valid range; safe fallback if LevelData is empty). */
	UFUNCTION(BlueprintPure, Category = "AdsTimeDilation")
	const FAdsTimeDilationLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
