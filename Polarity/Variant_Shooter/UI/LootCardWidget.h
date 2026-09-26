// LootCardWidget.h
// What the player is about to fetch with the grapple, in the spirit of Apex's loot card: one card, on
// the thing under the crosshair, saying what it is, how rare, and what pressing the key would do.
// Two sizes of the same answer:
//   - far (anywhere inside the fetch radius): just the item's name, over the brackets;
//   - close (inside LootCardFullDistance): the full card over the brackets -- key and verb, the
//     category and rarity line, icon, name, a line of description, stat rows, chips, and what taking
//     it would replace.
//
// Started as the weapon drop card and grew to every fetchable (the author, 2026-09-26): dropped guns,
// melee drops, upgrades, abilities, scripted pickups, inventory pickups (money, ammo, attachments),
// the riot shield and the items in the dispenser's boxes. What each of them says is gathered in
// BuildData, one branch per kind, reading only what the owning client already has.
//
// One instance per local player, created and driven by AShooterCharacter::UpdateGrappleFetchAiming.
// Inherit in Blueprint and add any of the optional widgets below; C++ fills whichever exist, and the
// two events hand the Blueprint everything else it may want to style from.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "LootCardWidget.generated.h"

class AActor;
class AShooterCharacter;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;

/** What kind of thing the card is about. Drives the category line and lets a Blueprint pick a look. */
UENUM(BlueprintType)
enum class ELootCardCategory : uint8
{
	Weapon,
	Melee,
	Upgrade,
	Ability,
	Attachment,
	Ammo,
	Money,
	Shield,
	Buff,
	/** A scripted pickup, or anything else without a kind of its own. */
	Item
};

/** What pressing the key would do with this item, for this player.
 *
 *  Append only: the first three are saved in Blueprints under the enum's old name. */
UENUM(BlueprintType)
enum class ELootTakeKind : uint8
{
	/** Into an empty place: a new gun into a free loot slot, anything into the bag. */
	PickUp,
	/** A new gun, and the looted gun already carried goes back on the ground. Also an upgrade that
	 *  pushes another one out of its slot. */
	Replace,
	/** The player already carries this gun, so the drop is worth its rounds only. */
	AmmoOnly,
	/** Used up on the spot: a buff from the dispenser, an ability, a scripted pickup. */
	Take,
	/** An upgrade the player already owns, one level up. */
	LevelUp,
	/** No room for it: the bag is full, or the upgrade is at its last level. */
	NoRoom
};

