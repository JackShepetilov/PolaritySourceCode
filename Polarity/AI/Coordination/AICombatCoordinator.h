// AICombatCoordinator.h
// Global coordinator for NPC attack permissions, token-based combat, battle circle positioning, and role/pressure management

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AICombatCoordinator.generated.h"

// ==================== Enums ====================

/** Type of attack token (determines which pool the NPC draws from) */
UENUM(BlueprintType)
enum class EAttackTokenType : uint8
{
	Ranged,     // ShooterNPC burst fire, FlyingDrone shooting
	Melee,      // MeleeNPC dash + melee attack
	Special,    // Reserved for boss abilities, grenades, etc.
	Kamikaze    // KamikazeDroneNPC dive attacks (separate pool, dynamic sizing)
};

/** Role assigned to NPC for combat coordination */
UENUM(BlueprintType)
enum class EAICombatRole : uint8
{
	Aggressor,      // Actively pushing player, inner ring, always attacks
	Supporter,      // Mid-range fire support, middle ring
	Flanker,        // Positioned >90 degrees from player facing
	Pressurer       // Responds to player state (low HP, no armor)
};

/** Ring definition for battle circle positioning */
UENUM(BlueprintType)
enum class EBattleRing : uint8
{
	Inner,    // 400-600cm, melee/aggressive
	Middle,   // 600-1200cm, shooters
	Outer     // 1200-2000cm, drones/snipers
};

// ==================== Structs ====================

/** Token pool for a specific attack type */
USTRUCT()
struct FTokenPool
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxTokens = 2;

	TArray<TWeakObjectPtr<APawn>> HeldBy;

	int32 GetAvailableCount() const { return FMath::Max(0, MaxTokens - HeldBy.Num()); }
	bool HasToken(APawn* NPC) const;
	bool TryAcquire(APawn* NPC);
	void Release(APawn* NPC);
	void CleanupInvalid();
};

/** Strafe slot assignment for a drone in confined spaces */
USTRUCT()
struct FStrafeSlot
{
	GENERATED_BODY()

	TWeakObjectPtr<APawn> AssignedDrone;
	FVector Center = FVector::ZeroVector;
	FVector Axis = FVector::ZeroVector;
	float HeightOffset = 0.0f;
	float AngleDeg = 0.0f;
};

/** Battle circle slot — a position around the player that an NPC is assigned to */
USTRUCT()
struct FBattleSlot
{
	GENERATED_BODY()

	FVector WorldPosition = FVector::ZeroVector;
	float AngleDeg = 0.0f;
	EBattleRing Ring = EBattleRing::Middle;
	TWeakObjectPtr<APawn> AssignedNPC;

	bool IsOccupied() const { return AssignedNPC.IsValid(); }
};

/** Internal data for registered NPC */
USTRUCT()
struct FRegisteredNPCData
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<APawn> NPC;

	/** Which player this NPC is fighting. Remembered, not re-derived: the whole point of keeping it
	 *  here is that "nearest player" recomputed every frame flips between two teammates standing
	 *  near each other, and an enemy that changes its mind sixty times a second reads as broken.
	 *  @see AAICombatCoordinator::UpdateNPCTargets */
	UPROPERTY()
	TWeakObjectPtr<AActor> Target;

	/** How long a closer player has been closer by more than the switch margin. Reset the moment the
	 *  contender stops leading, so a teammate has to genuinely take over, not merely brush past. */
	float TargetSwitchPressure = 0.0f;

	/** Index into Groups, or INDEX_NONE. Rebuilt each tick alongside the membership lists; never
	 *  trust it across a frame boundary. */
	int32 GroupIndex = INDEX_NONE;

	EAICombatRole Role = EAICombatRole::Supporter;
	float AttackScore = 0.0f;
	float WaitTime = 0.0f;
	float PermissionTime = 0.0f;
	float AttackingTime = 0.0f;
	bool bHasAttackPermission = false;
	bool bIsCurrentlyAttacking = false;

	// Token system
	EAttackTokenType TokenType = EAttackTokenType::Ranged;
	bool bHasToken = false;
	bool bProximityOverride = false;

	// Battle Circle
	int32 AssignedSlotIndex = -1;
	FVector AssignedSlotPosition = FVector::ZeroVector;

	// Role/Pressure
	float AngleToPlayerFacing = 0.0f;
};

/** Cached player state for pressure system */
struct FPlayerStateCache
{
	float HPPercent = 1.0f;
	float ArmorPercent = 0.0f;
	float Speed = 0.0f;
	FVector FacingDirection = FVector::ForwardVector;
	FVector Position = FVector::ZeroVector;
	bool bIsValid = false;
};

