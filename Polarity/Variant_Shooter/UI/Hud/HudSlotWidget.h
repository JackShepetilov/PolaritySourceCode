// HudSlotWidget.h
// Base for new HUD content: a UserWidget that the registry can bind to the player's character.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HudBindable.h"
#include "HudSlotWidget.generated.h"

class AShooterCharacter;

/**
 * Inherit in Blueprint for a slot that needs no C++ of its own: On Bound hands you the character,
 * On Unbound tells you to let go. C++ children override NativeBind/NativeUnbind and get the events
 * for free. The bound character is kept as a weak pointer for Blueprint reads.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudSlotWidget : public UUserWidget, public IHudBindable
{
	GENERATED_BODY()

public:

	virtual void BindCharacter(AShooterCharacter* Character) override;
	virtual void UnbindCharacter() override;

	/** The character this slot currently follows, or null between binds. */
	UFUNCTION(BlueprintPure, Category = "HUD Slot")
	AShooterCharacter* GetBoundCharacter() const { return BoundCharacter.Get(); }

protected:

	/** C++ hook, called after BoundCharacter is set and before the Blueprint event. */
	virtual void NativeBind(AShooterCharacter* Character) {}

	/** C++ hook, called before BoundCharacter is cleared and before the Blueprint event. */
	virtual void NativeUnbind() {}

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD Slot", meta = (DisplayName = "On Bound"))
	void BP_OnBound(AShooterCharacter* Character);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD Slot", meta = (DisplayName = "On Unbound"))
	void BP_OnUnbound();

	virtual void NativeDestruct() override;

private:

	TWeakObjectPtr<AShooterCharacter> BoundCharacter;
};
