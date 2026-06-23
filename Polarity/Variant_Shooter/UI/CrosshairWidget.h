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

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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

private:
	/** Resting on-screen size (no bloom) for the current weapon + viewport. */
	float ComputeBaseSizePixels() const;

	/** The weapon whose firing state drives bloom. Weak so a swapped/destroyed weapon can't dangle. */
	TWeakObjectPtr<AShooterWeapon> ActiveWeapon;
};
