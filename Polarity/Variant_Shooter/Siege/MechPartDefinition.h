// MechPartDefinition.h
// One mech part: which slot of the mech it fills and how rare it is.
//
// The forest camps on the lanes map hand these out (ASiegeCampSite, Docs/Lane_Camps_Design_2026-09-30.md).
// A part goes straight into the looter's inventory as an EInventorySlotKind::MechPart cell, and the mech
// is assembled at the farm's barn once the team holds one part of every slot (ASiegeMechBay). The slots
// follow the Titanfall 2 titans (Docs/MOBA_Lanes_Concept_2026-09-29.md, section 6). What a part does to
// the mech is the second act's business; the demo only needs the slot and the rarity.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "MechPartDefinition.generated.h"

class UStaticMesh;
class UTexture2D;

/** Which slot of the mech a part fills. The mech needs one of each. */
UENUM(BlueprintType)
enum class EMechPartSlot : uint8
{
	Chassis,
	MainWeapon,
	Tactical,
	Heavy,
	Core,

	Count UMETA(Hidden)
};

UCLASS(BlueprintType)
class POLARITY_API UMechPartDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mech Part")
	EMechPartSlot Slot = EMechPartSlot::Chassis;

	/** Set by the camp's tier: easy camps give Common, medium Rare, hard Epic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mech Part")
	EUpgradeRarity Rarity = EUpgradeRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mech Part")
	FText DisplayName;

	/** The inventory cell's picture. None = the cell shows no picture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mech Part")
	TSoftObjectPtr<UTexture2D> Icon;

	/** What lies on the crate in the camp. The shape tells the slot from a distance, so a player can
	 *  see what a camp holds before choosing to fight for it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mech Part")
	TObjectPtr<UStaticMesh> DisplayMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mech Part")
	FVector DisplayScale = FVector(1.0f);

	static FString SlotName(EMechPartSlot InSlot);
};
