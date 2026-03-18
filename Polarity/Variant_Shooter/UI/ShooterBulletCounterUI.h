// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialTypes.h"
#include "ShooterBulletCounterUI.generated.h"

class AShooterCharacter;

/**
 * Charge polarity state for UI color changes
 */
UENUM(BlueprintType)
enum class EChargePolarity : uint8
{
	Neutral		UMETA(DisplayName = "Neutral (0)"),
	Positive	UMETA(DisplayName = "Positive (+)"),
	Negative	UMETA(DisplayName = "Negative (-)")
};


/**
 *  Simple bullet counter UI widget for a first person shooter game
 *  Also displays Heat factor, Speed indicators, and Charge polarity
 */
UCLASS(abstract)
class POLARITY_API UShooterBulletCounterUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Allows Blueprint to update sub-widgets with the new bullet count */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter", meta = (DisplayName = "UpdateBulletCounter"))
	void BP_UpdateBulletCounter(int32 MagazineSize, int32 BulletCount);

	/** Allows Blueprint to update sub-widgets with the new life/armor totals and play a damage effect on the HUD */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter", meta = (DisplayName = "Damaged"))
	void BP_Damaged(float LifePercent, float ArmorPercent);

	// ==================== Heat System UI ====================

	/**
	 * ÐžÐ±Ð½Ð¾Ð²Ð»ÑÐµÑ‚ Ð¾Ñ‚Ð¾Ð±Ñ€Ð°Ð¶ÐµÐ½Ð¸Ðµ Heat-Ñ„Ð°ÐºÑ‚Ð¾Ñ€Ð° Ð¾Ñ€ÑƒÐ¶Ð¸Ñ.
	 * @param HeatPercent - Ñ‚ÐµÐºÑƒÑ‰Ð¸Ð¹ Ð½Ð°Ð³Ñ€ÐµÐ² (0-1), Ð³Ð´Ðµ 0 = Ñ…Ð¾Ð»Ð¾Ð´Ð½Ð¾Ðµ, 1 = Ð¼Ð°ÐºÑÐ¸Ð¼Ð°Ð»ÑŒÐ½Ñ‹Ð¹ Ð½Ð°Ð³Ñ€ÐµÐ²
	 * @param DamageMultiplier - Ñ‚ÐµÐºÑƒÑ‰Ð¸Ð¹ Ð¼Ð½Ð¾Ð¶Ð¸Ñ‚ÐµÐ»ÑŒ ÑƒÑ€Ð¾Ð½Ð° Ð¾Ñ‚ Ð½Ð°Ð³Ñ€ÐµÐ²Ð° (1.0 = Ð¿Ð¾Ð»Ð½Ñ‹Ð¹ ÑƒÑ€Ð¾Ð½, 0.2 = Ð¼Ð¸Ð½Ð¸Ð¼Ð°Ð»ÑŒÐ½Ñ‹Ð¹)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Heat", meta = (DisplayName = "UpdateHeat"))
	void BP_UpdateHeat(float HeatPercent, float DamageMultiplier);

	// ==================== Speed UI ====================

	/**
	 * ÐžÐ±Ð½Ð¾Ð²Ð»ÑÐµÑ‚ Ð¾Ñ‚Ð¾Ð±Ñ€Ð°Ð¶ÐµÐ½Ð¸Ðµ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸ Ð¸Ð³Ñ€Ð¾ÐºÐ°.
	 * @param SpeedPercent - Ð½Ð¾Ñ€Ð¼Ð°Ð»Ð¸Ð·Ð¾Ð²Ð°Ð½Ð½Ð°Ñ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚ÑŒ (0-1), Ð³Ð´Ðµ 0 = ÑÑ‚Ð¾Ð¸Ñ‚, 1 = Ð¼Ð°ÐºÑÐ¸Ð¼Ð°Ð»ÑŒÐ½Ð°Ñ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚ÑŒ
	 * @param CurrentSpeed - Ð°Ð±ÑÐ¾Ð»ÑŽÑ‚Ð½Ð°Ñ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚ÑŒ Ð² ÑÐ¼/Ñ
	 * @param MaxSpeed - Ñ€ÐµÑ„ÐµÑ€ÐµÐ½ÑÐ½Ð°Ñ Ð¼Ð°ÐºÑÐ¸Ð¼Ð°Ð»ÑŒÐ½Ð°Ñ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚ÑŒ Ð´Ð»Ñ Ð½Ð¾Ñ€Ð¼Ð°Ð»Ð¸Ð·Ð°Ñ†Ð¸Ð¸
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Speed", meta = (DisplayName = "UpdateSpeed"))
	void BP_UpdateSpeed(float SpeedPercent, float CurrentSpeed, float MaxSpeed);

	// ==================== Charge Polarity UI ====================

	/**
	 * Ð’Ñ‹Ð·Ñ‹Ð²Ð°ÐµÑ‚ÑÑ Ð¿Ñ€Ð¸ Ð¸Ð·Ð¼ÐµÐ½ÐµÐ½Ð¸Ð¸ Ð·Ð½Ð°ÐºÐ° Ð·Ð°Ñ€ÑÐ´Ð° Ð¿ÐµÑ€ÑÐ¾Ð½Ð°Ð¶Ð°.
	 * Ð˜ÑÐ¿Ð¾Ð»ÑŒÐ·ÑƒÐ¹Ñ‚Ðµ Ð´Ð»Ñ ÑÐ¼ÐµÐ½Ñ‹ Ñ†Ð²ÐµÑ‚Ð¾Ð²Ð¾Ð¹ ÑÑ…ÐµÐ¼Ñ‹ Ð¸Ð½Ñ‚ÐµÑ€Ñ„ÐµÐ¹ÑÐ°.
	 * @param NewPolarity - Ð½Ð¾Ð²Ñ‹Ð¹ Ð·Ð½Ð°Ðº Ð·Ð°Ñ€ÑÐ´Ð° (Positive/Negative/Neutral)
	 * @param ChargeValue - Ñ‚Ð¾Ñ‡Ð½Ð¾Ðµ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ Ð·Ð°Ñ€ÑÐ´Ð° (-1 Ð´Ð¾ +1)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Polarity", meta = (DisplayName = "OnPolarityChanged"))
	void BP_OnPolarityChanged(EChargePolarity NewPolarity, float ChargeValue);

	/**
	 * ÐžÐ±Ð½Ð¾Ð²Ð»ÑÐµÑ‚ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ Ð·Ð°Ñ€ÑÐ´Ð° (Ð²Ñ‹Ð·Ñ‹Ð²Ð°ÐµÑ‚ÑÑ ÐºÐ°Ð¶Ð´Ñ‹Ð¹ ÐºÐ°Ð´Ñ€).
	 * @param ChargeValue - Ñ‚ÐµÐºÑƒÑ‰ÐµÐµ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ Ð·Ð°Ñ€ÑÐ´Ð° (-1 Ð´Ð¾ +1)
	 * @param Polarity - Ñ‚ÐµÐºÑƒÑ‰Ð¸Ð¹ Ð·Ð½Ð°Ðº Ð·Ð°Ñ€ÑÐ´Ð°
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Polarity", meta = (DisplayName = "UpdateCharge"))
	void BP_UpdateCharge(float ChargeValue, EChargePolarity Polarity);

	/**
	 * Extended charge update with stable/unstable breakdown.
	 * Stable charge = permanent, does not decay (from melee dummies)
	 * Unstable charge = temporary, decays over time (from enemy melee hits)
	 * Use this for progress bar showing both sections.
	 * @param TotalCharge - total charge value (base + bonus)
	 * @param StableCharge - base/permanent charge (does not decay)
	 * @param UnstableCharge - bonus charge (decays over time)
	 * @param MaxStableCharge - maximum possible stable charge
	 * @param MaxUnstableCharge - maximum possible unstable charge
	 * @param Polarity - current polarity sign (Positive/Negative/Neutral)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Polarity", meta = (DisplayName = "UpdateChargeExtended"))
	void BP_UpdateChargeExtended(float TotalCharge, float StableCharge, float UnstableCharge,
		float MaxStableCharge, float MaxUnstableCharge, EChargePolarity Polarity);

	// ==================== Drop Kick Cooldown ====================

	/**
	 * Called when drop kick cooldown starts.
	 * Use this to start a cooldown timer animation.
	 * @param CooldownDuration - total cooldown duration in seconds
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DropKick", meta = (DisplayName = "OnDropKickCooldownStarted"))
	void BP_OnDropKickCooldownStarted(float CooldownDuration);

	/**
	 * Called when drop kick cooldown ends and ability is ready.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DropKick", meta = (DisplayName = "OnDropKickCooldownEnded"))
	void BP_OnDropKickCooldownEnded();

	// ==================== Melee Charge Cooldown ====================

	/**
	 * Called when melee cooldown starts (charges dropped below max).
	 * Use this to show and start cooldown overlay animation.
	 * @param TotalCooldownDuration - total time to recover all charges (seconds)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Melee", meta = (DisplayName = "OnMeleeCooldownStarted"))
	void BP_OnMeleeCooldownStarted(float TotalCooldownDuration);

	/**
	 * Called when all melee charges are fully recovered.
	 * Use this to hide cooldown overlay.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Melee", meta = (DisplayName = "OnMeleeCooldownEnded"))
	void BP_OnMeleeCooldownEnded();

	/**
	 * Called when melee charges change (consumed or recovered).
	 * Use this to update charge counter / pips.
	 * @param CurrentCharges - current available charges
	 * @param MaxCharges - maximum charges
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Melee", meta = (DisplayName = "OnMeleeChargeChanged"))
	void BP_OnMeleeChargeChanged(int32 CurrentCharges, int32 MaxCharges);

	/**
	 * Вызывается при экипировке/снятии оружия ближнего боя.
	 * Используйте для показа/скрытия счётчика оставшихся ударов.
	 * @param bEquipped - true = меч экипирован, false = убран/сломан
	 * @param RemainingHits - оставшиеся удары (0 если не экипирован или бесконечный)
	 * @param MaxHits - максимум ударов (0 если бесконечный)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Melee", meta = (DisplayName = "OnMeleeWeaponEquipped"))
	void BP_OnMeleeWeaponEquipped(bool bEquipped, int32 RemainingHits, int32 MaxHits);

	// ==================== Hit Marker ====================

	/**
	 * ÐŸÐ¾ÐºÐ°Ð·Ñ‹Ð²Ð°ÐµÑ‚ Ñ…Ð¸Ñ‚Ð¼Ð°Ñ€ÐºÐµÑ€ Ð¿Ñ€Ð¸ Ð¿Ð¾Ð¿Ð°Ð´Ð°Ð½Ð¸Ð¸.
	 * @param bHeadshot - true ÐµÑÐ»Ð¸ Ð¿Ð¾Ð¿Ð°Ð´Ð°Ð½Ð¸Ðµ Ð² Ð³Ð¾Ð»Ð¾Ð²Ñƒ
	 * @param bKilled - true ÐµÑÐ»Ð¸ Ñ†ÐµÐ»ÑŒ ÑƒÐ±Ð¸Ñ‚Ð°
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|HitMarker", meta = (DisplayName = "ShowHitMarker"))
	void BP_ShowHitMarker(bool bHeadshot, bool bKilled);

	// ==================== Damage Direction Indicator ====================

	/**
	 * Shows damage direction indicator when player takes damage.
	 * @param AngleDegrees - angle relative to player forward (0 = front, 90 = right, 180/-180 = back, -90 = left)
	 * @param Damage - amount of damage received
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DamageDirection", meta = (DisplayName = "ShowDamageDirection"))
	void BP_ShowDamageDirection(float AngleDegrees, float Damage);

	// ==================== Respawn Rebinding ====================

	/**
	 * Rebinds the widget to a new character after respawn.
	 * Implement in Blueprint to reconnect HitMarkerComponent delegate.
	 * @param NewCharacter - the newly spawned player character
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Respawn", meta = (DisplayName = "BindToCharacter"))
	void BP_BindToCharacter(AShooterCharacter* NewCharacter);

	// ==================== Tutorial Arrows ====================

	/**
	 * Show a tutorial arrow pointing at a specific HUD element.
	 * Implement in Blueprint to show arrow + description text near the target element.
	 * The arrow lives inside the HUD widget (same coordinate space = resolution-safe).
	 * @param Element - Which HUD element to point at
	 * @param DescriptionText - Description text shown alongside the arrow
	 * @param CloseKeyIcon - Icon for the close key (for "Hold X to continue" prompt)
	 * @param CloseHintText - Text for close prompt
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Tutorial", meta = (DisplayName = "ShowTutorialArrow"))
	void BP_ShowTutorialArrow(EHUDElement Element, const FText& DescriptionText, UTexture2D* CloseKeyIcon, const FText& CloseHintText);

	/**
	 * Hide the tutorial arrow for a specific HUD element.
	 * @param Element - Which arrow to hide
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Tutorial", meta = (DisplayName = "HideTutorialArrow"))
	void BP_HideTutorialArrow(EHUDElement Element);

	/**
	 * Update hold progress for the close button on tutorial arrows.
	 * @param Progress - 0.0 = just started, 1.0 = about to close
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Tutorial", meta = (DisplayName = "UpdateTutorialHoldProgress"))
	void BP_UpdateTutorialHoldProgress(float Progress);

	/**
	 * Called when hold is cancelled (key released early).
	 * Use to reset progress indicator.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|Tutorial", meta = (DisplayName = "OnTutorialHoldCancelled"))
	void BP_OnTutorialHoldCancelled();
};