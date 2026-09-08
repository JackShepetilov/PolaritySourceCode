// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class ACharacter;
class UPrimitiveComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class AShooterWeapon;

/**
 * Whether this round ionizes, and whose number decides it.
 *
 * Three states rather than a signed float, because ionization is SIGNED: a weapon whose charge per
 * hit is negative electrifies the other way, so "does not ionize" cannot be spelled as zero the way
 * HitDamage spells "defer" as a negative.
 */
UENUM(BlueprintType)
enum class EProjectileIonization : uint8
{
	/** Whatever the gun that fired this round does. The ordinary answer for an ordinary bullet. */
	FromWeapon UMETA(DisplayName = "From Weapon"),

	/** This round never charges anything, whatever the gun is set to. A payload that is purely
	 *  kinetic in an otherwise electrifying weapon. */
	Never UMETA(DisplayName = "Never"),

	/** This round carries its own amount, and the gun's is ignored. */
	Override UMETA(DisplayName = "Own Amount"),
};

/**
 *  Simple projectile class for a first person shooter game
 *
 *  THE SPLIT between this and AShooterWeapon (the long version is on the weapon, above its Damage
 *  section):
 *
 *  - The WEAPON owns what a SHOT is worth: damage, headshot, damage type, ionization, tag
 *    multipliers, the shield gate. Same numbers whether a trace, a bolt or a round carried the shot.
 *  - THIS class owns what the ROUND is: flight (speed, gravity, bounce, homing -- all of it on the
 *    ProjectileMovement component, and read straight off this CDO by the weapon's ballistic solver),
 *    what impact means (explosion, radius, falloff, rocket jump, physics force, noise), and lifetime.
 *  - The "Weapon Overrides" section below is where a SPECIAL round overrules the gun. Every field
 *    there defaults to deferring, so an ordinary bullet configures nothing and inherits everything.
 */
UCLASS(abstract)
class POLARITY_API AShooterProjectile : public AActor
{
	GENERATED_BODY()

protected:

	/** Provides collision detection for the projectile */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USphereComponent* CollisionComponent;

	/** Handles movement for the projectile.
	 *
	 *  EVERYTHING about how this round flies is configured here and nowhere else: InitialSpeed,
	 *  MaxSpeed, ProjectileGravityScale, bounce, homing. The weapon deliberately keeps no copy --
	 *  AShooterWeapon::SolveBallisticAim reads speed and gravity straight off this CDO to decide
	 *  whether a shot needs an arc, so a second set of numbers on the gun would be a second set to
	 *  get wrong. Want a slower shell that lobs? Change it on the round, and the AI's solver follows
	 *  with no configuration anywhere saying it should. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UProjectileMovementComponent* ProjectileMovement;

	/** Loudness of the AI perception noise done by this projectile on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Noise", meta = (ClampMin = 0, ClampMax = 100))
	float NoiseLoudness = 3.0f;

	/** Range of the AI perception noise done by this projectile on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Noise", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float NoiseRange = 3000.0f;

	/** Tag of the AI perception noise done by this projectile on hit */
	UPROPERTY(EditAnywhere, Category="Noise")
	FName NoiseTag = FName("Projectile");

	/** Physics force to apply on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Hit", meta = (ClampMin = 0, ClampMax = 50000))
	float PhysicsForce = 100.0f;

	/** Launch velocity applied to characters caught by the EXPLOSION (cm/s). CharacterMovement ignores
	 *  physics impulses, so characters are launched instead of pushed. Set to 0 to disable.
	 *
	 *  Deliberately not used by a direct hit any more. A direct hit goes through the weapon's shared
	 *  funnel, which drops small launches on a grounded target: LaunchCharacter always forces
	 *  MOVE_Falling, so every bullet used to make the target hop instead of playing its flinch.
	 *  The rocket keeps this number because the pop off an explosion is the point of it. */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion", meta = (ClampMin = 0, ClampMax = 10000, Units = "cm/s"))
	float CharacterKnockbackForce = 1200.0f;

