// DamageSumLayerWidget.h
// One full-screen layer that draws the damage number over every enemy the player is hitting.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "DamageSumLayerWidget.generated.h"

struct FDamageNumberReport;
class UTexture2D;

/**
 * The damage number as the author chose it (2026-10-04): Apex's "Stacking" setting. One number per
 * enemy that adds up every hit coming within a quarter of a second of the last; a slower hit starts a
 * new number while the old one finishes its life. Timings, the anchor point and the screen offset
 * follow Apex's client script (UEnemyHudSettings, "Damage Number").
 *
 * WHERE: over the enemy. Point-blank at the body centre, blending to the top of the head by
 * AnchorBlendDistance, then lifted a little on screen. The same size at any distance. A kill leaves the
 * number where the enemy was.
 *
 * COLOUR (AUTHOR 2026-10-04): white digits, the outline in the last hit's colour. Armour takes the
 * armour tier colour (dark grey for the white tier), the body red; a headshot keeps that outline and
 * turns the digits yellow-orange. A kill changes no colour, it pops harder and stays longer. A number made only of the player's turret hits is
 * grey and smaller.
 *
 * One layer for all enemies rather than a widget each, so there is nothing to pool and a kill's number
 * outlives the enemy's bar.
 */
UCLASS()
class POLARITY_API UDamageSumLayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void AddReport(const FDamageNumberReport& Report);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	enum class EKind : uint8
	{
		Flesh,
		Armor,
		Remote
	};

	struct FEntry
	{
		TWeakObjectPtr<AActor> Target;
		float Total = 0.0f;
		EKind Kind = EKind::Flesh;
		int32 ArmorTier = 0;
		bool bDirect = false;
		bool bKilled = false;
		/** The last hit was a headshot: the digits turn yellow-orange, the outline stays the kind's. */
		bool bHead = false;

		/** Since the last hit. Decides whether the next hit stacks, and when this number ends. */
		float Age = 0.0f;
		float PopAge = 1000.0f;
		float PopFrom = 1.0f;

		/** Creation order. Newer numbers on the same enemy sit lowest and push older ones up. */
		uint32 Serial = 0;
		/** Place in that column, smoothed: 0 = newest. */
		float Slot = 0.0f;

		FVector LastWorldAnchor = FVector::ZeroVector;
		bool bHasWorldAnchor = false;

		/** Centre of the number, layer space. */
		FVector2f Anchor = FVector2f::ZeroVector;
		bool bOnScreen = false;
	};

	/** How long this number lives after its last hit. */
	float GetLife(const FEntry& Entry) const;

	void UpdateAnchor(FEntry& Entry, APlayerController* PC, float ViewportScale, float LayerHeight) const;

	FLinearColor ColorFor(const FEntry& Entry) const;

	TArray<FEntry> Entries;
	uint32 NextSerial = 0;

	UPROPERTY(Transient)
	TObjectPtr<UObject> FontObject;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> KillIconTexture;

	FSlateBrush KillIconBrush;

	FLinearColor FleshColor = FLinearColor::White;
	FLinearColor HeadColor = FLinearColor::Yellow;
	FLinearColor RemoteColor = FLinearColor::Gray;
	FLinearColor OutlineColor = FLinearColor::Black;
	FLinearColor WhiteArmorOutlineColor = FLinearColor::Black;
};
