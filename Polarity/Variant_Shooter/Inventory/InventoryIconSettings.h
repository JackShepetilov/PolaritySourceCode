// InventoryIconSettings.h
// Project Settings -> Polarity -> Inventory Icons: how weapon attachment slots are laid out and what
// attachments look like when they have no picture of their own.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "InventoryIconSettings.generated.h"

class AShooterWeapon;
class UTexture2D;

/**
 * One place for the look of the weapon slots, Apex-style [author, 2026-09-26]:
 *
 *  - ORDER is the same on every gun (muzzle, magazine, optic, stock). Which slots a gun HAS is the
 *    gun's own list, AShooterWeapon::AttachmentSlots; whether a particular attachment fits it is
 *    the attachment's whitelist. The three never overlap.
 *  - An icon is a white silhouette tinted by rarity. An attachment's picture is its own Icon if it
 *    has one (a scope, a flashlight), else its family's, else its type's. The type icon is also
 *    what an empty slot shows, dimmed.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Inventory Icons"))
class POLARITY_API UInventoryIconSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UInventoryIconSettings();

	virtual FName GetCategoryName() const override { return FName("Polarity"); }

	/** Left to right under every weapon. A gun without a slot of some type just skips it. A type
	 *  missing from here is never shown, even on a gun that has it. */
	UPROPERTY(EditAnywhere, config, Category = "Weapon Slots")
	TArray<EWeaponAttachmentType> AttachmentSlotOrder;

	/** Picture per attachment type: the last fallback for an attachment, and the dimmed hint in an
	 *  empty slot of that type. */
	UPROPERTY(EditAnywhere, config, Category = "Icons", meta = (ForceInlineRow))
	TMap<EWeaponAttachmentType, TSoftObjectPtr<UTexture2D>> AttachmentTypeIcons;

	/** Picture per attachment family (UWeaponAttachmentDefinition::Family, e.g. Mag.Light). Only
	 *  for a family that should look different from its type's picture; the rest leave it out. */
	UPROPERTY(EditAnywhere, config, Category = "Icons", meta = (ForceInlineRow))
	TMap<FName, TSoftObjectPtr<UTexture2D>> AttachmentFamilyIcons;

	static UTexture2D* GetTypeIcon(EWeaponAttachmentType Type);
	static UTexture2D* GetFamilyIcon(FName Family);

	/** Rarity colour: Palette.Rarity.* in Project Settings -> Polarity -> Palette, with the
	 *  dispenser's colours when the palette has no entry. The one source for it. */
	static FLinearColor GetRarityColor(EUpgradeRarity Rarity);

	/** Weapon's slots in display order: AttachmentSlotOrder filtered by what Weapon has. */
	static void GetWeaponSlotTypes(const AShooterWeapon* Weapon, TArray<EWeaponAttachmentType>& OutTypes);
};
