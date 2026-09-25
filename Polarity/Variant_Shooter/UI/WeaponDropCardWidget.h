// WeaponDropCardWidget.h
// What the player is about to fetch with the grapple. Two sizes of the same answer:
//   - far (anywhere inside the fetch radius): just the weapon's name, over the brackets;
//   - close (inside WeaponCardFullDistance): a full card beside the brackets, in the spirit of
//     Apex's loot card -- key and verb, icon, name, a line of description, the numbers that decide
//     whether it is worth taking, and what it would replace.
//
// One instance per local player, created and driven by AShooterCharacter::UpdateGrappleFetchAiming.
// Inherit in Blueprint and add any of the optional widgets below; C++ fills whichever exist, and the
// two events hand the Blueprint everything else it may want to style from.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "WeaponDropCardWidget.generated.h"

class ADroppedRangedWeapon;
class AShooterCharacter;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;

/** What pressing the key would do with this drop, for this player. */
UENUM(BlueprintType)
enum class EWeaponDropTakeKind : uint8
{
	/** A new gun into an empty loot slot. */
	PickUp,
	/** A new gun, and the looted gun already carried goes back on the ground. */
	Replace,
	/** The player already carries this gun, so the drop is worth its rounds only. */
	AmmoOnly
};

/** Everything the card shows, gathered once per drop. */
USTRUCT(BlueprintType)
struct FWeaponDropCardData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	FText WeaponName;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	TObjectPtr<UTexture2D> AmmoBadge;

	/** Palette entry that tints this weapon's ammo. @see AShooterWeapon::AmmoColorTag */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	FGameplayTag AmmoColorTag;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	float RoundsPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	int32 MagazineSize = 0;

	/** Rounds that come with this drop: loaded plus spare. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	int32 Rounds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	bool bFullAuto = false;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	EWeaponDropTakeKind TakeKind = EWeaponDropTakeKind::PickUp;

	/** The looted gun that goes back on the ground. Empty unless TakeKind is Replace. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	FText ReplacedWeaponName;

	/** The key bound to the ability, as the player's bindings name it. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card")
	FText KeyLabel;
};

UCLASS(Abstract, Blueprintable)
class POLARITY_API UWeaponDropCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show the card for this drop this frame. ScreenPosition and BracketPixelRadius are the brackets'
	 *  centre and size, so the name can sit above them and the full card beside them. */
	void ShowForDrop(const ADroppedRangedWeapon* Drop, const AShooterCharacter* Viewer,
		const FVector2D& ScreenPosition, float BracketPixelRadius, bool bShowFullCard, const FText& KeyLabel);

	/** Nothing bracketed this frame. */
	void HideCard();

	UFUNCTION(BlueprintPure, Category = "Weapon Card")
	const FWeaponDropCardData& GetCardData() const { return CardData; }

	UFUNCTION(BlueprintPure, Category = "Weapon Card")
	bool IsFullCard() const { return bFullCard; }

	// ==================== Layout ====================

	/** Gap between the top of the brackets and the bottom of the name (pixels). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Card|Layout")
	float CompactGap = 10.0f;

	/** Gap between the right side of the brackets and the left edge of the full card (pixels). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Card|Layout")
	float CardGap = 28.0f;

	/** The brackets are drawn a little outside the target. Matches UCaptureReticleWidget::BracketPadding
	 *  so the name clears the brackets rather than the gun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Card|Layout", meta = (ClampMin = "1.0"))
	float BracketPadding = 1.25f;

protected:
	/** Fires when the drop, or what taking it would mean, changes. Not every frame. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon Card", meta = (DisplayName = "On Card Data Changed"))
	void BP_OnCardDataChanged(const FWeaponDropCardData& NewData);

	/** Fires when the card grows from the name into the full card, and back. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon Card", meta = (DisplayName = "On Full Card Changed"))
	void BP_OnFullCardChanged(bool bNowFull);

	// ==================== Optional widgets (by name) ====================

	/** Shown far away: holds the name over the brackets. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CompactPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CompactNameText;

	/** Shown close: the whole card. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CardPanel;

	/** "[G] PICK UP", "[G] REPLACE Rifle", "[G] TAKE AMMO". */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	/** "AUTO" or "SEMI". */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FireModeText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DamageText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FireRateText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MagazineText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RoundsText;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	/** Tinted by the weapon's AmmoColorTag from the palette. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Card", meta = (BindWidgetOptional))
	TObjectPtr<UImage> AmmoBadgeImage;

private:
	/** Gather what the card says about this drop for this viewer. */
	static FWeaponDropCardData BuildData(const ADroppedRangedWeapon* Drop, const AShooterCharacter* Viewer,
		const FText& KeyLabel);

	/** Push CardData into whichever optional widgets exist. */
	void ApplyData();

	FWeaponDropCardData CardData;

	/** What CardData was built from, so it is rebuilt on change only. */
	TWeakObjectPtr<const ADroppedRangedWeapon> ShownDrop;

	bool bFullCard = false;
	bool bShown = false;
};