	/** Upward bias added to the knockback direction (0 = purely radial, 1 = strong upward kick). Gives the TF2-style "pop" so explosions at the feet launch up, not sideways. */
	UPROPERTY(EditAnywhere, Category="Projectile|Hit", meta = (ClampMin = 0, ClampMax = 1))
	float KnockbackUpwardBias = 0.4f;

	/**
	 * Damage on a direct hit, as an OVERRIDE of the weapon's number.
	 *
	 * Negative (the default) means "whatever the gun that fired me does", which is what an ordinary
	 * round should say: the weapon owns the balance number, exactly as it does when it traces, and
	 * one gun has one figure to tune instead of one per payload.
	 *
	 * Zero or above is a payload that insists on its own damage, and a special one should insist:
	 * that is how a rocket stays a rocket after Upgrade_RocketProjectileSwap loads a different round
	 * into the same launcher. The weapon reads this back through GetShotDamage, so the HUD's damage
	 * readout still tells the truth about a gun firing an overriding payload.
	 *
	 * A projectile with no weapon behind it (a trap, an ability) has nothing to inherit from and
	 * must set its own number.
	 */
	UPROPERTY(EditAnywhere, Category="Projectile|Weapon Overrides", meta = (ClampMin = -1, ClampMax = 100))
	float HitDamage = -1.0f;

	/**
	 * Headshot multiplier for this round, as an OVERRIDE of the weapon's number.
	 *
	 * Negative (the default) defers to the gun, which is what a bullet wants: one figure per weapon.
	 * Zero or above is a payload that decides for itself, and an explosive one should decide 1.0 --
	 * a rocket that went off against somebody's skull did not go off any harder than one that went
	 * off against their chest, and inheriting the rifle's x2 makes a launcher a sniper by accident.
	 *
	 * Read through AShooterWeapon::GetShotHeadshotMultiplier, so it applies on the one path that
	 * resolves the bone (ApplyWeaponHit) and nowhere else.
	 */
	UPROPERTY(EditAnywhere, Category="Projectile|Weapon Overrides", meta = (ClampMin = -1, ClampMax = 10))
	float HeadshotMultiplierOverride = -1.0f;

	/**
	 * Whether this round charges what it hits, and whose number says by how much.
	 *
	 * Defaults to the gun's answer. Set it to Never for a payload that must stay purely kinetic in a
	 * weapon that otherwise electrifies, or to Own Amount when the round is the thing carrying the
	 * charge -- which is what makes an ionizing payload possible in a launcher whose other rounds
	 * are not.
	 */
	UPROPERTY(EditAnywhere, Category="Projectile|Weapon Overrides")
	EProjectileIonization IonizationOverride = EProjectileIonization::FromWeapon;

	/** Charge this round puts into its target per hit, when IonizationOverride is Own Amount.
	 *  Signed, exactly as the weapon's is: negative electrifies the other way. */
	UPROPERTY(EditAnywhere, Category="Projectile|Weapon Overrides",
		meta = (EditCondition = "IonizationOverride == EProjectileIonization::Override", EditConditionHides))
	float IonizationChargePerHit = 2.0f;

	/** Type of damage to apply. Can be used to represent specific types of damage such as fire,
	 *  explosion, etc. Overrides the weapon's damage type: fire and blast belong to the payload
	 *  rather than to the barrel it left. */
	UPROPERTY(EditAnywhere, Category="Projectile|Weapon Overrides")
	TSubclassOf<UDamageType> HitDamageType;

	/** If true, the projectile can damage the character that shot it. Independent of the weapon's
	 *  own owner-damage flag on purpose: a rocket that is supposed to hurt whoever fired it must not
	 *  be silenced by a checkbox that was written for the trace path. */
	UPROPERTY(EditAnywhere, Category="Projectile|Weapon Overrides")
	bool bDamageOwner = false;

