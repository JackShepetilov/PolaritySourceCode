// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_PistolStun.generated.h"

class UAnimMontage;
class USoundBase;

/** Per-level tuning for "Pistol Stun" upgrade. */
USTRUCT(BlueprintType)
struct FPistolStunLevelData
{
	GENERATED_BODY()

	/** Stun duration applied to the NPC on each ionization hit (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float StunDuration = 0.1f;

	/** Minimum delay before the same NPC can be stunned again (anti-spam, per-target). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float StunCooldownPerTarget = 0.5f;

	/** Optional stun montage. If null, NPC falls back to its KnockbackMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun")
	TObjectPtr<UAnimMontage> StunMontage = nullptr;

	/** Optional sound played on the NPC when stunned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun")
	TObjectPtr<USoundBase> StunSound = nullptr;

	/** Sound volume multiplier (1.0 = unchanged). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SoundVolume = 1.0f;

	/** Sound pitch multiplier (1.0 = unchanged). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float SoundPitch = 1.0f;
};

/**
 * "Pistol Stun" upgrade definition.
 * Each successful ionization hit (wave pistol or any hitscan with bUseHitscanIonization)
 * applies a short stun to the target NPC. Per-target cooldown prevents stun-lock spam.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_PistolStun : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** Per-level tuning. Index 0 = Lv 1, index 1 = Lv 2. Length auto-synced to MaxLevel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pistol Stun")
	TArray<FPistolStunLevelData> LevelData;

	/** Returns the per-level data clamped to [0, LevelData.Num()-1]. */
	UFUNCTION(BlueprintPure, Category = "Pistol Stun")
	const FPistolStunLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
