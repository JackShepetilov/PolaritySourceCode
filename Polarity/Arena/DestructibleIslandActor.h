// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DestructibleIslandActor.generated.h"

class ADestructibleIslandActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIslandDestroyed, ADestructibleIslandActor*, Island, AActor*, Destroyer);

/**
 * Destructible flying island that can be destroyed by:
 * - EMF Projectile (high speed collision)
 * - EMF Physics Prop (high speed collision, especially reverse flight)
 * - Player Melee (while player is moving at high speed)
 *
 * Destruction broadcasts OnIslandDestroyed delegate for arena completion / rewards.
 */
UCLASS(Blueprintable)
class POLARITY_API ADestructibleIslandActor : public AActor
{
	GENERATED_BODY()

public:
	ADestructibleIslandActor();

	// ==================== Config ====================

	/** Current HP of the island */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float IslandHP = 500.f;

	/** Max HP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float MaxIslandHP = 500.f;

	/** Minimum impact speed (cm/s) for projectile/prop to deal damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Impact")
	float MinImpactSpeed = 1500.f;

	/** Minimum player speed (cm/s) for melee attacks to count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Impact")
	float MinMeleeSpeed = 800.f;

	/** Damage = (Speed - MinSpeed) * DamagePerSpeed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Impact")
	float DamagePerSpeed = 1.0f;

	/** Unique ID for persistence (tracks which islands are destroyed within session) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Persistence")
	FName IslandID;

	/** BP actor to spawn on destruction (VFX, debris, Niagara, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|VFX")
	TSubclassOf<AActor> DestroyedEffectClass;

	// ==================== Events ====================

	/** Fired when the island is destroyed */
	UPROPERTY(BlueprintAssignable, Category = "Island|Events")
	FOnIslandDestroyed OnIslandDestroyed;

	// ==================== State ====================

	UFUNCTION(BlueprintPure, Category = "Island")
	bool IsDestroyed() const { return bIsDestroyed; }

	UFUNCTION(BlueprintPure, Category = "Island")
	float GetHPPercent() const { return MaxIslandHP > 0.f ? IslandHP / MaxIslandHP : 0.f; }

protected:
	virtual void BeginPlay() override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

private:
	/** Main platform mesh */
	UPROPERTY(VisibleAnywhere, Category = "Island")
	TObjectPtr<UStaticMeshComponent> IslandMesh;

	/** Whether the island has been destroyed */
	bool bIsDestroyed = false;

	/** Called when something physically collides with the island mesh */
	UFUNCTION()
	void OnIslandHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Apply speed-based impact damage */
	void TakeImpactDamage(float Speed, float MinSpeed, AActor* DamageCauser);

	/** Destroy the island: hide, disable, spawn VFX, broadcast delegate */
	void DestroyIsland(AActor* Destroyer);
};
