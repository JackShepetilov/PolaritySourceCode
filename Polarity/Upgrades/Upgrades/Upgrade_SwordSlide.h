// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_SwordSlide.generated.h"

class UApexMovementComponent;

/**
 * Slide-slot upgrade: shots fired while sliding come out of the reserve and leave the magazine
 * alone, up to a share of the magazine per slide. @see FSwordSlideLevelData
 *
 * The weapon asks TryTakeShotFromReserve on the machine that fires (the owning client, or the host
 * for its own character) and does the ammo bookkeeping itself. The per-slide count lives here and
 * is only meaningful on that machine.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Sliding Shooter"))
class POLARITY_API UUpgrade_SwordSlide : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_SwordSlide();

	virtual bool TryTakeShotFromReserve(const AShooterWeapon* Weapon) override;

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;

private:

	/** A new slide starts a new allowance. */
	UFUNCTION()
	void HandleSlideStarted();

	TWeakObjectPtr<UApexMovementComponent> CachedMovement;

	/** Shots taken from the reserve during the current slide. */
	int32 ReserveShotsThisSlide = 0;
};