/** Everything the coordinator arranges AROUND one player.
 *
 *  The coordinator used to hold exactly one of each of these and point them all at a single
 *  PrimaryTarget, so with four players the enemies chose their opponents correctly and then formed
 *  up around whoever happened to be busiest. A group is that same set of things, once per player
 *  somebody is actually fighting.
 *
 *  Groups PERSIST between ticks. They cannot be rebuilt from scratch each frame because the token
 *  pools hold live grants: throwing them away would hand every enemy a fresh permission to attack
 *  sixty times a second. Membership is rebuilt each tick; the group itself is not.
 *
 *  Single player is the same code with one group, which is why this can be checked without a bench. */
struct FTargetGroup
{
	/** The player this group forms up around. A group with no target is retired. */
	TWeakObjectPtr<AActor> Target;

	/** Indices into RegisteredNPCs. Rebuilt every tick, cheap, never persists. */
	TArray<int32> Members;

	/** Ring of positions around Target. */
	TArray<FBattleSlot> BattleSlots;
	float TimeSinceLastSlotRecalc = 0.0f;
	int32 LastSlotNPCCount = -1;

	/** How much pressure this ONE player is under. The global ceiling sits above all groups
	 *  together; these are what stop four enemies piling onto the same person. */
	FTokenPool Ranged;
	FTokenPool Melee;
	FTokenPool Special;

	/** This player's health, armour, speed and facing, for the role and pressure system. */
	FPlayerStateCache State;
};

/** Something loud on the ground that enemies near it should fight instead of a player.
 *
 *  The Tank's item verb: a fully charged prop thrown anywhere turns into one of these for a few
 *  seconds. It lives here rather than on the prop because "who is this NPC fighting" has exactly one
 *  owner, and adding a second answer somewhere else is how enemies end up chasing one thing while
 *  their formation is laid out around another.
 *
 *  Not a USTRUCT and not a UPROPERTY, same as FTargetGroup: plain data with weak pointers, nothing
 *  for the garbage collector to keep alive. A decoy that is destroyed mid-pull simply stops being
 *  found, and the NPCs holding it go back to the nearest player on the next tick. */
struct FActiveDecoy
{
	TWeakObjectPtr<AActor> Actor;

	/** How far its noise carries (cm). An enemy outside this is not distracted at all — the decoy is
	 *  a local event, not a global one. */
	float Radius = 0.0f;

	/** World time it stops working. */
	float ExpiryTime = 0.0f;
};


// ==================== Coordinator ====================

/**
 * Singleton coordinator that manages NPC combat behavior:
 * - Token-based attack permissions (Ranged/Melee/Special pools)
 * - Battle circle positioning (slot-based rings around player)
 * - Role & pressure management (dynamic roles based on player state)
 * Spawn one instance in the level or use GetCoordinator() to auto-spawn.
 */
UCLASS(BlueprintType)
class POLARITY_API AAICombatCoordinator : public AActor
{
	GENERATED_BODY()

public:
	AAICombatCoordinator();

	// ==================== General Settings ====================

