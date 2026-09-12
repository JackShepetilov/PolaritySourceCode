// SlowPuddle.h
// The Wizard's active, once it has landed: a patch of floor that slows every enemy standing in it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SlowPuddle.generated.h"

class AShooterNPC;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;

/**
 * Slows enemies while they stand in it, and nothing else: no damage, no tick of anything, no effect
 * on players. Catalyst's tactical with the damage taken off, which is the author's call rather than
 * an omission - the Wizard already multiplies the team's damage, and a puddle that also hurt would
 * read as "more damage" instead of as control.
 *
 * Server only, and that is what makes it much simpler than ANitroGate. The gate had to be predicted
 * because it moves the PLAYER, and a player's movement is simulated on both ends of the wire. This
 * moves enemies, and enemies are simulated on the server alone - clients only ever see the result of
 * their position replicating. So there is no registry, no geometry query inside the movement
 * simulation, and nothing here has to agree with anything on another machine.
 *
 * Who is inside is re-measured on a slow timer rather than tracked with overlap events. Overlaps
 * would need begin/end pairs to stay balanced through the cases that actually happen here - an enemy
 * dying inside the puddle, an enemy spawning inside it, the puddle expiring under a crowd - and each
 * unbalanced pair is an enemy left permanently slowed. Re-measuring cannot drift: whoever is not in
 * the answer this time is released.
 */
UCLASS()
class POLARITY_API ASlowPuddle : public AActor
{
	GENERATED_BODY()

public:
	ASlowPuddle();

	/** Hand over the ability's numbers and start it. Server only. */
	void Begin(float InRadius, float InDuration, float InSlowMultiplier);

	// ==================== Tuning the ability does not set ====================

	/** How far above the puddle's own plane an enemy still counts as standing in it. A puddle is
	 *  flat, so this is deliberately short: an enemy on the walkway above must not be slowed by
	 *  something on the floor below it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.0", Units = "cm"))
	float HeightTolerance = 120.0f;

	/** How often the set of enemies inside is re-measured. Cheap enough to be generous with: one
	 *  distance check per enemy per tick of this timer, and there is at most a handful of puddles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.05", Units = "s"))
	float RefreshInterval = 0.2f;

	/** Played for as long as the puddle lasts, on every machine. Assign on the Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle|VFX")
	TObjectPtr<UNiagaraSystem> PuddleFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle|Audio")
	TObjectPtr<USoundBase> SpawnSound;

	/** The radius the effect is playing at, so a Blueprint can scale its own visuals to match the
	 *  gameplay radius instead of the two being authored separately and disagreeing. */
	UFUNCTION(BlueprintPure, Category = "Puddle")
	float GetRadius() const { return Radius; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Re-measure who is inside and move the slow to match. */
	void RefreshOccupants();

	/** Let go of everybody currently held. Called when the puddle ends, however it ends. */
	void ReleaseAll();

	UFUNCTION()
	void OnRep_Radius();

	UPROPERTY(VisibleAnywhere, Category = "Puddle")
	TObjectPtr<UNiagaraComponent> PuddleVFX;

	/** Replicated only so the visuals on a client can be sized to it. The gameplay reads it on the
	 *  server, which is the only place any of this runs. */
	UPROPERTY(ReplicatedUsing = OnRep_Radius)
	float Radius = 250.0f;

	float SlowMultiplier = 0.5f;

	/** Who this puddle is currently slowing. Weak: an enemy inside it can die at any moment, and the
	 *  puddle must neither keep it alive nor chase it. */
	TArray<TWeakObjectPtr<AShooterNPC>> Occupants;

	FTimerHandle RefreshTimer;
	FTimerHandle ExpiryTimer;
};
