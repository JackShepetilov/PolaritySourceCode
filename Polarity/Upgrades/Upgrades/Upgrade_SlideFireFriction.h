// Upgrade_SlideFireFriction.h

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_SlideFireFriction.generated.h"

class UApexMovementComponent;

/**
 * Slide-slot upgrade: less slide friction while the owner fires without a break.
 * @see FSlideFireFrictionLevelData
 *
 * Two halves on purpose. The streak is decided HERE, on the owning client only, because that is the
 * one machine that runs Fire() for its own gun: every shot turns it on and a timer one refire window
 * long turns it off unless the next shot comes first. It reaches the server as a move flag
 * (EPolarityMoveFlag::SlideFireStreak). What the streak is worth is GetSlideFireFrictionScale, a
 * pure read of the level data that the movement asks on every machine that simulates the move.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Slide Fire Friction"))
class POLARITY_API UUpgrade_SlideFireFriction : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_SlideFireFriction();

	virtual float GetSlideFireFrictionScale() const override;

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnWeaponFired() override;

private:

	void EndStreak();

	TWeakObjectPtr<UApexMovementComponent> CachedMovement;

	FTimerHandle StreakTimer;
};
