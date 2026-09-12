// HudBindable.h
// The one thing a HUD slot's content must be able to do: follow the local player's character.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HudBindable.generated.h"

class AShooterCharacter;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UHudBindable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by every widget the registry puts into a slot. The registry calls BindCharacter each
 * time the local controller gets a pawn (spawn, respawn, the client's late possess) and
 * UnbindCharacter before the next bind and on teardown. The widget subscribes to whatever it needs
 * from the character itself; nothing pushes data at it. Blueprint-only content inherits
 * UHudSlotWidget, which turns these into events.
 */
class POLARITY_API IHudBindable
{
	GENERATED_BODY()

public:

	virtual void BindCharacter(AShooterCharacter* Character) = 0;
	virtual void UnbindCharacter() = 0;
};
