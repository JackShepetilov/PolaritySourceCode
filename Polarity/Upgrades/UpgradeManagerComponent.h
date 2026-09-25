// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UpgradeDefinition.h"
#include "UpgradeManagerComponent.generated.h"

class UUpgradeDefinition;
class UUpgradeComponent;
class UUpgradeRegistry;
class AShooterWeapon;
class ADroppedRangedWeapon;
class UInputAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeGranted, UUpgradeDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeRemoved, UUpgradeDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUpgradeLeveledUp, UUpgradeDefinition*, Definition, int32, NewLevel);

/** How a dispenser card changes the player's upgrades. */
UENUM(BlueprintType)
enum class EUpgradeOfferKind : uint8
{
	/** Not owned, its slot free (or a passive): granted at the card's level. */
	New,
	/** Already owned: raised by the card's levels. */
	LevelUp,
	/** Its slot holds another upgrade: that one goes, this one comes a level above it (Hades). */
	Replace,
};

/** One card of a dispenser offer, decided entirely on the server. */
USTRUCT(BlueprintType)
struct FUpgradeOfferCard
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Offer")
	TObjectPtr<UUpgradeDefinition> Definition = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Offer")
	EUpgradeOfferKind Kind = EUpgradeOfferKind::New;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Offer")
	EUpgradeRarity Rarity = EUpgradeRarity::Common;

	/** Level before (0 for a new one) and after taking the card. */
	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Offer")
	int32 FromLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Offer")
	int32 ToLevel = 1;

	/** Replace only: the upgrade that leaves its slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Offer")
	TObjectPtr<UUpgradeDefinition> Replaces = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUpgradeOfferChanged);

/** Broadcast when the shared health-pickup pool count changes (used by HealthBlast, ChargedPunch, future upgrades) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStoredHealthPickupsChanged, int32, CurrentCount, int32, MaxCount);

/**
 * Manages all active upgrades on the owning ShooterCharacter.
 * Handles granting, removing, querying, and persistence of upgrades.
 */
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class POLARITY_API UUpgradeManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UUpgradeManagerComponent();

	// ==================== Core API ====================

	/**
	 * Grant an upgrade to the player.
	 *  - If not yet owned: creates the upgrade component at CurrentLevel=1 and calls OnUpgradeActivated.
	 *  - If already owned and below MaxLevel: increments CurrentLevel and calls OnLevelChanged.
	 *  - If already at MaxLevel: returns false (no-op).
	 * @return True if upgrade was newly granted OR levelled up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrades")
	bool GrantUpgrade(UUpgradeDefinition* Definition);

	/**
	 * Remove an upgrade from the player.
	 * @return True if upgrade was removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrades")
	bool RemoveUpgrade(FGameplayTag UpgradeTag);

	/** Check if player has a specific upgrade */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	bool HasUpgrade(FGameplayTag UpgradeTag) const;

	/** Current level for an owned upgrade (1+). Returns 0 if not owned. */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	int32 GetUpgradeLevel(FGameplayTag UpgradeTag) const;

	/** True if upgrade is owned AND CurrentLevel >= Definition->MaxLevel. False if not owned (still grantable). */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	bool IsUpgradeMaxedOut(UUpgradeDefinition* Definition) const;

	/**
	 * True if the player already owns an upgrade that is mutually exclusive with Candidate.
	 * Checked bidirectionally via UUpgradeDefinition::MutuallyExclusiveWith. Used to filter the
	 * choice pool and to refuse conflicting grants (e.g. the full-HP vs low-HP archetypes).
	 */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	bool OwnsConflicting(const UUpgradeDefinition* Candidate) const;

	/** Get all acquired upgrade definitions (for UI) */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	TArray<UUpgradeDefinition*> GetAcquiredUpgrades() const;

	/** Get the active component for a specific upgrade (nullptr if not owned) */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	UUpgradeComponent* GetUpgradeComponent(FGameplayTag UpgradeTag) const;

	// ==================== Dispenser offers ====================
	//
	// Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md. The server builds the cards, the owner
	// sees them (PendingOffer replicates to the owner only), picks one, and the server applies it on
	// both machines. Nothing else in this component is networked: an upgrade's logic runs wherever
	// it was granted, so a dispenser pick is granted on the server AND on the owning client.

	/** The upgrade this player holds in Slot, or null. None (passives) is never "held". */
	UUpgradeDefinition* GetOwnedInSlot(EUpgradeSlot Slot) const;

	/** Every card this player could be offered right now, before rarity: in the pool, unlocked by
	 *  MinWave, not maxed, not in conflict, and with room under LevelCap. Server. */
	void GatherOfferCandidates(const class UUpgradeRegistry* Registry, int32 Wave, int32 LevelCap,
		TArray<FUpgradeOfferCard>& OutCandidates) const;

	/** Draw one card per rarity from the candidates, weighted by OfferWeight, no repeats. Fewer
	 *  cards when the pool runs short. Server. */
	void BuildOffer(const class UUpgradeRegistry* Registry, int32 Wave, int32 LevelCap,
		const TArray<EUpgradeRarity>& Rarities, TArray<FUpgradeOfferCard>& OutOffer) const;

	UFUNCTION(BlueprintPure, Category = "Upgrades|Offer")
	bool HasPendingOffer() const { return PendingOffer.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "Upgrades|Offer")
	const TArray<FUpgradeOfferCard>& GetPendingOffer() const { return PendingOffer; }

	/** Hand the cards to the player. Server. */
	void SetPendingOffer(const TArray<FUpgradeOfferCard>& Offer);

	/** Server time of the last spin, for the dispenser's cooldown. */
	float GetLastSpinServerTime() const { return LastSpinServerTime; }
	void MarkSpin(float ServerTime) { LastSpinServerTime = ServerTime; }

	/** The owner picks card Index. Checked again on the server; the card is applied on both machines. */
	UFUNCTION(Server, Reliable)
	void Server_PickOffer(int32 Index);

	/** Owner side of a pick: the same card, applied locally. Skipped where the server already did it. */
	UFUNCTION(Client, Reliable)
	void Client_ApplyOfferCard(const FUpgradeOfferCard& Card);

	UPROPERTY(BlueprintAssignable, Category = "Upgrades|Offer")
	FOnUpgradeOfferChanged OnOfferChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== Persistence ====================

	/** Get list of upgrade tags for checkpoint/save serialization */
	TArray<FGameplayTag> GetUpgradeTagsForSave() const;

	/** Restore upgrades from saved tags (used by checkpoint/save system) */
	void RestoreUpgradesFromTags(const TArray<FGameplayTag>& Tags, const UUpgradeRegistry* Registry);

	/** Restore upgrades AT their saved levels (run-scoped cross-level carry via URunSubsystem).
	 *  Removes any owned upgrade not present in the map, then grants/levels each entry up to its
	 *  stored level. Unlike RestoreUpgradesFromTags, this preserves levels. Does NOT trigger the
	 *  level-up choice UI (that flow is XP-driven) — it only re-applies the upgrade logic. */
	void RestoreUpgrades(const TMap<FGameplayTag, int32>& TagToLevel, const UUpgradeRegistry* Registry);

	// ==================== Delegates ====================

	/** Broadcast when an upgrade is granted */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades")
	FOnUpgradeGranted OnUpgradeGranted;

	/** Broadcast when an upgrade is removed */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades")
	FOnUpgradeRemoved OnUpgradeRemoved;

	/** Broadcast when an already-owned upgrade is levelled up by a repeat grant. */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades")
	FOnUpgradeLeveledUp OnUpgradeLeveledUp;

	// ==================== Event Broadcasting ====================
	// Called by core systems (ShooterCharacter, ShooterWeapon).
	// Routes events to all active upgrade components.

	/** Called by ShooterWeapon after a shot is fired */
	void NotifyWeaponFired();

	/** Called by ShooterCharacter when weapon is switched */
	void NotifyWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon);

	/** Called by ShooterCharacter when taking damage */
	void NotifyOwnerTookDamage(float Damage, AActor* DamageCauser);

	/** Called when owner deals damage to a target */
	void NotifyOwnerDealtDamage(AActor* Target, float Damage, bool bKilled);

	/** Called when a specific owner weapon deals damage to a target */
	void NotifyWeaponDealtDamage(AShooterWeapon* Weapon, AActor* Target, float Damage, bool bKilled);

	/** Called when an enemy killed by this owner spawns a ranged weapon drop. */
	void NotifyEnemyDroppedRangedWeapon(ADroppedRangedWeapon* DroppedWeapon, AActor* DroppingEnemy);

	/** Called whenever owner's hitscan ionization successfully applies charge to a target.
	 *  Decoupled from damage so 0-damage ionizers (wave pistol) still notify upgrades. */
	void NotifyOwnerHitscanIonized(AActor* Target);

	/** Called when owner collects a health pickup while at full HP */
	void NotifyHealthPickupCollectedAtFullHP();

	/**
	 * Called by AShooterWeapon when ADS / secondary action is pressed.
	 * Returns true if an active upgrade handled the input and normal ADS should be blocked.
	 */
	bool HandleWeaponSecondaryAction(AShooterWeapon* Weapon);

	/** Called by AShooterWeapon when ADS / secondary action is released. */
	void HandleWeaponSecondaryActionReleased(AShooterWeapon* Weapon);

	/** Query all active upgrades for their combined damage multiplier against a target */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	float GetCombinedDamageMultiplier(AActor* Target) const;

	/** Max copies of the same yanked class the player may carry, per Bandolier's current level.
	 *  Returns 1 if the Bandolier upgrade is not owned (no expansion). */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	int32 GetBandolierMaxCopies() const;

	/** Same as above but for the MELEE damage track (fist + melee weapon). */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	float GetCombinedMeleeDamageMultiplier(AActor* Target) const;

	/** Query all active upgrades for their combined knockback-distance multiplier on a melee target. */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	float GetCombinedMeleeKnockbackDistanceMultiplier(AActor* Target) const;

	// ==================== Shared Health-Pickup Pool ====================
	// A counter, incremented when the player collects a health pickup at full HP,
	// shared between every upgrade that consumes "stored pickups" (HealthBlast,
	// ChargedPunch, etc). Each consumer reads/decrements via the API below.

	/** Maximum size of the shared pool (cap for all consumers combined). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrades|Shared Pool", meta = (ClampMin = "1", ClampMax = "99"))
	int32 MaxStoredHealthPickups = 10;

	/** Broadcast whenever StoredHealthPickups changes (after Add/Consume/Set). */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades|Shared Pool")
	FOnStoredHealthPickupsChanged OnStoredHealthPickupsChanged;

	/** Current pool count. */
	UFUNCTION(BlueprintPure, Category = "Upgrades|Shared Pool")
	int32 GetStoredHealthPickups() const { return StoredHealthPickups; }

	UFUNCTION(BlueprintPure, Category = "Upgrades|Shared Pool")
	int32 GetMaxStoredHealthPickups() const { return MaxStoredHealthPickups; }

	/** True if the player currently owns at least one upgrade that consumes the shared pool
	 *  (UUpgradeDefinition::bUsesStoredHealthPickups). The HUD uses this to gate the heal-charge entry. */
	UFUNCTION(BlueprintPure, Category = "Upgrades|Shared Pool")
	bool HasStoredHealthPickupConsumer() const;

	/** Input action that spends the pool for the currently-owned consumer (null if none / not set).
	 *  Consumers are mutually exclusive, so at most one is owned. Used for the HUD keybind hint. */
	UFUNCTION(BlueprintPure, Category = "Upgrades|Shared Pool")
	UInputAction* GetHealSpendInputAction() const;

	/** Increment by 1 (clamped to Max). Returns true if the value actually changed. */
	UFUNCTION(BlueprintCallable, Category = "Upgrades|Shared Pool")
	bool AddStoredHealthPickup();

	/** Consume up to RequestedCount from the pool. Returns the count actually consumed. */
	UFUNCTION(BlueprintCallable, Category = "Upgrades|Shared Pool")
	int32 ConsumeStoredHealthPickups(int32 RequestedCount);

	/** Reset to 0 (e.g. on death/respawn). */
	UFUNCTION(BlueprintCallable, Category = "Upgrades|Shared Pool")
	void ResetStoredHealthPickups();

protected:

	virtual void BeginPlay() override;

private:

	/** Map of UpgradeTag -> active upgrade component */
	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UUpgradeComponent>> ActiveUpgrades;

	/** The cards waiting for this player's pick. Owner only. */
	UPROPERTY(ReplicatedUsing = OnRep_PendingOffer)
	TArray<FUpgradeOfferCard> PendingOffer;

	UFUNCTION()
	void OnRep_PendingOffer();

	/** Remove the replaced upgrade, then grant or raise the card's upgrade to its ToLevel. */
	bool ApplyOfferCard(const FUpgradeOfferCard& Card);

	float LastSpinServerTime = -1000.0f;

	/** Shared pool counter — see GetStoredHealthPickups. */
	UPROPERTY()
	int32 StoredHealthPickups = 0;

	/** Currently bound weapon (for delegate cleanup) */
	UPROPERTY()
	TWeakObjectPtr<AShooterWeapon> BoundWeapon;

	/** Bind/unbind OnShotFired delegate on weapon */
	void BindToWeapon(AShooterWeapon* Weapon);
	void UnbindFromWeapon();

	/** Callback for weapon's OnShotFired delegate */
	UFUNCTION()
	void OnWeaponShotFiredCallback();
};
