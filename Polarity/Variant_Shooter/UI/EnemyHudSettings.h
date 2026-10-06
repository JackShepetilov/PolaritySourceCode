// EnemyHudSettings.h
// Project Settings -> Polarity -> Enemy HUD: the bar over an enemy and the damage number beside it.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyHudSettings.generated.h"

class UEnemyBarWidget;
class UDamageSumLayerWidget;
class UFont;
class UTexture2D;

/**
 * Sizes and timings of the enemy bar and the damage sum. Colours are not here: they live in the
 * palette (Palette.HUD.Enemy.* and Palette.HUD.Damage.*) like every other HUD colour.
 *
 * A settings object rather than fields on a widget blueprint because both widgets are made on every
 * machine, and the class that would carry a blueprint's values is only ever handed out by the game
 * mode, which a client does not have. Config reaches every machine the same way.
 *
 * Mockup and the reasoning behind every number: Docs/HUD/Enemy_Bar_DamageNumbers_Mockup_2026-10-04.html
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Enemy HUD"))
class POLARITY_API UEnemyHudSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	virtual FName GetCategoryName() const override { return FName("Polarity"); }

	static const UEnemyHudSettings* Get() { return GetDefault<UEnemyHudSettings>(); }

	// ==================== Classes ====================

	/** Off = enemies go back to the old charge indicator (WBP_EMFChargeIndicator) and the old
	 *  per-hit numbers. Kept as a switch so the two can be compared in the same build. */
	UPROPERTY(EditAnywhere, config, Category = "Classes")
	bool bUseEnemyBar = true;

	/** Empty = the bare C++ class, which draws everything itself. */
	UPROPERTY(EditAnywhere, config, Category = "Classes")
	TSoftClassPtr<UEnemyBarWidget> EnemyBarClass;

	/** Empty = the bare C++ class. */
	UPROPERTY(EditAnywhere, config, Category = "Classes")
	TSoftClassPtr<UDamageSumLayerWidget> DamageSumLayerClass;

	// ==================== Bar: layout ====================

	/** Bar width at 1 m scale (close range), in HUD units (pixels at 1080p). */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "20"))
	float BarWidth = 120.0f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "1"))
	float ArmorHeight = 6.0f;

	/** Part rows reserve enough height for the approved icons, independently of armour cells. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "1"))
	float PartRowHeight = 10.4f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "1"))
	float PartIconWidth = 16.8f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "0"))
	float PartIconGap = 2.8f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "1"))
	float HealthHeight = 8.0f;

	/** Between the armour row and the health row. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "0"))
	float RowGap = 2.0f;

	/** Between two armour cells. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "0"))
	float CellGap = 2.0f;

	/** Dark plate around both rows. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "0"))
	float Padding = 3.0f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "0"))
	float PlateCornerRadius = 3.0f;

	/** One armour cell holds this much armour: 50 is two cells, 125 is five. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "1"))
	float ArmorPerCell = 25.0f;

	/** Lean of the armour cells, as tan of the angle (0.25 is the 14 degrees of the rest of the HUD).
	 *  Negative leans the other way. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Layout", meta = (ClampMin = "-1", ClampMax = "1"))
	float CellShear = 0.25f;

	// ==================== Bar: motion ====================

	/** How long the chunk a hit took off stays on the bar before it drains. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0", Units = "s"))
	float GhostHoldSeconds = 0.35f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0.01", Units = "s"))
	float GhostDrainSeconds = 0.3f;

	/** Shake of the bar on a hit. A break or a headshot shakes it twice as hard. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0"))
	float JoltPixels = 3.0f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0.01", Units = "s"))
	float JoltSeconds = 0.11f;

	/** A cell that just emptied jumps and flashes for this long. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0.01", Units = "s"))
	float CellPopSeconds = 0.19f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0", ClampMax = "40"))
	int32 BreakShardCount = 12;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0.05", Units = "s"))
	float BreakShardSeconds = 0.52f;

	/** Below this share of health the health row pulses. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.3f;

	UPROPERTY(EditAnywhere, config, Category = "Bar|Motion", meta = (ClampMin = "0.1"))
	float LowHealthPulseHz = 2.0f;

	// ==================== Bar: when it shows ====================

	/** The bar shows under the crosshair (the old screen-centre focus) and also for this long after
	 *  this player's own hit on the enemy, wherever the crosshair has gone since. 0 = crosshair only. */
	UPROPERTY(EditAnywhere, config, Category = "Bar|Visibility", meta = (ClampMin = "0", Units = "s"))
	float ShowAfterHitSeconds = 4.0f;

	// ==================== Damage number (Apex stacking) ====================
	// AUTHOR'S CALL 2026-10-04: the damage number works like Apex's "Stacking" setting. Behaviour and
	// numbers come from Apex's client script (cl_hud.gnut, DamageFlyout), not its code:
	//   - one number per enemy, adding up every hit that comes within StackWindowSeconds of the last;
	//   - a slower hit starts a new number, the old one finishes its own life;
	//   - the number lives StackDurationSeconds after its last hit;
	//   - it sits over the enemy: at the body centre point-blank, blending to the top of the head by
	//     AnchorBlendDistance, then lifted by ScreenOffsetUp of the screen height;
	//   - one size on screen at any distance.
	// AUTHOR, from play 2026-10-04: numbers drift up slowly, a new one pushes the older ones higher,
	// and they stay attached to the enemy. The pop on each hit and the growth with the sum are ours.

	/** The number's font: pick any Font asset here, nothing else to change. AUTHOR 2026-10-04: Lobster
	 *  for now. Impact was /Game/Variant_Shooter/UI/Fonts/unicode_impact_Font. Empty or missing = the
	 *  engine's bold Roboto. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (AllowedClasses = "/Script/Engine.Font"))
	TSoftObjectPtr<UObject> NumberFont = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Variant_Shooter/UI/Fonts/Lobster-Regular_Font.Lobster-Regular_Font")));

	/** Drawn to the left of the number on a kill. Empty = no icon, the colour alone says it. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number")
	TSoftObjectPtr<UTexture2D> KillIcon;

	/** Font size, before the number grows with the sum. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "6"))
	float FontSize = 24.0f;

	/** Outline thickness, font pixels. AUTHOR 2026-10-04: thicker. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0", ClampMax = "12"))
	int32 OutlineSize = 4;

	/** A soft halo in the number's own colour around the outline, so it glows against the scene.
	 *  Extra outline size; 0 = off. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0"))
	int32 GlowSize = 5;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0", ClampMax = "1"))
	float GlowAlpha = 0.35f;

	/** Apex's look: the digits are white and the hit kind's colour is the outline. Off = coloured
	 *  digits with a dark outline. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number")
	bool bColoredOutline = true;

	/** Every hit flashes the number white-hot, then it cools to its colour over the pop. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0", ClampMax = "1"))
	float HitFlashWhite = 0.85f;

	/** Hits closer together than this add to the same number (Apex: 0.25). */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.01", Units = "s"))
	float StackWindowSeconds = 0.25f;

	/** A number lives this long after its last hit (Apex: 1.0). */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.1", Units = "s"))
	float StackDurationSeconds = 1.0f;

	/** A kill's number lives this long instead. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.1", Units = "s"))
	float KillHoldSeconds = 1.3f;

	/** The last part of the life the number spends fading. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.01", Units = "s"))
	float FadeSeconds = 0.3f;

	/** Point-blank the number sits at the body centre; by this distance it has reached the top of the
	 *  head. Apex: 256 of its units, times 1.96 for ours. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "1", Units = "cm"))
	float AnchorBlendDistance = 500.0f;

	/** From that point, up and to the left, as shares of the screen height. Apex lifts by 0.015 and
	 *  does not move sideways; AUTHOR 2026-10-04: higher and left of the head, clear of the bar.
	 *  The mockup's sliders give the same numbers. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0", ClampMax = "0.3"))
	float ScreenOffsetUp = 0.06f;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "-0.3", ClampMax = "0.3"))
	float ScreenOffsetLeft = 0.1f;

	/** The number drifts slowly upward from its last hit, HUD units per second. A new hit on the same
	 *  number starts the drift over, the way Apex restarts its animation on every hit. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0"))
	float RiseSpeed = 30.0f;

	/** A new number on the same enemy pushes the older ones up by this, as a share of the screen
	 *  height per number (Apex spaces its texts by 0.025). */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0", ClampMax = "0.2"))
	float StackSpacing = 0.025f;

	/** How fast an older number slides up into its new place. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.1"))
	float PushSpeed = 14.0f;

	/** The number grows with the sum: scale = 1 + Sum * GrowthPerDamage, capped at MaxGrowth. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0"))
	float GrowthPerDamage = 1.0f / 150.0f;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "1"))
	float MaxGrowth = 1.9f;

	/** Every hit makes the number jump from this scale back to 1. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "1"))
	float PopScale = 1.45f;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "1"))
	float HeadshotPopScale = 1.8f;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "1"))
	float ArmorBreakPopScale = 1.6f;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "1"))
	float KillPopScale = 1.8f;

	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.01", Units = "s"))
	float PopSeconds = 0.14f;

	/** A number made only of hits the player's turret landed is drawn this much smaller. */
	UPROPERTY(EditAnywhere, config, Category = "Damage Number", meta = (ClampMin = "0.1", ClampMax = "1"))
	float RemoteScale = 0.75f;

	// ==================== Helpers ====================

	/** EnemyBarClass, loaded, or the C++ class when it is empty or fails to load. Never null. */
	static UClass* GetEnemyBarClass();

	/** DamageSumLayerClass, loaded, or the C++ class. Never null. */
	static UClass* GetDamageSumLayerClass();

	/** Armour tier 0..3 (white, blue, purple, red) from the size of the pool, matching
	 *  UShieldFieldComponent::SetShieldTier (50 / 75 / 100 / 125). */
	static int32 ArmorTierFromMax(float MaxArmor);

	/** Palette colour by tag name, or the fallback when the tag is not registered or not set. */
	static FLinearColor PaletteColor(const TCHAR* TagName, const FLinearColor& Fallback);

	/** The armour colour of a tier. The bar and the damage number share it. */
	static FLinearColor ArmorTierColor(int32 Tier);
};
