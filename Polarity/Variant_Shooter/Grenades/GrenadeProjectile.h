// GrenadeProjectile.h
// A thrown grenade: bounces, rolls, goes off when its fuse runs out.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/AimTargetInterface.h"
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
class POLARITY_API AGrenadeProjectile : public AActor, public IAimTargetInterface
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

	/** Gone off and still working (the microwave). @see UGrenadeDefinition::GetActiveSeconds */
	bool IsWorking() const { return bWorking; }

	/** Something to keep away from: not gone off yet, or still working. */
	bool IsDangerous() const { return !bDetonated || bWorking; }

	/** Authority: an enemy of this side saw it. Its mates near the grenade are warned shortly after
	 *  without seeing it themselves (the shout). @see WasShoutedTo */
	void ReportNoticedBy(const AActor* Noticer);

	/** Authority: has somebody of Listener's side shouted about this grenade long enough ago to be heard. */
	bool WasShoutedTo(const AActor* Listener) const;

	/** Is Point inside the reach of a live grenade (armed, or working) and not behind a wall from it,
	 *  for the kinds a wall stops. The cover search and the escape ask it, so nobody hides inside the
	 *  bang. Server. */
	static bool IsPointInDanger(const UWorld* World, const FVector& Point);

	/** The reach right now: the bang's danger radius while armed, the working reach after. */
	float GetCurrentDangerRadius() const;

	// IAimTargetInterface: shot at while working (the microwave), with the definition's comical aim.
	virtual FVector GetLocalAimPoint() const override { return FVector::ZeroVector; }
	virtual float GetAimVerticalJitter() const override { return 0.0f; }
	virtual float GetAimHitChanceOverride() const override;

	/** Hits from the enemy's side while working; enough of them break it. @see UGrenadeDefinition::GetHitsToBreak */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Seconds from a shout to the mates acting on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "s"))
	float ShoutDelay = 0.3f;

	/** Seconds after a bounce during which it counts as heard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "s"))
	float HeardWindow = 0.4f;

	/** True within HeardWindow of a bounce and within the definition's BounceHearRadius of it; for a working grenade, anywhere in its working reach. */
	bool WasHeardBy(const FVector& ListenerLocation) const;

	/** How often the enemies around are told about it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.05", Units = "s"))
	float WarnInterval = 0.15f;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_Pulse(FVector Origin);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopWorking(FVector Origin);

	void PulseOnce();
	void StopWorking();

	void Detonate();
	void WarnAround();
	void PredictRest();

private:

	FTimerHandle FuseTimer;
	FTimerHandle WarnTimer;
	FTimerHandle PulseTimer;
	FTimerHandle WorkEndTimer;

	bool bWorking = false;
	int32 HitsTaken = 0;
	float WorkEndAt = 0.0f;
	FVector WorkOrigin = FVector::ZeroVector;

	/** The working loop, on this machine. */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> WorkingLoop;

	float DetonateAt = 0.0f;
	bool bDetonated = false;

	FVector DangerCenter = FVector::ZeroVector;

	FVector LastBounceLocation = FVector::ZeroVector;
	float LastBounceTime = -1000.0f;

	/** Per side: when somebody of it first saw this grenade. */
	TMap<uint8, float> NoticedAtBySide;
};
