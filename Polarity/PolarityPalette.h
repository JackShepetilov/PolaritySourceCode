// PolarityPalette.h
// Every colour the game names, in one place.
//
// Lives in Project Settings -> Polarity -> Palette. It is deliberately NOT hung off the player
// classes: the class colours are the loudest users of it, but they are not the only ones, and
// anything that wants to be tinted the same violet as the wizard should be able to say so without
// holding a reference to the wizard.
//
// Keyed by GameplayTag rather than by name or enum, for two reasons. A tag gets a picker with
// autocomplete, so a colour is chosen from a list instead of retyped and misspelled into silence;
// and a new colour can be added by an author in the editor without a code change, which an enum
// would not allow.
//
// Suggested shape for the keys, not enforced:
//   Palette.Class.Wizard / Tank / Melee / Sniper   - the four class identities
//   Palette.Ammo.<Kind>                            - ammo badges, related to the class they favour
//
// The weapon side of this is AShooterWeapon::AmmoColorTag.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "PolarityPalette.generated.h"

/**
 * Project Settings entry: the named colours of the game.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Palette"))
class POLARITY_API UPolarityPalette : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	/** Groups this next to the project's other settings rather than under Engine. */
	virtual FName GetCategoryName() const override { return FName("Polarity"); }

	/** Tag to colour. Add a row per named colour.
	 *
	 *  Empty by default on purpose: seeding four colours in code would put the palette in two
	 *  places at once, and the first time an author edited one of them in Project Settings the
	 *  code copy would start lying. */
	UPROPERTY(EditAnywhere, config, Category = "Palette", meta = (ForceInlineRow))
	TMap<FGameplayTag, FLinearColor> Colors;

	/** The colour named by Tag, or Fallback when the palette does not name it.
	 *
	 *  Falling back rather than returning black is deliberate: a missing entry should look
	 *  unstyled, not broken, so an unfinished palette does not read as a rendering bug. */
	UFUNCTION(BlueprintPure, Category = "Polarity|Palette")
	static FLinearColor GetColor(FGameplayTag Tag, FLinearColor Fallback = FLinearColor::White);

	/** Whether the palette names this tag at all. Use it when "unset" has to behave differently
	 *  from "set to white". */
	UFUNCTION(BlueprintPure, Category = "Polarity|Palette")
	static bool HasColor(FGameplayTag Tag);

#if WITH_EDITOR
	/** Write the palette to DefaultGame.ini.
	 *
	 *  Editing the panel in Project Settings saves by itself; this exists for the other direction,
	 *  where a script fills the palette in. Without it, a scripted edit lives only in memory and is
	 *  gone at the next restart, and the alternative - hand-writing the config file - means
	 *  guessing the engine's own serialisation format for a TMap keyed by a struct, which is not
	 *  something to guess at.
	 *
	 *  Returns whether the file was actually written. */
	UFUNCTION(BlueprintCallable, Category = "Polarity|Palette")
	static bool SaveToDefaultConfig();

	/** Set one entry and write the file. The scripted equivalent of adding a row in the panel. */
	UFUNCTION(BlueprintCallable, Category = "Polarity|Palette")
	static bool SetColor(FGameplayTag Tag, FLinearColor Color);
#endif
};
