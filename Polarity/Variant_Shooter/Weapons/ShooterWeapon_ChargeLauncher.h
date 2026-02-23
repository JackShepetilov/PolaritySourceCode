// ShooterWeapon_ChargeLauncher.h
// Weapon subclass that overrides ADS with a charge ability:
// Hold ADS → charge up (sway + sound + consume charge/sec)
// Release → spawn static EMF charge at SpawnDistance from muzzle

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_ChargeLauncher.generated.h"

class AEMFStaticCharge;
class UEMFVelocityModifier;
class UAudioComponent;
class UWeaponRecoilComponent;

UCLASS()
class POLARITY_API AShooterWeapon_ChargeLauncher : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterWeapon_ChargeLauncher();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Secondary Action Overrides ====================

	virtual bool OnSecondaryAction() override;
	virtual void OnSecondaryActionReleased() override;

	// ==================== Charge Ability Settings ====================

	/** Charge consumed per second while holding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability", meta = (ClampMin = "0.1"))
	float ChargeConsumedPerSecond = 5.0f;

	/** Minimum hold time to spawn the static charge (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float MinHoldTime = 0.5f;

	/** Distance from muzzle to spawn the static charge (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability", meta = (ClampMin = "10.0"))
	float SpawnDistance = 100.0f;

	/** Sway multiplier applied during charging (higher = shakier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float ChargingSwayMultiplier = 4.0f;

	/** Static charge actor class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability")
	TSubclassOf<AEMFStaticCharge> StaticChargeClass;

	// ==================== Charge Ability SFX ====================

	/** Looping sound while charging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability|SFX")
	TObjectPtr<USoundBase> ChargingLoopSound;

	/** Sound on successful charge release */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability|SFX")
	TObjectPtr<USoundBase> ChargeReleaseSound;

	/** Sound on cancelled charge (below minimum hold time) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge Ability|SFX")
	TObjectPtr<USoundBase> ChargeCancelSound;

private:
	// ==================== Charge Ability State ====================

	bool bIsCharging = false;
	float ChargeStartTime = 0.0f;
	float AccumulatedCharge = 0.0f;

	UPROPERTY()
	TObjectPtr<UEMFVelocityModifier> CachedEMFMod;

	UPROPERTY()
	TObjectPtr<UWeaponRecoilComponent> CachedRecoilComp;

	UPROPERTY()
	TObjectPtr<UAudioComponent> ChargingAudioComponent;

	// ==================== Internal Methods ====================

	void StartCharging();
	void StopCharging(bool bAutoRelease = false);
	void CancelCharge();
	void SpawnStaticCharge();
	void UpdateCharging(float DeltaTime);
};
