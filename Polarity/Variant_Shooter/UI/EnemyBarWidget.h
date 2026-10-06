// EnemyBarWidget.h
// The bar over an enemy: armour in cells by tier, health under it, drawn in code.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/UI/EMFChargeWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Variant_Shooter/Shield/HitZoneComponent.h"
#include "EnemyBarWidget.generated.h"

class AShooterNPC;

/**
 * Enemy-only replacement for the charge indicator. Positioning, the screen-centre focus, occlusion and
 * distance scale are the parent's; everything drawn is here, in NativePaint, from the two numbers that
 * matter: the armour pool (UShieldFieldComponent) and health.
 *
 * Every hit is read off those numbers dropping, not off a damage event, so it looks the same on the
 * host and on a client: both see replicated values fall. What a drop does:
 *   - the chunk it took stays on the bar in a light colour, then drains (GhostHold/GhostDrain);
 *   - the bar shakes;
 *   - an armour cell that just emptied jumps and flashes;
 *   - armour reaching zero throws shards in the tier colour and flashes the row.
 *
 * Sizes and timings: UEnemyHudSettings. Colours: palette, Palette.HUD.Enemy.*.
 * Mockup: Docs/HUD/Enemy_Bar_DamageNumbers_Mockup_2026-10-04.html
 */
UCLASS()
class POLARITY_API UEnemyBarWidget : public UEMFChargeWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual void ResetWidget() override;
	virtual void TickWidgetState(float DeltaTime) override;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FShard
	{
		FVector2f Position = FVector2f::ZeroVector;
		FVector2f Velocity = FVector2f::ZeroVector;
		FVector2f Size = FVector2f::ZeroVector;
		float Angle = 0.0f;
		float Spin = 0.0f;
		float Age = 0.0f;
		FLinearColor Color = FLinearColor::White;
	};
	struct FPartSegment
	{
		FName Zone;
		float Shown = 1.0f;
		float Ghost = 1.0f;
		float Hold = 0.0f;
		float Drain = 0.0f;
		float FlashAge = 1000.0f;
		bool bBroken = false;
	};
	struct FPartRow
	{
		EHitZoneBarIcon Icon = EHitZoneBarIcon::None;
		TArray<FPartSegment> Segments;
	};
	TArray<FPartRow> PartRows;
	float GetFillX() const;

	/** Whole widget size: both rows plus the plate around them. Fixed, so an enemy without armour
	 *  keeps its bar in the same place. */
	FVector2f GetBarSize() const;

	int32 GetCellCount() const;

	/** Take the current numbers as they are, with no animation: a new target, or a pooled widget. */
	void SnapToTarget();

	void ResolveColors();

	void SpawnShards(FVector2f Origin, float Width, FLinearColor Color);

	TWeakObjectPtr<AShooterNPC> TrackedNPC;

	float ShownArmor = 0.0f;
	float ShownHP = 0.0f;
	float GhostArmor = 0.0f;
	float GhostHP = 0.0f;
	float MaxArmor = 0.0f;
	float MaxHP = 1.0f;

	/** Time the ghost still holds before draining; the drain speeds are taken when it runs out. */
	float GhostHoldLeft = 0.0f;
	float ArmorDrainSpeed = 0.0f;
	float HPDrainSpeed = 0.0f;

	float JoltAge = 1000.0f;
	float JoltStrength = 1.0f;
	float ArmorFlashAge = 1000.0f;
	float Clock = 0.0f;

	TArray<float> CellPopAge;
	TArray<FShard> Shards;

	int32 Tier = 0;
	FLinearColor PlateColor = FLinearColor::Black;
	FLinearColor TrackColor = FLinearColor::Black;
	FLinearColor HealthColor = FLinearColor::Red;
	FLinearColor GhostColor = FLinearColor::White;
	FLinearColor TierColor = FLinearColor::White;
	FLinearColor PartColor = FLinearColor::White;

	FSlateRoundedBoxBrush PlateBrush = FSlateRoundedBoxBrush(FLinearColor::White, 3.0f);
};
