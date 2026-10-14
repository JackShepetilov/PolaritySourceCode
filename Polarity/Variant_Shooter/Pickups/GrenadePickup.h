// GrenadePickup.h
// Grenades lying in the world.
//
// Thin, like the medkit: AInventoryPickup does the taking, the asking for room and the staying behind
// with what did not fit. This adds which type, and how many. Picking it up throws nothing: it goes into
// the bag (EInventorySlotKind::Grenade) and is thrown on its own key (UGrenadeComponent).

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "GrenadePickup.generated.h"

class UGrenadeDefinition;
class UInventoryComponent;

UCLASS(Blueprintable)
class POLARITY_API AGrenadePickup : public AInventoryPickup
{
	GENERATED_BODY()

public:

	AGrenadePickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade Pickup")
	TObjectPtr<UGrenadeDefinition> Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade Pickup", meta = (ClampMin = "1"))
	int32 Count = 1;

protected:

	virtual void BeginPlay() override;

	/** A pickup made out of a thrown-away cell holds what the cell held. */
	virtual void InitializeSpawnedItem(const FInventoryItem& InItem) override;

private:

	void RebuildItem();
};
