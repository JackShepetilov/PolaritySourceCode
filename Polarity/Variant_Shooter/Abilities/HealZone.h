// HealZone.h
// What the heal grenade leaves where it bursts: one heal, for the first player to reach it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HealZone.generated.h"

class AShooterCharacter;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;

/**
 * Heals ONE player, once, and is gone. Whoever gets there first takes it, the thrower included.
 * Left alone it expires after its lifetime.
 *
 * Who is inside is re-measured on a slow timer rather than tracked with overlap events, for the
 * reason the health pickup learned the hard way: a zone that bursts already on top of somebody
 * never gets a BEGIN overlap for them.
 *
 * Server decides; the actor is replicated only so every machine sees and hears it.
 */
UCLASS()
class POLARITY_API AHealZone : public AActor
{
	GENERATED_BODY()

public:
	AHealZone();

	/** Hand over the grenade's numbers and start it. Server only.
	 *  @param InStrength 0..1, how far along the distance scale the throw was. Cosmetic. */
	void Begin(float InHealAmount, float InStrength, float InRadius, float InLifetime);

	/** How often the players nearby are re-measured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Zone", meta = (ClampMin = "0.02", Units = "s"))
	float RefreshInterval = 0.1f;

	/** Played for as long as the zone waits, on every machine. Assign on the Blueprint.
	 *  Given three user parameters: Radius (cm), Strength (0..1) and Duration (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Zone|VFX")
	TObjectPtr<UNiagaraSystem> ZoneFX;

	/** One-shot where the heal is taken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Zone|VFX")
	TObjectPtr<UNiagaraSystem> CollectedFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Zone|Audio")
	TObjectPtr<USoundBase> SpawnSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Zone|Audio")
	TObjectPtr<USoundBase> CollectedSound;

	UFUNCTION(BlueprintPure, Category = "Heal Zone")
	float GetRadius() const { return Radius; }

	UFUNCTION(BlueprintPure, Category = "Heal Zone")
	float GetStrength() const { return Strength; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Look for a player to heal. */
	void RefreshOccupants();

	/** Push the replicated numbers into the effect. */
	void ApplyVisualParams();

	UFUNCTION()
	void OnRep_VisualParams();

	/** Sound and burst where the heal was taken. Takes the location because the actor is destroyed
	 *  in the same breath. Unreliable: cosmetic, and the heal has already been applied. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCollected(FVector Location);

	UPROPERTY(VisibleAnywhere, Category = "Heal Zone")
	TObjectPtr<UNiagaraComponent> ZoneVFX;

	/** Replicated only so the visuals on a client can be sized to them. */
	UPROPERTY(ReplicatedUsing = OnRep_VisualParams)
	float Radius = 150.0f;

	UPROPERTY(ReplicatedUsing = OnRep_VisualParams)
	float Strength = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_VisualParams)
	float Lifetime = 10.0f;

	float HealAmount = 0.0f;

	/** Set the instant the heal is paid, so two refreshes cannot both pay. */
	bool bCollected = false;

	FTimerHandle RefreshTimer;
	FTimerHandle ExpiryTimer;
};
