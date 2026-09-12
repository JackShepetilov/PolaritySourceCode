// HudJuice.h
// The small motions every HUD number and bar shares: a count that rolls to its target, a punch
// on change, a colour that flashes and settles. Plain structs, ticked by whoever owns them.

#pragma once

#include "CoreMinimal.h"

class UTextBlock;
class UWidget;

/** A displayed number that rolls toward its target instead of jumping, and punches on change. */
struct FHudNumberTween
{
	/** Seconds the roll takes for any distance. */
	float RollTime = 0.25f;

	/** Scale at the top of the punch; 1 = no punch. */
	float PunchScale = 1.18f;
	float PunchTime = 0.18f;

	int32 Target = 0;
	float Displayed = 0.0f;
	float Punch = 0.0f;

	/** Set the target. bInstant skips the roll (first draw, respawn). */
	void Set(int32 NewTarget, bool bInstant);

	/** Advance and write the number and scale into the text block. */
	void Update(float DeltaTime, UTextBlock* Text);

	int32 Shown() const { return FMath::RoundToInt(Displayed); }

private:
	float RollFrom = 0.0f;
	float RollElapsed = 0.0f;
};

/** A colour that can be flashed to another colour and fades back over FlashTime. */
struct FHudColorFlash
{
	FLinearColor Rest = FLinearColor::White;
	FLinearColor FlashColor = FLinearColor::White;
	float FlashTime = 0.35f;
	float Remaining = 0.0f;

	void Flash(const FLinearColor& Color, float Time);

	/** Advance and return the colour to draw this frame. */
	FLinearColor Update(float DeltaTime);
};
