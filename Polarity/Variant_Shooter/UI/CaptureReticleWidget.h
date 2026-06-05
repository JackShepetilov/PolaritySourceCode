// CaptureReticleWidget.h
// On-target "capture brackets" reticle. A SINGLE instance is created and driven by
// EMFChargeWidgetSubsystem, positioned over the current best capture candidate's CENTER
// (not above its head like the charge bar). Inherit in Blueprint and add the brackets Image.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CaptureReticleWidget.generated.h"

/**
 * Single HUD reticle that hugs the object the player is about to capture.
 * The subsystem already picks exactly one "best candidate" per frame, so this needs no pool
 * and holds no per-target state — it just follows whatever target the subsystem hands it.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UCaptureReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Position + size the reticle over a target this frame.
	 *  @param ScreenPosition    Projected target center (viewport pixels).
	 *  @param TargetPixelRadius On-screen bounding radius of the target (pixels).
	 *  @param Polarity          Target polarity (0 = neutral, 1 = positive, 2 = negative). */
	void UpdateForTarget(const FVector2D& ScreenPosition, float TargetPixelRadius, uint8 Polarity);

	/** No capture target this frame — hide the reticle. */
	void ClearTarget();

	/** Fires once on transition (gained vs lost target). Use for pop-in / fade animations. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Capture Reticle",
		meta = (DisplayName = "On Target Changed"))
	void BP_OnTargetChanged(bool bInHasTarget);

	/** Fires when the target's polarity changes (0 = neutral, 1 = positive, 2 = negative).
	 *  Use to tint the brackets per sign (e.g. blue for negative, orange for positive). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Capture Reticle",
		meta = (DisplayName = "On Polarity Changed"))
	void BP_OnPolarityChanged(uint8 Polarity);

	UFUNCTION(BlueprintPure, Category = "Capture Reticle")
	bool HasTarget() const { return bHasTarget; }

	// ==================== Layout ====================

	/** Design-time pixel size of the brackets image at render scale 1.0 (square).
	 *  MUST match the size of the Image you place in the Blueprint. The reticle scales this
	 *  to roughly the target's on-screen diameter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Capture Reticle|Layout", meta = (ClampMin = "8"))
	float ReferenceSize = 128.0f;

	/** Extra margin so brackets sit slightly OUTSIDE the target bounds (1.0 = exact fit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Reticle|Layout", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float BracketPadding = 1.25f;

	/** Lower clamp on render scale (prevents the reticle vanishing at extreme distance). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Reticle|Layout", meta = (ClampMin = "0.05"))
	float MinScale = 0.4f;

	/** Upper clamp on render scale (prevents the reticle filling the screen at point-blank range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Reticle|Layout", meta = (ClampMin = "0.1"))
	float MaxScale = 4.0f;

protected:
	bool bHasTarget = false;
	uint8 LastPolarity = 255; // sentinel so the first real polarity always fires the event
};
