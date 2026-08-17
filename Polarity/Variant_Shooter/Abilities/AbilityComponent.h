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
class UNiagaraSystem;
class USoundBase;
class AActor;
class AController;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityAdded, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityRemoved, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilitySwitched, int32, NewActiveSlot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAbilityLevelChanged, int32, SlotIndex, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityActivated, UAbilityDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityCompleted, UAbilityDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityCancelled, UAbilityDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityCooldownStarted, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAbilityCooldownEnded);

/** One slot as it travels: what is in it and at what level.
 *
 *  The handlers themselves are UObjects and stay local. Each machine builds its own from this, so a
 *  handler is a mirror rather than a replicated subobject — the same shape the rest of this project
 *  uses, and it keeps handler authoring free of networking concerns entirely. */
USTRUCT()
struct FAbilitySlotState
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UAbilityDefinition> Definition = nullptr;

	UPROPERTY()
	int32 Level = 1;

	bool operator==(const FAbilitySlotState& Other) const
	{
		return Definition == Other.Definition && Level == Other.Level;
	}
};

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UAbilityComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== Network ====================
	// Abilities decide things about the world — who is slowed, what takes damage — so the decision
	// belongs to the server, and the inventory that gates it has to be known on both ends. Before
	// this the component had no replication at all: a client's ability ran only on its own machine,
	// which is the same defect the melee, the death and the health all had.

	/** Client asks to activate whatever is in its active slot. The server re-checks everything
	 *  TryActivate checks and runs the handler there. Reliable: a dropped press is an ability that
	 *  visibly did nothing. */
	UFUNCTION(Server, Reliable)
	void Server_TryActivate();

	/** The release half, for hold-style abilities. */
	UFUNCTION(Server, Reliable)
	void Server_OnButtonReleased();

	/** Client asks to change slot. Switching is not a world decision, so the client changes locally
	 *  at once and tells the server, which will agree — the same shape as the weapon switch. */
	UFUNCTION(Server, Reliable)
	void Server_SwitchToSlot(int32 SlotIndex);

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

	// ==================== Passive channel ====================
	// A passive is an ability by construction -- same definition, same handler, same levels -- and
	// nothing like one in how it is held. It is never selected, never activated, never on cooldown,
	// and it must not eat one of MaxAbilitySlots. So it lives beside the inventory rather than in it.
	// Putting it in a slot, which is what the first pass did, gave the player an ability they could
	// switch to and press, and cost them a third of their inventory to carry something they never
	// chose.

	/** Install the always-on ability. Authority only, like the rest of the inventory API; the client
	 *  learns about it when PassiveDefinition replicates and builds its own mirror handler.
	 *  Passing null removes whatever is installed. */
	UFUNCTION(BlueprintCallable, Category = "Ability|Passive")
	void GrantPassive(UAbilityDefinition* Definition);

	UFUNCTION(BlueprintPure, Category = "Ability|Passive")
	UAbilityDefinition* GetPassiveAbility() const { return PassiveDefinition; }

	UFUNCTION(BlueprintPure, Category = "Ability|Passive")
	UAbilityHandler* GetPassiveHandler() const { return PassiveHandler; }

	/** Told by the owning character from its TakeDamage, on the authority. Forwarded to the passive,
	 *  which is the only thing that can answer damage today. */
	void NotifyOwnerDamaged(float Damage, AActor* DamageCauser, AController* InstigatedBy, const FHitResult& HitInfo);

	/** Play a one-shot beam on every machine, from Start to End, and a sound where it lands.
	 *
	 *  Abilities decide on the authority and their effects have to be seen and heard everywhere, so
	 *  spawning either locally in a handler would leave only the server aware of it. Generic rather
	 *  than named after the ability that needed it first: the whole contract is a system, a sound, two
	 *  points and a scale. Both are optional on their own — null System plays no beam, null Sound
	 *  plays no sound, and either can be used without the other.
	 *
	 *  The system is expected to take the end of the beam as a Vec3 user parameter named
	 *  BeamEndPoint, which is the shape the channelling capture and launch beams already use --
	 *  the same asset works in both places without being rebuilt. A system without that parameter
	 *  still plays, at Start, ignoring where it was supposed to reach.
	 *
	 *  Unreliable: it is cosmetic, and the damage it illustrates has already happened. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBeamVFX(UNiagaraSystem* System, USoundBase* Sound, FVector Start, FVector End, FVector Scale);

	/** `polarity.ability.beamdebug 1` — log every beam and draw it as a line with a ball on each end.
	 *
	 *  Shared rather than a CVar read in each file, because the question being answered spans three of
	 *  them: where the damage event said it landed, what the ability made of that, and what actually
	 *  arrived on the far machine. One switch turns on all three or the trail has holes in it. */
	static bool IsBeamDebugEnabled();

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

	/** Local mirrors, built from ReplicatedSlots. Never replicated themselves. */
	UPROPERTY()
	TArray<TObjectPtr<UAbilityHandler>> EquippedHandlers;

	/** What the authority says is in the inventory. Owner-only: nobody else needs to know what a
	 *  teammate is carrying, and the cost of telling them is per-slot per-change. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSlots)
	TArray<FAbilitySlotState> ReplicatedSlots;

	UFUNCTION()
	void OnRep_ReplicatedSlots();

	UPROPERTY(ReplicatedUsing = OnRep_ActiveSlotIndex)
	int32 ActiveSlotIndex = INDEX_NONE;

	UFUNCTION()
	void OnRep_ActiveSlotIndex();

	/** Casting and cooldown are the authority's answer, replicated so the owner's HUD and its own
	 *  activation gate agree with the machine that actually decides. */
	UPROPERTY(Replicated)
	bool bIsCasting = false;

	UPROPERTY(Replicated)
	float CooldownTimeRemaining = 0.0f;

	/** The always-on ability, owner-only for the same reason the slots are: nobody else's screen has
	 *  any use for it. */
	UPROPERTY(ReplicatedUsing = OnRep_PassiveDefinition)
	TObjectPtr<UAbilityDefinition> PassiveDefinition;

	UFUNCTION()
	void OnRep_PassiveDefinition();

	/** Local mirror, built from PassiveDefinition on both sides. Never replicated itself. */
	UPROPERTY()
	TObjectPtr<UAbilityHandler> PassiveHandler;

	/** Put PassiveHandler in step with PassiveDefinition. Keeps an existing handler whose definition
	 *  is unchanged, so a passive holding a binding is not torn down for nothing. */
	void RebuildPassiveHandler();

	/** Rebuild EquippedHandlers so they match ReplicatedSlots, keeping handlers whose definition and
	 *  level are unchanged. Called on the authority after any inventory change and on a client from
	 *  OnRep. */
	void RebuildHandlersFromSlots();

	/** Copy the authoritative slot list out of the live handlers. Authority side only. */
	void PublishSlotsFromHandlers();

	// ==================== Internals ====================

	UAbilityHandler* CreateHandler(UAbilityDefinition* Definition, int32 Level);
	void DestroyHandler(UAbilityHandler* Handler);
	int32 FindSlotIndexForDefinition(UAbilityDefinition* Definition) const;
	void StartCooldown(float Duration);
};
