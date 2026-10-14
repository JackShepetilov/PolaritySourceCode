// GrenadeProjectile.h
// A thrown grenade: bounces, rolls, goes off when its fuse runs out.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrenadeProjectile.generated.h"

class UGrenadeDefinition;
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * Not an AShooterProjectile: that one stops on its first hit and lives in a pool, and a grenade has to
 * bounce, roll and lie there ticking. Replicated with its movement, like the nitro gate, because every
 * player has to see where it lies to get away from it.
 *
 * What it does when it goes off is the definition's (UGrenadeDefinition::Detonate, authority); the
 * flash and the bang are played here on every machine.
 *
 * While it is armed it tells the enemies around where it will go off, a few times a second
 * (AShooterNPC::NotifyGrenadeThreat). Whether they notice and where they run is theirs to decide.
 */
UCLASS(Blueprintable)
class POLARITY_API AGrenadeProjectile : public AActor
{
	GENERATED_BODY()

public:

	AGrenadeProjectile();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority: send it off. Starts the fuse. */
	void Launch(UGrenadeDefinition* InDefinition, const FVector& Velocity);

	UGrenadeDefinition* GetDefinition() const { return Definition; }

	/** Where it is expected to go off: where it lies, or where its flight will put it down. */
	FVector GetDangerCenter() const { return DangerCenter; }

	/** Seconds until it goes off. */
	float GetTimeToDetonate() const;

	bool HasDetonated() const { return bDetonated; }

	/** Authority: an enemy of this side saw it. Its mates near the grenade are warned shortly after
	 *  without seeing it themselves (the shout). @see WasShoutedTo */
	void ReportNoticedBy(const AActor* Noticer);

	/** Authority: has somebody of Listener's side shouted about this grenade long enough ago to be heard. */
	bool WasShoutedTo(const AActor* Listener) const;

	/** Seconds from a shout to the mates acting on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "s"))
	float ShoutDelay = 0.3f;

	/** An enemy within this of a bounce hears it land, whatever it was looking at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "cm"))
	float HearRadius = 900.0f;

	/** Seconds after a bounce during which it counts as heard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "s"))
	float HeardWindow = 0.4f;

	/** True within HeardWindow of a bounce and within HearRadius of it. */
	bool WasHeardBy(const FVector& ListenerLocation) const;

	/** How often the enemies around are told about it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.05", Units = "s"))
	float WarnInterval = 0.15f;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UProjectileMovementComponent> Movement;

	/** Replicated so every machine plays this type's own flash, bang and bounce. */
	UPROPERTY(Replicated)
	TObjectPtr<UGrenadeDefinition> Definition;

	UFUNCTION()
	void OnBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_Bounce(FVector Location, float Strength);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Detonate(FVector Origin);

	void Detonate();
	void WarnAround();
	void PredictRest();

private:

	FTimerHandle FuseTimer;
	FTimerHandle WarnTimer;

	float DetonateAt = 0.0f;
	bool bDetonated = false;

	FVector DangerCenter = FVector::ZeroVector;

	FVector LastBounceLocation = FVector::ZeroVector;
	float LastBounceTime = -1000.0f;

	/** Per side: when somebody of it first saw this grenade. */
	TMap<uint8, float> NoticedAtBySide;
};
