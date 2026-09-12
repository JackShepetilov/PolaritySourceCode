// AbilityDefinition_NitroGate.h
// Throw down a gate; whoever walks over it is thrown into a slide.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_NitroGate.generated.h"

class ANitroGate;

USTRUCT(BlueprintType)
struct FNitroGateLevelStats
{
	GENERATED_BODY()

	// ==== Common (mirrored from FAbilityCommonStats) ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	// ==== Ability-specific ====

	/** Speed handed to whoever steps on the pad, replacing whatever they had.
	 *
	 *  Apex's gate gives 750 in its units against a sprint of about 400, so the boost is a bit under
	 *  twice a run. This project sprints at 1150 (MovementSettings::SprintSpeed), and the default
	 *  here is that same ratio rather than a converted number - the feel is "much faster than
	 *  running", and running is not the same speed in the two games. Well under the slide's own
	 *  SpeedCap of 3000, so nothing clips it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "0.0", Units = "cm/s"))
	float BoostSpeed = 2150.0f;

	/** How many of this player's gates may stand at once. Throwing another past this destroys the
	 *  oldest, which is Apex's rule and also the only one that cannot leave a player with no way to
	 *  clear a bad throw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaxGatesInWorld = 2;
};

/**
 * The Tank's active: Axle's Nitro Gate.
 *
 * It replaces the shield-restore active, which was deliberately antisocial. This one is not
 * antisocial, it is indiscriminate - the pad launches whoever stands on it, and the design decision
 * on record is that enemies count. @see ANitroGate::WantsToBoost, which is the one place that
 * changes when the planned upgrade gives enemies a different outcome.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_NitroGate : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	/** The gate thrown. A Blueprint child of ANitroGate in practice: the mesh, the Niagara systems
	 *  and the sounds live there, where they can be seen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
	TSubclassOf<ANitroGate> GateClass;

	/** How hard it is thrown. It falls under gravity from there, so this is also the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ThrowSpeed = 1600.0f;

	/** Radius of the pad on the floor. Level-invariant on purpose: an upgrade that quietly widened
	 *  the trigger would be an upgrade nobody could see. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "20.0", Units = "cm"))
	float PadRadius = 120.0f;

	/** How much damage a gate takes before it goes. Apex's number; shooting your own gate is the
	 *  way to take back a bad throw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "1.0"))
	float GateHealth = 100.0f;

	/** Whether enemies who step on a gate are launched by it too.
	 *
	 *  On, and that is the author's call rather than a default nobody thought about: the gate is a
	 *  piece of terrain and it does not check whose side you are on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
	bool bAffectsEnemies = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate|Levels", meta = (TitleProperty = "BoostSpeed"))
	TArray<FNitroGateLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return Levels.Num(); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	UFUNCTION(BlueprintPure, Category = "Gate|Levels")
	FNitroGateLevelStats GetStatsAtLevel(int32 Level) const;
};
