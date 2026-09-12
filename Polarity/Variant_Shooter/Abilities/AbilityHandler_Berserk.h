// AbilityHandler_Berserk.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_Berserk.generated.h"

class AShooterCharacter;

/**
 * One press, one or two people covered.
 *
 * Everything here runs on the AUTHORITY, like every handler: UAbilityComponent::TryActivate routes a
 * client's press to the server before any of this happens. That matters more than usual for this
 * ability, because WHO gets covered is decided by looking, and letting the pressing machine name the
 * teammate would let a client buff anybody it liked from anywhere.
 *
 * The server does the looking from its own copy of the caster's aim, which is what a remote pawn's
 * rotation already carries. The answer can differ from the client's by a hair at long range; the
 * cone is wide enough that it does not matter, and being wrong by a hair is better than being told.
 */
UCLASS()
class POLARITY_API UAbilityHandler_Berserk : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;

protected:
	/** The teammate the caster is looking at, or null for "nobody, so this one is for me alone".
	 *  Never returns the caster. */
	AShooterCharacter* FindAimedAlly(float Range, float AimHalfAngleDegrees) const;
};
