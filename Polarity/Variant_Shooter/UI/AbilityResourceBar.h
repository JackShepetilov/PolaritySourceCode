// AbilityResourceBar.h
// Screen-space HUD bar that holds a dynamic row of small entries (UBarEntryWidget):
//   - the active ability       (icon + shared cooldown radial)
//   - the current limited-ammo weapon (bullet count) OR the equipped melee weapon (remaining hits)
//   - stored heal charges      (shared Health-Blast / Charged-Punch pool)
//
// The bar is its own driver: InitializeFor(character) subscribes to the relevant gameplay
// delegates and translates them into AddOrGetEntry / RemoveEntry / SetCount / SetCooldown calls.
// Mirrors the register/unregister ownership pattern of UEMFChargeWidgetSubsystem, but screen-space
// and player-scoped. Created by AShooterPlayerController alongside the bullet-counter HUD.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AbilityResourceBar.generated.h"

class UHorizontalBox;
class UBarEntryWidget;
class UTexture2D;
class AShooterCharacter;
class UAbilityComponent;
class UUpgradeManagerComponent;
class UUpgradeDefinition;
class AShooterWeapon;
class UInputAction;
class UTutorialSubsystem;

/**
 * HUD container for the ability/resource entry row. Inherit in Blueprint (WBP_AbilityResourceBar),
 * provide an EntryContainer Horizontal Box and the entry classes / icons below.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UAbilityResourceBar : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Bind to the character's ability/weapon/upgrade systems and seed initial entries.
	 *  Safe to call again after respawn — it unbinds the previous character first. */
	UFUNCTION(BlueprintCallable, Category = "Ability Bar")
	void InitializeFor(AShooterCharacter* InCharacter);

	/** Unbind all delegates and clear every entry. */
	UFUNCTION(BlueprintCallable, Category = "Ability Bar")
	void Shutdown();

protected:

	virtual void NativeDestruct() override;

	/** Row container the entries are inserted into. Must exist in the WBP with this exact name. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> EntryContainer;

	// ==================== Entry classes (set in WBP) ====================

	/** Widget class for the ability entry (icon + cooldown radial). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Bar|Classes")
	TSubclassOf<UBarEntryWidget> AbilityEntryClass;

	/** Widget class for numeric entries (ammo / melee hits / heal charges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Bar|Classes")
	TSubclassOf<UBarEntryWidget> CountEntryClass;

	// ==================== Static icons (set in WBP) ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Bar|Icons")
	TObjectPtr<UTexture2D> AmmoIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Bar|Icons")
	TObjectPtr<UTexture2D> MeleeIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability Bar|Icons")
	TObjectPtr<UTexture2D> HealIcon;

private:

	// ==================== Entry management ====================

	UPROPERTY()
	TMap<FName, TObjectPtr<UBarEntryWidget>> Entries;

	/** Create (or fetch existing) entry for Key. On first creation, if FirstTimeCategory is set and this
	 *  is the first time that category is shown this session, plays the celebratory first-time intro. */
	UBarEntryWidget* AddOrGetEntry(FName Key, TSubclassOf<UBarEntryWidget> EntryClass, FName FirstTimeCategory = NAME_None);

	/** Play the entry's outro animation, then unparent it. */
	void RemoveEntry(FName Key);

	// ==================== Bound systems ====================

	TWeakObjectPtr<AShooterCharacter> BoundCharacter;
	TWeakObjectPtr<UAbilityComponent> BoundAbilityComp;
	TWeakObjectPtr<UUpgradeManagerComponent> BoundUpgradeManager;

	/** Last active weapon seen by HandleBulletCount — used to detect weapon switches so the
	 *  switch-to-equip hints (hidden on the active weapon) refresh on swap, not just on inventory change. */
	TWeakObjectPtr<AShooterWeapon> LastSeenActiveWeapon;

	/** Last common-cooldown duration reported by OnCooldownStarted, so a slot switch mid-cooldown
	 *  can re-apply the radial with the correct total. */
	float LastCooldownDuration = 0.0f;

	void UnbindAll();

	// ==================== Ability handlers ====================

	UFUNCTION() void HandleAbilityAdded(int32 SlotIndex);
	UFUNCTION() void HandleAbilityRemoved(int32 SlotIndex);
	UFUNCTION() void HandleAbilitySwitched(int32 NewActiveSlot);
	UFUNCTION() void HandleCooldownStarted(float Duration);
	UFUNCTION() void HandleCooldownEnded();

	/** Re-evaluate the active ability: add/refresh its entry, or remove it if there is none. */
	void RefreshAbilityEntry();

	// ==================== Weapon handlers ====================

	UFUNCTION() void HandleBulletCount(int32 MagazineSize, int32 Bullets);
	UFUNCTION() void HandleMeleeEquipped(bool bEquipped, int32 RemainingHits, int32 MaxHits);
	UFUNCTION() void HandleWeaponInventoryChanged();

	/** Rebuild the per-weapon entry set against the owned-weapon inventory (one entry per owned
	 *  limited-ammo or melee weapon): updates icon/count and the switch-to-equip keybind hint
	 *  (hint shown only on NON-active weapons). */
	void RefreshWeaponEntries();

	/** Stable per-weapon entry key (prefixed "weapon."). */
	static FName WeaponEntryKey(const AShooterWeapon* Weapon);

	/** True if Key belongs to a per-weapon entry. */
	static bool IsWeaponEntryKey(FName Key);

	// ==================== Resource handlers ====================

	UFUNCTION() void HandleHealPoolChanged(int32 CurrentCount, int32 MaxCount);
	UFUNCTION() void HandleUpgradeGranted(UUpgradeDefinition* Definition);
	UFUNCTION() void HandleUpgradeRemoved(UUpgradeDefinition* Definition);

	/** Re-evaluate the heal-charge entry: shown only when count>0 AND a consumer upgrade is owned. */
	void RefreshHealEntry();

	// ==================== Keybind hint / first-time helpers ====================

	/** Resolve Action's current key for the local player and push it to the entry's hint (text + icon
	 *  via UTutorialSubsystem). Null Action hides the hint. */
	void ApplyKeybindHint(UBarEntryWidget* Entry, UInputAction* Action);

	/** GameInstance tutorial subsystem (hosts the key resolver + first-time tracking). May be null. */
	UTutorialSubsystem* GetTutorialSubsystem() const;

	// ==================== Entry keys ====================

	static const FName Key_Ability;
	static const FName Key_Heal;
};
