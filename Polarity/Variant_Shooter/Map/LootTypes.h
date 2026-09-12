// LootTypes.h
// The words the loot generator is written in.
//
// Rarity here is CONTINUOUS [author, 2026-09-02]. There are no Common/Rare/Epic buckets in the
// simulation and no per-tier quotas: a thing has a Value between 0 and 1, a place has a Quality
// between 0 and 1, and a slot is a number drawn around that Quality. Buckets survive only as
// ELootBand, which exists to put a colour on the screen and is read by nothing that rolls.
//
// Why continuous rather than four tiers: half of what this game drops is an AMOUNT - rounds,
// money, armour, healing - and an amount was already continuous. Expressing "30 rounds" and "180
// rounds" as two rows of a table was a bucket pretending to be data. Now one row carries a range
// and the roll picks a point inside it, which is the difference between a list and a generator.
//
// The shape of the roll is still Apex's (apexlegends.wiki.gg/wiki/Loot, read 2026-09-02): a place
// is worth a certain amount, a slot rolls a category and then a thing, and a few small places
// carry their own table and ignore the place they are standing in.

#pragma once

#include "CoreMinimal.h"
#include "LootTypes.generated.h"

/** What kind of thing it is. One per pool asset. Every one of these is backed by a pickup class
 *  that already exists in the project - this list is what the game HAS, not what it might want. */
UENUM(BlueprintType)
enum class ELootCategory : uint8
{
	/** AAmmoPickup. One pool for every weapon; the roll scales Rounds. */
	Ammo,

	/** ACurrencyPickup. The roll scales Amount, and the stack is the cell dilemma. */
	Money,

	/** AAttachmentPickup. Nothing to scale: an attachment is what it is, so its Value is the
	 *  designer's opinion of it and the roll only decides whether it comes up. */
	Attachment,

	/** AHealthPickup. The roll scales HealAmount. */
	Healing,

	/** AArmorPickup. The roll scales ArmorAmount. */
	Armor,

	/** AAbilityPickup. */
	Ability,

	/** AUpgradePickup. */
	Upgrade
};

/**
 * A colour band. FOR THE EYE ONLY [author, 2026-09-02].
 *
 * Nothing rolls on this and nothing is stored as this. A player has to judge a floor at a glance
 * without reading numbers, and a smooth gradient from white to gold is exactly the thing nobody can
 * read at a glance - 0.55 and 0.65 look identical across a room. So the maths stays continuous and
 * the screen gets four steps.
 */
UENUM(BlueprintType)
enum class ELootBand : uint8
{
	Common,
	Rare,
	Epic,
	Legendary
};

/** One line of a loot pool: a thing that can come up, how good it is, and how much of it. */
USTRUCT(BlueprintType)
struct POLARITY_API FPoiLootEntry
{
	GENERATED_BODY()

	/** What to spawn. Any actor: the three inventory pickups (ammo, money, attachment) plus health,
	 *  armour, abilities and upgrades, which are pickups without being inventory items. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	TSubclassOf<AActor> PickupClass = nullptr;

	/** Which pool this belongs in. The pool overwrites it when the line is rolled, so a line in the
	 *  wrong pool is a typo rather than a decision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	ELootCategory Category = ELootCategory::Ammo;

	/** How good this line is, 0..1. A slot rolls a number and the lines nearest that number are the
	 *  ones that can come up: this is the whole ordering of the game's loot on one axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Value = 0.0f;

	/** Against other lines of the same category at a similar Value. Two at 1 and 3 are a quarter and
	 *  three quarters of the lines that close; the number means nothing on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** How many actors this line puts down. Interpolated by the slot's roll, so a good roll on a
	 *  1..3 line is three of them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1", ClampMax = "20"))
	int32 CountMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1", ClampMax = "20"))
	int32 CountMax = 1;

	/** What the pickup's own payload is multiplied by: Rounds on ammo, Amount on money, HealAmount
	 *  on a medkit, ArmorAmount on a plate. Interpolated by the same roll.
	 *
	 *  A multiplier rather than an absolute number on purpose - one pair of fields then works for
	 *  every category, and the pickup asset stays the place its own size is decided. 1..1 means the
	 *  line hands over exactly what the pickup is worth by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.01"))
	float AmountScaleMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.01"))
	float AmountScaleMax = 1.0f;

	bool IsMoney() const { return Category == ELootCategory::Money; }
};

/** One thing a sheet was told to put down: the line that won, and the roll that won it. The roll
 *  is carried along because it is what decides how much, and what colour it wears. */
USTRUCT(BlueprintType)
struct POLARITY_API FLootRoll
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	FPoiLootEntry Entry;

	/** The slot's rolled value, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	float Value = 0.0f;

	/** How many actors, already resolved from Value. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 Count = 1;

	/** What the payload is multiplied by, already resolved from Value. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	float AmountScale = 1.0f;
};

/** Loot maths that more than one class needs. Free functions, because none of this has state. */
namespace PolarityLoot
{
	/** The colour band a value wears. The only place the four buckets exist. */
	POLARITY_API ELootBand BandForValue(float Value);

	/** Scales whatever payload this pickup happens to have: Rounds, Amount, HealAmount,
	 *  ArmorAmount. Called before the actor finishes spawning, because every one of those is read
	 *  in BeginPlay and a value written afterwards is a number nobody looks at. Returns false when
	 *  the class has no payload to scale, which is normal for attachments. */
	POLARITY_API bool ApplyAmountScale(AActor* Pickup, float Scale);
}
