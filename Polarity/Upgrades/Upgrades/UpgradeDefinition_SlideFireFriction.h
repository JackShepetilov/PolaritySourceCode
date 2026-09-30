// UpgradeDefinition_SlideFireFriction.h
// Slide-slot upgrade: while the owner keeps firing, the slide loses less speed to friction.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_SlideFireFriction.generated.h"

USTRUCT(BlueprintType)
struct FSlideFireFrictionLevelData
{
	GENERATED_BODY()

	/** Multiplier on the slide's flat-ground friction while firing without a break. 0.6 = 40% less
	 *  friction. Slopes are not touched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide Fire Friction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FrictionScale = 0.6f;

	/** "Without a break" means the next shot comes within RefireRate * (1 + RefireSlack) of the last
	 *  one. 0.25 on a gun with 0.1 s between shots gives 0.125 s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide Fire Friction", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float RefireSlack = 0.25f;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_SlideFireFriction : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** One entry per level (index 0 = Lv 1). Length defines MaxLevel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide Fire Friction")
	TArray<FSlideFireFrictionLevelData> LevelData;

	/** Data for Level (1-based); the last entry past the end, defaults when empty. */
	const FSlideFireFrictionLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
