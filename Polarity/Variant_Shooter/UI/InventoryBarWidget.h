// InventoryBarWidget.h
// The weapon block in the corner of the screen: two weapon rows and nothing else.
//
// This used to hold the cell grid as well, and that was the mistake. Apex splits the inventory
// across two surfaces with different jobs, and the split is the design:
//
//  - the corner carries only what is read with peripheral vision while shooting: which two guns
//    are carried, which one is up, how many rounds are in it and how many are left over;
//  - everything that has to be thought about lives in the inventory overlay, behind a key.
//
// The grid moved to UInventoryScreenWidget for that reason. The class keeps its old name because
// WBP_InventoryBar inherits from it and BP_ShooterPlayerController points at that Blueprint;
// renaming a UCLASS would need a redirect bought with nothing but a nicer word.
//
// See Docs/Inventory_Apex_Rework_Plan_2026-08-28.md.
//
// Driver pattern copied from UAbilityResourceBar: this widget subscribes to the gameplay delegates
// itself and rebuilds, rather than being polled from a tick or pushed at from gameplay code.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Variant_Shooter/Inventory/InventoryTypes.h"
#include "Hud/HudBindable.h"
#include "InventoryBarWidget.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UInventoryComponent;
class UInventorySlotWidget;
class UPanelWidget;

/**
 * HUD block for the two weapon slots. Inherit in Blueprint (WBP_InventoryBar), give it the two
 * attachment containers by name, and set the attachment square class.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UInventoryBarWidget : public UUserWidget, public IHudBindable
{
	GENERATED_BODY()

public:

	// IHudBindable: the HUD registry drives this widget through InitializeFor / Shutdown.
	virtual void BindCharacter(AShooterCharacter* Character) override { InitializeFor(Character); }
	virtual void UnbindCharacter() override { Shutdown(); }


	/** Bind to the character's inventory and weapons and draw the current state.
	 *  Safe to call again after a respawn: it unbinds the previous character first. */
	UFUNCTION(BlueprintCallable, Category = "Inventory Bar")
	void InitializeFor(AShooterCharacter* InCharacter);

	/** Unbind everything. */
	UFUNCTION(BlueprintCallable, Category = "Inventory Bar")
	void Shutdown();

protected:

	virtual void NativeDestruct() override;

	// ==================== Containers (must exist in the WBP under these names) ====================

	/** Attachment squares beside the first weapon row. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> FirstWeaponAttachments;

	/** Attachment squares beside the second weapon row. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> SecondWeaponAttachments;

	// ==================== Classes (set in the WBP) ====================

	/** 48x48 square used beside a weapon. Same C++ class as a grid cell, different Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Bar|Classes")
	TSubclassOf<UInventorySlotWidget> AttachmentSlotClass;

	// ==================== Blueprint hooks ====================

	/** Draw one weapon row.
	 *
	 *  RowIndex is a SLOT, not a role: 0 is the first weapon owned and 1 the second, and they never
	 *  trade places. Sorting the equipped one to the front was tried and it made both names jump
	 *  sideways on every switch. bIsEquipped is what moves instead, and the Blueprint uses it to
	 *  decide which row fills the big plate and which name segment lights up.
	 *
	 *  The weapon itself is handed over rather than a name and an icon, because AShooterWeapon
	 *  carries no display name today and inventing one here would put a second source of truth next
	 *  to the Blueprint that already knows what the gun looks like. Weapon is null for an empty row,
	 *  which the Blueprint should draw as an empty holster.
	 *
	 *  ReserveAmmo is this gun's own rounds, read out of the grid: ammo is never pooled between
	 *  weapons. Drawn under the magazine count, the way Apex reads "20" over "100".
	 *
	 *  bInfiniteAmmo is the class weapon, which is energy and never runs out. Its reserve is
	 *  meaningless rather than zero, so the Blueprint hides the number instead of drawing a 0 that
	 *  would read as "empty". */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Bar", meta = (DisplayName = "Set Weapon Row"))
	void BP_SetWeaponRow(int32 RowIndex, AShooterWeapon* Weapon, int32 CurrentAmmo, int32 MagazineSize,
		int32 ReserveAmmo, bool bInfiniteAmmo, bool bIsEquipped);

private:

	/** Rebuild everything. Both weapon rows are two squares and a plate, so a full rebuild costs
	 *  less than working out what changed. */
	void Rebuild();

	void RebuildWeaponRow(int32 RowIndex, AShooterWeapon* Weapon, const UInventoryComponent* Inventory);
	/** Draw one weapon's attachment squares. Read only: the corner is glanced at while shooting, so
	 *  nothing here is a drop target and nothing starts a drag. Fitting is done on the overlay. */
	void RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon, int32 FreeSlots, int32 MaxSlots);

	/** Reuses the square already at Index in Container, or spawns one. Reuse matters because the
	 *  block is rebuilt on every pickup, and rebuilding from scratch would restart any Blueprint
	 *  animation on squares that did not change. */
	UInventorySlotWidget* GetOrCreateSquare(UPanelWidget* Container, int32 Index, TSubclassOf<UInventorySlotWidget> SquareClass);

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleWeaponInventoryChanged();

	/** Every round fired, every reload and every weapon switch arrives here.
	 *
	 *  Without this the row is drawn once when it binds and never again, which reads as an ammo
	 *  counter frozen at a full magazine, and as an "equipped" highlight that never moves. The
	 *  arguments are ignored on purpose: the rebuild reads the weapons themselves, so there is one
	 *  path to the numbers rather than two that can disagree. */
	UFUNCTION()
	void HandleBulletCount(int32 MagazineSize, int32 Bullets);

	UPROPERTY(Transient)
	TWeakObjectPtr<AShooterCharacter> BoundCharacter;
};
