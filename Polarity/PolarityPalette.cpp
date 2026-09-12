// PolarityPalette.cpp

#include "PolarityPalette.h"

FLinearColor UPolarityPalette::GetColor(FGameplayTag Tag, FLinearColor Fallback)
{
	if (!Tag.IsValid())
	{
		return Fallback;
	}

	// GetDefault rather than a cached pointer: a UDeveloperSettings CDO is the live object the
	// Project Settings panel edits, so reading it every call is what makes an edit show up without
	// restarting anything.
	if (const UPolarityPalette* Palette = GetDefault<UPolarityPalette>())
	{
		if (const FLinearColor* Found = Palette->Colors.Find(Tag))
		{
			return *Found;
		}
	}

	return Fallback;
}

bool UPolarityPalette::HasColor(FGameplayTag Tag)
{
	if (!Tag.IsValid())
	{
		return false;
	}

	const UPolarityPalette* Palette = GetDefault<UPolarityPalette>();
	return Palette && Palette->Colors.Contains(Tag);
}

#if WITH_EDITOR

bool UPolarityPalette::SaveToDefaultConfig()
{
	UPolarityPalette* Palette = GetMutableDefault<UPolarityPalette>();
	if (!Palette)
	{
		return false;
	}

	// The engine's own writer, so the config ends up in whatever format the engine reads back.
	return Palette->TryUpdateDefaultConfigFile();
}

bool UPolarityPalette::SetColor(FGameplayTag Tag, FLinearColor Color)
{
	if (!Tag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PALETTE] Refused to store a colour under an invalid tag."));
		return false;
	}

	UPolarityPalette* Palette = GetMutableDefault<UPolarityPalette>();
	if (!Palette)
	{
		return false;
	}

	Palette->Colors.Add(Tag, Color);
	return Palette->TryUpdateDefaultConfigFile();
}

#endif // WITH_EDITOR
