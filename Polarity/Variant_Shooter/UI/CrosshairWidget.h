// CrosshairWidget.h
// Single persistent HUD crosshair (one Image). Owned + driven by AShooterPlayerController (mirrors
// how BulletCounterUI is owned). The controller pushes the active weapon via SetActiveWeapon()
// whenever AShooterCharacter::OnActiveWeaponChanged fires:
//   - Weapon != nullptr -> armed: show the crosshair, apply the weapon's FCrosshairConfig.
//   - Weapon == nullptr -> unarmed: show the simple idle dot.
//
// COSMETIC BLOOM: the crosshair is a single image that grows while firing (and optionally moving /
// airborne) and settles back. C++ integrates a 0..1 bloom each frame and resolves the on-screen
// size in pixels (resolution-independent: BaseHeightFraction * viewport height * weapon Scale, then
// * (1 + bloom * BloomScaleAdd)). LAYOUT is the Blueprint's job: inherit this class, place the
// crosshair Image + idle dot, and resize the Image from the size events below.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairConfig.h"
#include "CrosshairWidget.generated.h"

class AShooterWeapon;

UCLASS(Abstract, Blueprintable)
class POLARITY_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Push the currently held weapon (or nullptr when unarmed). Applies config, resets bloom, and
	 *  fires the BP transition event. */
	void SetActiveWeapon(AShooterWeapon* Weapon);

	UFUNCTION(BlueprintPure, Category = "Crosshair")
	bool IsArmed() const { return bArmed; }

	UFUNCTION(BlueprintPure, Category = "Crosshair")
	const FCrosshairConfig& GetConfig() const { return ActiveConfig; }

	/** Current on-screen crosshair size in pixels (already includes the bloom growth). */
	UFUNCTION(BlueprintPure, Category = "Crosshair")
	float GetSizePixels() const { return CurrentSizePixels; }

	/** Current bloom amount 0..1 (for any extra reactive fx, e.g. opacity). */
	UFUNCTION(BlueprintPure, Category = "Crosshair")
	float GetBloom01() const { return CurrentBloom; }

	/** The weapon's spread this frame, in degrees. This is the number the crosshair is drawn from
	 *  in spread mode, and the same one the bullets are fired with. */
	UFUNCTION(BlueprintPure, Category = "Crosshair")
	float GetSpreadDegrees() const { return CurrentSpreadDegrees; }

	/** True while the crosshair is being kept off the screen by aiming down sights. */
	UFUNCTION(BlueprintPure, Category = "Crosshair")
	bool IsHiddenByAiming() const { return bHiddenByAiming; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Draws the four spread bars (and the centre dot) when Config.bDrawProceduralTicks is on.
	 *  Everything it needs was already resolved in NativeTick, because painting is const. */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Fired when the crosshair is taken off the screen for ADS and when it comes back. The widget
	 *  already fades itself out natively; this is for anything else that should react. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crosshair", meta = (DisplayName = "On Crosshair Visibility Changed"))
	void BP_OnCrosshairVisibilityChanged(bool bVisible);

	/** Crosshair size at weapon Scale 1.0 and zero bloom, as a fraction of viewport HEIGHT
	 *  (resolution-independent). 0.06 = ~65px at 1080p. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float BaseHeightFraction = 0.06f;

	/** Fired on arm / disarm / weapon swap. BP: show the crosshair Image (bInArmed) or the idle dot;
	 *  apply Config.Image + Config.Color; resize the crosshair to SizePixels square. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crosshair", meta = (DisplayName = "On Crosshair Changed"))
	void BP_OnCrosshairChanged(bool bInArmed, const FCrosshairConfig& Config, float SizePixels);

	/** Fired every frame the size changes (bloom growing/settling) while armed. BP: resize the
	 *  crosshair Image to SizePixels (use Bloom01 for any extra fx like opacity). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crosshair", meta = (DisplayName = "On Crosshair Resized"))
	void BP_OnCrosshairResized(float SizePixels, float Bloom01);

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair")
	bool bArmed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair")
	FCrosshairConfig ActiveConfig;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair")
	float CurrentBloom = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair")
	float CurrentSizePixels = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair")
	float CurrentSpreadDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair")
	bool bHiddenByAiming = false;

private:
	/** Resting on-screen size (no bloom) for the current weapon + viewport. */
	float ComputeBaseSizePixels() const;

	/** A spread cone of SpreadDegrees, projected onto the screen: the full WIDTH in pixels of the
	 *  circle a shot can land inside. Reads the player's live FOV, so zooming does not make the
	 *  crosshair lie. */
	float ComputeSpreadSizePixels(float SpreadDegrees) const;

	/** Applies the ADS hide (native fade) and returns true when the crosshair is currently off. */
	bool UpdateAimingVisibility();

	/** The weapon whose firing state drives bloom. Weak so a swapped/destroyed weapon can't dangle. */
	TWeakObjectPtr<AShooterWeapon> ActiveWeapon;
};
