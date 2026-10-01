// TacticalChargeWidget.h
// The side rail device's charge bar. Draws itself, so it needs no Blueprint: put the class straight
// into the HUD layout (slot HUD.Slot.Tactical). Docs/TacticalAttachment_Plan_2026-10-01.md.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Variant_Shooter/UI/Hud/HudBindable.h"
#include "TacticalChargeWidget.generated.h"

class UTacticalDeviceComponent;

/**
 * A flat bar in the device's own colour. Full height while the device can switch on, dimmed below
 * its MinChargeToActivate line (drawn as a tick), brighter while it runs. Hidden with no device on
 * the gun in hand. Prototype look: an art pass replaces the paint, the numbers stay.
 */
UCLASS(Blueprintable)
class POLARITY_API UTacticalChargeWidget : public UUserWidget, public IHudBindable
{
	GENERATED_BODY()

public:

	virtual void BindCharacter(AShooterCharacter* Character) override;
	virtual void UnbindCharacter() override;

	/** Track colour behind the fill. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Bar")
	FLinearColor TrackColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.55f);

	/** Fill brightness while the charge is under the switch-on line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NotReadyAlpha = 0.35f;

	/** Inset of the fill inside the track, px. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Bar", meta = (ClampMin = "0.0"))
	float FillInset = 2.0f;

protected:

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	TWeakObjectPtr<UTacticalDeviceComponent> Device;

	/** What was drawn last, so the bar only repaints when it changes. */
	float LastCharge = -1.0f;
	uint8 LastState = 0xFF;
};
