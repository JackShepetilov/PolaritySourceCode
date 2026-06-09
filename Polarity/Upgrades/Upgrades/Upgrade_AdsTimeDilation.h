// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_AdsTimeDilation.generated.h"

class UUpgradeDefinition_AdsTimeDilation;
class UUpgradeManagerComponent;
class UWeaponRecoilComponent;
class UAudioComponent;
class UNiagaraComponent;

/**
 * Runtime logic for the "ADS Time Dilation" upgrade. See UpgradeDefinition_AdsTimeDilation.h.
 *
 * Poll-based: there is no ADS delegate on AShooterCharacter, so this component ticks and engages
 * when IsAiming() && CharacterMovement->IsFalling() and the shared pool has >= MinPickupsToActivate.
 * While engaged it dilates global time, compensates the player back up, reduces weapon recoil, and
 * drains the shared health-pickup pool in real seconds. It disengages (and blends out) when the
 * player lands, lowers their sights, dies, or the pool empties.
 *
 * Global time dilation is only written while the effect is non-neutral, so it does not fight other
 * systems that drive global time (e.g. HitMarkerComponent's kill slow-mo) when idle.
 */
UCLASS(BlueprintType, meta = (DisplayName = "ADS Time Dilation"))
class POLARITY_API UUpgrade_AdsTimeDilation : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_AdsTimeDilation();

	/** True while the slow-mo state is engaged (conditions met + draining the pool). */
	UFUNCTION(BlueprintPure, Category = "AdsTimeDilation")
	bool IsSlowMoActive() const { return bEngaged; }

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	TWeakObjectPtr<UUpgradeManagerComponent> CachedUpgradeManager;
	TWeakObjectPtr<UWeaponRecoilComponent> CachedRecoilComp;

	/** Attached loop SFX while active. */
	UPROPERTY()
	TObjectPtr<UAudioComponent> ActiveLoopSound;

	/** Attached VFX while active. */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveVFXComp;

	/** True while conditions hold and the pool isn't empty. */
	bool bEngaged = false;

	/** 0..1 blend of the slow-mo effect (time dilation, recoil, fx). */
	float BlendAlpha = 0.0f;

	/** True while we are actively writing global/player time dilation — gates RestoreNeutral so we
	 *  don't stomp other time-dilation systems when this upgrade is idle. */
	bool bTimeApplied = false;

	/** Fractional pickup-drain accumulator (consume whole pickups once it exceeds 1.0). */
	float DrainAccumulator = 0.0f;

	const UUpgradeDefinition_AdsTimeDilation* GetDef() const;

	/** Apply time dilation + recoil reduction for the current blend alpha. */
	void ApplyForAlpha(float Alpha);

	/** Reset global time, player time, and recoil to neutral (idempotent; only touches global time if applied). */
	void RestoreNeutral();

	void StartCosmetics();
	void StopCosmetics();
};
