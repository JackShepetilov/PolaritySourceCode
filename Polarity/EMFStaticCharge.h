// EMFStaticCharge.h
// Static point charge actor spawned by ChargeLauncher ability
// Participates in EMF field system, has HP, can be destroyed

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EMFStaticCharge.generated.h"

class UEMF_FieldComponent;
class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStaticChargeDeath, AEMFStaticCharge*, Charge, AActor*, Killer);

UCLASS(Blueprintable)
class POLARITY_API AEMFStaticCharge : public AActor
{
	GENERATED_BODY()

public:
	AEMFStaticCharge();

	// ==================== Components ====================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Collision sphere for damage reception */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	/** EMF field component configured as PointCharge */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== EMF Settings ====================

	/** Default charge (overridden at spawn by ChargeLauncher) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF")
	float DefaultCharge = 10.0f;

	/** Mass for EMF force calculations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF", meta = (ClampMin = "0.1"))
	float DefaultMass = 5.0f;

	// ==================== Health ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHP = 50.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float CurrentHP = 50.0f;

	// ==================== Collision ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "5.0"))
	float CollisionRadius = 30.0f;

	// ==================== VFX (assign in Blueprint) ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> PositiveChargeVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> NegativeChargeVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> DeathVFX;

	// ==================== SFX (assign in Blueprint) ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX")
	TObjectPtr<USoundBase> AmbientLoopSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX")
	TObjectPtr<USoundBase> DeathSound;

	// ==================== Lifetime ====================

	/** Maximum lifetime in seconds (0 = infinite) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "0.0"))
	float MaxLifetime = 0.0f;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnStaticChargeDeath OnStaticChargeDeath;

	// ==================== Public API ====================

	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthPercent() const { return MaxHP > 0.f ? CurrentHP / MaxHP : 0.f; }

	// ==================== AActor Overrides ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool bIsDead = false;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveVFXComponent;

	UPROPERTY()
	TObjectPtr<UAudioComponent> AmbientAudioComponent;

	void Die(AActor* Killer);
	void SpawnChargeVFX();
};
