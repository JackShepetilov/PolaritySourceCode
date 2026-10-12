// ShieldFieldComponent.h
// The enemy's shield, as a thing of its own: a pool of durability that takes the hit first, breaks,
// and comes back the way its owner is configured to let it.
//
// THE SHIELD REWORK, Phases 1-2 (Docs/Shield_Field_Rework_Plan_2026-09-22.md). Before the rework the
// "shield" was the EMF charge meter read upside down - zero charge meant whole, a full meter meant
// broken - so every system that moved charge for physics reasons silently healed or broke shields.
//
// THE INVARIANT THIS FILE EXISTS FOR: shield state flows ONE WAY, shield -> charge. The shield hands
// polarity to an UEMFVelocityModifier (FeedPolarityCharge, scaled by ChargePerShieldDamage) and
// mirrors its transitions into it for the old listeners; it NEVER reads the charge meter as its own
// state, and the EMF plugin is not part of the shield anywhere in this file.
//
// Damage enters through exactly two functions - AbsorbDamage (any hit) and ApplyIonization (an
// ionizing shot). Everything else asks and never writes: IsShieldUp is the single source of truth
// for the weapon's damage gate, the AI's push/peek condition, the impact surface, the grab gate and
// the HUD.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShieldFieldComponent.generated.h"

class UEMFVelocityModifier;
class AActor;
class UPrimitiveComponent;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

/** How the shield comes back after it has been stripped. */
UENUM(BlueprintType)
enum class EShieldRegenMode : uint8
{
	/** It does not come back on its own. ShieldRestore and ShieldLoan are what put a shield back.
	 *  The Apex reading of an enemy shield, and the default. */
	Manual,

	/** After RegenDelay seconds without a hit on the shield it rebuilds at RegenRate per second.
	 *  A new hit on the shield restarts the delay from zero. */
	Timed
};

/** The shield's level moved (any change, up or down). The bar reads this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShieldFieldChanged, float, Current, float, Max);

/** A hit landed on a shield that was still holding: what it took, what went through, and whether
 *  this hit is the one that took the shield down. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnShieldFieldHit, float, ShieldDamage, float, OverflowDamage, bool, bBrokeThisHit);

/** This hit took the shield down. Exactly once per break. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShieldFieldBroken, AActor*, Instigator, UPrimitiveComponent*, HitComponent);

/** The shield is whole again after having been broken. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShieldFieldRestored);

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class POLARITY_API UShieldFieldComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShieldFieldComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	// ==================== The questions the game asks ====================

	/** True while the owner still has a shield to hide behind.
	 *
	 *  THE single source of truth. AShooterWeapon::IsTargetShieldDown, the AI's push/peek condition,
	 *  the impact-surface choice, the grab gate and the HUD all come through here instead of each
	 *  reading a charge of their own, which is what let "may I hurt it" and "should it stop pushing"
	 *  drift apart in the first place. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsShieldUp() const { return CurrentShield > KINDA_SMALL_NUMBER; }

	/** The same answer, spelled out for the call sites that ask it that way. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsBroken() const { return !IsShieldUp(); }

	/** How much of the shield is still there: 1 = whole, 0 = none. This is the number a bar draws. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetShieldFraction() const;

	/** How much of the shield is GONE: 0 = whole, 1 = stripped. The shape the AI's recovered
	 *  fraction and the class passives ("more reach the more it is stripped") are written in. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetStrippedFraction() const { return 1.0f - GetShieldFraction(); }

	/** Visibly damaged but still holding: at or below CrackedFraction of the pool left. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsCracked() const { return IsShieldUp() && GetShieldFraction() <= CrackedFraction; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetCurrentShield() const { return CurrentShield; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetMaxShield() const { return MaxShield; }

	/** Does the armour cover a hit on this bone. True for an empty CoveredBones list (whole body)
	 *  and for a hit with no bone at all (explosion, melee, a drone with no skeleton). */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool CoversBone(FName BoneName) const;

	// ==================== The two ways damage enters ====================

	/** Take InDamage against the pool and return what is LEFT OVER for health.
	 *
	 *  Authority only: health is server-side, and this is called from AShooterNPC::TakeDamage, the
	 *  one funnel every kind of damage passes through. A caller that must not be absorbed (the
	 *  Wizard's opened window) simply does not call it.
	 *
	 *  ArmorScale is how hard this round bites armour (UDamageType_Ranged::ArmorDamageScale): the
	 *  pool loses InDamage x ArmorScale, and what the pool could not hold comes back divided by it,
	 *  so health is hurt in the round's own damage. */
	float AbsorbDamage(float InDamage, AActor* Instigator, UPrimitiveComponent* HitComponent, float ArmorScale = 1.0f);

	/** What a hit on a bone the armour does not cover is scaled by (a forearm, a shin). The head is
	 *  not a limb and is not scaled. @see AShooterNPC::TakeDamage */
	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetUncoveredDamageMultiplier() const { return UncoveredDamageMultiplier; }

	/** An ionizing shot lands. Strips the pool by ShieldDamage and hands the shot's polarity to the
	 *  field, which is what the owner's physics (push, pull, the grab's opposite-sign rule) reads.
	 *
	 *  On a shield that is already broken this restores nothing - a shooter must not be able to heal
	 *  an enemy by shooting it - but it still polarises, which is Phase 2's DoD.
	 *
	 *  @param SignedChargePerHit  the shot's ionization, sign included: it sets the field's sign.
	 *  @param ShieldDamage        how much pool this shot strips; <= 0 means abs(SignedChargePerHit).
	 *  @return what was actually stripped (0 on a broken field). */
	float ApplyIonization(float SignedChargePerHit, float ShieldDamage, AActor* Instigator);

	/** Give shield back (ShieldRestore and friends). Returns what was actually restored; an enemy
	 *  whose field is whole has nothing to give back, and that is the ability's incentive. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	float AddShield(float Amount);

	/** Whole again, timers cleared. The NPC pool calls this on recycle so a reused body does not
	 *  come back out carrying the last life's damage. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void ResetForReuse();

	// ==================== Configuration ====================

	/** Pool size. The one number "how much shield this enemy has". */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void SetMaxShield(float NewMaxShield);

	/** Apply one of the four armour tiers (0 white, 1 blue, 2 purple, 3 red) as the pool size.
	 *  MaxShield stays the authoritative number; this is a convenience for authoring. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void SetShieldTier(int32 NewTier);

	// ==================== Events ====================

	/** The shield's level moved (any change, up or down). The bar reads this. */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldFieldChanged OnShieldChanged;

	/** A hit landed on a shield that was still holding, with what it took and what went through. */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldFieldHit OnShieldHit;

	/** This hit took the shield down. Exactly once per break. */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldFieldBroken OnShieldBroken;

	/** The shield is whole again after having been broken. */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldFieldRestored OnShieldRestored;

