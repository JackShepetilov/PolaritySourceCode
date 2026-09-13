// HudJuice.h
// The small motions every HUD number and bar shares: a count that rolls to its target, a punch
// on change, a colour that flashes and settles. Plain structs, ticked by whoever owns them.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UPanelWidget;
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

/**
 * The "+25" / "-19" numbers that float over a counter when it changes.
 *
 * The rules are the ones every looter settles on, because a burst of pickups otherwise turns into a
 * column of "+1 +1 +1" nobody can read:
 *  - a change of the SAME sign arriving while the newest number is still holding is added into it,
 *    the number re-pops and its hold restarts, so a scrap pile reads as one growing "+47";
 *  - gains and losses NEVER merge: a purchase during a pickup stands as its own red "-19";
 *  - at most MaxEntries are up at once, the oldest goes first;
 *  - every number pops in, holds, then rises and fades.
 *
 * The owner hands it a panel to put the text blocks in (a VerticalBox aligned to its bottom edge,
 * so a new number appears nearest the counter and pushes the older ones up) and ticks it.
 */
struct FHudDeltaStack
{
	/** Seconds a number stays fully readable before it starts to fade. A merge restarts it. */
	float HoldTime = 0.9f;

	/** Seconds the rise-and-fade takes. */
	float FadeTime = 0.4f;

	/** Slate units the number drifts up while fading. */
	float Rise = 14.0f;

	/** Scale at the top of the pop-in; 1 = no pop. */
	float PopScale = 1.35f;
	float PopTime = 0.12f;

	int32 MaxEntries = 3;

	FLinearColor GainColor = FLinearColor::White;
	FLinearColor LossColor = FLinearColor::White;
	FSlateFontInfo Font;
	FVector2D ShadowOffset = FVector2D(0.0, 1.0);
	FLinearColor ShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.8f);

	/** Report a change. Zero is ignored. Outer owns the text blocks created here. */
	void Push(int32 Delta, UPanelWidget* Panel, UObject* Outer);

	/** Advance every number; expired ones leave the panel. */
	void Update(float DeltaTime);

	/** Drop everything at once (unbind, teardown). */
	void Clear();

private:

	struct FEntry
	{
		TWeakObjectPtr<UTextBlock> Text;
		int32 Value = 0;
		/** Seconds since it appeared or was last merged into. */
		float Age = 0.0f;
		/** Pop-in remaining, 1 -> 0. */
		float Pop = 0.0f;
		/** Below zero while holding; otherwise seconds into the fade. */
		float Fading = -1.0f;
	};

	/** Oldest first; the newest is the only one a merge can touch. */
	TArray<FEntry> Entries;

	void WriteValue(const FEntry& Entry) const;
};
