// BuildableActor.h
// A thing a player put down: the common life of a turret, a dispenser and a teleporter end.
//
// Placed by UBuilderComponent through the server, owned by an AShooterPlayerState (the character
// dies, the building stays, exactly as in TF2), built up over time, kept alive by wrench hits and
// killed by damage. Everything a KIND of building does on top of that lives in a subclass and hangs
// off the hooks at the bottom.
//
// Server-authoritative in the project's usual shape: the authority writes the replicated fields,
// every machine redraws from OnRep, nothing is decided twice. The wrench is not a new input: a
// friendly melee hit arriving in TakeDamage IS the wrench (see the friendly branch there).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "Curves/CurveFloat.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "BuildableActor.generated.h"

class UUpgradeRegistry;
class AShooterPlayerState;
class AShooterCharacter;
class AShooterWeapon;
class UBoxComponent;
class UBuildableDefinition;
class ULootDropComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EBuildableState : uint8
{
	/** Rising out of the ground. Takes damage, does not work yet. */
	Constructing,
	/** Standing and doing its job. */
	Active,
	/** Standing, switched off by something (a sapper, an EMP). Reserved, nothing sets it yet. */
	Disabled,
	/** Gone. Kept for the few frames between the death and the actor leaving. */
	Destroyed
};

/** What one wrench hit did, mostly for the log and for a subclass that wants a sound per case. */
UENUM(BlueprintType)
enum class EBuildableWrenchResult : uint8
{
	Nothing,
	SpedUpConstruction,
	Repaired,
	Upgraded,
	UpgradeProgress
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBuildableChangedDelegate, ABuildableActor*, Buildable);

