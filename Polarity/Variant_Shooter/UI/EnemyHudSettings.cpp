// EnemyHudSettings.cpp

#include "Variant_Shooter/UI/EnemyHudSettings.h"
#include "Variant_Shooter/UI/EnemyBarWidget.h"
#include "Variant_Shooter/UI/DamageSumLayerWidget.h"
#include "PolarityPalette.h"
#include "GameplayTagContainer.h"

UClass* UEnemyHudSettings::GetEnemyBarClass()
{
	const UEnemyHudSettings* const Settings = Get();
	if (Settings && !Settings->EnemyBarClass.IsNull())
	{
		if (UClass* const Loaded = Settings->EnemyBarClass.LoadSynchronous())
		{
			return Loaded;
		}
		UE_LOG(LogTemp, Warning, TEXT("[ENEMY_HUD] EnemyBarClass %s did not load, using the C++ class"),
			*Settings->EnemyBarClass.ToString());
	}
	return UEnemyBarWidget::StaticClass();
}

UClass* UEnemyHudSettings::GetDamageSumLayerClass()
{
	const UEnemyHudSettings* const Settings = Get();
	if (Settings && !Settings->DamageSumLayerClass.IsNull())
	{
		if (UClass* const Loaded = Settings->DamageSumLayerClass.LoadSynchronous())
		{
			return Loaded;
		}
		UE_LOG(LogTemp, Warning, TEXT("[ENEMY_HUD] DamageSumLayerClass %s did not load, using the C++ class"),
			*Settings->DamageSumLayerClass.ToString());
	}
	return UDamageSumLayerWidget::StaticClass();
}

int32 UEnemyHudSettings::ArmorTierFromMax(float MaxArmor)
{
	// Midpoints between the four pool sizes, so an armour tuned off the grid still gets the nearest.
	if (MaxArmor < 62.5f)
	{
		return 0;
	}
	if (MaxArmor < 87.5f)
	{
		return 1;
	}
	if (MaxArmor < 112.5f)
	{
		return 2;
	}
	return 3;
}

FLinearColor UEnemyHudSettings::PaletteColor(const TCHAR* TagName, const FLinearColor& Fallback)
{
	// Not an error when missing: the tags are registered by the seeding script, and until then the
	// fallbacks are the mockup's colours.
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
	return UPolarityPalette::GetColor(Tag, Fallback);
}

FLinearColor UEnemyHudSettings::ArmorTierColor(int32 Tier)
{
	switch (FMath::Clamp(Tier, 0, 3))
	{
	case 0:
		return PaletteColor(TEXT("Palette.HUD.Enemy.Armor.T0"), FLinearColor(FColor(0xF4, 0xF6, 0xFF)));
	case 1:
		return PaletteColor(TEXT("Palette.HUD.Enemy.Armor.T1"), FLinearColor(FColor(0x3F, 0xA9, 0xF5)));
	case 2:
		return PaletteColor(TEXT("Palette.HUD.Enemy.Armor.T2"), FLinearColor(FColor(0xB4, 0x5C, 0xF0)));
	default:
		return PaletteColor(TEXT("Palette.HUD.Enemy.Armor.T3"), FLinearColor(FColor(0xF0, 0x40, 0x4F)));
	}
}
