// AbilityPickup.h
// World pickup that grants an ability when captured via channeling pull.
// Mirrors AUpgradePickup's capture/pull flow but routes the grant to UAbilityComponent.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilityPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UAbilityDefinition;
class AShooterCharacter;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UEMF_FieldComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityPickupCollected, AShooterCharacter*, Player);

/**
 * World pickup actor for abilities. Placed manually in levels.
 * Player captures it via EMF channeling pull; on completion the UAbilityDefinition
 * is added to the player's UAbilityComponent.
 */
UCLASS(Blueprintable)
class POLARITY_API AAbilityPickup : public AActor
{
	GENERATED_BODY()

public:

	AAbilityPickup();

	// ==================== Components ====================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> PickupCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** EMF field component (charge storage for capture detection — mirrors UpgradePickup). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Ability ====================

	/** Which ability this pickup grants */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup")
	TObjectPtr<UAbilityDefinition> AbilityDefinition;

	/** Level granted (1-based). If player already has the ability at >= this level the pickup
	 *  is treated as a no-op (BP_OnAbilityAlreadyOwned fires). At a lower level, picking this up
	 *  upgrades the ability in place. Clamped to Definition->GetMaxLevel() at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup", meta = (ClampMin = "1"))
	int32 GrantedLevel = 1;

	// ==================== Visuals ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup", meta = (ClampMin = "50.0", Units = "cm"))
	float PickupRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup|Effects")
	TObjectPtr<UNiagaraSystem> IdleVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup|Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup|Effects")
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup|Effects", meta = (ClampMin = "0.0"))
	float RotationSpeed = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup|Effects", meta = (ClampMin = "0.0"))
	float BobAmplitude = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Pickup|Effects", meta = (ClampMin = "0.0"))
	float BobFrequency = 1.0f;

	// ==================== Capture Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	/** Base capture range (cm). Same logarithmic charge formula as UpgradePickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "50.0", Units = "cm"))
	float CaptureBaseRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float CaptureChargeNormCoeff = 50.0f;

	// ==================== Pull Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FVector PullTargetOffset = FVector(60.0f, 0.0f, -10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FRotator PullTargetRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PullDuration = 0.4f;

	// ==================== Public API ====================

	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	float CalculateCaptureRange() const;

	void StartPull(AShooterCharacter* PullingPlayer);

	bool IsBeingPulled() const { return bIsBeingPulled; }
	bool IsPullComplete() const { return bPullComplete; }

	UPROPERTY(BlueprintAssignable, Category = "Ability Pickup")
	FOnAbilityPickupCollected OnPickedUp;

	UFUNCTION(BlueprintImplementableEvent, Category = "Ability Pickup", meta = (DisplayName = "On Ability Picked Up"))
	void BP_OnAbilityPickedUp(AShooterCharacter* Player);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ability Pickup", meta = (DisplayName = "On Ability Already Owned"))
	void BP_OnAbilityAlreadyOwned(AShooterCharacter* Player);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> IdleVFXComponent;

	float InitialMeshZ = 0.0f;
	float BobTime = 0.0f;

	bool bIsBeingPulled = false;
	bool bPullComplete = false;
	float PullElapsed = 0.0f;
	FVector PullStartLocation = FVector::ZeroVector;
	FRotator PullStartRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> PullingCharacter;

	void UpdatePull(float DeltaTime);
	void CompletePull();
};
