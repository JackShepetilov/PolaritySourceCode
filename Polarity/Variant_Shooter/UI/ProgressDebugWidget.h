// ProgressDebugWidget.h
// A debug panel for the run's progression: run level, mastery of the gun in hand and what it opens,
// the handling times with their breakdown (attachments x mastery), ADS fatigue, the tactical sprint,
// the gadget section. Toggled with polarity.debug.progress [author, 2026-10-16].
//
// Drawn in NativePaint from the live state every frame, like the map: no blueprint, no bindings.
// Local by nature: it shows the player at this screen.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProgressDebugWidget.generated.h"

UCLASS()
class POLARITY_API UProgressDebugWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	/** The panel's lines for the pawn this widget's player controls. */
	void BuildLines(TArray<FString>& OutLines) const;
};