	/** Maximum number of NPCs that can attack simultaneously (legacy, still enforced as total cap) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxSimultaneousAttackers = 3;

	// ==================== Target selection ====================
	// One owner for "who is this NPC fighting". Before this the answer was CoopPlayers::GetNearest
	// recomputed at each of ~18 call sites, with no memory anywhere, so two teammates standing close
	// together made every enemy flicker between them. It also left no single place to hang a threat
	// value on later, which is the whole point of putting it here.

	/** A contender has to be this much closer (cm) than the current target before it counts as
	 *  leading at all. Pure distance ties are what cause the flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Targeting", meta = (ClampMin = "0.0"))
	float TargetSwitchMargin = 300.0f;

	/** And it has to keep leading for this long (s) before the enemy actually turns. Together with the
	 *  margin this is the difference between "somebody ran past" and "somebody took over". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Targeting", meta = (ClampMin = "0.0"))
	float TargetSwitchDelay = 0.75f;

	/** Write a full state snapshot to the log on an interval, tagged [COOP_DEBUG]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bLogStateSnapshot = true;

	/** Seconds between snapshots. Two is slow enough not to drown the log and fast enough to catch a
	 *  target switch, which cannot happen more often than TargetSwitchDelay anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug", meta = (ClampMin = "0.25"))
	float StateSnapshotInterval = 2.0f;

	/** How the total pressure budget grows with the size of the team. 1.0 is linear (four players
	 *  fight four times as much at once), 0.0 is flat (four players share what one faced). Sublinear
	 *  is the usual co-op answer: more going on with a full team, but not proportionally more per
	 *  head. @see GetEffectiveMaxAttackers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressureScalingExponent = 0.75f;

	/** Minimum time between attack permission grants (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MinTimeBetweenAttacks = 0.1f;

	/** Time before attack permission expires if not used (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float AttackPermissionTimeout = 2.0f;

	/** Maximum time an NPC can hold "attacking" status before being reset (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float MaxAttackingTime = 10.0f;

	// ==================== Token System ====================

	/** Maximum simultaneous ranged attack tokens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxRangedTokens = 2;

	/** Maximum simultaneous melee attack tokens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxMeleeTokens = 1;

	/** Maximum simultaneous special attack tokens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxSpecialTokens = 1;

	// --- Kamikaze Token Pool (dynamic sizing based on alive count) ---

	/** Number of alive kamikaze drones per token (e.g. 5 drones → 1 token, 10 → 2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens|Kamikaze", meta = (ClampMin = "1", ClampMax = "20"))
	int32 DronesPerKamikazeToken = 5;

	/** Maximum kamikaze tokens cap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens|Kamikaze", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxKamikazeTokensCap = 5;

	/** Stagger delay between kamikaze attack commits (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens|Kamikaze", meta = (ClampMin = "0"))
	float KamikazeStaggerDelay = 0.3f;

	/** Random range added to stagger delay (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens|Kamikaze", meta = (ClampMin = "0"))
	float KamikazeStaggerRandom = 0.3f;

	/** Distance threshold for proximity override (cm). NPC within this range attacks without token. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0"))
	float ProximityOverrideDistance = 250.0f;

	/** If true, NPC with LOS can steal token from NPC without LOS who is farther */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens")
	bool bAllowTokenStealing = true;

	// ==================== Scoring Weights ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float DistanceWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float LineOfSightWeight = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float WaitTimeWeight = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "100.0"))
	float MaxScoringDistance = 3000.0f;

	// ==================== Engagement Range ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Range", meta = (ClampMin = "0.0"))
	float MaxEngagementDistance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Range")
	bool bAllowFreeAttackOutsideRange = true;

	// ==================== Battle Circle ====================

	/** If true, use battle circle positioning instead of random NavMesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle")
	bool bUseBattleCircle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float InnerRingMinRadius = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float InnerRingMaxRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float MiddleRingMinRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float MiddleRingMaxRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float OuterRingMinRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float OuterRingMaxRadius = 2000.0f;

	/** How often to recalculate slot world positions (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "0.1"))
	float SlotRecalculationInterval = 0.5f;

	// ==================== Strafe Coordination ====================

	/** Number of sample directions for strafe slot calculation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Strafe", meta = (ClampMin = "4", ClampMax = "36"))
	int32 StrafeSampleDirections = 12;

	/** Height offset step between drones sharing similar strafe angles (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Strafe", meta = (ClampMin = "50"))
	float StrafeHeightStep = 100.0f;

	// ==================== Role & Pressure ====================

	/** HP percentage threshold below which pressure tactics activate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Pressure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHPThreshold = 0.3f;

	/** Armor percentage threshold below which grouping tactics activate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Pressure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowArmorThreshold = 0.1f;

	/** Minimum angle from player facing direction to qualify as Flanker (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Pressure", meta = (ClampMin = "45", ClampMax = "180"))
	float FlankerMinAngle = 90.0f;

	// ==================== Debug ====================

	/** Draw token/attacker status debug info */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawDebug = false;

