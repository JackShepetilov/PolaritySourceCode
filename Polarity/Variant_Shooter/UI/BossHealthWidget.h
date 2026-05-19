// BossHealthWidget.h
// HUD widget for displaying boss health bar

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "BossHealthWidget.generated.h"

class ABossCharacter;
class UDamageType;
class AArenaManager;

/**
 * Base class for boss health bar widget
 * Displays boss HP as a progress bar on the player's HUD
 * Inherit in Blueprint to create the visual representation
 *
 * Usage:
 * 1. Create Blueprint child of this class (WBP_BossHealth)
 * 2. Design the visual layout (ProgressBar, boss name, phase indicator, etc.)
 * 3. Bind to BP_OnHealthChanged for smooth animations
 * 4. Call ShowForBoss() when boss fight starts
 * 5. Call Hide() when boss is defeated
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UBossHealthWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Initialize and show the widget for a specific boss
	 * Automatically binds to boss damage events
	 * @param Boss The boss character to track
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss Health")
	void ShowForBoss(ABossCharacter* Boss);

	/**
	 * Hide the widget and unbind from boss events
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss Health")
	void Hide();

	/**
	 * Get current health percentage (0-1)
	 */
	UFUNCTION(BlueprintPure, Category = "Boss Health")
	float GetHealthPercent() const;

	/**
	 * Get the tracked boss character
	 */
	UFUNCTION(BlueprintPure, Category = "Boss Health")
	ABossCharacter* GetTrackedBoss() const { return TrackedBoss.Get(); }

	/**
	 * Check if widget is currently tracking a boss
	 */
	UFUNCTION(BlueprintPure, Category = "Boss Health")
	bool IsTrackingBoss() const { return TrackedBoss.IsValid(); }

	/**
	 * Get datacenter (arena prop) percent (0..1). 1.0 = all alive.
	 * Falls back to 1.0 if the boss has no LinkedArena set.
	 */
	UFUNCTION(BlueprintPure, Category = "Boss Health|Datacenter")
	float GetDatacenterHealthPercent() const { return CurrentDatacenterPercent; }

protected:
	// ==================== Blueprint Events ====================

	/**
	 * Called when the widget should appear
	 * Implement in Blueprint to play show animation
	 * @param BossName Display name of the boss
	 * @param InitialHealthPercent Starting health (usually 1.0)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss Health",
		meta = (DisplayName = "On Show"))
	void BP_OnShow(const FString& BossName, float InitialHealthPercent);

	/**
	 * Called when the widget should disappear
	 * Implement in Blueprint to play hide animation
	 * @param bBossDefeated True if boss was defeated, false if just hiding
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss Health",
		meta = (DisplayName = "On Hide"))
	void BP_OnHide(bool bBossDefeated);

	/**
	 * Called when boss health changes
	 * Implement in Blueprint to update progress bar with animation
	 * @param NewHealthPercent New health percentage (0-1)
	 * @param OldHealthPercent Previous health percentage
	 * @param DamageAmount Amount of damage taken
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss Health",
		meta = (DisplayName = "On Health Changed"))
	void BP_OnHealthChanged(float NewHealthPercent, float OldHealthPercent, float DamageAmount);

	/**
	 * Called when boss phase changes
	 * Implement in Blueprint to show phase transition effect
	 * @param NewPhaseIndex 0=Ground, 1=Finisher
	 * @param PhaseName Display name of the new phase
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss Health",
		meta = (DisplayName = "On Phase Changed"))
	void BP_OnPhaseChanged(int32 NewPhaseIndex, const FString& PhaseName);

	/**
	 * Called when the datacenter HP (arena prop %) changes.
	 * The bar driven by this event is the *true* health pool — boss Posture
	 * (from BP_OnHealthChanged) only gates the finisher window.
	 * @param NewPercent  New remaining prop percent (0..1, 1 = all alive)
	 * @param OldPercent  Previous remaining prop percent
	 * @param AliveCount  How many tracked props are still alive
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss Health|Datacenter",
		meta = (DisplayName = "On Datacenter Health Changed"))
	void BP_OnDatacenterHealthChanged(float NewPercent, float OldPercent, int32 AliveCount);

	// ==================== Internal ====================

	/** Currently tracked boss */
	UPROPERTY(BlueprintReadOnly, Category = "Boss Health")
	TWeakObjectPtr<ABossCharacter> TrackedBoss;

	/** Cached current health percent for smooth interpolation */
	UPROPERTY(BlueprintReadOnly, Category = "Boss Health")
	float CurrentHealthPercent = 1.0f;

	/** Cached max HP for percentage calculations */
	float CachedMaxHP = 1.0f;

	/** Currently tracked arena (datacenter source) */
	TWeakObjectPtr<AArenaManager> TrackedArena;

	/** Cached datacenter prop percent (1.0 = all alive). */
	UPROPERTY(BlueprintReadOnly, Category = "Boss Health|Datacenter")
	float CurrentDatacenterPercent = 1.0f;

	/** Handle damage taken event from boss */
	UFUNCTION()
	void OnBossDamageTaken(class AShooterNPC* Boss, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser);

	/** Handle phase changed event from boss */
	UFUNCTION()
	void OnBossPhaseChanged(EBossPhase OldPhase, EBossPhase NewPhase);

	/** Handle boss defeated event */
	UFUNCTION()
	void OnBossDefeated();

	/** Handle datacenter prop-percent change from the linked arena */
	UFUNCTION()
	void OnDatacenterPropPercentChanged(float RemainingPercent, int32 AliveCount);

	/** Unbind all delegates from current boss */
	void UnbindFromBoss();

	/** Unbind from current arena */
	void UnbindFromArena();

	virtual void NativeDestruct() override;
};
