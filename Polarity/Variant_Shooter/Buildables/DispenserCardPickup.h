// DispenserCardPickup.h
// The item lying in an open box of the dispenser slot machine: one card (an upgrade, an attachment,
// a small buff). Taken with the grapple, like a dropped weapon, and only by the player who spun;
// everybody else sees it and cannot hook it. When it reaches the spinner the machine gives them the
// card and shuts the other boxes.
//
// It WEARS the real pickup: every machine spawns its own copy of the card's pickup Blueprint (the
// upgrade's hologram on the cartridge, the scope itself, a health pack) with all of its behaviour
// switched off, and hangs it here. A copy per machine because those pickups are not built to be
// spawned for a network (an upgrade pickup would reach a client without its upgrade); this actor
// carries what the card is, and the copy only shows it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/Abilities/GrappleFetchable.h"
#include "DispenserCardPickup.generated.h"

class AShooterCharacter;
class UDispenserSlotMachineComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(Blueprintable)
class POLARITY_API ADispenserCardPickup : public AActor, public IGrappleFetchable
{
	GENERATED_BODY()

public:

	ADispenserCardPickup();

	/** Which machine and which card. Server, between SpawnActorDeferred and FinishSpawning. */
	void InitCard(UDispenserSlotMachineComponent* InMachine, int32 InCardIndex, const FText& InLabel, const FLinearColor& InColor);

	/** The machine and the card this box holds, for the loot card. Both replicate. */
	UDispenserSlotMachineComponent* GetMachine() const { return Machine; }
	int32 GetCardIndex() const { return CardIndex; }

	/** Seconds the flight to the player's hand takes once hooked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float PullDuration = 0.35f;

	// ==================== IGrappleFetchable ====================

	virtual bool CanBeGrappleFetchedBy(const AShooterCharacter* Caster) const override;
	virtual bool BeginGrappleFetchPull(AShooterCharacter* Caster) override;
	virtual bool IsGrappleFetchInFlight() const override { return bPulling; }
	virtual bool IsGrappleFetchDone() const override { return bTaken || IsHidden(); }
	virtual bool FinishesFetchItself() const override { return true; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Card")
	TObjectPtr<USphereComponent> Bounds;

	/** Fallback look, hidden once the real pickup's copy is on (a card with no pickup class). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Card")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Card")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(Replicated)
	TObjectPtr<UDispenserSlotMachineComponent> Machine;

	UPROPERTY(Replicated)
	int32 CardIndex = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	FText LabelText;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(Replicated)
	bool bPulling = false;

	UFUNCTION()
	void OnRep_Look();

private:

	void ApplyLook();
	void UpdatePresentation();

	/** Spawn this machine's copy of the card's real pickup and switch its behaviour off. Retried from
	 *  Tick until the spin it belongs to has replicated. */
	void TryBuildVisual();

	UPROPERTY(Transient)
	TObjectPtr<AActor> Visual;
	bool bVisualTried = false;
	bool bWasPresented = true;

	/** Server: the flight. */
	TWeakObjectPtr<AShooterCharacter> Puller;
	FVector PullStart = FVector::ZeroVector;
	float PullElapsed = 0.0f;
	bool bTaken = false;
};
