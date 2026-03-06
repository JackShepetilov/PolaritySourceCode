// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UpgradePickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UUpgradeDefinition;
class UUpgradeTooltipWidget;
class AShooterCharacter;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UEMF_FieldComponent;

/**
 * World pickup actor for upgrades.
 * Placed manually in levels by the designer.
 * Shows upgrade icon/name as a hologram above the pickup.
 * Player captures it via EMF channeling (scripted pull to camera-relative point),
 * then the upgrade is granted.
 */
UCLASS(Blueprintable)
class POLARITY_API AUpgradePickup : public AActor
{
	GENERATED_BODY()

public:

	AUpgradePickup();

	// ==================== Components ====================

	/** Overlap sphere for actual pickup */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> PickupCollision;

	/** Visual mesh (base platform/crystal/etc) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Overlap sphere that triggers tooltip visibility */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> TooltipTrigger;

	/** World-space widget showing upgrade info */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> TooltipWidgetComponent;

	/** EMF field component (charge storage for capture detection) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Upgrade ====================

	/** Which upgrade this pickup grants */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup")
	TObjectPtr<UUpgradeDefinition> UpgradeDefinition;

	// ==================== Visuals ====================

	/** Pickup radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup", meta = (ClampMin = "50.0", Units = "cm"))
	float PickupRadius = 100.0f;

	/** Radius at which the tooltip becomes visible (must be larger than PickupRadius) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Tooltip", meta = (ClampMin = "100.0", Units = "cm"))
	float TooltipRadius = 400.0f;

	/** Widget class for the tooltip (Blueprint inheriting UUpgradeTooltipWidget) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Tooltip")
	TSubclassOf<UUpgradeTooltipWidget> TooltipWidgetClass;

	/** Vertical offset above the mesh where the tooltip appears (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Tooltip", meta = (Units = "cm"))
	float TooltipHeight = 80.0f;

	/** Idle VFX (looping particles around the pickup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Effects")
	TObjectPtr<UNiagaraSystem> IdleVFX;

	/** VFX played on pickup */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	/** Sound to play when picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Effects")
	TObjectPtr<USoundBase> PickupSound;

	/** Rotation speed for the mesh (degrees per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Effects", meta = (ClampMin = "0.0"))
	float RotationSpeed = 90.0f;

	/** Vertical bob amplitude (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Effects", meta = (ClampMin = "0.0"))
	float BobAmplitude = 10.0f;

	/** Vertical bob frequency (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup|Effects", meta = (ClampMin = "0.0"))
	float BobFrequency = 1.0f;

	// ==================== Capture Settings ====================

	/** Can be captured by channeling? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	/** Base capture range (cm). Actual range = BaseRange * max(1, 1 + ln(|q_player * q_pickup| / NormCoeff)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "50.0", Units = "cm"))
	float CaptureBaseRange = 500.0f;

	/** Charge normalization coefficient for capture range formula */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float CaptureChargeNormCoeff = 50.0f;

	// ==================== Pull Settings ====================

	/** Camera-relative offset where the pickup flies to during pull (X=forward, Y=right, Z=up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FVector PullTargetOffset = FVector(60.0f, 0.0f, -10.0f);

	/** Target rotation at pull end (relative to camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FRotator PullTargetRotation = FRotator::ZeroRotator;

	/** Duration of the pull interpolation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PullDuration = 0.4f;

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

	/** Has pull completed (upgrade granted)? */
	bool IsPullComplete() const { return bPullComplete; }

	// ==================== Blueprint Events ====================

	/** Called when upgrade is successfully picked up */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Pickup", meta = (DisplayName = "On Upgrade Picked Up"))
	void BP_OnUpgradePickedUp(AShooterCharacter* Player);

	/** Called when player touches but already has this upgrade */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Pickup", meta = (DisplayName = "On Upgrade Already Owned"))
	void BP_OnUpgradeAlreadyOwned(AShooterCharacter* Player);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:

	/** Idle VFX component instance */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> IdleVFXComponent;

	/** Initial Z location for bob effect */
	float InitialMeshZ = 0.0f;

	/** Time accumulator for bob */
	float BobTime = 0.0f;

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

	/** Called when pull interpolation completes: hide self, grant upgrade */
	void CompletePull();

	/** Called when player enters tooltip range */
	UFUNCTION()
	void OnTooltipBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Called when player leaves tooltip range */
	UFUNCTION()
	void OnTooltipEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
