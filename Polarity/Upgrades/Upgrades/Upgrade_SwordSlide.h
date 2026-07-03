// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "UpgradeDefinition_SwordSlide.h"
#include "Upgrade_SwordSlide.generated.h"

class UApexMovementComponent;
class AShooterWeapon;

UCLASS(BlueprintType, meta = (DisplayName = "Sword Slide"))
class POLARITY_API UUpgrade_SwordSlide : public UUpgradeComponent
{
	GENERATED_BODY()

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;
	virtual void OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon) override;
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;

private:
	TWeakObjectPtr<UUpgradeDefinition_SwordSlide> CachedDef;
	TWeakObjectPtr<UApexMovementComponent> CachedMovement;

	bool bAppliedSlideOverride = false;

	void BindMovement();
	void ApplySlideOverrideIfEligible();
	void ClearSlideOverride();

	bool IsEligibleWeapon(const AShooterWeapon* Weapon) const;
	bool IsSlidingWithEligibleWeapon() const;
	const FSwordSlideLevelData& GetCurrentLevelData() const;
};
