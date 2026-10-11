// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_MeleeCloak.generated.h"

class UMaterialInterface;
class UMeleeAttackComponent;

/**
 * Hold the melee key: a hold attack comes out by itself when the hold time is up, and if it lands
 * the weapon stays away and the owner is cloaked. @see UUpgradeDefinition_MeleeCloak
 *
 * The cloak itself is the character's (AShooterCharacter::SetCloaked, replicated, read by the AI's
 * sight). The hold itself is the character's too (it asks every upgrade through
 * GetMeleeHoldAttack). This component only decides, on the machine that swung:
 *   - to ask for the cloak when a HOLD attack lands, and to turn the melee mesh see-through from
 *     that moment to the end of the swing, so the player sees themselves going;
 *   - to give the cloak up when a weapon is in the hands, or when the next swing starts. That swing
 *     does no damage (the multiplier below answers zero for it) and shoves as usual.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Melee Cloak"))
class POLARITY_API UUpgrade_MeleeCloak : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_MeleeCloak();

	/** Read by the server when it grants the cloak, so the duration is its own number. */
	float GetCloakDuration() const;

	virtual bool GetMeleeHoldAttack(FMeleeHoldAttack& Out) const override;

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<UMeleeAttackComponent> CachedMeleeComp;

	/** True for the length of a swing that was started from inside the cloak. */
	bool bSwingFromCloak = false;

	/** True once this machine has asked for the running cloak to end, so it asks once. */
	bool bEndRequested = false;

	/** What the melee mesh wore before it went see-through, slot by slot. Empty = not swapped. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> SavedMeleeMaterials;

	void ApplyCloakMaterial();
	void RestoreMeleeMaterials();

	UFUNCTION() void HandleAttackStarted();
	UFUNCTION() void HandleAttackEnded();
	UFUNCTION() void HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);
};
