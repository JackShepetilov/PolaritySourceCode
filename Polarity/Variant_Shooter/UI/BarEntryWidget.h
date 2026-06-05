// BarEntryWidget.h
// One small entry in the ability/resource bar (icon + optional number + optional cooldown radial).
// C++ owns the state; the visual representation (icon image, number text, cooldown material,
// intro/outro animations) lives in Blueprint subclasses driven through the BP_* events below.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BarEntryWidget.generated.h"

class UTexture2D;

/**
 * Base class for a single entry in UAbilityResourceBar's row. Inherit in Blueprint to lay out
 * the icon/number/cooldown visuals. The owning bar pushes data via SetIcon/SetCount/SetCooldown
 * and drives add/remove animations via PlayIntro/PlayOutro.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UBarEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Stable key the bar uses to track this entry (e.g. "ability.active", "weapon.ammo"). */
	void InitEntry(FName InKey) { EntryKey = InKey; }

	UFUNCTION(BlueprintPure, Category = "Bar Entry")
	FName GetEntryKey() const { return EntryKey; }

	/** Set the displayed icon. */
	void SetIcon(UTexture2D* InIcon);

	/** Set the numeric counter. bShow=false hides the number entirely (icon-only entry). */
	void SetCount(int32 Count, bool bShow);

	/** Start/refresh a cooldown radial. Total<=0 clears the cooldown. The entry counts the
	 *  remaining time down itself in NativeTick so the radial animates smoothly without the
	 *  bar having to poll. */
	void SetCooldown(float Remaining, float Total);

	/** Play the spawn animation (called by the bar right after the entry is added). */
	void PlayIntro();

	/** Play the removal animation. Returns its length in seconds (0 = remove immediately). */
	float PlayOutro();

protected:

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ==================== Blueprint visual hooks ====================

	UFUNCTION(BlueprintImplementableEvent, Category = "Bar Entry", meta = (DisplayName = "Set Icon"))
	void BP_SetIcon(UTexture2D* Icon);

	UFUNCTION(BlueprintImplementableEvent, Category = "Bar Entry", meta = (DisplayName = "Set Count"))
	void BP_SetCount(int32 Count, bool bShow);

	/** Remaining/Total in seconds; NormalizedRemaining = Remaining/Total (1 = just started, 0 = ready). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bar Entry", meta = (DisplayName = "Set Cooldown"))
	void BP_SetCooldown(float Remaining, float Total, float NormalizedRemaining);

	UFUNCTION(BlueprintImplementableEvent, Category = "Bar Entry", meta = (DisplayName = "Play Intro"))
	void BP_PlayIntro();

	/** Return the outro animation length in seconds (0 = no animation, remove immediately). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bar Entry", meta = (DisplayName = "Play Outro"))
	float BP_PlayOutro();

	UPROPERTY(BlueprintReadOnly, Category = "Bar Entry")
	FName EntryKey;

	/** Total cooldown duration; >0 means a cooldown is animating (and NativeTick is live). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar Entry")
	float CooldownTotal = 0.0f;

	/** Remaining cooldown time, counted down in NativeTick. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar Entry")
	float CooldownRemaining = 0.0f;
};
