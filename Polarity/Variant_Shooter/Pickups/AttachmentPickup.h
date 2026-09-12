// AttachmentPickup.h
// One attachment lying in the world.
//
// Thin on purpose. AInventoryPickup already does the whole of the hard part: the yank, asking the
// grid whether there is room, staying behind with the remainder, and turning back into a cell. An
// attachment adds exactly two facts to that -- which attachment it is, and what it looks like -- so
// this class is those two facts and nothing else.
//
// It does NOT know how to fit itself to a weapon. Mounting is a decision made in the inventory
// screen, on a gun the player picked, and it costs a cell or does not depending on numbers this
// actor cannot see. Picking one up puts it in the bag; that is the whole of what a pickup does.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "AttachmentPickup.generated.h"

class UWeaponAttachmentDefinition;

UCLASS(Blueprintable)
class POLARITY_API AAttachmentPickup : public AInventoryPickup
{
	GENERATED_BODY()

public:

	AAttachmentPickup();

	/** Which attachment this is. Everything else about the pickup comes from here, including the
	 *  mesh it wears: a scope on the floor should be that scope, not a crate with a label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Pickup")
	TObjectPtr<UWeaponAttachmentDefinition> Attachment;

	/** Push the definition's mesh onto this actor. Safe to call again; it is how a pickup spawned
	 *  at runtime catches up after being told what it is holding. */
	UFUNCTION(BlueprintCallable, Category = "Attachment Pickup")
	void ApplyAttachmentLook();

protected:

	virtual void BeginPlay() override;

	/** A pickup spawned from a thrown-away cell learns its attachment from the item, so a dropped
	 *  scope lands as that scope rather than as whatever this Blueprint was authored holding. */
	virtual void InitializeSpawnedItem(const FInventoryItem& InItem) override;

private:

	/** Fill Item from Attachment. One attachment is one cell, always: they do not stack, and two of
	 *  the same scope are two separate things to carry. */
	void RebuildItem();
};
