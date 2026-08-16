// PlayerClassDefinition.h
// What makes one player class different from another, as data.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlayerClassDefinition.generated.h"

class AShooterWeapon;
class UAbilityDefinition;
class UTexture2D;

/**
 * What a class does with a fully charged object.
 *
 * Every class electrifies items the same way and spends them differently; this is which verb this
 * class uses. Author's design, one per class, and deliberately an enum rather than four booleans
 * because they are alternatives, never combinations.
 */
UENUM(BlueprintType)
enum class EClassItemVerb : uint8
{
	/** No charged-item interaction. */
	None,
	/** Grab it and throw it, to stun. */
	Throw,
	/** Blow it up from a distance. */
	Detonate,
	/** Turn it into a decoy: a loud object thrown anywhere, which pulls enemy aggression onto itself
	 *  for a few seconds. Replaces the earlier "disintegrate into a shield" reading of the Tank's
	 *  verb; the handoff document's per-class section is what this follows.
	 *
	 *  Renaming an enum value is not free: tagged property serialisation stores the VALUE NAME, so
	 *  DA_Class_Tank would have loaded as None and the Tank would have quietly stopped interacting
	 *  with props at all. The redirect that prevents it is in Config/DefaultEngine.ini,
	 *  [CoreRedirects]. */
	Decoy,
	/** Decompose it into healing, for yourself or a teammate. */
	Heal
};

/**
 * One player class, as data rather than as a C++ subclass.
 *
 * The design document is explicit about this: build ONE ability system that classes configure with
 * data, not four hardcoded classes. This asset is that configuration.
 *
 * What is deliberately NOT here: the mesh, the animation class, the capsule. Those live on the class
 * Blueprint (BP_WizardCharacter and friends, children of BP_ShooterCharacter), where they can be seen
 * in the viewport and are inherited from the base for everything not overridden. Each thing lives in
 * exactly one of the two places — the moment a mesh is set here as well, there are two sources of
 * truth and they will disagree.
 *
 * A class Blueprint sets ClassDefinition as its default, so the pointer costs nothing to replicate
 * for the normal case: a client spawning BP_WizardCharacter already has it from the archetype. It
 * stays replicated anyway so a class can be changed at runtime later without reworking any of this.
 */
UCLASS(BlueprintType)
class POLARITY_API UPlayerClassDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==================== Identity ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TObjectPtr<UTexture2D> Icon;

	// ==================== Loadout ====================

	/** The weapon this class starts a run with. Handed over by the existing run-start path rather
	 *  than a new one: this only supplies the value AShooterCharacter::StartingWeaponClass already
	 *  had, so the animated draw and everything around it are untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loadout")
	TSubclassOf<AShooterWeapon> StartingWeaponClass;

	/** What this class does with a fully charged object. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loadout")
	EClassItemVerb ItemVerb = EClassItemVerb::None;

	// ==================== Abilities ====================

	/** Always-on ability, granted at spawn and never switched away from by the player. Passive and
	 *  active are separate fields rather than one list because they mean different things to the
	 *  player and to the HUD, even though both are ordinary UAbilityDefinitions underneath. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilityDefinition> PassiveAbility;

	/** The ability on the ability key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilityDefinition> ActiveAbility;

	// NOTE: which weapons this class may pick up is missing on purpose. Weapons carry no category
	// today -- no type, no tag, nothing to filter on -- and inventing one to fill this field would be
	// building a system nobody asked for yet. It belongs here the moment weapons can answer "what
	// kind am I".
};
