// InventoryDragDropOperation.h
// What is being carried by the cursor between two cells of the inventory overlay.
//
// The payload is an INDEX, not an item. The grid is replicated and server-authoritative, so by the
// time a drop lands the contents of the cell may already have changed under the cursor; sending the
// index means the server re-reads the cell it was actually told about and cannot be handed a stale
// copy of an item that is no longer there.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "InventoryDragDropOperation.generated.h"

class AShooterWeapon;

UCLASS()
class POLARITY_API UInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:

	/** Which cell of the grid was picked up. INDEX_NONE when the drag started on a weapon's
	 *  attachment square instead, in which case the two fields below say which part it was. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 SourceIndex = INDEX_NONE;

	/** Set when the drag started on a mounted attachment: the weapon it is currently fitted to.
	 *
	 *  Dragging off a weapon and dragging out of the grid are the same gesture in opposite
	 *  directions, so they share one operation rather than two that would have to be told apart at
	 *  every drop target. Which one this is comes from which of the two fields is set. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AShooterWeapon> SourceWeapon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EWeaponAttachmentType SourceAttachmentType = EWeaponAttachmentType::Optic;

	/** What is being carried, when it is an attachment. A HINT for the cursor only: it lets a
	 *  weapon's squares refuse a drop that is obviously not an attachment without a round trip. The
	 *  server re-reads the cell it is told about and never trusts this. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UWeaponAttachmentDefinition> DraggedAttachment = nullptr;
};
