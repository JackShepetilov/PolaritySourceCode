// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_MeleeCloak.generated.h"

class UMeleeAttackComponent;

/**
 * Hold the melee key through a swing that lands: the weapon stays away and the owner is cloaked.
 * @see UUpgradeDefinition_MeleeCloak
 *
 * The cloak itself is the character's (AShooterCharacter::SetCloaked, replicated, read by the AI's
 * sight). This component only decides when to ask for it and when to give it up, on the machine
 * that holds the key:
 *   - asks when a swing lands while the key is down;
 *   - gives it up when a weapon comes out, or when the next swing starts. That swing does no
 *     damage (the multiplier below answers zero for it) and shoves as usual.
 *
 * There is no dedicated attack animation yet: the held swing is the ordinary one.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Melee Cloak"))
class POLARITY_API UUpgrade_MeleeCloak : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_MeleeCloak();

	/** Read by the server when it grants the cloak, so the duration is its own number. */
	float GetCloakDuration() const;

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<UMeleeAttackComponent> CachedMeleeComp;

	/** True while the melee key is down on this machine. */
	bool bKeyHeld = false;

	/** True for the length of a swing that was started from inside the cloak. */
	bool bSwingFromCloak = false;

	/** True once this machine has asked for the running cloak to end, so it asks once. */
	bool bEndRequested = false;

	UFUNCTION() void HandleHoldStarted();
	UFUNCTION() void HandleHoldReleased();
	UFUNCTION() void HandleAttackStarted();
	UFUNCTION() void HandleAttackEnded();
	UFUNCTION() void HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);
};
