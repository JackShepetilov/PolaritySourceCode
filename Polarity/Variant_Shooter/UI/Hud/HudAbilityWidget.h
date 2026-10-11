// HudAbilityWidget.h
// HUD.Slot.Abilities: the one ability the player carries, its key and its cooldown.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HudSlotWidget.h"
#include "HudAbilityWidget.generated.h"

class UAbilityComponent;
class UAbilityDefinition;
class UHudBarWidget;
class UHudShapeWidget;
class UImage;
class UTextBlock;

/**
 * Inherit in Blueprint (WBP_Ability): a plate (optional), an Image AbilityIcon, a CooldownFill image
 * (or a CooldownBar) that fills as the ability comes back, a text block CooldownText for the seconds
 * left and a text block KeyText for the key that fires it. The cell in Docs/HUD/HUD_Layout.html. Every one of them is optional, so the look is the
 * Blueprint's to decide.
 *
 * Reads the ability component by looking at it each tick rather than through its delegates. The
 * cooldown is replicated to the owning client as a number and its start and end events are not, so
 * a widget driven by the events works for the host and shows nothing for everybody else.
 *
 * Its content is hidden while the player carries no ability.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudAbilityWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:
	/** Palette colour of the small caption. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Palette"))
	FGameplayTag CaptionColorTag;

	/** Palette colour of the seconds and of the key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Palette"))
	FGameplayTag NumberColorTag;

	/** Plate flash at the moment the ability comes back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Juice", meta = (Categories = "Palette"))
	FGameplayTag ReadyFlashTag;

	/** Icon opacity while the ability cannot be used (cooling down or mid-cast). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnavailableIconOpacity = 0.35f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;

	/** Empty right after the throw, full when the ability is back. */
	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UHudBarWidget> CooldownBar;

	/** The same thing as CooldownBar for a square cell: a flat image that grows from the bottom edge
	 *  as the ability comes back (render scale Y, pivot on the bottom). Hidden when ready. */
	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UImage> CooldownFill;

	/** The ability's own picture. Left untinted: it is art, the palette does not own it. Hidden
	 *  while the ability has none. */
	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UImage> AbilityIcon;

	/** Whole seconds left, empty when ready. */
	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CooldownText;

	/** The key bound to the ability, as the player's own bindings have it. */
	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(BlueprintReadOnly, Category = "Ability", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Caption;

private:
	/** Show Ability's picture and the key that fires it. */
	void ShowAbility(const UAbilityDefinition* Ability);

	TWeakObjectPtr<UAbilityComponent> Abilities;
	TWeakObjectPtr<const UAbilityDefinition> ShownAbility;

	/** Length of the cooldown that is running, as seen from this machine. */
	float CooldownTotal = 0.0f;
	float LastRemaining = 0.0f;
	int32 LastSeconds = -1;
	bool bShown = false;
	FLinearColor PlateRest = FLinearColor::Black;
};
