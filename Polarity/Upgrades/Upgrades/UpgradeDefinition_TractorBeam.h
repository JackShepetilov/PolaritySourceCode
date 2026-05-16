// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_TractorBeam.generated.h"

class UCurveFloat;

/** Per-level tuning for "Tractor Beam" upgrade. */
USTRUCT(BlueprintType)
struct FTractorBeamLevelData
{
	GENERATED_BODY()

	/** Distance to yank the hit target toward the player on each hit (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Pull", meta = (ClampMin = "10.0", ClampMax = "2000.0", Units = "cm"))
	float PullDistance = 200.0f;

	/** Duration of the knockback interpolation that carries the target toward the player (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Pull", meta = (ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float PullDuration = 0.2f;

	/** Beyond this distance the pull is skipped (cm). 0 = no limit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Pull", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxPullDistance = 3000.0f;

	/** Pull-stop mode:
	 *  - true  (Lv1): stop pull when target enters the actual capture range (charge-dependent,
	 *    matches EMFVelocityModifier::CalculateCaptureRange formula) minus CaptureRangeBuffer.
	 *    Hands off to channel-capture — player can then grab the target normally.
	 *  - false (Lv2): stop pull at AbsoluteMinDistance from player. Used when you want pull to
	 *    drag targets all the way in (with a tiny body-contact buffer). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Pull Stop")
	bool bStopAtCaptureRange = true;

	/** Buffer subtracted from the effective capture range. Pull stops at (CaptureRange - this).
	 *  Used when bStopAtCaptureRange = true. Default 50cm — ensures target lands just INSIDE
	 *  capture range so the player can reliably channel-grab. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Pull Stop", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bStopAtCaptureRange"))
	float CaptureRangeBuffer = 50.0f;

	/** Absolute minimum distance from the player (cm). Used when bStopAtCaptureRange = false.
	 *  Default 100cm — keeps the pulled target just outside body-contact so melee combos land. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Pull Stop", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "!bStopAtCaptureRange"))
	float AbsoluteMinDistance = 100.0f;

	/** Melee damage multiplier applied to NPCs hit while currently being pulled by this upgrade.
	 *  1.0 = no bonus. Used to make Lv2's "pull-then-punch" combo hit harder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Combo", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MeleeDamageMultiplier = 1.0f;

	/** Melee knockback distance multiplier applied to NPCs hit while being pulled.
	 *  1.0 = no bonus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Combo", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MeleeKnockbackMultiplier = 1.0f;

	// ==================== Cinematic pull (Lv2 only) ====================
	// When a hit lands on a target ALREADY inside (NPC.CaptureBaseRange - CaptureRangeBuffer),
	// the pull switches to a one-shot cinematic mode: the NPC interpolates precisely onto
	// AbsoluteMinDistance from the player using its own duration + curve. The pull cannot
	// be interrupted — additional hits during it are ignored.

	/** Duration of the cinematic pull (s). Independent of PullDuration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Cinematic", meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s", EditCondition = "!bStopAtCaptureRange"))
	float CinematicPullDuration = 0.5f;

	/** Curve mapping linear time (0..1) → eased position alpha (0..1) for the cinematic pull.
	 *  Y = 0 keeps the NPC at the start point; Y = 1 places it at the end (AbsoluteMinDistance).
	 *  Null = linear interpolation. Use a custom curve for ease-in/ease-out, overshoot, etc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Cinematic", meta = (EditCondition = "!bStopAtCaptureRange"))
	TObjectPtr<UCurveFloat> CinematicPullCurve;
};

/**
 * "Tractor Beam" upgrade definition.
 * Every successful ionization hit (wave pistol or any hitscan with bUseHitscanIonization)
 * yanks the hit NPC a short distance toward the player.
 *
 * Two levels:
 *  - Lv1: pull stops when target is inside player's capture range (minus a small buffer),
 *    handing off to channel-capture instead of slamming into the player.
 *  - Lv2: pull continues even at point-blank (stops at a small body-contact buffer).
 *    NPCs hit by melee while currently being pulled take bonus damage + knockback,
 *    and gain a separate kill-method flag (`bKilledByTractorBeamCombo`).
 *
 * `bClassicMode` is an easter-egg toggle that disables the MinDistance gate entirely —
 * pull always applies regardless of distance, reproducing the original wallslam behaviour.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_TractorBeam : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** Per-level tuning. Index 0 = Lv 1, index 1 = Lv 2. Length auto-synced to MaxLevel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam")
	TArray<FTractorBeamLevelData> LevelData;

	/** EASTER EGG: disable the MinDistanceFromPlayer gate entirely. Pull will always apply,
	 *  including at point-blank — same behaviour as the original (un-gated) implementation.
	 *  Hilarious wallslam kills included. Off by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Easter Egg")
	bool bClassicMode = false;

	/** How long after a pull hit the target is still considered "being pulled" for combo bonuses (s).
	 *  Should be >= max PullDuration across all levels. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tractor Beam|Combo", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	float ComboWindow = 0.4f;

	/** Returns the per-level data clamped to [0, LevelData.Num()-1]. */
	UFUNCTION(BlueprintPure, Category = "Tractor Beam")
	const FTractorBeamLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
