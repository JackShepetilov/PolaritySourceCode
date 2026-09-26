// UpgradeDefinition_ChargedJump.h
// Jump-slot upgrade: hold jump on the ground, let go to launch higher. Moved out of the Melee
// passive, where it only worked inside the player's own smoke.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_ChargedJump.generated.h"

class USoundBase;
class UCameraShakeBase;

USTRUCT(BlueprintType)
struct FChargedJumpLevelData
{
	GENERATED_BODY()

	/** Upward speed at a full charge. A charge of nothing gives the ordinary JumpZVelocity, so a
	 *  tap stays an ordinary jump. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxZVelocity = 2000.0f;

	/** How long the charge takes to fill. Holding longer adds nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump", meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s"))
	float ChargeTime = 0.55f;

	/** Horizontal speed added along the direction being pushed at a full charge, scaled with it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ForwardBoost = 400.0f;

	/** Ground speed multiplier while charging. 1 = no slowdown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeMoveScale = 1.0f;

	/** Seconds before another charged jump. Only a jump that came out charged pays it: a tap
	 *  leaves it untouched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump", meta = (ClampMin = "0.0", Units = "s"))
	float Cooldown = 3.0f;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_ChargedJump : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** One entry per level (index 0 = Lv 1). Length defines MaxLevel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump")
	TArray<FChargedJumpLevelData> LevelData;

	// ==================== While charging (owning player only) ====================

	/** Plays from the start of the charge and stops the moment the jump goes (or the charge is
	 *  dropped). Its pitch rises with the charge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump|Feedback")
	TObjectPtr<USoundBase> ChargeSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump|Feedback", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float ChargePitchStart = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump|Feedback", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float ChargePitchFull = 1.3f;

	/** Shake held for the whole charge, restarted if it runs out, scaled up with the charge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump|Feedback")
	TSubclassOf<UCameraShakeBase> ChargeCameraShake;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump|Feedback", meta = (ClampMin = "0.0"))
	float ChargeShakeScaleStart = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charged Jump|Feedback", meta = (ClampMin = "0.0"))
	float ChargeShakeScaleFull = 1.0f;

	/** Data for Level (1-based); the last entry past the end, defaults when empty. */
	const FChargedJumpLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
