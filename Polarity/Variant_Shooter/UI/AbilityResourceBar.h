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

	/** Create (or fetch existing) entry for Key, inserted in left-to-right priority order. */
	UBarEntryWidget* AddOrGetEntry(FName Key, TSubclassOf<UBarEntryWidget> EntryClass);

	/** Play the entry's outro animation, then unparent it. */
	void RemoveEntry(FName Key);

	/** Left-to-right ordering weight for a key (lower = further left). */
	static int32 GetEntryPriority(FName Key);

	// ==================== Bound systems ====================

	TWeakObjectPtr<AShooterCharacter> BoundCharacter;
	TWeakObjectPtr<UAbilityComponent> BoundAbilityComp;
	TWeakObjectPtr<UUpgradeManagerComponent> BoundUpgradeManager;

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

	// ==================== Resource handlers ====================

	UFUNCTION() void HandleHealPoolChanged(int32 CurrentCount, int32 MaxCount);

	// ==================== Entry keys ====================

	static const FName Key_Ability;
	static const FName Key_Ammo;
	static const FName Key_Melee;
	static const FName Key_Heal;
};
