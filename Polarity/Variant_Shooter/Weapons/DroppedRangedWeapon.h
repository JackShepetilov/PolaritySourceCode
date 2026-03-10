// DroppedRangedWeapon.h
// World actor for a ranged weapon dropped by an NPC on death.
// Player captures it via EMF channeling (scripted pull to camera-relative point),
// then it equips as a permanent ShooterWeapon.
// Works identically to DroppedMeleeWeapon but grants regular weapons.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroppedRangedWeapon.generated.h"

class UStaticMeshComponent;
class UEMF_FieldComponent;
class AShooterWeapon;
class AShooterCharacter;
class USoundBase;

UCLASS(Blueprintable)
class POLARITY_API ADroppedRangedWeapon : public AActor
{
	GENERATED_BODY()

public:
	ADroppedRangedWeapon();

	// ==================== Components ====================

	/** Visible weapon mesh — root, physics-simulated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** EMF field component (charge storage for capture detection) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Weapon Data ====================

	/** Weapon class to grant when player captures this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	// ==================== Capture Settings ====================

	/** Can be captured by channeling? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	/** Base capture range (cm). Actual range = BaseRange * max(1, 1 + ln(|q_player * q_weapon| / NormCoeff)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "50.0", Units = "cm"))
	float CaptureBaseRange = 500.0f;

	/** Charge normalization coefficient for capture range formula */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float CaptureChargeNormCoeff = 50.0f;

	// ==================== Pull Settings ====================

	/** Camera-relative offset where the weapon flies to during pull (X=forward, Y=right, Z=up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FVector PullTargetOffset = FVector(60.0f, 10.0f, -15.0f);

	/** Target rotation at pull end (relative to camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FRotator PullTargetRotation = FRotator::ZeroRotator;

	/** Duration of the pull interpolation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PullDuration = 0.4f;

	// ==================== Effects ====================

	/** Sound played when weapon is picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<USoundBase> PickupSound;

	// ==================== Public API ====================

	/** Get current charge */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Set charge directly */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Calculate effective capture range (logarithmic, same formula as DroppedMeleeWeapon) */
	float CalculateCaptureRange() const;

	/** Begin scripted pull toward camera-relative target */
	void StartPull(AShooterCharacter* PullingPlayer);

	/** Is currently being pulled toward player? */
	bool IsBeingPulled() const { return bIsBeingPulled; }

	/** Has pull completed (weapon granted)? */
	bool IsPullComplete() const { return bPullComplete; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// ==================== Pull State ====================

	bool bIsBeingPulled = false;
	bool bPullComplete = false;
	float PullElapsed = 0.0f;
	FVector PullStartLocation = FVector::ZeroVector;
	FRotator PullStartRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> PullingCharacter;

	/** Update pull interpolation each tick */
	void UpdatePull(float DeltaTime);

	/** Called when pull interpolation completes: hide self, grant weapon */
	void CompletePull();
};
