// SmokeCloud.h
// The Melee class's active, once it has landed: one puff of smoke that enemies cannot see through.
//
// One canister makes THREE of these. @see ASmokeCanisterProjectile

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmokeCloud.generated.h"

class AShooterNPC;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;

/** Everything one canister carries into one cloud.
 *
 *  Replicated whole rather than as loose floats: a client that has half the numbers cannot draw
 *  anything, so there is no state worth having between "nothing" and "all of it". */
USTRUCT()
struct FSmokeCloudShape
{
	GENERATED_BODY()

	UPROPERTY()
	float Radius = 350.0f;

	/** How long the puff takes to fill out. Blindness and picture ramp in together over this. */
	UPROPERTY()
	float GrowTime = 0.6f;

	/** How long the cloud blocks sight, growth included. The picture lives exactly this long. */
	UPROPERTY()
	float Duration = 11.0f;

	/** How long the picture takes to thin out at the end. Inside the Duration, not added to it. */
	UPROPERTY()
	float FadeTime = 1.2f;
};

/**
 * Blinds enemies that try to look through it, and slows how fast an enemy inside it can turn.
 * Nothing else: no damage, no movement slow, no effect on players.
 *
 * It has NO collision component. Being seen through is answered by USmokeVisionSubsystem measuring
 * segments against the puff sphere, so bullets, the camera and every cover trace in the project go
 * on ignoring the smoke entirely.
 *
 * The turn slow is server side, like ASlowPuddle's movement slow and for the same reason: enemies
 * are simulated on the server alone. Who is inside is re-measured on a timer rather than tracked
 * with overlap events, so an enemy that dies, spawns or is left under an expiring cloud can never
 * be stranded in the slowed state.
 *
 * The visuals are driven from here rather than authored to match. The Niagara system is told an
 * Opacity every tick, computed by the same function that decides how big the blind sphere is, so
 * "how long the smoke lasts" is one number in the ability and both halves obey it. A curve inside
 * the effect would be a second opinion about the same thing.
 */
UCLASS()
class POLARITY_API ASmokeCloud : public AActor
{
	GENERATED_BODY()

public:
	ASmokeCloud();

	/** Hand over the ability's numbers and start it. Server only. */
	void Begin(const FSmokeCloudShape& InShape, float InSightPenetration, float InTurnRateMultiplier);

	// ==================== Queries used by the vision subsystem ====================

	/** World-space centre of the puff. One entry: a cloud is one sphere, and a wall is three clouds. */
	const TArray<FVector>& GetPuffCentres() const { return PuffCentres; }

	/** Radius the puff blinds at right now. Ramps in over GrowTime and then holds for the rest of
	 *  the life: the smoke stops blocking when it ENDS, not while it is visibly thinning, which is
	 *  the reading the player gets from "it lasts eleven seconds". */
	UFUNCTION(BlueprintPure, Category = "Smoke")
	float GetCurrentRadius() const;

	/** How solid the picture should be right now, 0 to 1. Ramps in over GrowTime, holds, and fades
	 *  out over the last FadeTime seconds of the life. */
	UFUNCTION(BlueprintPure, Category = "Smoke")
	float GetCurrentOpacity() const;

	UFUNCTION(BlueprintPure, Category = "Smoke")
	float GetFullRadius() const { return Shape.Radius; }

	/** Take the enemies this cloud is standing over out of a list somebody else already gathered, and
	 *  move the turn slow to match. Called by USmokeVisionSubsystem for every live cloud at once.
	 *
	 *  Re-measured rather than tracked with overlap events: whoever is not in this answer is
	 *  released, so an enemy that died, spawned or was left under an expiring cloud cannot be
	 *  stranded slowed. */
	void RefreshOccupantsFrom(const TArray<AShooterNPC*>& Candidates);

	// ==================== Tuning the ability does not set ====================

	/** The effect. Assign on the Blueprint.
	 *  @see the user parameters in Docs/Smoke_Ability_Spec_2026-09-03.md */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|VFX")
	TObjectPtr<UNiagaraSystem> SmokeFX;

	/** Colour handed to the effect. Alpha is the smoke's own density, multiplied by the Opacity this
	 *  actor drives, so a thinner smoke is authored here rather than by making it die sooner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|VFX")
	FLinearColor SmokeColor = FLinearColor(0.72f, 0.73f, 0.76f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Audio")
	TObjectPtr<USoundBase> SpawnSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Start the effect and the ageing. Runs on every machine, from Begin on the server and from
	 *  OnRep on a client, and is safe to run twice. */
	void ApplyShape();

	/** Let go of everybody currently held, however the cloud is ending. */
	void ReleaseAll();

	UFUNCTION()
	void OnRep_Shape();

	UPROPERTY(ReplicatedUsing = OnRep_Shape)
	FSmokeCloudShape Shape;

	/** Local, one entry, the actor's own location. */
	TArray<FVector> PuffCentres;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> SmokeVFX;

	/** When the cloud started ageing, in world seconds. */
	float StartTime = 0.0f;

	bool bShapeApplied = false;

	float TurnRateMultiplier = 0.4f;

	/** Who this cloud is currently slowing. Weak: an enemy inside it can die at any moment. */
	TArray<TWeakObjectPtr<AShooterNPC>> Occupants;

	FTimerHandle ExpiryTimer;
};