/** Everything the card shows, gathered once per item. */
USTRUCT(BlueprintType)
struct FLootCardData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	ELootCardCategory Category = ELootCardCategory::Item;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	TObjectPtr<UTexture2D> Icon;

	/** False for things that have no rarity (money, ammo, a plain gun): the rarity bar stays hidden
	 *  and the category line has no rarity word. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	bool bHasRarity = false;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	EUpgradeRarity Rarity = EUpgradeRarity::Common;

	/** The rarity's colour from the palette (Palette.Rarity.*), or a plain one without a rarity. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FLinearColor RarityColor = FLinearColor::White;

	/** "WEAPON", "ATTACHMENT | MAGAZINE | EPIC", "UPGRADE | RARE". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText CategoryLine;

	/** Label and value rows: damage and RPM on a gun, the level's rows on an upgrade, the size a
	 *  magazine gives each of the player's guns. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	TArray<FUpgradeStat> Stats;

	/** Short tags under the name: the guns an attachment fits. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	TArray<FText> Chips;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	ELootTakeKind TakeKind = ELootTakeKind::PickUp;

	/** What leaves to make room: the looted gun, the upgrade in the slot, the attachment on the gun.
	 *  Empty when nothing does. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText ReplacedName;

	/** The verb after the key: "PICK UP", "REPLACE Rifle", "LEVEL 1 > 2", "BAG FULL". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText ActionVerb;

	/** The comparison line under the card: "Replace: EMPTY", "Replaces: Scope 2x", "Fits: 40 of 60". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText CompareLine;

	/** Units in the pile, for money and ammo. 0 for the rest. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	int32 Count = 0;

	// ---------- Weapon only ----------

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	TObjectPtr<UTexture2D> AmmoBadge;

	/** Palette entry that tints this weapon's ammo. @see AShooterWeapon::AmmoColorTag */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	FGameplayTag AmmoColorTag;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	float RoundsPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	int32 MagazineSize = 0;

	/** Rounds that come with this drop: loaded plus spare. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	int32 Rounds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card|Weapon")
	bool bFullAuto = false;

	/** The key bound to the ability, as the player's bindings name it. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card")
	FText KeyLabel;
};

UCLASS(Abstract, Blueprintable)
class POLARITY_API ULootCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show the card for this item this frame. ScreenPosition and BracketPixelRadius are the brackets'
	 *  centre and size, so the name can sit above them and the full card beside them. Hides the card
	 *  when the item has nothing to say (an empty dispenser box). */
	void ShowFor(const AActor* Target, const AShooterCharacter* Viewer,
		const FVector2D& ScreenPosition, float BracketPixelRadius, bool bShowFullCard, const FText& KeyLabel);

	/** Nothing bracketed this frame. */
	void HideCard();

	UFUNCTION(BlueprintPure, Category = "Loot Card")
	const FLootCardData& GetCardData() const { return CardData; }

	UFUNCTION(BlueprintPure, Category = "Loot Card")
	bool IsFullCard() const { return bFullCard; }

	/** Gather what the card says about Target for Viewer. False when there is nothing to show. */
	static bool BuildData(const AActor* Target, const AShooterCharacter* Viewer, const FText& KeyLabel,
		FLootCardData& OutData);

	/** The colour of a rarity: the palette's Palette.Rarity.<Name> when set, else the dispenser's. */
	static FLinearColor GetRarityColor(EUpgradeRarity Rarity);

	// ==================== Layout ====================

	/** Gap between the top of the brackets and the bottom of the name (pixels). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Loot Card|Layout")
	float CompactGap = 10.0f;

	/** Gap between the top of the brackets and the bottom of the full card (pixels). The card sits
	 *  over the item, the way Apex shows it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Loot Card|Layout")
	float CardGap = 16.0f;

	/** How much of the rarity colour the card's background takes (CardPanel, when it is a Border):
	 *  0 keeps the Blueprint's own colour, 1 is the full rarity colour. The alpha stays the
	 *  Blueprint's. Things without a rarity keep the Blueprint's colour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Loot Card|Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RarityTint = 0.35f;

	/** The brackets are drawn a little outside the target. Matches UCaptureReticleWidget::BracketPadding
	 *  so the name clears the brackets rather than the item. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Loot Card|Layout", meta = (ClampMin = "1.0"))
	float BracketPadding = 1.25f;

protected:
	/** Fires when the item, or what taking it would mean, changes. Not every frame. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot Card", meta = (DisplayName = "On Card Data Changed"))
	void BP_OnCardDataChanged(const FLootCardData& NewData);

	/** Fires when the card grows from the name into the full card, and back. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot Card", meta = (DisplayName = "On Full Card Changed"))
	void BP_OnFullCardChanged(bool bNowFull);

	// ==================== Optional widgets (by name) ====================

	/** Shown far away: holds the name over the brackets. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CompactPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CompactNameText;

	/** Shown close: the whole card. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CardPanel;

	/** "[G] PICK UP", "[G] REPLACE Rifle", "[G] LEVEL 1 > 2". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	/** "ATTACHMENT | MAGAZINE | EPIC". Tinted with the rarity colour. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CategoryText;

	/** The Apex stripe: tinted with the rarity colour, hidden for things without a rarity. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UImage> RarityBar;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	/** Every stat row, one per line: "Damage  24". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatsText;

	/** The chips in one line: "Sniper | LMG | AR". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ChipsText;

	/** "Replace: EMPTY", "Replaces: Scope 2x". */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CompareText;

	/** The row of gun numbers with its labels (DMG, RATE, MAG, AMMO). Shown for guns only. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WeaponStatsPanel;

	/** "AUTO" or "SEMI". Weapons only. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FireModeText;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DamageText;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FireRateText;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MagazineText;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RoundsText;

	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	/** Tinted by the weapon's AmmoColorTag from the palette. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot Card", meta = (BindWidgetOptional))
	TObjectPtr<UImage> AmmoBadgeImage;

private:
	/** Push CardData into whichever optional widgets exist. */
	void ApplyData();

	FLootCardData CardData;

	/** What CardData was built from, so it is pushed on change only. */
	TWeakObjectPtr<const AActor> ShownTarget;

	bool bFullCard = false;
	bool bShown = false;

	/** CardPanel's own colour, read once, so the rarity tint can always be undone. */
	FLinearColor CardBaseColor = FLinearColor::White;
	bool bCardBaseColorRead = false;
};
