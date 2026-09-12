// InventoryPickup.h
// A thing lying in the world that costs a cell to carry.
//
// This is the parent of every pickup that goes into the grid rather than being consumed on touch.
// Health and armour are not in this family: they are instant and own no cell (see
// Docs/Inventory_Slot_Contract_2026-08-28.md section 3). Currency, spare magazines, ability
// upgrades and attachments are.
//
// Three things make this class worth existing rather than copying UpgradePickup a fourth time:
//
//  - It asks before it takes. Whether there is room is answered by the grid, not guessed here.
//  - It STAYS IN THE WORLD WITH THE REMAINDER. Currency arrives in 15-60 unit drops against a
//    stack of 100, so "part of it fits" is the common case, not an edge case. A pickup that
//    vanished after a partial take would quietly delete the rest.
//  - It is what a dropped cell turns back into. UInventoryComponent::DropSlotToWorld spawns one of
//    these, which is why the thing it carries is a plain FInventoryItem rather than an amount of
//    money: the same actor has to be able to hold rounds or an upgrade just as well.
//
// Taken by YANK, like a weapon drop, not by walking into it. That is the author's decision from
// 2026-08-30 and it is the reason there is no overlap handler anywhere in this file.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/Inventory/InventoryTypes.h"
#include "InventoryPickup.generated.h"

class UEMF_FieldComponent;
class UInventoryComponent;
class UNiagaraSystem;
class UStaticMeshComponent;
class USoundBase;
class AShooterCharacter;

UCLASS(Blueprintable)
class POLARITY_API AInventoryPickup : public AActor
{
	GENERATED_BODY()

public:

	AInventoryPickup();

	// ==================== Components ====================

	/** Visible mesh and root. Simulated on the server and mirrored everywhere else, the same deal
	 *  AEMFPhysicsProp and ADroppedRangedWeapon make: one simulation, one truth about where it is. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Charge storage. The yank scan needs a non-zero charge to compute a reach at all, so a
	 *  pickup with no charge is invisible to the grab even while it sits in plain sight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== What is in it ====================

	/** The offer. Replicated because the count shrinks when part of it is taken, and the number
	 *  over a half-taken pile has to be right on every screen that can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Item, Category = "Inventory Pickup")
	FInventoryItem Item;

	/** Charge given to the field component at BeginPlay when it has none of its own. A pickup
	 *  placed by hand in the editor would otherwise be uncapturable, because reach is a function of
	 *  charge and zero charge is zero reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Pickup", meta = (ClampMin = "0.0"))
	float DefaultCharge = 1.0f;

	// ==================== Capture ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	/** How long after a partial take before this can be grabbed again. Without it the pickup that
	 *  stayed behind is yanked again on the next frame of the same held channel, and the player
	 *  never sees that anything was refused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float RegrabDelay = 0.75f;

	/** Does a pickup PLACED IN THE LEVEL fall and roll, or stay where it was put?
	 *
	 *  An ammo point or an attachment on a shelf is a PLACE: the player walks there, and it has to
	 *  still be there next lap rather than at the bottom of the nearest slope. A pile that came out
	 *  of somebody's bag is not a place and always falls -- both runtime paths override this, so
	 *  this flag really does only describe the hand-placed case. Those paths are
	 *  SpawnForItem (a thrown-away cell) and ReturnToWorld (what did not fit coming back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Pickup")
	bool bStartSimulatingPhysics = true;

	// ==================== Pull ====================

	/** Camera-relative point the pickup flies to (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FVector PullTargetOffset = FVector(60.0f, 0.0f, -10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PullDuration = 0.4f;

	// ==================== Effects ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<UNiagaraSystem> PickupVFX;

	// ==================== API ====================

	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Reach for this pickup, by the same logarithmic curve every other yank target uses. */
	float CalculateCaptureRange() const;

	/** Begin the scripted flight. Server only in practice: it ends in a write to the grid, which
	 *  only the server may do, so a client asks through
	 *  AShooterCharacter::Server_RequestInventoryPickup instead of flying its own copy. */
	void StartPull(AShooterCharacter* InPullingPlayer);

	/** Server-side entry for a client's request. False when the pickup is already spoken for. */
	bool TryStartPullForClient(AShooterCharacter* Requester);

	bool IsBeingPulled() const { return bIsBeingPulled; }
	bool IsPullComplete() const { return bPullComplete; }

	/** Spawn one of these holding InItem. Used by the drop-from-a-cell path and by anything else
	 *  that has to put an item back on the floor. */
	static AInventoryPickup* SpawnForItem(const UObject* WorldContextObject,
		TSubclassOf<AInventoryPickup> PickupClass, const FTransform& Where,
		const FInventoryItem& InItem, float InCharge);

	// ==================== Blueprint hooks ====================

	/** Redraw the label over the pile. Fired on every machine whenever the offer changes, which
	 *  includes a partial take shrinking it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Pickup", meta = (DisplayName = "On Item Changed"))
	void BP_OnItemChanged();

	/** Somebody took some or all of it. Taken is what went into the bag, Left is what is still
	 *  lying here, so Left == 0 means this actor is about to be destroyed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Pickup", meta = (DisplayName = "On Collected"))
	void BP_OnCollected(AShooterCharacter* Player, int32 Taken, int32 Left);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Same fault as the weapon drop: the engine's physics-state correction lands on a body that is
	 *  deliberately not simulating on a client, so the transform is taken out of the replicated
	 *  state by hand instead. */
	virtual void PostNetReceivePhysicState() override;

	/** Fill this pickup in from an item that already exists, as SpawnForItem does when a cell is
	 *  thrown away. Virtual because a child may keep the same quantity in a second, authored field
	 *  of its own (ACurrencyPickup::Amount) and would otherwise overwrite what it was just handed
	 *  the moment BeginPlay runs. */
	virtual void InitializeSpawnedItem(const FInventoryItem& InItem);

	/** What is actually offered to THIS bag. The base answers with Item unchanged; a child whose
	 *  numbers depend on the player (currency stacks to whatever that player's grid says) fills
	 *  them in here, at the only moment a player is known. */
	virtual FInventoryItem MakeItemFor(const UInventoryComponent& Inventory) const;

	UFUNCTION()
	void OnRep_Item();

	UFUNCTION()
	void OnRep_DropCharge();

private:

	void UpdatePull(float DeltaTime);

	/** Hand the item over, then either die or stay behind holding the remainder. */
	void CompletePull();

	/** Put the pickup back on the floor in front of the player with what would not fit. */
	void ReturnToWorld(AShooterCharacter* Player, int32 Remaining);

	void AllowRegrab();

	/** Replicated so a second player's scan skips a pickup somebody already called dibs on. */
	UPROPERTY(Replicated)
	bool bIsBeingPulled = false;

	UPROPERTY(Replicated)
	bool bPullComplete = false;

	/** The authority's charge, mirrored for clients. The EMF plugin's field component replicates
	 *  nothing, so without this a client scans every pickup as uncharged and refuses to grab any. */
	UPROPERTY(ReplicatedUsing = OnRep_DropCharge)
	float ReplicatedCharge = 0.0f;

	float PullElapsed = 0.0f;
	FVector PullStartLocation = FVector::ZeroVector;
	FRotator PullStartRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> PullingCharacter;

	FTimerHandle RegrabTimer;
};
