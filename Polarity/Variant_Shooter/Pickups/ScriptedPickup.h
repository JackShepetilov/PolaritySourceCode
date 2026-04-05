// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ScriptedPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UUpgradeTooltipWidget;
class AShooterCharacter;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UEMF_FieldComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScriptedPickupCollected, AShooterCharacter*, Player);

/**
 * Generic pickup actor for scripted level moments.
 * Captured via EMF channeling (same pull system as UpgradePickup).
 * No upgrade attached — fires Blueprint-assignable events on collection.
 * Uses the same tooltip widget as UpgradePickup, but tooltip data is set directly.
 */
UCLASS(Blueprintable)
class POLARITY_API AScriptedPickup : public AActor
{
	GENERATED_BODY()

public:

	AScriptedPickup();

	// ==================== Components ====================

	/** Overlap sphere for capture scan detection */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> PickupCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> TooltipTrigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> TooltipWidgetComponent;

	/** EMF field component (charge storage for capture detection) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Events ====================

	/** Fired when a player collects this pickup. Bind in Blueprint or C++. */
	UPROPERTY(BlueprintAssignable, Category = "Scripted Pickup")
	FOnScriptedPickupCollected OnPickedUp;

	/** Blueprint-overridable event on collection */
	UFUNCTION(BlueprintImplementableEvent, Category = "Scripted Pickup", meta = (DisplayName = "On Picked Up"))
	void BP_OnPickedUp(AShooterCharacter* Player);

	// ==================== Tooltip Data ====================

	/** Display name shown in tooltip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip")
	FText DisplayName;

	/** Description shown in tooltip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip")
	FText Description;

	/** Icon shown in tooltip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip")
	TObjectPtr<UTexture2D> Icon;

	/** Tier number shown in tooltip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip", meta = (ClampMin = "0"))
	int32 Tier = 0;

	/** Whether the tooltip trigger is active (can be toggled at runtime) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip")
	bool bTooltipEnabled = true;

	/** Widget class for the tooltip (same Blueprint as UpgradePickup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip")
	TSubclassOf<UUpgradeTooltipWidget> TooltipWidgetClass;

	/** Vertical offset above the mesh for tooltip (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip", meta = (Units = "cm"))
	float TooltipHeight = 80.0f;

	/** Radius at which tooltip becomes visible */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Tooltip", meta = (ClampMin = "100.0", Units = "cm"))
	float TooltipRadius = 400.0f;

	// ==================== Pickup Settings ====================

	/** Overlap radius for capture scan detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup", meta = (ClampMin = "50.0", Units = "cm"))
	float PickupRadius = 100.0f;

	/** Destroy self after being collected? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup")
	bool bDestroyOnPickup = true;

	/** Can be captured by channeling? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	// ==================== Capture Settings ====================

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

	// ==================== Visuals ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Effects")
	TObjectPtr<UNiagaraSystem> IdleVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Effects")
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Effects", meta = (ClampMin = "0.0"))
	float RotationSpeed = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Effects", meta = (ClampMin = "0.0"))
	float BobAmplitude = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scripted Pickup|Effects", meta = (ClampMin = "0.0"))
	float BobFrequency = 1.0f;

	// ==================== Public API ====================

	/** Get current charge */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Set charge directly */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Calculate effective capture range (logarithmic formula) */
	float CalculateCaptureRange() const;

	/** Begin scripted pull toward camera-relative target */
	void StartPull(AShooterCharacter* PullingPlayer);

	/** Is currently being pulled toward player? */
	bool IsBeingPulled() const { return bIsBeingPulled; }

	/** Has pull completed? */
	bool IsPullComplete() const { return bPullComplete; }

	/** Enable/disable tooltip at runtime */
	UFUNCTION(BlueprintCallable, Category = "Scripted Pickup")
	void SetTooltipEnabled(bool bEnabled);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> IdleVFXComponent;

	float InitialMeshZ = 0.0f;
	float BobTime = 0.0f;

	// ==================== Pull State ====================

	bool bIsBeingPulled = false;
	bool bPullComplete = false;
	float PullElapsed = 0.0f;
	FVector PullStartLocation = FVector::ZeroVector;
	FRotator PullStartRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> PullingCharacter;

	void UpdatePull(float DeltaTime);
	void CompletePull();

	UFUNCTION()
	void OnTooltipBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTooltipEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