	/** Damage multipliers based on target actor tags. Multiple matching tags multiply together.
	 *  Used ONLY when this round has no weapon behind it (a trap, an ability). With a gun present
	 *  the gun's map is the one that counts, because applying both would square them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projectile|Weapon Overrides")
	TMap<FName, float> TagDamageMultipliers;

	/** If true, the projectile will explode and apply radial damage to all actors in range */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion")
	bool bExplodeOnHit = false;

	/** Max distance for actors to be affected by explosion damage */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion", meta = (ClampMin = 0, ClampMax = 5000, Units = "cm"))
	float ExplosionRadius = 500.0f;	

	/** Damage multiplier at the outer edge of the explosion. TF2-like splash uses about 0.5 at the edge. */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExplosionEdgeDamageMultiplier = 0.5f;

	/** Falloff curve exponent from center to edge. 1 = linear, >1 keeps damage higher near the center. */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ExplosionFalloffExponent = 1.0f;

	/** If true, the actor directly hit by an explosive projectile receives full splash damage. */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion")
	bool bDirectHitIgnoresSplashFalloff = true;

	/** If true, world geometry blocks explosion damage and knockback. */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion")
	bool bRequireExplosionLineOfSight = true;

	/** If true, the owner can be launched by their own explosion even when self-damage is disabled. */
	UPROPERTY(EditAnywhere, Category="Projectile|Rocket Jump")
	bool bEnableOwnerRocketJump = true;

