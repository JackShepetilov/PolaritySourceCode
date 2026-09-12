// NitroGate.h
// The Tank's active, once it has landed: a pad that throws whoever steps on it into a slide.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NitroGate.generated.h"

class APolarityCharacter;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Thrown, sticks to the floor, and hands a fixed speed plus a forced slide to anything that walks
 * over it. Apex's Nitro Gate, with its own bet kept: the pad does not ask whose side you are on.
 *
 * The boost is NOT applied from here, and that is the whole design of this actor. An overlap event
 * fires outside the movement simulation, so a client would be corrected off its own boost every
 * time (Docs/Gotchas/Movement_Network.md: anything that writes Velocity lives inside the simulated
 * move or it works for the host alone). Instead the gate registers itself in UNitroGateSubsystem
 * and UApexMovementComponent asks, every simulated move, whether it is standing on one. The gate is
 * a replicated actor at a replicated position, so the owning client and the server read the same
 * geometry and reach the same answer with nothing sent between them.
 *
 * Everything the movement side reads is therefore replicated: the pad's size and the speed it
 * hands out, not just its transform.
 */
UCLASS()
class POLARITY_API ANitroGate : public AActor
{
	GENERATED_BODY()

public:
	ANitroGate();

	/** Hand over the ability's numbers and throw it. Server only: the handler that calls this runs
	 *  on the authority, and the flight replicates from there. */
	void LaunchFrom(const FVector& Start, const FRotator& AimRotation, float ThrowSpeed,
		float InBoostSpeed, float InPadRadius, float InHealth, bool bInAffectsEnemies);

	// ==================== What the movement simulation asks ====================

	/** True when a capsule standing at Feet is on the pad. A flat cylinder rather than a box: the
	 *  test runs on both ends of the wire and a radius has no rotation to disagree about. */
	bool IsOnPad(const FVector& Feet) const;

	/** Whether this gate acts on Who at all.
	 *
	 *  The single place the "does it work on enemies" question is answered, and deliberately a
	 *  function rather than a bare field read: the planned upgrade that gives enemies a DIFFERENT
	 *  outcome instead of the same one changes this and nothing else. */
	bool WantsToBoost(const APolarityCharacter* Who) const;

	float GetBoostSpeed() const { return BoostSpeed; }

	/** Which way the boost throws somebody who is not already moving. The direction the thrower was
	 *  facing when it landed, so a gate placed down a corridor sends people down the corridor. */
	FVector GetPadForward() const { return FRotationMatrix(FRotator(0.0f, DeployedYaw, 0.0f)).GetUnitAxis(EAxis::X); }

	bool IsDeployed() const { return bDeployed; }

	// ==================== Tuning the ability does not set ====================

	/** How far above and below the pad's own plane a pair of feet still counts as on it. Generous
	 *  downward for the slope the pad sat on, tight upward so a jump over it is a jump over it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "0.0", Units = "cm"))
	float PadHeightTolerance = 90.0f;

	/** Played on the deployed pad, on every machine. Assign on the Blueprint: the C++ default is
	 *  empty on purpose so a missing asset is invisible rather than a hard reference to nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate|VFX")
	TObjectPtr<UNiagaraSystem> DeployedFX;

	/** Played once where somebody is launched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate|VFX")
	TObjectPtr<UNiagaraSystem> LaunchFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate|Audio")
	TObjectPtr<USoundBase> DeploySound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate|Audio")
	TObjectPtr<USoundBase> LaunchSound;

	/** Told by the movement component on the machine that actually launched somebody, so the effect
	 *  plays on the client that predicted it as well as on the server. Cosmetic only. */
	void NotifyLaunched(const FVector& Where);

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnFlightHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** Stick here. Server only; clients arrive through OnRep_Deployed with the same numbers. */
	void Deploy(const FVector& Location, float Yaw);

	UFUNCTION()
	void OnRep_Deployed();

	/** Put the actor on the deployed spot and switch it from a thrown object into a pad. Runs on
	 *  every machine, from Deploy on the server and from OnRep_Deployed elsewhere. */
	void ApplyDeployedState();

	/** Collision while flying only. Switched off on landing: the pad must not push the people it is
	 *  meant to launch, and the launch test is geometry rather than an overlap. */
	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<USphereComponent> FlightCollision;

	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<UStaticMeshComponent> PadMesh;

	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<UNiagaraComponent> PadFX;

	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// ==================== Replicated state ====================
	//
	// The movement simulation reads all of this on both ends of the wire, so all of it replicates.
	// A client that knew where the pad was but not how big it is would launch at a different moment
	// than the server and get corrected for it.

	UPROPERTY(ReplicatedUsing = OnRep_Deployed)
	bool bDeployed = false;

	UPROPERTY(Replicated)
	FVector DeployedLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float DeployedYaw = 0.0f;

	UPROPERTY(Replicated)
	float BoostSpeed = 2150.0f;

	UPROPERTY(Replicated)
	float PadRadius = 120.0f;

	UPROPERTY(Replicated)
	bool bAffectsEnemies = true;

	/** Shootable, like Axle's own gates: the Tank can take back a badly placed one, and in a fight
	 *  where enemies use it too there is a way to close it. */
	float Health = 100.0f;
};