protected:
	// ==================== State (the server decides, the client mirrors) ====================

	/** Durability left. Replicated: the shield is combat state and a client may not invent it. */
	UPROPERTY(ReplicatedUsing = OnRep_Shield)
	float CurrentShield = 0.0f;

	/** Pool size, replicated so a client's bar normalises against the number the server used.
	 *  Authored on the Blueprint that carries the component; UEnemyCombatProfile::ShieldCharge
	 *  overrides it when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Shield, Category = "Shield", meta = (ClampMin = "0.0"))
	float MaxShield = 50.0f;

	/** Polarity the field carries, -1 / 0 / +1, written by the ionization working on it. Presentation. */
	UPROPERTY(Replicated)
	int32 FieldSign = 0;

	/** Client mirror of "was the shield up", so OnRep catches the edge instead of the level. */
	bool bClientShieldWasUp = true;

	UFUNCTION()
	void OnRep_Shield();

	// ==================== Coverage ====================

	/** Bones the armour sits on. Empty = the whole body. Names are the bodies of the physics asset
	 *  on the owner mesh, matched exactly (a hit reports the body it struck): listing spine_03 does
	 *  NOT pull in the arms hanging off it. A hit on any other bone goes past the armour, straight
	 *  to health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Coverage")
	TArray<FName> CoveredBones;

	/** A hit on a bone outside CoveredBones goes past the armour and is scaled by this: a limb is
	 *  the way round the plate, and the price of it [author, 2026-10-12]. Only read while
	 *  CoveredBones is not empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Coverage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UncoveredDamageMultiplier = 0.7f;

	// ==================== Shield recovery ====================

	/** See EShieldRegenMode. Manual is the default: the abilities are what bring a shield back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Recovery")
	EShieldRegenMode RegenMode = EShieldRegenMode::Manual;

	/** Seconds after the last hit on the shield before a Timed rebuild starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Recovery", meta = (ClampMin = "0.1", Units = "s", EditCondition = "RegenMode == EShieldRegenMode::Timed"))
	float RegenDelay = 5.0f;

	/** Shield per second rebuilt in Timed mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Recovery", meta = (ClampMin = "0.0", EditCondition = "RegenMode == EShieldRegenMode::Timed"))
	float RegenRate = 10.0f;

	// ==================== Presentation hooks (Phase 4) ====================

	/** Below this fraction of the pool a holding shield reads as cracked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Presentation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrackedFraction = 0.33f;

	/** Played on the owner when the shield breaks: the event has no hit point, so the enemy states
	 *  it itself (energy pop on a drone, plate cracking on a mercenary). Every machine plays its own
	 *  from the same transition, no RPC. What a HIT on the shield looks like is not here: that is
	 *  the weapon's impact by surface, AShooterNPC::ShieldImpactSurface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Presentation")
	TObjectPtr<UNiagaraSystem> BreakEffect;

	/** World sound of the break. Leave empty while the owner's UEMFVelocityModifier::ShieldBreakSound
	 *  is set: that one still plays through the mirror, and both together would double it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Presentation")
	TObjectPtr<USoundBase> BreakSound;

	/** Played on the owner when a broken shield comes back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Presentation")
	TObjectPtr<UNiagaraSystem> RestoredEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Presentation")
	TObjectPtr<USoundBase> RestoredSound;

	/** Attenuation for both sounds above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Presentation")
	TObjectPtr<USoundAttenuation> PresentationSoundAttenuation;

	// ==================== The bridge to polarity physics ====================

	/** Shield damage converted into EMF charge for the owner's physics, per point of shield taken.
	 *
	 *  1.0 keeps today's dynamics exactly: the pool is the old charge cap, so a shield that strips
	 *  fully leaves the meter exactly where the meter used to end up. The grab gate no longer
	 *  depends on this (it reads the field), but push/pull strength and the opposite-sign rule do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Polarity")
	float ChargePerShieldDamage = 1.0f;

	/** Master switch for the bridge above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Polarity")
	bool bFeedPolarityCharge = true;

	// ==================== Internals ====================

	/** Authority: write the level, catch the transition, mirror into the charge, tell the listeners. */
	void SetShieldLevel(float NewShield, AActor* Instigator, UPrimitiveComponent* HitComponent, float IncomingDamage);

	/** Both sides of a whole<->broken transition. Server walks it in the mutation, client in OnRep. */
	void HandleShieldTransition(bool bWasUp, bool bIsUp, AActor* Instigator, UPrimitiveComponent* HitComponent);

	/** The owner's own break / restore presentation. Cosmetic, skipped on a dedicated server. */
	void PlayTransitionPresentation(UNiagaraSystem* Effect, USoundBase* Sound) const;

	/** One-way bridge: the shield hands its polarity to the charge meter. Never the reverse. */
	void FeedPolarityCharge(float SignedAmount);

	/** Restart (or stop) the Timed rebuild timer. */
	void UpdateRegenTimer();

	UFUNCTION()
	void TickRegen();

	/** Server clock of the last hit the shield took; the Timed delay is measured from it. */
	float LastShieldHitTime = -1000.0f;

	static constexpr float RegenTickInterval = 0.25f;

	FTimerHandle RegenTimerHandle;
};

/**
 * One door for "has this actor got a shield up".
 *
 * An actor carrying a UShieldFieldComponent answers for itself. Everyone else - a player before it
 * gets one, a physics prop, a bare charge carrier - keeps the answer the game had before the field
 * existed, and that answer still lives at the call site (AShooterWeapon::IsTargetShieldDown's own
 * branches), deliberately NOT here: those targets have no shield state to report, and pretending
 * otherwise is how a crate ends up with a shield bar.
 */
UCLASS()
class POLARITY_API UShieldFieldStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The shield field on this actor, or null when it has none. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	static UShieldFieldComponent* GetShieldField(const AActor* Actor);

	/** True while this actor still has a shield.
	 *
	 *  FALSE FOR AN ACTOR WITH NO FIELD, and that means "there is no shield state here", not "the
	 *  shield is down": a chargeless target is freely hurtable, and the callers that must treat it
	 *  that way (the weapon gate, the AI condition) keep their own rule for such actors. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	static bool IsShieldUp(const AActor* Actor);
};
