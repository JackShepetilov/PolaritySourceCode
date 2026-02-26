// ArmorPickup.h
// Armor pickup that spawns on channeling kills and magnetically flies to the player

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArmorPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class AShooterCharacter;
class AShooterNPC;
class UNiagaraSystem;
class USoundBase;

/**
 * Armor pickup dropped by NPCs killed via channeling (capture/launch).
 * Sits at spawn location, then magnetically flies toward the player
 * when they enter MagnetRadius. Restores armor on contact.
 */
UCLASS(Blueprintable)
class POLARITY_API AArmorPickup : public AActor
{
	GENERATED_BODY()

public:

	AArmorPickup();

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

	/** Amount of armor to restore on pickup */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup", meta = (ClampMin = "1.0"))
	float ArmorAmount = 25.0f;

	/** Radius at which pickup starts flying toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup|Magnet", meta = (ClampMin = "50.0", Units = "cm"))
	float MagnetRadius = 500.0f;

	/** Maximum speed when flying toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup|Magnet", meta = (ClampMin = "100.0"))
	float MagnetSpeed = 1500.0f;

	/** Acceleration when flying toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup|Magnet", meta = (ClampMin = "100.0"))
	float MagnetAcceleration = 3000.0f;

	/** Time before pickup disappears if not collected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup", meta = (ClampMin = "1.0", Units = "s"))
	float Lifetime = 15.0f;

	/** Sound to play when picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup|Effects")
	TObjectPtr<USoundBase> PickupSound;

	/** VFX to spawn when picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor Pickup|Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	// ==================== Static Helpers ====================

	/**
	 * Check if a dying NPC should drop an armor pickup.
	 * Returns true if the NPC was ever captured/launched by channeling.
	 */
	UFUNCTION(BlueprintPure, Category = "Armor Pickup")
	static bool ShouldDropArmor(const AShooterNPC* DyingNPC);

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