	/** Draw battle circle rings and slots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawBattleCircle = false;

	/** Draw role names, player facing, pressure status */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawRoleDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorAttacking = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorWaiting = FColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorOutOfRange = FColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorInnerRing = FColor(255, 100, 100);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorMiddleRing = FColor(100, 255, 100);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorOuterRing = FColor(100, 100, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorAggressor = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorFlanker = FColor::Magenta;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorPressurer = FColor(255, 128, 0);

	// ==================== API ====================

	/** Get the combat coordinator instance. Creates one if it doesn't exist. */
	UFUNCTION(BlueprintPure, Category = "Coordination", meta = (WorldContext = "WorldContext"))
	static AAICombatCoordinator* GetCoordinator(const UObject* WorldContext);

	/** Register an NPC with the coordinator. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void RegisterNPC(APawn* NPC);

	/** Unregister an NPC from the coordinator. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void UnregisterNPC(APawn* NPC);

	/** Request permission to attack (bridges to token system internally). */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	bool RequestAttackPermission(APawn* Requester);

	/** Check if NPC has attack permission without requesting. */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	bool HasAttackPermission(APawn* NPC) const;

	/** Notify that attack has started (for tracking). */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void NotifyAttackStarted(APawn* Attacker);

	/** Notify that attack has completed. Releases attack token. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void NotifyAttackComplete(APawn* Attacker);

	/** Grant immediate retaliation permission (bypasses tokens). */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void GrantRetaliationPermission(APawn* NPC);

	/** Get the current role of an NPC. */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	EAICombatRole GetNPCRole(APawn* NPC) const;

	/** Set the role of an NPC. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void SetNPCRole(APawn* NPC, EAICombatRole NewRole);

	/** Get the current number of active attackers. */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	int32 GetActiveAttackerCount() const;

	/** Get the primary target (usually the player). */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	AActor* GetPrimaryTarget() const { return PrimaryTarget.Get(); }

	/** Who this NPC is fighting. The single answer, remembered rather than recomputed.
	 *
	 *  Call sites that still ask CoopPlayers::GetNearest for themselves should move onto this: they
	 *  each re-decide independently, so one enemy can be chasing player A while the coordinator
	 *  arranges its battle slots around player B. That migration is deliberately not done in this
	 *  change — it touches ~18 places across the AI and the arena, and belongs with a bench session
	 *  rather than a blind sweep. Returns null if the NPC is not registered or has no target yet. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Targeting")
	AActor* GetTargetFor(APawn* NPC) const;

	// --- Decoys ---

	/** Start pulling enemies within Radius onto Decoy for Duration seconds.
	 *
	 *  Server-side, like every other AI decision here. Calling it again for the same actor refreshes
	 *  the radius and the deadline rather than stacking a second entry, so a prop that somehow
	 *  registers twice does not become twice as loud.
	 *
	 *  Whether a decoy should out-shout a player who is standing on top of the enemy, and how the two
	 *  ought to weigh against each other, is a balance question nobody has answered yet: for now the
	 *  decoy simply wins inside its radius. @see UnregisterDecoy */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Targeting")
	void RegisterDecoy(AActor* Decoy, float Radius, float Duration);

	/** Stop it working now, before its time is up: it was destroyed, or its owner cancelled it.
	 *  Every NPC holding it is released in the same call rather than waiting for the next tick, so
	 *  the enemies turn back at the moment the prop breaks. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Targeting")
	void UnregisterDecoy(AActor* Decoy);

	/** True while this actor is a live decoy. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Targeting")
	bool IsActiveDecoy(const AActor* Actor) const;

	/** The global ceiling on simultaneous attackers, grown sublinearly with the size of the team.
	 *  This is the ceiling for the WHOLE fight; per-target limits are what stop four enemies piling
	 *  onto one player, and they come from the token pools. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Targeting")
	int32 GetEffectiveMaxAttackers() const;

	/** Set the primary target for all NPCs. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void SetPrimaryTarget(AActor* Target);

	// --- Token API ---

	/** Request a typed attack token. Returns true if token acquired or proximity override active. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Tokens")
	bool RequestAttackToken(APawn* Requester, EAttackTokenType TokenType);

	/** Release a held attack token. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Tokens")
	void ReleaseAttackToken(APawn* Attacker);

	/** Check if NPC has a token or proximity override. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Tokens")
	bool HasAttackToken(APawn* NPC) const;

	// --- Battle Circle API ---

	/** How dangerous this player is, all in: the class's standing BaseThreat plus whatever the
	 *  situational UThreatComponent is still carrying. The single number both target selection and
	 *  cover choice are weighted by - see design doc 5.3 for why those must not drift apart. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Threat")
	float GetPlayerThreat(APawn* Player) const;

	// --- Cover claims ---
	//
	// An occupied corner blocks a radius around itself, so two enemies do not end up behind the same
	// wall from opposite sides interfering with each other's peeks (design doc 5.5). It lives here
	// rather than in the cover component because this is already the object that knows about every
	// registered NPC at once.
	//
	// The pleasant side effect is that the second NPC to look gets the second-best spot, which is by
	// definition more open to the dangerous players - so enemy positions spread out on their own,
	// and it is visible.

	/** Take the corner at Location for this NPC. Replaces any claim it already held. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Cover")
	void ClaimCover(AActor* NPC, const FVector& Location);

	/** Give it back. Must be called on every exit from cover, including death and pool recycling:
	 *  leaked claims accumulate as phantom occupied corners and squeeze the NPCs into the open over
	 *  the course of a fight, with nothing about the symptom pointing at the cause. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Cover")
	void ReleaseCover(AActor* NPC);

	/** Is this spot inside somebody else's claim? Asker is excluded so an NPC is never blocked by
	 *  its own corner when re-covering nearby. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Cover")
	bool IsCoverBlocked(const FVector& Location, const AActor* Asker) const;

	/** How much room a claimed corner reserves. Must be comfortably larger than the cover
	 *  component's PeekStepDistance, or two NPCs end up on opposite sides of one corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Cover", meta = (ClampMin = "0.0"))
	float CoverBlockRadius = 500.0f;

	/** Get the assigned slot position for an NPC. Returns false if no slot assigned. */
	UFUNCTION(BlueprintPure, Category = "Coordination|BattleCircle")
	bool GetAssignedSlotPosition(APawn* NPC, FVector& OutPosition) const;

	/** Get the ring assignment for an NPC. */
	UFUNCTION(BlueprintPure, Category = "Coordination|BattleCircle")
	EBattleRing GetNPCRing(APawn* NPC) const;

	// --- Strafe Coordination API ---

	/** Request a strafe slot for a drone. Fills OutCenter and OutAxis for lateral oscillation.
	 *  OrbitDistance determines how far from player to sample. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Strafe")
	void RequestStrafeSlot(APawn* Drone, float OrbitDistance, FVector& OutCenter, FVector& OutAxis);

	/** Release a strafe slot when switching back to orbit or dying. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Strafe")
	void ReleaseStrafeSlot(APawn* Drone);

	// --- Enemy Cluster API ---

	/** Get direction from player toward highest non-kamikaze NPC density (XY only, normalized).
	 *  Returns ZeroVector if no NPCs or no clear cluster. Cached, updated every ~0.5s. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Cluster")
	FVector GetEnemyClusterDirection() const { return CachedClusterDirection; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	/** Registered NPCs */
	UPROPERTY()
	TArray<FRegisteredNPCData> RegisteredNPCs;

	/** Primary target (player) */
	TWeakObjectPtr<AActor> PrimaryTarget;

	/** Time since last attack permission was granted */
	float TimeSinceLastAttackGrant = 0.0f;

	/** Singleton instance */
	static TWeakObjectPtr<AAICombatCoordinator> Instance;

	/** One claimed corner. Weak on the owner for the same reason FStrafeSlot is: an NPC can die,
	 *  despawn or be recycled without anybody remembering to release, and a stale claim is a corner
	 *  nobody can ever use again. */
	struct FCoverClaim
	{
		TWeakObjectPtr<const AActor> Owner;
		FVector Location = FVector::ZeroVector;
	};

	TArray<FCoverClaim> CoverClaims;

	// --- Core helpers ---
	FRegisteredNPCData* FindNPCData(APawn* NPC);
	const FRegisteredNPCData* FindNPCData(APawn* NPC) const;
	void UpdateAttackScores();
	float CalculateAttackScore(const FRegisteredNPCData& Data) const;
	bool HasLineOfSightToTarget(APawn* NPC) const;
	void CleanupInvalidNPCs();
	void UpdatePermissionTimeouts(float DeltaTime);
	int32 CountCurrentAttackers() const;
	bool IsNPCInEngagementRange(APawn* NPC) const;
	float GetDistanceToTarget(APawn* NPC) const;

	// --- Token system ---
	/** One per player anybody is fighting. Persistent: see FTargetGroup. */
	TArray<FTargetGroup> Groups;

	/** Kamikaze stays a single global pool on purpose. Its size follows the number of live drones,
	 *  not the number of players, so splitting it per group would hand a four-player team four times
	 *  the divers — linear growth, which is exactly what the sublinear ceiling exists to avoid. */
	FTokenPool KamikazeTokenPool;

	/** Time of last kamikaze token grant (for stagger enforcement) */
	float LastKamikazeTokenGrantTime = -100.0f;

	FTokenPool& GetPoolForType(EAttackTokenType Type);
	const FTokenPool& GetPoolForType(EAttackTokenType Type) const;
	EAttackTokenType DetermineTokenType(APawn* NPC) const;
	bool TryStealToken(APawn* Requester, FTokenPool& Pool);
	void UpdateProximityOverrides();
	void UpdateTokenPools();

	/** Count alive (non-dead) kamikaze drones among registered NPCs */
	int32 CountAliveKamikazeDrones() const;
	/** Update kamikaze token pool size based on alive count */
	void UpdateKamikazeTokenPoolSize();

	// --- Strafe Coordination ---
	TArray<FStrafeSlot> StrafeSlots;

	// --- Battle Circle ---
	// Battle slots, their recalc clocks and the player state cache all moved into FTargetGroup.

	/** Clock for the log snapshot. */
	float TimeSinceLastSnapshot = 0.0f;

	void GenerateBattleSlots();
	void RecalculateSlotPositions();
	void AssignNPCsToSlots();
	EBattleRing GetPreferredRing(const FRegisteredNPCData& Data) const;
	float GetRingMidRadius(EBattleRing Ring) const;

	// --- Role & Pressure ---
	// CachedPlayerState moved into FTargetGroup::State.

	/** Give every registered NPC a target, and let it keep the one it has unless somebody genuinely
	 *  takes over. Also derives PrimaryTarget from the result. */
	void UpdateNPCTargets(float DeltaTime);

	// --- Decoys ---

	/** Live decoys. Handful at most, walked once per NPC per tick at 10Hz. */
	TArray<FActiveDecoy> ActiveDecoys;

	/** Drop the expired and the destroyed. Called at the top of UpdateNPCTargets so a decoy that ran
	 *  out is gone before anybody is targeted at it. */
	void PruneDecoys();

	/** The nearest live decoy whose radius covers NPCLocation, or null. Nearest rather than first, so
	 *  two decoys thrown into the same room do not depend on registration order. */
	AActor* FindDecoyFor(const FVector& NPCLocation) const;

	/** Point this NPC's CONTROLLER at the decoy and lock it there for what remains of the decoy's
	 *  life. The coordinator's own Data.Target is not what the behaviour tree reads. */
	void ApplyDistraction(APawn* NPC, AActor* Decoy, float SecondsRemaining);

	/** Release the controller lock, if this NPC has one. */
	void ClearDistraction(APawn* NPC);

	/** The player this NPC is fighting, falling back to PrimaryTarget when it has none yet. Every
	 *  per-NPC gate goes through here rather than reading PrimaryTarget directly. */
	/** Distance to Player as an enemy weighs it: real distance divided by that player's current
	 *  threat, so somebody loud looks nearer than they are. Plain distance when there is no
	 *  UThreatComponent, which is what this did before threat existed. */
	float GetApparentDistance(const FVector& FromLocation, APawn* Player) const;

	/** A distinct colour per group, so two formations can be told apart on screen. */
	static FColor GetGroupDebugColor(int32 GroupIndex);

	/** Write the whole coordinator state to the log every so often.
	 *
	 *  On-screen shapes only help somebody who is looking at the right screen at the right moment,
	 *  and they cannot be read afterwards. This is the same picture in a form that survives the
	 *  session and can be diffed between the host's log and the client's. */
	void LogStateSnapshot();

	AActor* ResolveTargetFor(APawn* NPC) const;

	/** Bring Groups in line with the targets the NPCs currently hold: create the missing ones, retire
	 *  the ones nobody is fighting any more, and refill the membership lists. */
	void RebuildTargetGroups();

	FTargetGroup* FindGroupFor(APawn* NPC);
	const FTargetGroup* FindGroupFor(APawn* NPC) const;

	/** The pool this NPC draws from: its own group's, except for kamikaze, which is global.
	 *  Null when the NPC has no group yet. */
	FTokenPool* GetPoolFor(APawn* NPC, EAttackTokenType Type);

	/** Slots and state for one group. */
	void GenerateBattleSlotsForGroup(FTargetGroup& Group);
	void RecalculateSlotPositionsForGroup(FTargetGroup& Group);
	void AssignNPCsToSlotsForGroup(FTargetGroup& Group);
	void UpdatePlayerStateCacheForGroup(FTargetGroup& Group);

	void UpdatePlayerStateCache();
	void AssignRoles();
	float CalculateAngleFromPlayerFacing(APawn* NPC) const;

	// --- Enemy Cluster ---
	FVector CachedClusterDirection = FVector::ZeroVector;
	float TimeSinceLastClusterCalc = 0.0f;
	void UpdateEnemyClusterDirection();

	// --- Debug ---
	void DrawDebugInfo();
	void DrawBattleCircleDebug();
	void DrawRoleDebug();
};
