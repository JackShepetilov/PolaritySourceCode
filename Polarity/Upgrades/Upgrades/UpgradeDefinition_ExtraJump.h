// UpgradeDefinition_ExtraJump.h
// Jump-slot upgrade: jumps in the air. Moved out of MovementSettings::MaxJumpCount, which used to
// give every player a double jump for free.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_ExtraJump.generated.h"

USTRUCT(BlueprintType)
struct FExtraJumpLevelData
{
	GENERATED_BODY()

	/** Jumps in the air on top of the ground one. 1 = double jump, 2 = triple jump. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Extra Jump", meta = (ClampMin = "1", ClampMax = "5"))
	int32 ExtraJumps = 1;

	/** Seconds the air jumps stay locked after the first one of a flight. The rest of that flight's
	 *  air jumps are still allowed; landing does not unlock them early. 0 = no cooldown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Extra Jump", meta = (ClampMin = "0.0", Units = "s"))
	float Cooldown = 3.0f;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_ExtraJump : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** One entry per level (index 0 = Lv 1). Length defines MaxLevel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Extra Jump")
	TArray<FExtraJumpLevelData> LevelData;

	/** Data for Level (1-based); the last entry past the end, defaults when empty. */
	const FExtraJumpLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
