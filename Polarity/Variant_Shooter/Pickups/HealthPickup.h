// HealthPickup.h
// HP pickup that spawns on non-weapon NPC kills and magnetically flies to the player

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HealthPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class AShooterCharacter;
class UNiagaraSystem;
class USoundBase;
class UDamageType;

/**
 * Health pickup dropped by NPCs killed with non-weapon damage.
 * Sits at spawn location, then magnetically flies toward the player
 * when they enter MagnetRadius. Restores HP on contact.
 */
UCLASS(Blueprintable)
class POLARITY_API AHealthPickup : public AActor
{
	GENERATED_BODY()

public:

	AHealthPickup();

	// ==================== Components ====================

	/** Overlap sphere for actual pickup (small radius) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> PickupCollision;

	/** Overlap sphere for magnet attraction trigger (large radius) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> MagnetTrigger;

	/** Visual mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// ==================== Settings ====================

	/** Amount of HP to restore on pickup */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup", meta = (ClampMin = "1.0"))
	float HealAmount = 25.0f;

	/** Radius at which pickup starts flying toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup|Magnet", meta = (ClampMin = "50.0", Units = "cm"))
	float MagnetRadius = 500.0f;

	/** Maximum speed when flying toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup|Magnet", meta = (ClampMin = "100.0"))
	float MagnetSpeed = 1500.0f;

	/** Acceleration when flying toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup|Magnet", meta = (ClampMin = "100.0"))
	float MagnetAcceleration = 3000.0f;

	/** Time before pickup disappears if not collected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup", meta = (ClampMin = "1.0", Units = "s"))
	float Lifetime = 15.0f;

	/** Sound to play when picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup|Effects")
	TObjectPtr<USoundBase> PickupSound;

	/** VFX to spawn when picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health Pickup|Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	// ==================== Static Helpers ====================

	/**
	 * Check if a killing damage type should trigger health pickup drop.
	 * Returns true for all damage types EXCEPT DamageType_Ranged and DamageType_EMFWeapon.
	 */
	UFUNCTION(BlueprintPure, Category = "Health Pickup")
	static bool ShouldDropHealth(TSubclassOf<UDamageType> KillingDamageType);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:

	/** Player we're flying toward (set when player enters magnet radius) */
	TWeakObjectPtr<AShooterCharacter> MagnetTarget;

	/** Current velocity for magnet flight */
	FVector CurrentVelocity = FVector::ZeroVector;

	/** Lifetime self-destruct timer */
	FTimerHandle LifetimeTimer;

	/** Called when player enters magnet trigger radius */
	UFUNCTION()
	void OnMagnetOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Called when player touches pickup collision */
	UFUNCTION()
	void OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Destroy after lifetime expires */
	void OnLifetimeExpired();
};
