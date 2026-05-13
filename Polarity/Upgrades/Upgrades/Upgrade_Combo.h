// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_Combo.generated.h"

class UUpgradeDefinition_Combo;
class UMeleeAttackComponent;
class AShooterWeapon_Melee;
class AShooterWeapon;

/**
 * Delegate fired whenever the combo state changes — count, multiplier, or reset.
 * UI (ShooterBulletCounterUI::OnComboChanged) subscribes to this.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComboChanged, int32, ComboCount, float, SpeedMultiplier);

/**
 * Runtime logic for the Combo upgrade. See UpgradeDefinition_Combo.h for design.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Combo"))
class POLARITY_API UUpgrade_Combo : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_Combo();

	UFUNCTION(BlueprintPure, Category = "Combo")
	int32 GetComboCount() const { return ComboCount; }

	UFUNCTION(BlueprintPure, Category = "Combo")
	float GetCurrentMultiplier() const { return CurrentMultiplier; }

	UPROPERTY(BlueprintAssignable, Category = "Combo")
	FOnComboChanged OnComboChanged;

	/**
	 * Manually increment the combo by Amount (used by ChargedPunch multi-kill etc.).
	 * Each unit is treated like one successful hit and resets the timeout window.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combo")
	void AddComboHits(int32 Amount);

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	TWeakObjectPtr<UUpgradeDefinition_Combo> CachedDef;
	TWeakObjectPtr<UMeleeAttackComponent> CachedMeleeComp;
	TWeakObjectPtr<AShooterWeapon_Melee> CachedMeleeWeapon;

	/** Current combo count (0 when reset). */
	int32 ComboCount = 0;

	/** Current speed multiplier driven into the melee component / weapon. */
	float CurrentMultiplier = 1.0f;

	/** Seconds remaining until timeout reset. Negative = no active timer. */
	float ResetTimer = -1.0f;

	// ==================== Hit/Miss Callbacks ====================

	UFUNCTION()
	void HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);

	UFUNCTION()
	void HandleSwordHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);

	UFUNCTION()
	void HandleMeleeAttackStarted();

	UFUNCTION()
	void HandleMeleeAttackEnded();

	/** True if at least one hit registered during the current fist swing (reset on swing start). */
	bool bHitThisSwing = false;

	// ==================== Apply / Reset ====================

	/** Re-bind to a (possibly new) melee weapon as it's swapped in/out. */
	void BindToMeleeWeapon(AShooterWeapon_Melee* Weapon);
	void UnbindFromMeleeWeapon(AShooterWeapon_Melee* Weapon);

	/** Recompute multiplier from curve at ComboCount, push to melee comp + weapon, broadcast. */
	void ApplyCurrentMultiplier();

	/** Reset combo to 0 and push 1.0x speed multiplier downstream. */
	void ResetCombo();

	/** Evaluate the combo curve (or fallback) at a given count. */
	float EvaluateMultiplier(int32 Count) const;

	/** Cache references and bind delegates on the melee subsystem we found. */
	void BindToMeleeSubsystem();
	void UnbindFromMeleeSubsystem();
};
