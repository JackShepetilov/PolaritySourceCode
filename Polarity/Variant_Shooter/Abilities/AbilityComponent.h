// AbilityComponent.h
// Multi-slot ability inventory + activation gate + common cooldown.
//
// The component does NOT drive a pipeline. It hands control to the handler on TryActivate
// and waits for the handler to call NotifyAbilityCompletedFromHandler or
// NotifyAbilityCancelledFromHandler. While the handler runs, bIsCasting is true and further
// activations are blocked. After completion the component starts a common cooldown using
// the ability's GetCommonStatsAtLevel(level).Cooldown — locking ALL abilities until expiry.
// Switching slots is allowed during cooldown (just not activating).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilityComponent.generated.h"

class UAbilityDefinition;
class UAbilityHandler;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityAdded, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityRemoved, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilitySwitched, int32, NewActiveSlot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAbilityLevelChanged, int32, SlotIndex, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityActivated, UAbilityDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityCompleted, UAbilityDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityCancelled, UAbilityDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityCooldownStarted, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAbilityCooldownEnded);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UAbilityComponent();

	// ==================== Configuration ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxAbilitySlots = 3;

	/** When pickup arrives and inventory is full, replace the active slot.
	 *  When false, pickup is ignored and the player keeps existing inventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	bool bReplaceActiveWhenFull = true;

	// ==================== Inventory API ====================

	/** Add an ability at a specific level.
	 *  - If the same Definition is already equipped at lower level → upgrade in place (same slot).
	 *  - If already equipped at >= Level → INDEX_NONE (no downgrade via this path).
	 *  - If inventory empty → new ability becomes active.
	 *  - If full and bReplaceActiveWhenFull → replace active slot. */
	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory", meta = (AdvancedDisplay = "Level"))
	int32 AddAbility(UAbilityDefinition* Definition, int32 Level = 1);

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory", meta = (AdvancedDisplay = "Level"))
	bool ReplaceAbility(int32 SlotIndex, UAbilityDefinition* Definition, int32 Level = 1);

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory")
	bool RemoveAbility(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory")
	bool SetAbilityLevel(int32 SlotIndex, int32 NewLevel);

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory")
	bool LevelUpAbility(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	int32 GetAbilityLevel(int32 SlotIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory")
	bool SwitchToSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory")
	bool SwitchToNext();

	UFUNCTION(BlueprintCallable, Category = "Ability|Inventory")
	bool SwitchToPrevious();

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	bool HasAbility(UAbilityDefinition* Definition) const;

	// ==================== Activation API ====================

	UFUNCTION(BlueprintCallable, Category = "Ability|Activation")
	bool TryActivate();

	UFUNCTION(BlueprintCallable, Category = "Ability|Activation")
	void OnButtonReleased();

	/** Cancel the in-progress cast. Tells handler to clean up via OnCancelRequested. */
	UFUNCTION(BlueprintCallable, Category = "Ability|Activation")
	void CancelCast();

	UFUNCTION(BlueprintPure, Category = "Ability|Activation")
	bool CanActivate() const;

	// ==================== API for handlers (called from UAbilityHandler) ====================

	/** Handler signals successful completion. Component starts cooldown and clears casting state. */
	void NotifyAbilityCompletedFromHandler(UAbilityHandler* Handler);

	/** Handler signals abort. Component clears casting state without starting cooldown. */
	void NotifyAbilityCancelledFromHandler(UAbilityHandler* Handler);

	// ==================== Queries ====================

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	int32 GetActiveSlotIndex() const { return ActiveSlotIndex; }

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	UAbilityDefinition* GetActiveAbility() const;

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	UAbilityHandler* GetActiveHandler() const;

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	int32 GetSlotCount() const { return EquippedHandlers.Num(); }

	UFUNCTION(BlueprintPure, Category = "Ability|Inventory")
	UAbilityDefinition* GetAbilityAtSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Ability|Activation")
	bool IsCasting() const { return bIsCasting; }

	UFUNCTION(BlueprintPure, Category = "Ability|Activation")
	float GetCooldownRemaining() const { return CooldownTimeRemaining; }

	UFUNCTION(BlueprintPure, Category = "Ability|Activation")
	bool IsOnCooldown() const { return CooldownTimeRemaining > 0.0f; }

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityAdded OnAbilityAdded;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityRemoved OnAbilityRemoved;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilitySwitched OnAbilitySwitched;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityLevelChanged OnAbilityLevelChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityActivated OnAbilityActivated;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityCompleted OnAbilityCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityCancelled OnAbilityCancelled;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityCooldownStarted OnCooldownStarted;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Events")
	FOnAbilityCooldownEnded OnCooldownEnded;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== State ====================

	UPROPERTY()
	TArray<TObjectPtr<UAbilityHandler>> EquippedHandlers;

	int32 ActiveSlotIndex = INDEX_NONE;

	bool bIsCasting = false;

	float CooldownTimeRemaining = 0.0f;

	// ==================== Internals ====================

	UAbilityHandler* CreateHandler(UAbilityDefinition* Definition, int32 Level);
	void DestroyHandler(UAbilityHandler* Handler);
	int32 FindSlotIndexForDefinition(UAbilityDefinition* Definition) const;
	void StartCooldown(float Duration);
};
