// Upgrade_ChargedJump.h

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_ChargedJump.generated.h"

class UAudioComponent;
class UCameraShakeBase;

/**
 * Jump-slot upgrade: hold jump on the ground, let go to launch higher.
 *
 * The jump itself lives in the movement simulation (UApexMovementComponent's charged jump), which
 * reads the numbers through GetJumpSlotParams. What this component does on its own is local and
 * cosmetic: on the owning player's machine it plays the charge sound and holds a camera shake for
 * as long as the charge lasts, so the player knows the jump is coming out higher.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Charged Jump"))
class POLARITY_API UUpgrade_ChargedJump : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_ChargedJump();

	virtual bool GetJumpSlotParams(FJumpSlotParams& Out) const override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void OnUpgradeDeactivated() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:

	void UpdateFeedback(float Alpha);
	void StopFeedback();

	TWeakObjectPtr<UAudioComponent> ChargeAudio;
	TWeakObjectPtr<UCameraShakeBase> ChargeShake;

	/** True from the first charging frame to the end of that charge: the sound is started once per
	 *  charge, not again every frame after a one-shot sound has finished. */
	bool bFeedbackActive = false;
};
