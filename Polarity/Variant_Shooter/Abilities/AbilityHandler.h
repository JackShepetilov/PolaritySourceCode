// AbilityHandler.h
// Runtime logic for one ability instance. Subclass to implement specific abilities or to add
// archetype-shared pipelines (see UAbilityHandler_Burst).
//
// The component does NOT drive a pipeline. It owns inventory + cooldown only and gives the
// handler control on activate. The handler is responsible for its own state machine, animation
// orchestration, and signaling completion via NotifyAbilityComplete / NotifyAbilityCancelled.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/HitResult.h"
#include "AbilityDefinition.h"
#include "AbilityHandler.generated.h"

class UAbilityComponent;
class AShooterCharacter;
class UAnimMontage;
class USkeletalMeshComponent;
class UAnimInstance;
class AActor;
class AController;

UCLASS(Blueprintable, Abstract)
class POLARITY_API UAbilityHandler : public UObject
{
	GENERATED_BODY()

public:

	UAbilityHandler();

	// ==================== Lifecycle (called by UAbilityComponent) ====================

	void Initialize(UAbilityComponent* InOwningComponent, UAbilityDefinition* InDefinition, int32 InLevel);
	void SetLevel(int32 NewLevel);

	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnEquip();
	virtual void OnEquip_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnUnequip();
	virtual void OnUnequip_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnLevelChanged(int32 NewLevel);
	virtual void OnLevelChanged_Implementation(int32 NewLevel) {}

	/** Entry point when the player activates the ability via component->TryActivate.
	 *  Handler owns its own pipeline from here. Must eventually call NotifyAbilityComplete
	 *  or NotifyAbilityCancelled. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnActivate();
	virtual void OnActivate_Implementation() {}

	/** For Hold-mode abilities: called when activation button released. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnButtonReleased();
	virtual void OnButtonReleased_Implementation() {}

	/** Called by component when activation is cancelled externally (death, weapon swap, slot replace).
	 *  Subclass should clean up its state machine and call NotifyAbilityCancelled when done. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnCancelRequested();
	virtual void OnCancelRequested_Implementation() {}

	// ==================== Passive lifecycle ====================
	// A passive is never activated, so OnActivate and everything around it never runs for one. These
	// are the hooks it lives on instead, driven by the component for the granted passive only. Plain
	// virtuals rather than BlueprintNativeEvents: they run on the authority in the middle of damage
	// and tick, and a Blueprint override there would be a trap rather than a feature.

	/** Every component tick. Do not put per-frame work here — this exists for handlers that need to
	 *  notice something changed and have nothing to be told by. */
	virtual void OnPassiveTick(float DeltaTime) {}

	/** The owning character took damage, on the authority, after armour and health were applied.
	 *  Damage is the amount that arrived, before armour absorbed any of it.
	 *
	 *  HitInfo is the damage event's own best answer for where it landed, so a point hit carries the
	 *  real impact point and bone and a generic one carries the actor. Anything drawing a reaction
	 *  from the wound outwards needs it, and it cannot be recovered afterwards. */
	virtual void OnOwnerDamaged(float Damage, AActor* DamageCauser, AController* InstigatedBy, const FHitResult& HitInfo) {}

	/** Every component tick, for the ACTIVE handler, and only while it is casting.
	 *
	 *  Most abilities need nothing here: they drive their own pipeline from montages and timers and
	 *  know exactly when they are finished. It exists for the ones whose end is decided somewhere
	 *  they do not control — the grapple's swing ends inside the movement simulation, on each
	 *  machine independently, so the ability can only notice it, never be told. */
	virtual void OnActiveTick(float DeltaTime) {}