UCLASS(Abstract, Blueprintable)
class POLARITY_API ABuildableActor : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:

	ABuildableActor();

	// ==================== Components ====================

	/** The body: blocks movement, blocks and stops projectiles, casts the shadow, is what the
	 *  placement ghost copies. Set the asset and its offset in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Object type Pawn, query only, no responses: what a hitscan weapon finds, because
	 *  PerformHitscan asks for Pawn-typed objects and nothing else (Weapons.md). Fitted to the mesh
	 *  bounds in BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Hitbox;

	/** What falls out when it is destroyed: the metal scraps, set on the Blueprint as a loot list,
	 *  never in code. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<ULootDropComponent> LootDrop;

	// ==================== Settings ====================

	/** Whose side it is on. Players by default; the engine's team attitude is what every AI and
	 *  every damage test reads, so an enemy turret is this same class with another number. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buildable")
	uint8 TeamByte = 0;

	/** Fraction of full health a fresh building starts with; the rest grows in as it is built. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buildable", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartHealthFraction = 0.2f;

	/** Seconds the destroyed shell stays for its effect before the actor goes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buildable", meta = (ClampMin = "0.0", Units = "s"))
	float DestroyDelay = 0.5f;

	/** Pad added around the mesh bounds when the hitbox is fitted (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buildable", meta = (ClampMin = "0.0", Units = "cm"))
	float HitboxPadding = 5.0f;

	// ==================== Setup (server) ====================

	/** Called by the builder between SpawnActorDeferred and FinishSpawning, so BeginPlay already
	 *  knows what it is and whose it is. */
	void InitializeBuildable(UBuildableDefinition* InDefinition, AShooterPlayerState* InOwner);

	// ==================== Dispenser behaviour ====================
	//
	// A building IS a dispenser when its definition carries the Buildable.Dispenser tag, or when
	// its class is ADispenserBuildable. The behaviour lives on this base class so a dispenser
	// Blueprint that predates ADispenserBuildable and is a DIRECT child of ABuildableActor still
	// heals, still dispenses fuel and still takes sacrificed weapons (BP_Buildable_Dispenser is
	// exactly that Blueprint).

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Healing", meta = (ClampMin = "0.0"))
	float HealPerSecond = 12.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Healing", meta = (ClampMin = "0.0", Units = "cm"))
	float ServiceRadius = 350.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Ammo", meta = (ClampMin = "0.0"))
	float AmmoRoundsPerSecond = 2.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Ammo", meta = (ClampMin = "1"))
	int32 MaxFuel = 240;

	/** True when this building behaves as a dispenser, whatever its class. */
	UFUNCTION(BlueprintPure, Category = "Dispenser")
	bool IsDispenser() const;

	/** Fuel units in the hopper, fed by sacrificed weapons and burned into ammunition. */
	UFUNCTION(BlueprintPure, Category = "Dispenser")
	int32 GetFuel() const { return DispenserFuel; }

	/** Refill the hopper (a weapon was just melted into it). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dispenser")
	void AddFuel(int32 Amount);

	/** Take one of Donor's ranged weapons through the same authoritative inventory release a
	 *  turret uses, and bet it: its fair price (AShooterWeapon::GetDepositMoneyValue) is the stake,
	 *  and every card of the upgrade offer rolls its own rarity from stake x PayoutCurve. One gun,
	 *  one spin. Refused before the gun leaves the hands while a pick is pending or on cooldown. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dispenser")
	bool AcceptWeaponForFuel(AShooterCharacter* Donor, AShooterWeapon* Weapon);

	// ==================== Dispenser casino ====================
	//
	// Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md. Each card of an offer rolls its own
	// value: stake x PayoutCurve(roll), the curve drawn by inverse transform (a uniform roll 0..1
	// on X, the multiplier on Y; a wide flat stretch is common, a steep tail is rare). The value
	// against the thresholds below is the card's rarity, and the wave's level cap clips it.

	/** X: uniform roll 0..1. Y: multiplier on the stake. Keep it rising left to right. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino")
	FRuntimeFloatCurve PayoutCurve;

	/** Every upgrade the offers draw from (those with bInDispenserPool). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino")
	TSoftObjectPtr<UUpgradeRegistry> UpgradeRegistry;

	/** Cards per offer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino", meta = (ClampMin = "1", ClampMax = "5"))
	int32 OfferCardCount = 3;

	/** Card value (stake x multiplier) at which a card is Rare, Epic, Legendary. Below Rare: Common. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino", meta = (ClampMin = "0.0"))
	float RareValue = 60.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino", meta = (ClampMin = "0.0"))
	float EpicValue = 120.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino", meta = (ClampMin = "0.0"))
	float LegendaryValue = 240.0f;

	/** Highest upgrade level an offer may give, by siege wave (index = wave, 0 before the first;
	 *  the last entry holds from there on). The guard against a player out-growing the run early. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino")
	TArray<int32> MaxLevelByWave;

	/** Seconds a player waits between two spins, on top of having to pick the last offer first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino", meta = (ClampMin = "0.0", Units = "s"))
	float SpinCooldownSeconds = 4.0f;

	/** Rarity of a card worth Value. */
	UFUNCTION(BlueprintPure, Category = "Dispenser|Casino")
	EUpgradeRarity RarityForValue(float Value) const;

	/** Level cap of the current siege wave (MaxLevelByWave; no director or empty table: no cap). */
	UFUNCTION(BlueprintPure, Category = "Dispenser|Casino")
	int32 GetCurrentLevelCap() const;

	/** Current siege wave, 0 before the first or without a director. */
	UFUNCTION(BlueprintPure, Category = "Dispenser|Casino")
	int32 GetCurrentWave() const;

	/** Multiplier for one roll in 0..1, never negative. */
	UFUNCTION(BlueprintPure, Category = "Dispenser|Casino")
	float EvaluatePayoutMultiplier(float Roll) const;

	/** Average multiplier of PayoutCurve (return to player), integrated over Samples steps. */
	UFUNCTION(BlueprintPure, Category = "Dispenser|Casino")
	float ComputePayoutMean(int32 Samples = 1000) const;

	/** Fair price of everything bet here so far. */
	UFUNCTION(BlueprintPure, Category = "Dispenser|Casino")
	int32 GetTotalDepositedMoney() const { return TotalDepositedMoney; }

	/** Health gained per unit of fair price bet here, maximum and current alike. The core grows
	 *  from what it is fed (the bet, not the payout: luck at the casino does not build walls). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dispenser|Casino", meta = (ClampMin = "0.0"))
	float HealthPerDepositedMoney = 1.0f;

	// ==================== Siege core ====================
	//
	// The one rule of the core (the author's, 2026-09-14): while a player stands within DefendRadius
	// of it, the enemy fights the players as usual; the moment nobody is that close, the enemy turns
	// on the core itself. A building becomes the core either by being an ASiegeCoreBuildable placed
	// in a level (a bench), or by being the first dispenser a player builds on a siege map
	// (ASiegeDirector::NotifyBuildablePlaced).

	/** No player within this of the core (cm): the enemy attacks the core instead of the players. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siege", meta = (ClampMin = "0.0", Units = "cm"))
	float DefendRadius = 4000.0f;

	UFUNCTION(BlueprintPure, Category = "Siege")
	bool IsSiegeCore() const { return bSiegeCore; }

	/** Make this building what the siege is after. Server only. */
	void MakeSiegeCore();

	/** A player is within DefendRadius. Server-side answer: only the server sees every pawn. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	bool IsDefended() const;

	/** The nearest standing core to From that no player is defending, or null when every core is
	 *  either defended or gone. What an attacker asks before it picks a pawn. */
	static ABuildableActor* FindUndefendedCore(const UWorld* World, const FVector& From);

	/** The nearest standing core to From, defended or not. What an attacker asks when it has no
	 *  player to fight: the tower-defence rule is that the base itself is the default target. */
	static ABuildableActor* FindNearestCore(const UWorld* World, const FVector& From);

	// ==================== State ====================

	UFUNCTION(BlueprintPure, Category = "Buildable")
	UBuildableDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	AShooterPlayerState* GetOwnerPlayerState() const { return OwnerPlayerState; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	EBuildableState GetBuildableState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	bool IsActive() const { return State == EBuildableState::Active; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	bool IsDestroyed() const { return State == EBuildableState::Destroyed; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	int32 GetBuildLevel() const { return BuildLevel; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	int32 GetMaxLevel() const;

	/** 0..1 while constructing, 1 afterwards. */
	UFUNCTION(BlueprintPure, Category = "Buildable")
	float GetConstructionProgress() const { return ConstructionProgress; }

	/** Metal hammered into the next level so far, and what the level costs. 0/0 at the top. */
	UFUNCTION(BlueprintPure, Category = "Buildable")
	int32 GetUpgradeMetal() const { return UpgradeMetal; }

	UFUNCTION(BlueprintPure, Category = "Buildable")
	int32 GetUpgradeCost() const;

	UFUNCTION(BlueprintPure, Category = "Buildable")
	UStaticMeshComponent* GetMesh() const { return Mesh; }

	/** Fires on every machine whenever health, level, state or progress changes. The HUD's building
	 *  panel lives on this. */
	UPROPERTY(BlueprintAssignable, Category = "Buildable")
	FBuildableChangedDelegate OnBuildableChanged;

	// ==================== Actions (server) ====================

	/** One swing of the wrench from Hitter, in TF2's order: speed the construction up, else repair,
	 *  else whatever the subclass wants (a sentry's ammunition), else put metal into the upgrade.
	 *  Metal is taken from the hitter, whoever they are: anyone on the side may maintain anyone's
	 *  building. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Buildable")
	EBuildableWrenchResult ReceiveWrenchHit(AShooterPlayerState* Hitter);

	/** Damage that skips the side check: a console command, a scripted event. Ordinary damage goes
	 *  through TakeDamage like everything else's. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Buildable")
	void ApplyBuildableDamage(float Damage);

	/** The owner taking it down on purpose. No scraps: those are for a building the enemy earned. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Buildable")
	void Demolish();

	// ==================== AActor ====================

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== IGenericTeamAgentInterface ====================

	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamByte); }
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { TeamByte = NewTeamId.GetId(); }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== Hooks for the kinds of building ====================
	// All run on every machine unless said otherwise, after the replicated field has landed, so a
	// subclass can react with visuals anywhere and with gameplay behind HasAuthority().

	/** Construction reached 100%: the building starts working. */
	virtual void OnConstructionFinished() {}

	/** Level went up (server first, then each client as the field arrives). */
	virtual void OnLevelChanged(int32 NewLevel) {}

	/** State changed, any direction. */
	virtual void OnStateChanged(EBuildableState OldState, EBuildableState NewState) {}

	/** Every frame while Active, server only. Where a turret looks for targets and a dispenser
	 *  heals. The base implementation runs the dispenser behaviour when this building is one. */
	virtual void TickActive(float DeltaSeconds);

	/** The heal/ammunition heartbeat, everything the base does for dispenser buildings. Server. */
	void TickDispenserBehavior(float DeltaSeconds);

	/** Whether the actor keeps ticking while active: the subclass flag, or being a dispenser. */
	bool NeedsActiveTick() const { return bTickWhileActive || IsDispenser(); }

	/** A wrench hit that neither sped construction nor repaired anything. Return true to say the hit
	 *  was used (a sentry restocking its shells), false to let it go into the upgrade. Server only. */
	/** Subclasses consume from RemainingBudget for their own resources (ammo, fuel, etc.). */
	virtual bool OnWrenchHitExtra(AShooterPlayerState* Hitter, int32& RemainingBudget) { return false; }

	/** -1 keeps legacy unlimited-per-hit behavior; specialized buildings may bound the hit. */
	virtual int32 GetWrenchMetalBudget() const { return -1; }

	/** The building is dead. Server only, before the loot and the destroy. */
	virtual void OnDestroyed_Native() {}

	/** Whether the actor keeps ticking once built. False here: the base has nothing to do per frame
	 *  after construction, so it stops. A turret sets this in its constructor and gets TickActive. */
	bool bTickWhileActive = false;

	// Blueprint-side echoes of the same moments, for effects and sounds.

	UFUNCTION(BlueprintImplementableEvent, Category = "Buildable", meta = (DisplayName = "On Construction Finished"))
	void BP_OnConstructionFinished();

	UFUNCTION(BlueprintImplementableEvent, Category = "Buildable", meta = (DisplayName = "On Level Changed"))
	void BP_OnLevelChanged(int32 NewLevel);

	UFUNCTION(BlueprintImplementableEvent, Category = "Buildable", meta = (DisplayName = "On Wrench Hit"))
	void BP_OnWrenchHit(EBuildableWrenchResult Result);

	UFUNCTION(BlueprintImplementableEvent, Category = "Buildable", meta = (DisplayName = "On Buildable Destroyed"))
	void BP_OnBuildableDestroyed(bool bDemolished);

	// ==================== Replicated state ====================

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Buildable")
	TObjectPtr<UBuildableDefinition> Definition;

	UPROPERTY(ReplicatedUsing = OnRep_OwnerPlayerState, BlueprintReadOnly, Category = "Buildable")
	TObjectPtr<AShooterPlayerState> OwnerPlayerState;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Buildable")
	EBuildableState State = EBuildableState::Constructing;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Buildable")
	float Health = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Buildable")
	float MaxHealth = 150.0f;

	UPROPERTY(ReplicatedUsing = OnRep_BuildLevel, BlueprintReadOnly, Category = "Buildable")
	int32 BuildLevel = 1;

	UPROPERTY(ReplicatedUsing = OnRep_ConstructionProgress, BlueprintReadOnly, Category = "Buildable")
	float ConstructionProgress = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_UpgradeMetal, BlueprintReadOnly, Category = "Buildable")
	int32 UpgradeMetal = 0;

	UPROPERTY(ReplicatedUsing = OnRep_DispenserFuel, BlueprintReadOnly, Category = "Dispenser|Ammo", meta = (ClampMin = "0"))
	int32 DispenserFuel = 0;

	/** What the siege is after. Replicated so a HUD can mark it. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Siege")
	bool bSiegeCore = false;

	UFUNCTION()
	void OnRep_OwnerPlayerState();

	UFUNCTION()
	void OnRep_State(EBuildableState OldState);

	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_BuildLevel();

	UFUNCTION()
	void OnRep_ConstructionProgress();

	UFUNCTION()
	void OnRep_UpgradeMetal();

	UFUNCTION()
	void OnRep_DispenserFuel();

	/** Effects of the death, everywhere. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnDestroyed(bool bDemolished);

	/** Effects of a wrench hit, everywhere. Unreliable: a lost clang is nothing. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_OnWrenchHit(EBuildableWrenchResult Result);

private:

	// ==================== Server-side internals ====================

	void SetState(EBuildableState NewState);
	void SetHealth(float NewHealth);
	void FinishConstruction();
	void Die(bool bDemolished);

	/** Why Donor may not spin now (a pick pending, cooldown, empty pool), or empty when they may.
	 *  Asked before the gun leaves the hands. */
	FString GetSpinRefusal(const AShooterCharacter* Donor) const;

	/** True for the kind of hit that counts as the wrench: melee, from a player on this side. */
	bool IsWrenchHit(const FDamageEvent& DamageEvent, AActor* DamageCauser) const;

	// ==================== Visuals, every machine ====================

	/** Re-derive everything visible from the replicated fields. */
	void RefreshVisuals();
	void FitHitboxToMesh();
	void Announce();

	/** Where the mesh sits when built; during construction it is sunk below this by its height. */
	FVector MeshRestLocation = FVector::ZeroVector;
	float MeshHeight = 100.0f;

	/** Progress as drawn, chasing the replicated value so a 10 Hz update does not step. */
	float VisualProgress = 0.0f;

	/** Construction boost from wrench hits, server only. */
	float BoostUntilTime = 0.0f;
	float BoostMultiplier = 0.0f;

	/** Fractional rounds hoarded by the dispenser's AmmoRoundsPerSecond between whole rounds. */
	float AmmoAccumulator = 0.0f;

	/** Server only, behind GetTotalDepositedMoney. */
	int32 TotalDepositedMoney = 0;

	/** Health the bets have added on top of the level's own, so a wrench upgrade keeps it. */
	float DepositHealthBonus = 0.0f;

	/** Walk the cores: the nearest standing one, dropped when bRequireUndefended and a player
	 *  stands inside its DefendRadius. */
	static ABuildableActor* FindCore(const UWorld* World, const FVector& From, bool bRequireUndefended);
};
