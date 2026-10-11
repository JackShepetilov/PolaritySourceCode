// MedkitPickup.h
// A medkit lying in the world.
//
// Thin, like the attachment pickup: AInventoryPickup does the taking, the asking for room and the
// staying behind with what did not fit. This adds one fact: it is medkits, and how many.
//
// Picking it up heals nothing. It goes into the bag as a cell (EInventorySlotKind::Medkit), and it is
// used by holding a key (UFieldCareComponent) [author, 2026-10-11].

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "MedkitPickup.generated.h"

class UInventoryComponent;

UCLASS(Blueprintable)
class POLARITY_API AMedkitPickup : public AInventoryPickup
{
	GENERATED_BODY()

public:

	AMedkitPickup();

	/** How many medkits are in this one pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Medkit Pickup", meta = (ClampMin = "1"))
	int32 Medkits = 1;

protected:

	virtual void BeginPlay() override;

	/** A pickup made out of a thrown-away cell holds what the cell held. */
	virtual void InitializeSpawnedItem(const FInventoryItem& InItem) override;

	/** The size of a cell is the taker's, not this actor's. */
	virtual FInventoryItem MakeItemFor(const UInventoryComponent& Inventory) const override;

private:

	void RebuildItem();
};