	/** The owner landed a hit with a weapon, on the machine that computed it. Damage is what was
	 *  actually applied, which on a client is what it ASKED for -- the true number only comes back a
	 *  round trip later as replicated health.
	 *
	 *  Deliberately not authority-only, unlike OnOwnerDamaged: a passive that remembers who the
	 *  player has shot is remembering it for that player's own screen, and the owning client is the
	 *  only machine where that memory is worth anything. */
	virtual void OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled) {}

	/** A shot is leaving the owner's weapon, before any of that shot's damage has been worked out.
	 *
	 *  This is where a per-shot resource is SPENT, and the timing is deliberate rather than
	 *  convenient: on the host, Fire() computes the damage and only then broadcasts that a shot
	 *  happened, while a client's shot reaches the server as two RPCs with the fire report arriving
	 *  first. Spending at the start of Fire is the only moment both routes agree on.
	 *
	 *  Runs on every machine that executes Fire, plus on the server for a remote pawn, where the
	 *  server does not run Fire at all. @see AShooterCharacter::Server_ReportWeaponFired */
	virtual void OnOwnerFiredWeapon() {}

	/** Damage this passive deals ITSELF for one hit on Target, straight to health and past whatever
	 *  shield the weapon is being held back by.
	 *
	 *  Not a multiplier on the weapon's damage, and the difference is the mechanic: the Sniper's
	 *  passive is supposed to hurt an enemy whose shield is still up, which no multiplier on a
	 *  gated number can ever do.
	 *
	 *  Answered for the shot that has already been latched by OnOwnerFiredWeapon, so asking twice
	 *  for the same shot gives the same answer — a shotgun's pellets are one shot and each pellet
	 *  carries it. Authority only in practice: the caller applies it. */
	virtual float GetBonusPierceDamage(const AActor* Target) const { return 0.0f; }

	/** The same number for the shot the owner would fire NEXT, for the HUD readout. Separate from
	 *  the one above because they answer different questions: one is what the last trigger pull was
	 *  worth, this is what the next one would be. */
	virtual float GetPredictedPierceDamage(const AActor* Target) const { return 0.0f; }

	/** Damage the owner's next shot would deal to Target, when this passive has a reason to show the
	 *  player that number. False from every passive that has nothing to show, which is all of them
	 *  but one, and false for a target this passive has no answer for.
	 *
	 *  Local and cosmetic: read by the overhead indicator on the machine the player is looking at. */
	virtual bool GetPredictedShotDamage(const AActor* Target, float& OutDamage) const { return false; }

	/** How far the owner's melee lunge may reach for one particular candidate.
	 *
	 *  Asked by UMeleeAttackComponent with its own configured LungeRange, once per candidate per
	 *  swing. A NULL Target asks a different question: the CEILING, the largest value this handler
	 *  could ever return for anybody. That is what the search sphere is sized to, before there is a
	 *  candidate to judge -- a search sized to the base range would never overlap the enemy an
	 *  extended reach exists for.
	 *
	 *  Unlike the two hooks above this one is NOT authority-only, and it must not become so: the
	 *  lunge is part of the movement simulation, so the client that swings and the server that
	 *  replays the move both ask it and both have to get the same answer. Keep it const, keep it
	 *  free of side effects, and keep it a pure function of the target. */
	virtual float ModifyLungeRange(const AActor* Target, float BaseRange) const { return BaseRange; }

	// ==================== Accessors ====================

	UFUNCTION(BlueprintPure, Category = "Ability")
	UAbilityDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	UAbilityComponent* GetOwningComponent() const { return OwningComponent; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	AShooterCharacter* GetOwningCharacter() const { return OwningCharacter; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	int32 GetCurrentLevel() const { return CurrentLevel; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	FAbilityCommonStats GetCommonStats() const;

protected:

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UAbilityDefinition> Definition;

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	int32 CurrentLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UAbilityComponent> OwningComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	TObjectPtr<AShooterCharacter> OwningCharacter;

	// ==================== Helpers (animation) ====================

	/** Cached owner FirstPersonMesh accessor. */
	USkeletalMeshComponent* GetFPMesh() const;
	UAnimInstance* GetFPAnimInstance() const;

	/** Play a montage on FirstPersonMesh. Returns its native length (not effective duration). */
	float PlayFPMontage(UAnimMontage* Montage, float PlayRate = 1.0f, FName StartSection = NAME_None);

	/** Stop a specific montage on FirstPersonMesh with a blend-out time. */
	void StopFPMontage(UAnimMontage* Montage, float BlendOutTime = 0.1f);

	/** Bind end delegate on a specific montage to a UFUNCTION on this handler. */
	void BindFPMontageEnd(UAnimMontage* Montage, FName CallbackFunctionName);

	// ==================== Helpers (charge) ====================

	float GetPlayerChargeModule() const;
	bool TryDeductCharge(float Amount);

	// ==================== Completion API ====================

	/** Signal to component that the ability finished successfully. Triggers cooldown. */
	void NotifyAbilityComplete();

	/** Signal to component that the ability was aborted. No cooldown applied. */
	void NotifyAbilityCancelled();
};
