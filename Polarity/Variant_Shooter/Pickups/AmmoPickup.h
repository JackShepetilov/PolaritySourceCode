// AmmoPickup.h
// A pile of rounds for one specific weapon, lying in the world.
//
// It exists because rounds had no shape of their own. Whatever left the grid or would not fit in it
// came back as ANOTHER WHOLE WEAPON DROP carrying that many bullets, so a full bag printed a second
// copy of the gun, and a team standing around a drop could stack identical rifles out of nothing.
// Rounds are not a gun; this is what they look like instead.
//
// It also gives the level something to place: an ammo point is a reason to walk somewhere, and the
// cell economy makes taking one an actual decision (a magazine costs the same cell as money).
//
// Everything visible lives INSIDE the Niagara system (NS_AmmoPickup), not on this actor:
//
//   - mesh 0 is the ammo shape,
//   - mesh 1 has no mesh of its own and is bound to the user parameter "Static Mesh",
//   - both wear M_PickupTint, whose emissive IS Particle Color, and the particle's colour is the
//     user parameter "AmmoColor".
//
// That is why the gun turns with the ammo and shares its colour: they are two meshes of the same
// particle, not two components that would have to be kept in step by hand. This class only answers
// the two questions the system cannot: which colour, and which gun.
//
// One ammo pickup Blueprint serves the whole game, and since 2026-09-01 there is only one kind of
// rounds: ammo is a single pool that every weapon eats from, so the pile has a colour and a shape
// of its own rather than borrowing a gun's.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "AmmoPickup.generated.h"

class UStaticMesh;
class UInventoryComponent;
class UNiagaraComponent;

UCLASS(Blueprintable)
class POLARITY_API AAmmoPickup : public AInventoryPickup
{
	GENERATED_BODY()

public:

	AAmmoPickup();

	// ==================== What it is ====================

	/** How many rounds are in the pile. Zero means one cell's worth, which is the useful default
	 *  for a hand-placed ammo point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo Pickup", meta = (ClampMin = "0"))
	int32 Rounds = 0;

	// ==================== Look ====================

	/** The swirl. Set its system (NS_AmmoPickup) on this component in the Blueprint; everything
	 *  else about it is driven from the weapon through the two user parameters below. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> Effect;

	/** Niagara user parameter carrying the tint. Must match the name in the system.
	 *
	 *  A mismatch is SILENT: Niagara ignores a write to a parameter that is not there and the pile
	 *  simply renders in the system's authored default, which looks like the palette being wrong
	 *  rather than the name being wrong. The log line in ApplyWeaponLook says what was written. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo Pickup|Look")
	FName ColorParameterName = FName("AmmoColor");

	/** Niagara user parameter carrying the gun's shape. Same silent-failure warning as above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo Pickup|Look")
	FName MeshParameterName = FName("Static Mesh");

	/** Push the pile's colour and mesh into the Niagara system. Safe to call again. */
	UFUNCTION(BlueprintCallable, Category = "Ammo Pickup")
	void ApplyWeaponLook();

	/** The tint the swirl wears. One pool of rounds, one colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo Pickup|Look")
	FLinearColor AmmoColor = FLinearColor(1.0f, 0.75f, 0.2f, 1.0f);

	/** The shape in the swirl. Set it in the Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo Pickup|Look")
	TObjectPtr<UStaticMesh> AmmoMesh = nullptr;

protected:

	virtual void BeginPlay() override;

	/** A pile spawned from a thrown-away cell learns its weapon and its count from the item. */
	virtual void InitializeSpawnedItem(const FInventoryItem& InItem) override;

	/** Rounds stack to the inventory's cell size. */
	virtual FInventoryItem MakeItemFor(const UInventoryComponent& Inventory) const override;

private:

	/** Fill Item from Rounds. */
	void RebuildItem();
};
