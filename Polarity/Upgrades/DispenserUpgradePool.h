// DispenserUpgradePool.h
// What the dispenser slot machine can hand out, edited by hand in one asset. Everything in it is a
// data asset (an upgrade, an attachment, a buff); the pickups in the boxes are made by code from
// them. What belongs to the THING (its rarity, its family, its look, its amount) lives in its own
// asset; what belongs to HANDING IT OUT (how often, and how often once the player has it) lives here.
// Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UpgradeDefinition.h"
#include "DispenserUpgradePool.generated.h"

class AActor;
class UStaticMesh;
class UTexture2D;
class UWeaponAttachmentDefinition;

/** What a small momentary buff does. */
UENUM(BlueprintType)
enum class EDispenserBuffKind : uint8
{
	/** Amount health, at once. */
	Heal,
	/** Amount armour, at once. */
	Armor,
	/** Amount magazines into the gun in hand (its reserve, or its cells). */
	Ammo,
};

/** A small momentary buff: not an upgrade, used up the moment it is taken. */
UCLASS(BlueprintType)
class POLARITY_API UDispenserBuffDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buff")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buff")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** What lies in the box. Empty: a plain sphere. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buff")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buff")
	EDispenserBuffKind Kind = EDispenserBuffKind::Heal;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buff", meta = (ClampMin = "0.0"))
	float Amount = 50.0f;
};

/** An upgrade in a slot, and how often it is drawn among the upgrades of the same rarity. */
USTRUCT(BlueprintType)
struct FDispenserUpgradeEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	TObjectPtr<UUpgradeDefinition> Upgrade = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

/** One action slot (jump, aim, slide...) and the upgrades that belong to it. */
USTRUCT(BlueprintType)
struct FDispenserUpgradeSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	FText DisplayName;

	/** One upgrade at a time, Hades-style: a new one replaces the one held, and only at a higher
	 *  level than it. Off: a bag of passives that sit side by side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	bool bExclusive = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TArray<FDispenserUpgradeEntry> Upgrades;
};

/** An attachment the reels may offer. Its rarity and family are in the attachment's own asset. */
USTRUCT(BlueprintType)
struct FDispenserAttachmentEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	TObjectPtr<UWeaponAttachmentDefinition> Attachment = nullptr;

	/** How often it is drawn among the attachments of the same rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Use WeightWhenEquipped instead of Weight while the player already has THIS attachment, in the
	 *  bag or on any of their guns (a scope: often while missing, rarely once found). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	bool bOverrideChanceWhenAlreadyEquipped = false;

	/** 0 = never offered again while the player has it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0.0", EditCondition = "bOverrideChanceWhenAlreadyEquipped"))
	float WeightWhenEquipped = 0.2f;
};

/** A buff the reels may offer. */
USTRUCT(BlueprintType)
struct FDispenserBuffEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	TObjectPtr<UDispenserBuffDefinition> Buff = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

UCLASS(BlueprintType)
class POLARITY_API UDispenserUpgradePool : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrades")
	TArray<FDispenserUpgradeSlot> Slots;

	/** What an upgrade looks like in its box: an AUpgradePickup Blueprint, for its hologram. Empty:
	 *  the plain C++ upgrade pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrades")
	TSubclassOf<AActor> UpgradePickupClass;

	/** The body the upgrade wears in the box, tinted by rarity and showing the upgrade's icon (its
	 *  material takes RarityColor and Icon). Empty: the Blueprint's own mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrades")
	TSoftObjectPtr<UStaticMesh> UpgradeCartridgeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachments")
	TArray<FDispenserAttachmentEntry> Attachments;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buffs")
	TArray<FDispenserBuffEntry> Buffs;

	/** Index of the slot Upgrade was put in, or INDEX_NONE. */
	int32 FindSlotOf(const UUpgradeDefinition* Upgrade) const;
};
