// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_KillBonus.generated.h"

/** What a kill pays. One asset per kind; they stack as separate upgrades. */
UENUM(BlueprintType)
enum class EKillBonusKind : uint8
{
	/** Reload faster for a while. Magnitude = speed multiplier (1.5 = half again as fast). */
	ReloadSpeed,

	/** Regenerate health for a while, cut short by taking damage. Magnitude = health per second. */
	HealthRegen,

	/** Take a share of the ability cooldown off at once. Magnitude = fraction of the FULL cooldown
	 *  (0.2 = a fifth). Duration is not used. */
	AbilityCooldown,
};

USTRUCT(BlueprintType)
struct FKillBonusLevelData
{
	GENERATED_BODY()

	/** Meaning depends on the kind. @see EKillBonusKind */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kill Bonus", meta = (ClampMin = "0.0"))
	float Magnitude = 1.5f;

	/** How long the bonus runs after the kill. A kill while it runs starts the time over. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kill Bonus", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 4.0f;
};

/**
 * A passive that pays out on a kill. These replace the flat always-on passives: the numbers a player
 * carries barely grow over a run, so a passive is something earned in the fight, not worn.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_KillBonus : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kill Bonus")
	EKillBonusKind Kind = EKillBonusKind::ReloadSpeed;

	/** Per-level tuning. Index 0 = level 1. MaxLevel on parent should equal array size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kill Bonus|Levels")
	TArray<FKillBonusLevelData> LevelData;

	const FKillBonusLevelData& GetLevelData(int32 Level) const
	{
		if (LevelData.Num() == 0)
		{
			static const FKillBonusLevelData Default;
			return Default;
		}
		return LevelData[FMath::Clamp(Level - 1, 0, LevelData.Num() - 1)];
	}
};
