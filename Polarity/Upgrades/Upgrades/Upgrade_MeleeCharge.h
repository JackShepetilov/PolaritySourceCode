// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "UpgradeDefinition_MeleeCharge.h"
#include "Upgrade_MeleeCharge.generated.h"

class UApexMovementComponent;
class AShooterWeapon;
class AShooterWeapon_Melee;

UCLASS(BlueprintType, meta = (DisplayName = "Melee Charge"))
class POLARITY_API UUpgrade_MeleeCharge : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_MeleeCharge();

	UFUNCTION(BlueprintPure, Category = "Melee Charge")
	bool IsCharging() const { return bIsCharging; }

	UFUNCTION(BlueprintPure, Category = "Melee Charge")
	float GetCooldownRemaining() const { return CooldownRemaining; }

	UFUNCTION(BlueprintPure, Category = "Melee Charge")
	bool HasPendingMeleeBoost() const { return bPendingMeleeBoost; }

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;
	virtual void OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon) override;
	virtual bool OnWeaponSecondaryAction(AShooterWeapon* Weapon) override;
	virtual void OnWeaponSecondaryActionReleased(AShooterWeapon* Weapon) override;
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<UUpgradeDefinition_MeleeCharge> CachedDef;
	TWeakObjectPtr<UApexMovementComponent> CachedMovement;
	TWeakObjectPtr<AShooterWeapon_Melee> BoundMeleeWeapon;

	bool bIsCharging = false;
	bool bPendingMeleeBoost = false;
	bool bHasPreviousChargeLocation = false;

	float ChargeTimeRemaining = 0.0f;
	float CooldownRemaining = 0.0f;
	float MeleeBoostTimeRemaining = 0.0f;
	float BlockedMoveTime = 0.0f;
	float ActiveChargeDuration = 0.0f;
	float SavedGroundFriction = 0.0f;
	float SavedBrakingDecelerationWalking = 0.0f;

	FVector ChargeDirection = FVector::ForwardVector;
	FVector PreviousChargeLocation = FVector::ZeroVector;

	TSet<AActor*> HitActorsThisCharge;

	bool bAppliedMovementOverrides = false;

	void BindMovement();
	void UnbindMovement();
	void BindToMeleeWeapon(AShooterWeapon_Melee* Weapon);
	void UnbindFromMeleeWeapon(AShooterWeapon_Melee* Weapon);

	bool CanStartCharge(AShooterWeapon* Weapon) const;
	bool IsEligibleWeapon(const AShooterWeapon* Weapon) const;
	bool StartCharge(AShooterWeapon* Weapon);
	void EndCharge(bool bInterrupted);
	void ApplyMovementOverrides();
	void RestoreMovementOverrides();
	void ArmMeleeBoost();
	void ConsumeMeleeBoost();

	const FMeleeChargeLevelData& GetCurrentLevelData() const;
	FVector GetDesiredChargeDirection() const;
	void HandlePreVelocityUpdate(float DeltaTime, FVector& InOutVelocity);
	void SweepChargePath(const FVector& Start, const FVector& End);
	void ApplyBashDamage(AActor* Target, const FHitResult& Hit);
	bool IsValidBashTarget(AActor* Actor) const;
	void LogBashHit(const FHitResult& Hit, bool bValidTarget, const TCHAR* Decision) const;
	bool IsActorDeadAfterDamage(AActor* Actor) const;

	UFUNCTION()
	void HandleMeleeWeaponHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);
};