	/** Owner knockback multiplier when the owner is still grounded at explosion time. */
	UPROPERTY(EditAnywhere, Category="Projectile|Rocket Jump", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float OwnerGroundKnockbackMultiplier = 0.45f;

	/** Owner knockback multiplier while airborne without crouch held. */
	UPROPERTY(EditAnywhere, Category="Projectile|Rocket Jump", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float OwnerAirKnockbackMultiplier = 1.0f;

	/** Owner knockback multiplier while airborne with crouch held. */
	UPROPERTY(EditAnywhere, Category="Projectile|Rocket Jump", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float OwnerAirCrouchKnockbackMultiplier = 1.5f;

	/** Self-damage multiplier for the owner. Only used when bDamageOwner is enabled. */
	UPROPERTY(EditAnywhere, Category="Projectile|Rocket Jump", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float OwnerSelfDamageMultiplier = 0.6f;

	/** If true, this projectile has already hit another surface */
	bool bHit = false;

	/**
	 * The weapon that fired this round.
	 *
	 * Without it a projectile knows nothing about the shot it IS: not the feedback set, not the
	 * headshot multiplier, not whether the target's shield gates damage, not that the owner has an
	 * upgrade waiting on the hit. That is the whole reason a projectile weapon used to punch through
	 * shields in silence while the same weapon firing traces did none of those things wrong.
	 *
	 * Replicated so a watching client's copy can answer for cosmetics too. Null is legal and means
	 * "no weapon behind this one" (a trap, an ability, a projectile spawned by design with no gun),
	 * and the old self-contained damage path is what runs then.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Projectile")
	TObjectPtr<AShooterWeapon> SourceWeapon = nullptr;

	/** The hit result NotifyHit came in with, kept for the length of the ProcessHit call below it.
	 *  ProcessHit takes a loose actor/point/direction (five subclasses override that signature), but
	 *  the funnel needs the whole result: the bone for headshots and the physical material for the
	 *  impact's surface. Valid only during that call, which is the only place that reads it. */
	FHitResult DirectHit;

	/** How long to wait after a hit before destroying this projectile */
	UPROPERTY(EditAnywhere, Category="Projectile|Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float DeferredDestructionTime = 5.0f;

	/**
	 * How far this round may fly before it gives up and goes away. Zero means no limit.
	 *
	 * Until this existed a round that hit NOTHING was never cleaned up at all: the destruction timer
	 * is armed by a hit, so a shot into the sky flew until the world bounds caught it, ticking and
	 * (when it still replicated) costing bandwidth the whole way. With hundreds of bullets alive
	 * that is the leak that matters.
	 *
	 * Expressed as a DISTANCE because that is the thing a designer actually knows about a weapon:
	 * how far it is meant to reach. It is turned into one timer at launch (distance / speed) rather
	 * than checked per frame, which is the whole point -- a per-frame distance test would hand back
	 * the actor tick this class just stopped paying for.
	 */
	UPROPERTY(EditAnywhere, Category="Projectile|Destruction", meta = (ClampMin = 0, Units = "cm"))
	float MaxTravelDistance = 50000.0f;   // 500 m. Unreal counts in centimetres.

	/** Ceiling on the flight timeout when the speed is unknown or absurdly low, so nothing can live
	 *  forever through a misconfigured projectile. */
	UPROPERTY(EditAnywhere, Category="Projectile|Destruction", meta = (ClampMin = "0.1", ClampMax = "60.0", Units = "s"))
	float MaxFlightSeconds = 10.0f;

	/** Timer for BOTH ends of this projectile's life: the flight timeout armed at launch, and the
	 *  deferred destruction armed by a hit. One handle on purpose -- a hit re-arms it, which cancels
	 *  the flight timeout in the same call rather than leaving two timers racing each other. */
	FTimerHandle DestructionTimer;

	// ==================== Pooling ====================

	/** Default number of projectiles to prewarm in pool */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Pooling", meta = (ClampMin = "1", ClampMax = "200"))
	int32 DefaultPoolSize = 20;

	/** True if this projectile is managed by the pool system */
	bool bIsPooled = false;

	/**
	 * True while this one is PARKED in the pool rather than flying.
	 *
	 * Guards the one failure a pool cannot survive: the same actor added to the free list twice.
	 * The pool would then hand it out for two shots at once, and those two shots would share one
	 * collision component, one bHit flag and one timer -- so a hit would register for whichever of
	 * them got there first and vanish for the other. Intermittent, unreproducible, and it looks
	 * exactly like a broken hitbox, which is why it is worth a bool rather than an argument about
	 * whether it can happen.
	 */
	bool bIsInPool = false;

	/** A shooting client spawns one of these the instant it pulls the trigger so the shot leaves the
	 *  barrel with no round trip, and asks the server for the real one at the same time. This copy
	 *  exists to be looked at: it hurts nothing and decides nothing, and the authoritative projectile
	 *  the server replicates is the one that lands the hit. */
	bool bIsCosmeticOnly = false;

	// ==================== VFX|Trail ====================

	/** Niagara system for projectile trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|VFX")
	TObjectPtr<UNiagaraSystem> TrailFX;

	/** Active trail component (spawned on BeginPlay) */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> TrailComponent;

public:

	/** Constructor */
	AShooterProjectile();

	// ==================== Pooling Interface ====================

	/** Get default pool size for this projectile class */
	int32 GetDefaultPoolSize() const { return DefaultPoolSize; }

	/** Set pooled flag before BeginPlay (called by pool subsystem during deferred spawn) */
	void SetPooledFlag() { bIsPooled = true; }

	/**
	 * Mark this one as decoration and strip it of everything that could affect the game.
	 *
	 * Call it before the projectile can hit anything. Beyond the flag it also takes the round out of
	 * the way of PAWNS: a decoration that answers Block on the pawn channel is a solid object in the
	 * path of every character on that machine, so a teammate could be shoved or stopped by a bullet
	 * that does not exist anywhere else. World geometry still blocks it, so it stops at a wall where
	 * it should; a body it flies through, and the impact the authority multicasts is what the player
	 * actually sees land.
	 */
	void SetCosmeticOnly();

	/** Tell this round which gun it came out of. Set at spawn, before it can hit anything. */
	void SetSourceWeapon(AShooterWeapon* InWeapon) { SourceWeapon = InWeapon; }

	/** Stop colliding with another round. Called on every pair of pellets a shotgun puts in the air,
	 *  because they all leave the same muzzle point and would otherwise block each other on the
	 *  frame they spawn. @see AShooterWeapon_Shotgun::FireProjectile */
	void IgnoreProjectileWhileFlying(AShooterProjectile* Other);

	/** The weapon that fired this round, or null for a projectile with no gun behind it. */
	UFUNCTION(BlueprintPure, Category="Projectile")
	AShooterWeapon* GetSourceWeapon() const { return SourceWeapon; }

	/** This payload's damage override, negative when it defers to the weapon. Read by the weapon
	 *  itself (GetShotDamage) so the damage readout can answer for a gun it has never fired. */
	float GetDirectHitDamageOverride() const { return HitDamage; }

	/** This payload's headshot override, negative when it defers to the weapon.
	 *  @see AShooterWeapon::GetShotHeadshotMultiplier */
	float GetHeadshotMultiplierOverride() const { return HeadshotMultiplierOverride; }

	/** Whether this payload ionizes, and whose number decides.
	 *  @see AShooterWeapon::GetShotIonization */
	EProjectileIonization GetIonizationOverride() const { return IonizationOverride; }

	/** This payload's own charge per hit. Meaningful only when GetIonizationOverride() is Override. */
	float GetIonizationChargeOverride() const { return IonizationChargePerHit; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * True when this projectile may change the world: the authority's copy, and never a decoration.
	 * Everything cosmetic ignores it; everything that damages, explodes or pushes has to ask.
	 *
	 * This used to read HasAuthority() && !bIsCosmeticOnly, and that stopped being safe the moment
	 * ordinary rounds stopped replicating. HasAuthority() means "this machine owns this actor", and
	 * an actor a CLIENT spawned for itself is owned by that client: a non-replicated projectile
	 * answers TRUE to HasAuthority() on every machine it exists on. So the old test would have left
	 * a single missed SetCosmeticOnly() call able to deal damage on a player's own computer.
	 *
	 * The net mode is asked instead, because it is a property of the MACHINE rather than of the
	 * actor and no spawn path can forget to set it: a client is never the authority for the game,
	 * whatever it happens to own. bIsCosmeticOnly then rules out the host's own decorations.
	 */
	bool CanAffectWorld() const;

	/** Called by pool to activate projectile for use */
	void ActivateFromPool(const FTransform& SpawnTransform, AActor* NewOwner, APawn* NewInstigator);

	/** Called by pool to deactivate projectile for reuse */
	void DeactivateToPool();

	/**
	 * Start the clock that takes this round out of the world if it never hits anything.
	 *
	 * Reads the speed it is ACTUALLY flying at, not the class default, and must therefore be called
	 * after whatever set that speed. Launch paths that override it (AShieldBypassProjectile::LaunchAt)
	 * call this again; the timer handle is shared, so re-arming replaces rather than stacks.
	 */
	void ArmFlightTimeout();

protected:
	
	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/**
	 * A decoration touching a body.
	 *
	 *  Overlap rather than block, and that distinction is the whole design. A BLOCKING sphere is a
	 *  real obstacle to character movement on the machine that owns it, so a bullet that exists
	 *  nowhere else could shove or stop a live teammate. An overlapping one obstructs nobody and
	 *  still learns exactly where it touched, which is all an impact needs.
	 *
	 *  Only decorations get here: the authority's round keeps Block and lands through NotifyHit.
	 */
	UFUNCTION()
	void OnCosmeticOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * The impact this round makes, on THIS machine.
	 *
	 * Who plays what depends on whether every machine has its own copy of the round:
	 *
	 *   non-replicated (the ordinary bullet) -- everyone simulates one and everyone plays its own
	 *     landing. Zero RPCs per bullet, and the blood cannot be lost to a dropped unreliable
	 *     multicast, which matters most exactly when it is busiest.
	 *   replicated (AEMFProjectile) -- only the authority has one, so it plays locally and
	 *     multicasts, exactly as the trace path does.
	 */
	void PlayImpactFeedback(const FHitResult& Hit);

	/**
	 * Fill in the physical material the hit came back without, so brick reads as brick.
	 *
	 * A projectile's hit is produced by the movement component's own sweep, and a component sweep
	 * sets bReturnPhysicalMaterial to false: Hit.PhysMaterial is ALWAYS null on this path. Every
	 * surface therefore resolved to SurfaceType_Default, and every impact played the default effect
	 * no matter what was struck -- half of the reason a projectile weapon knocked no chips off a
	 * wall. (The other half was that the impact fields were greyed out on the weapon.)
	 *
	 * Asked of the body directly rather than re-traced: the material is already on the component
	 * that was hit, and one pointer chase per impact is cheaper than a second query.
	 */
	void EnsureHitPhysicalMaterial(FHitResult& Hit) const;

	/** Handles collision */
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

protected:

	/** Looks up actors within the explosion radius and damages/launches them */
	void ExplosionCheck(const FVector& ExplosionCenter, AActor* DirectHitActor);

	/** Processes a projectile hit for the given actor */
	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection);

	/**
	 * The one door a DIRECT projectile hit goes through, and the reason subclasses no longer call
	 * ApplyDamage by hand.
	 *
	 * With a SourceWeapon it hands the hit to AShooterWeapon::ApplyWeaponHit, which is the same
	 * funnel a trace of that weapon lands in: shield gate, class passive, headshot, upgrades,
	 * ionization, hit marker, knockback under the grounded rule. Without one it falls back to plain
	 * damage plus a physics impulse, which is all a gunless projectile ever meant.
	 *
	 * FinalDamage is the caller's number BEFORE the weapon's own multipliers (the base HitDamage,
	 * a charge-scaled one, whatever the subclass worked out).
	 */
	void ApplyDirectHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation,
		const FVector& HitDirection, float FinalDamage);

	/** The damage this round starts from: its own override, or the weapon's number when it defers.
	 *  Zero with a loud log if it defers to a weapon that has no number configured, because a gun
	 *  that silently fires blanks is the one failure nobody finds by looking at it. */
	float ResolveDirectHitDamage() const;

	/** Processes explosion damage and knockback for one actor */
	void ProcessExplosionHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& ExplosionCenter, AActor* DirectHitActor);

	/** Calculates TF2-style splash scale for an actor at the given distance */
	float CalculateExplosionSplashScale(float Distance, const AActor* HitActor, const AActor* DirectHitActor) const;

	/** Calculates owner-specific rocket jump multiplier from ground/air/crouch state */
	float GetOwnerRocketJumpMultiplier(const ACharacter* HitCharacter) const;

	/** Additional damage multiplier for projectile subclasses. Base projectile has no extra scaling. */
	virtual float GetProjectileDamageMultiplier(AActor* Target) const;

	/** Calculate damage multiplier based on target's tags */
	float GetTagDamageMultiplier(AActor* Target) const;

	/** Passes control to Blueprint to implement any effects on hit. */
	UFUNCTION(BlueprintImplementableEvent, Category="Projectile", meta = (DisplayName = "On Projectile Hit"))
	void BP_OnProjectileHit(const FHitResult& Hit);

	/** Called from the destruction timer to destroy this projectile */
	void OnDeferredDestruction();

	/**
	 * Make the shooter and this round transparent to each other, or stop doing so.
	 *
	 * Two lists, not one, and the second is the one that used to leak. Telling the projectile to
	 * ignore the shooter costs nothing when it dies -- the list dies with it. Telling the SHOOTER to
	 * ignore the projectile writes into the character's own MoveIgnoreActors, which outlives the
	 * bullet: every round anybody fired stayed in that array for the rest of the match, and the
	 * array is walked on every single character move. Hundreds of bullets turn it into a per-move
	 * scan over hundreds of dead pointers.
	 *
	 * So it is removed again the moment the round stops flying, in both endings (pooled and
	 * destroyed).
	 */
	void SetShooterMoveIgnore(bool bIgnore);

	/** Reset projectile state for pool reuse. Override in subclasses for custom state. */
	virtual void ResetProjectileState();

	/** Return this projectile to pool (or destroy if not pooled) */
	void ReturnToPoolOrDestroy();
};
