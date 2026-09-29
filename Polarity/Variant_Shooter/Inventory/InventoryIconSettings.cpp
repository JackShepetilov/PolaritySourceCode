// InventoryIconSettings.cpp

#include "Variant_Shooter/Inventory/InventoryIconSettings.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "PolarityPalette.h"

UInventoryIconSettings::UInventoryIconSettings()
{
	// Apex's order. A default rather than an empty list so the slots show before anybody opens
	// the settings; saving them from Project Settings writes the list to DefaultGame.ini.
	AttachmentSlotOrder = {
		EWeaponAttachmentType::Muzzle,
		EWeaponAttachmentType::Magazine,
		EWeaponAttachmentType::Optic,
		EWeaponAttachmentType::Stock,
	};
}

UTexture2D* UInventoryIconSettings::GetTypeIcon(EWeaponAttachmentType Type)
{
	const TSoftObjectPtr<UTexture2D>* Found = GetDefault<UInventoryIconSettings>()->AttachmentTypeIcons.Find(Type);
	return Found ? Found->LoadSynchronous() : nullptr;
}

UTexture2D* UInventoryIconSettings::GetFamilyIcon(FName Family)
{
	if (Family.IsNone())
	{
		return nullptr;
	}
	const TSoftObjectPtr<UTexture2D>* Found = GetDefault<UInventoryIconSettings>()->AttachmentFamilyIcons.Find(Family);
	return Found ? Found->LoadSynchronous() : nullptr;
}

FLinearColor UInventoryIconSettings::GetRarityColor(EUpgradeRarity Rarity)
{
	const TCHAR* TagName = TEXT("Palette.Rarity.Common");
	FLinearColor Fallback(0.85f, 0.85f, 0.85f);
	switch (Rarity)
	{
	case EUpgradeRarity::Rare:      TagName = TEXT("Palette.Rarity.Rare");      Fallback = FLinearColor(0.15f, 0.45f, 1.0f); break;
	case EUpgradeRarity::Epic:      TagName = TEXT("Palette.Rarity.Epic");      Fallback = FLinearColor(0.65f, 0.2f, 1.0f); break;
	case EUpgradeRarity::Legendary: TagName = TEXT("Palette.Rarity.Legendary"); Fallback = FLinearColor(1.0f, 0.6f, 0.05f); break;
	default: break;
	}
	// Not an error when the tag is missing: the fallback stands until the author sets the palette.
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
	return UPolarityPalette::GetColor(Tag, Fallback);
}

void UInventoryIconSettings::GetWeaponSlotTypes(const AShooterWeapon* Weapon, TArray<EWeaponAttachmentType>& OutTypes)
{
	OutTypes.Reset();
	if (!Weapon)
	{
		return;
	}
	for (const EWeaponAttachmentType Type : GetDefault<UInventoryIconSettings>()->AttachmentSlotOrder)
	{
		if (Weapon->HasAttachmentSlot(Type) && !OutTypes.Contains(Type))
		{
			OutTypes.Add(Type);
		}
	}
}
