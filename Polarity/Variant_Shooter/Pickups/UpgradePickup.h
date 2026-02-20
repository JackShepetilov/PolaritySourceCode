// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UpgradePickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UUpgradeDefinition;
class AShooterCharacter;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

/**
 * World pickup actor for upgrades.
 * Placed manually in levels by the designer.
 * Shows upgrade icon/name as a hologram above the pickup.
 * Player walks into it to collect the upgrade.
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

	// ==================== Upgrade ====================

	/** Which upgrade this pickup grants */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup")
	TObjectPtr<UUpgradeDefinition> UpgradeDefinition;

	// ==================== Visuals ====================

	/** Pickup radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade Pickup", meta = (ClampMin = "50.0", Units = "cm"))
	float PickupRadius = 100.0f;

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

	/** Called when player enters pickup collision */
	UFUNCTION()
	void OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
