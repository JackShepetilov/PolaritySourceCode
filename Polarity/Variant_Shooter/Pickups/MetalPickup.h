// MetalPickup.h
// A pile of scrap metal. Taken by walking into it, TF2-style, not by yank.
//
// Behaves like AHealthPickup (burst out of the kill, land, fly into a player who comes close) with
// one real difference: metal has a cap, so a pile is often taken in part. Whatever does not fit
// stays on the floor, and a player with no room is never chased by the magnet at all. Otherwise a
// full engineer walking past would drag every pile in the area onto themselves and leave it there.
//
// No grid cell, no inventory: metal is a number on AShooterPlayerState (author's decision,
// 2026-09-11). No overlap events either: collection is a distance check every frame on the server,
// which is what AHealthPickup found to be the only reliable way (see its AcquireMagnetTargetByProximity).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalPickup.generated.h"

class UStaticMeshComponent;
class UNiagaraSystem;
class USoundBase;
class AShooterCharacter;

UCLASS(Blueprintable)
class POLARITY_API AMetalPickup : public AActor
{
	GENERATED_BODY()

public:

	AMetalPickup();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// ==================== Settings ====================

	/** Metal in this pile. Shrinks on a partial take. Replicated so a label over the pile can
	 *  show the right number on every screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Metal Pickup", meta = (ClampMin = "1"))
	int32 Amount = 20;

	/** How close the player's centre has to be to take it. The magnet flies the pile to the
	 *  centre of the capsule, so this only has to cover the last few centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup", meta = (ClampMin = "10.0", Units = "cm"))
	float PickupRadius = 60.0f;

	/** Seconds on the floor before a DROPPED pile disappears. A pile placed on the level by hand
	 *  never expires: it is part of the level, not loot from a fight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup", meta = (ClampMin = "1.0", Units = "s"))
	float Lifetime = 30.0f;

	/** A player with room this close starts pulling the pile in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Magnet", meta = (ClampMin = "0.0", Units = "cm"))
	float MagnetRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Magnet", meta = (ClampMin = "100.0"))
	float MagnetSpeed = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Magnet", meta = (ClampMin = "100.0"))
	float MagnetAcceleration = 3000.0f;

	/** Flight from the kill to the landing spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Burst", meta = (ClampMin = "0.1", ClampMax = "2.0", Units = "s"))
	float BurstDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Burst", meta = (ClampMin = "0", ClampMax = "500", Units = "cm"))
	float BurstArcHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Effects")
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metal Pickup|Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	// ==================== Spawning ====================

	/** Start the arc from where the pile was spawned to TargetLocation. No collection mid-flight.
	 *  Called by ULootDropComponent, which is the only thing that drops piles. */
	void InitBurst(const FVector& TargetLocation);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:

	/** True when this player can take at least one unit right now. */
	static bool HasRoom(const AShooterCharacter* Player);

	/** Nearest living player with room inside MagnetRadius. Not just the nearest player: when the
	 *  nearest one is full, the next one should still get the pile. */
	void AcquireMagnetTarget();

	/** Give as much as fits. Destroys the pile when it is empty, keeps the rest otherwise. */
	void TryCollect(AShooterCharacter* Player);

	void DropMagnetTarget();

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCollected(FVector Location);

	void OnLifetimeExpired();

	bool bIsBursting = false;
	FVector BurstStartLocation = FVector::ZeroVector;
	FVector BurstTargetLocation = FVector::ZeroVector;
	float BurstElapsedTime = 0.0f;

	TWeakObjectPtr<AShooterCharacter> MagnetTarget;
	float MagnetElapsed = 0.0f;

	FTimerHandle LifetimeTimer;
};
