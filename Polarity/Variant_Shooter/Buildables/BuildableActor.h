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
#include "BuildableActor.generated.h"

class AShooterPlayerState;
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
	 *  heals. Off by default: a building that needs it turns its tick on in its own constructor. */
	virtual void TickActive(float DeltaSeconds) {}

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
};
