// DroppedMeleeWeapon.h
// World actor for a melee weapon dropped by an NPC on death.
// Player captures it via EMF channeling (scripted pull to camera-relative point),
// then it equips as a ShooterWeapon_Melee with limited hit count.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/Abilities/GrappleFetchable.h"
#include "DroppedMeleeWeapon.generated.h"

class UStaticMeshComponent;
class UEMF_FieldComponent;
class AShooterWeapon_Melee;
class AShooterCharacter;
class UGeometryCollection;
class USoundBase;

UCLASS(Blueprintable)
class POLARITY_API ADroppedMeleeWeapon : public AActor, public IGrappleFetchable
{
	GENERATED_BODY()

public:
	ADroppedMeleeWeapon();

	// ==================== Components ====================

	/** Visible sword mesh — root, physics-simulated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** EMF field component (charge storage for capture detection) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Weapon Data ====================

	/** Weapon class to grant when player captures this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<AShooterWeapon_Melee> MeleeWeaponClass;

	/** Hit count for the granted weapon (0 = infinite) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	int32 GrantedHitCount = 5;

	// ==================== Capture Settings ====================

	/** Can be captured by channeling? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

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

	// ==================== Destruction (weapon break shatter) ====================

	/** Geometry Collection for shatter effect when weapon breaks in player's hand */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction")
	TObjectPtr<UGeometryCollection> BreakGeometryCollection;

	/** Radial impulse for break shatter (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0"))
	float BreakImpulse = 600.0f;

	/** Angular impulse for tumbling gibs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0"))
	float BreakAngularImpulse = 100.0f;

	/** How long gib pieces persist (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float BreakGibLifetime = 3.0f;

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

	/** Calculate effective capture range (logarithmic, same formula as EMFPhysicsProp) */
	float CalculateCaptureRange() const;

	/** Begin scripted pull toward camera-relative target */
	void StartPull(AShooterCharacter* PullingPlayer);

	/** Is currently being pulled toward player? */
	bool IsBeingPulled() const { return bIsBeingPulled; }

	// ==================== IGrappleFetchable ====================
	// Taken with the grapple, not the channeled yank (the author's call, 2026-09-25): the yank
	// keeps enemies, props and teammates.
	virtual bool CanBeGrappleFetchedBy(const AShooterCharacter* Caster) const override
	{
		return Caster && bCanBeCaptured && !bIsBeingPulled && !bPullComplete && !IsHidden();
	}
	virtual bool BeginGrappleFetchPull(AShooterCharacter* Caster) override { StartPull(Caster); return bIsBeingPulled; }
	virtual bool IsGrappleFetchInFlight() const override { return bIsBeingPulled; }
	virtual bool IsGrappleFetchDone() const override { return bPullComplete || IsHidden(); }

	/** Has pull completed (weapon granted)? */
	bool IsPullComplete() const { return bPullComplete; }

	/** Spawn break geometry collection at a given transform.
	 *  Called by ShooterWeapon_Melee when weapon breaks. */
	void SpawnBreakGC(const FTransform& BreakTransform);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
