// MapScreenWidget.h
// The map screen: the whole run on one page, behind one key.
//
// Built the way the inventory screen is built - a C++ UUserWidget the controller owns and toggles -
// because the thing this draws is a projection of live world state onto a rectangle, and that is
// painting, not layout. A UMG hierarchy would need one widget per marker created and destroyed as
// squads live and die; NativePaint draws the same picture from the current state every frame and
// has nothing to keep in sync.
//
// Styling stays available: subclass this in a Blueprint, set the background and the colours there.
//
// What it shows about the ENEMY is a debug affordance and is meant to go. bRevealEverything is on
// while the faction war is being built, because the question being asked every day is "why is that
// squad standing there" and that cannot be asked about markers you cannot see. Turn it off and the
// same screen shows only what the team has a right to know.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MapScreenWidget.generated.h"

class UTexture2D;

UCLASS()
class POLARITY_API UMapScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UMapScreenWidget(const FObjectInitializer& Init);

	// ==================== Framing ====================

	/** Middle of the playable area in world space. The map is drawn square around this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FVector2D WorldCentre = FVector2D::ZeroVector;

	/** Half the width of the area the map covers, in world units. The bench is 63000 across, so
	 *  31500 shows all of it with nothing cropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "1000.0"))
	float WorldExtent = 31500.0f;

	/** Top-down picture of the terrain, drawn under everything else. Optional: without it the map
	 *  is a dark square, which is still readable because the markers carry the meaning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	TObjectPtr<UTexture2D> Background = nullptr;

	// ==================== What to show ====================

	/** Draw every unit on the map, both sides. A wallhack, and deliberately a separate switch from
	 *  the rest of the screen so it can be turned off in one place when the game ships. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Debug")
	bool bRevealEverything = true;

	/** Print what each faction WANTS at a place and what it believes is there. The answer to "nine
	 *  of them are standing on that point doing nothing", which is otherwise unanswerable without
	 *  reading a log next to the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Debug")
	bool bShowFactionIntent = true;

	// ==================== Look ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style")
	FLinearColor GroundColour = FLinearColor(0.05f, 0.06f, 0.08f, 0.92f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style")
	FLinearColor NeutralColour = FLinearColor(0.82f, 0.82f, 0.82f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style")
	FLinearColor FactionAColour = FLinearColor(0.31f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style")
	FLinearColor FactionBColour = FLinearColor(1.0f, 0.35f, 0.31f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style")
	FLinearColor PlayerColour = FLinearColor(0.35f, 1.0f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style", meta = (ClampMin = "2.0"))
	float UnitMarkerSize = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Style", meta = (ClampMin = "4.0"))
	float PoiMarkerSize = 18.0f;

	// ==================== Open and close ====================
	//
	// Same three calls as the inventory screen, so the controller talks to both the same way.

	UFUNCTION(BlueprintCallable, Category = "Map")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "Map")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "Map")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "Map")
	bool IsOpen() const { return bIsOpen; }

protected:

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	/** World XY onto the drawn square. Screen up is world -Y, matching the top-down renders the
	 *  terrain pipeline produces, so a screenshot of the map and a picture from map_preview.py
	 *  cannot disagree about which way north is. */
	FVector2D WorldToMap(const FVector& World, const FVector2D& MapSize) const;

	FLinearColor ColourForTeam(uint8 Team) const;

	/** The square the map is drawn in: centred, as large as fits, so the picture never stretches. */
	static void MapRect(const FVector2D& WidgetSize, FVector2D& OutOrigin, FVector2D& OutSize);

	bool bIsOpen = false;
};
