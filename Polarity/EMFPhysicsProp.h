// EMFPhysicsProp.h
// Physics-simulated prop that integrates with the EMF system
// Receives/gives charge, affected by EM forces, can be captured by channeling, deals impact damage

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/ShooterDummyInterface.h"
#include "EMF_PluginBPLibrary.h"
#include "EMFPhysicsProp.generated.h"

class UEMF_FieldComponent;
class AEMFChannelingPlateActor;
class AShooterNPC;
class AShooterCharacter;
class UNiagaraSystem;
class USoundBase;
class UMaterialInterface;
class UAnimMontage;
class UCurveFloat;
class UGeometryCollection;
class AGeometryCollectionActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPropDeath, AEMFPhysicsProp*, Prop, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropDamaged, AEMFPhysicsProp*, Prop, float, Damage, AActor*, DamageCauser);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPropChargeChanged, float, NewCharge, uint8, NewPolarity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropExploded, AEMFPhysicsProp*, Prop, FVector, Location, float, DamageMultiplier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnNPCStunnedByExplosion, AShooterNPC*, StunnedNPC, AEMFPhysicsProp*, ExplodedProp, float, StunDuration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropCriticalVelocityImpact, AEMFPhysicsProp*, Prop, FVector, Location, float, Speed);

/**
 * Physics-simulated prop with full EMF system integration.
 *
 * Features:
 * - Receives charge from melee hits and laser ionization
 * - Affected by electromagnetic forces (like enemies and projectiles)
 * - Can be captured by player's channeling plate
 * - Deals kinetic and EMF damage to NPCs on impact
 * - Compatible with future destructibility (SceneComponent root)
 */
UCLASS(Blueprintable)
class POLARITY_API AEMFPhysicsProp : public AActor, public IShooterDummyTarget
{
	GENERATED_BODY()

public:
	AEMFPhysicsProp();

	// ==================== Components ====================

	/** Physics mesh — root component (simulates physics, generates hit events) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	/** EMF field component (charge storage + registry) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Geometry Collection Destruction ====================

	/** Optional Geometry Collection for prop destruction.
	 *  If assigned: GC actor spawns at PropMesh transform on death and shatters.
	 *  If not assigned: current static mesh behavior (no destruction visual).
	 *  Auto-assigned when PropMesh changes in editor (searches for GC_{MeshName} in same folder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction")
	TObjectPtr<UGeometryCollection> PropGeometryCollection;

	/** Fallback GC used when no matching GC_{MeshName} asset is found.
	 *  Set this in BP_EMFProp defaults to your generic cube GC. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Destruction")
	TObjectPtr<UGeometryCollection> FallbackGeometryCollection;

	/** How long gibs simulate physics before freezing in place (seconds).
	 *  After this time, physics and collision are disabled — gibs become static visuals with near-zero cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float GibPhysicsLifetime = 3.0f;

	/** How long frozen gibs remain visible before being destroyed (seconds).
	 *  0 = persist forever (no cleanup). Total gib lifespan = GibPhysicsLifetime + GibVisualLifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0.0"))
	float GibVisualLifetime = 0.0f;

	/** Radial velocity for scattering gibs outward on death (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0"))
	float DestructionImpulse = 800.0f;

	/** Angular velocity for tumbling gibs on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction", meta = (ClampMin = "0"))
	float DestructionAngularImpulse = 100.0f;

	/** Collision profile for GC gibs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction")
	FName GibCollisionProfile = FName("Ragdoll");

	// ==================== EMF Settings ====================

	/** Default charge (0 = starts uncharged) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float DefaultCharge = 0.0f;

	/** Default mass (affects EMF force response and physics weight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float DefaultMass = 10.0f;

	/** If true, prop velocity is affected by external electromagnetic fields */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics")
	bool bAffectedByExternalFields = true;

	/** Maximum EM force that can be applied (prevents extreme accelerations) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics")
	float MaxEMForce = 100000.0f;

	/** Maximum distance to consider EMF sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxSourceDistance = 10000.0f;

	// ==================== EMF Surface Friction ====================

	/** Apply Coulomb friction model against EMF forces when prop rests on a surface.
	 *  Only affects EMF-driven sliding — normal physics friction is unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Surface Friction")
	bool bApplyEMFSurfaceFriction = true;

	/** Friction coefficient for EMF forces against surfaces (μ). Higher = harder to drag.
	 *  Force needed to slide = μ * Mass * Gravity. At μ=1.5, 10 kg prop needs ~14700 N horizontal EMF force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Surface Friction", meta = (ClampMin = "0.0", EditCondition = "bApplyEMFSurfaceFriction"))
	float EMFSurfaceFriction = 1.5f;

	/** Downward trace distance to detect ground surface (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Surface Friction", meta = (ClampMin = "1.0", ClampMax = "50.0", Units = "cm", EditCondition = "bApplyEMFSurfaceFriction"))
	float EMFGroundTraceDistance = 10.0f;

	// ==================== Force Filtering ====================

	/** Off by default: a charged player pulling on a charged prop fought the hand carrying it and
	 *  kept a thrown one flying under its own power. Muted from both ends — the other half is
	 *  UEMFVelocityModifier::PhysicsPropForceMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PlayerForceMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float NPCForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float ProjectileForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float EnvironmentForceMultiplier = 1.0f;

	/** Default OFF to prevent prop-prop EMF chaos */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PhysicsPropForceMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float UnknownForceMultiplier = 1.0f;

	// ==================== Launched Force Filtering ====================
	// Second set of multipliers, active when prop is in reverse-capture flight (bIsInReverseFlight)

	/** Muted for the same reason as PlayerForceMultiplier above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedPlayerForceMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedNPCForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedProjectileForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedEnvironmentForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedPhysicsPropForceMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedUnknownForceMultiplier = 1.0f;

	/** Skip opposite-charge sources closer than OppositeChargeMinDistance to prevent Coulomb 1/r² singularity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	bool bEnableOppositeChargeDistanceCutoff = true;

	/** Minimum distance (cm) for opposite-charge force cutoff */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering", meta = (ClampMin = "1.0", Units = "cm", EditCondition = "bEnableOppositeChargeDistanceCutoff"))
	float OppositeChargeMinDistance = 35.0f;

	/** Viscous damping coefficient when prop is within cutoff distance of opposite-charge source.
	 *  Prevents prop from passing through after EM force cutoff. Units: 1/s. Higher = faster stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering", meta = (ClampMin = "0.0", ClampMax = "50.0", EditCondition = "bEnableOppositeChargeDistanceCutoff"))
	float OppositeChargeProximityDamping = 10.0f;

	// ==================== Line-of-Sight Shielding ====================

	/** Enable line-of-sight check: sources behind walls/geometry are ignored.
	 *  Uses a single-line trace per source. Only sources that pass distance culling are checked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|LOS Shielding")
	bool bEnableLOSShielding = false;

	/** Trace channel for LOS checks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|LOS Shielding", meta = (EditCondition = "bEnableLOSShielding"))
	TEnumAsByte<ECollisionChannel> LOSTraceChannel = ECC_Visibility;

	/** Draw debug lines for LOS traces (green = visible, red = blocked) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|LOS Shielding", meta = (EditCondition = "bEnableLOSShielding"))
	bool bDrawLOSDebug = false;

	// ==================== Health ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHP = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float CurrentHP = 100.0f;

	// ==================== Static Mode ====================

	/** When true, the static PropMesh is NEVER auto-enabled for physics simulation
	 *  (SetCharge, ResetProp, and any other path that would flip bSimulatePhysics are gated).
	 *  Use for static-style subclasses (e.g. destructible buildings) where the visible mesh
	 *  should stay kinematic until explicitly hidden by a custom destruction path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Static Mode")
	bool bKeepPropMeshStatic = false;

	// ==================== Collision Damage ====================

	/** Enable kinetic/EMF damage to NPCs on impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage")
	bool bDealCollisionDamage = true;

	/** Minimum speed to deal kinetic damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage", meta = (ClampMin = "0"))
	float CollisionVelocityThreshold = 800.0f;

	/** Kinetic damage per 100 units of speed above threshold */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage", meta = (ClampMin = "0"))
	float CollisionDamagePerVelocity = 10.0f;

	/** Base EMF damage when opposite-charged prop hits NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|EMF", meta = (ClampMin = "0"))
	float EMFProximityDamage = 10.0f;

	/** Minimum time between collision damage events */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float CollisionDamageCooldown = 0.2f;

	// ==================== Collision Effects ====================

	/** Sound to play on impact with NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|Effects")
	TObjectPtr<USoundBase> ImpactSound;

	/** VFX to spawn on EMF discharge impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|Effects")
	TObjectPtr<UNiagaraSystem> EMFDischargeVFX;

	/** Scale for EMF discharge VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|Effects", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float EMFDischargeVFXScale = 1.0f;

	// ==================== Explosive Impact ====================

	/** If true, prop explodes on high-speed collision during reverse channeling flight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact")
	bool bCanExplode = false;

	/** Minimum speed (cm/s) to trigger explosion when prop is launched by the player (reverse flight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float ExplosionSpeedThreshold = 500.0f;

	/** Minimum speed (cm/s) to trigger explosion from collateral impact (prop NOT launched by player).
	 *  Should be higher than ExplosionSpeedThreshold to avoid chain explosions from physics pushes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float CollateralExplosionSpeedThreshold = 1500.0f;

	/** Speed (cm/s) at which impact is considered critically destructive.
	 *  Triggers OnCriticalVelocityImpact for arena-level destruction events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float CriticalVelocity = 2000.0f;

	/** Minimum dot product between velocity direction and center-to-impact vector
	 *  for an environment hit to count as a "center hit" and detonate.
	 *  1.0 = only perfectly head-on impacts, 0.0 = any forward-facing hit, 0.5 = default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanExplode"))
	float CenterHitDotThreshold = 0.5f;

	/** Base radial damage dealt by explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float ExplosionDamage = 50.0f;

	/** Base explosion radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bCanExplode"))
	float ExplosionRadius = 300.0f;

	/** Damage type for explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (EditCondition = "bCanExplode"))
	TSubclassOf<UDamageType> ExplosionDamageType;

	/** VFX to spawn on explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Effects", meta = (EditCondition = "bCanExplode"))
	TObjectPtr<UNiagaraSystem> ExplosionVFX;

	/** Base scale for explosion VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Effects", meta = (ClampMin = "0.1", EditCondition = "bCanExplode"))
	float ExplosionVFXScale = 1.0f;

	/** Sound to play on explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Effects", meta = (EditCondition = "bCanExplode"))
	TObjectPtr<USoundBase> ExplosionSound;

	/** Damage falloff exponent (1 = linear, 2 = quadratic). Controls how damage decreases with distance from center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact", meta = (ClampMin = "0.0", ClampMax = "5.0", EditCondition = "bCanExplode"))
	float ExplosionDamageFalloff = 1.0f;

	// ==================== Charge-Proportionate Scaling ====================

	/** If true, explosion damage/impulse/stun scale proportionally to prop charge at detonation.
	 *  Scale = clamp(|CurrentCharge| / ReferenceCharge, MinChargeScale, MaxChargeScale).
	 *  Base UPROPERTY values (ExplosionDamage, ExplosionImpulseStrength, etc.) represent values at ReferenceCharge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Charge Scaling", meta = (EditCondition = "bCanExplode"))
	bool bScaleExplosionWithCharge = true;

	/** Charge level at which base explosion values apply without scaling (scale = 1.0).
	 *  A prop with |charge| = ReferenceCharge explodes at exactly the base values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Charge Scaling", meta = (ClampMin = "1.0", EditCondition = "bCanExplode && bScaleExplosionWithCharge"))
	float ExplosionReferenceCharge = 10.0f;

	/** Minimum scale multiplier (prevents explosions from being too weak at low charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Charge Scaling", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanExplode && bScaleExplosionWithCharge"))
	float MinChargeScale = 0.2f;

	/** Maximum scale multiplier (caps explosions at high charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Charge Scaling", meta = (ClampMin = "1.0", ClampMax = "10.0", EditCondition = "bCanExplode && bScaleExplosionWithCharge"))
	float MaxChargeScale = 3.0f;

	// ==================== Weak Impact (Below Charge Threshold) ====================
	// When |charge| < ExplosionMinCharge at impact, the prop does NOT explode. Instead:
	//  - Environment hits: just no explosion (physics handles the prop normally)
	//  - NPC hits: deals reduced damage + stun, splits charge with NPC, reflects velocity to bounce off

	/** Minimum |charge| required for the prop to explode on impact.
	 *  Below this threshold, the prop bounces off NPCs (with weak damage/stun) instead of detonating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float ExplosionMinCharge = 5.0f;

	/** Damage dealt to NPC on weak impact (no explosion). Used as fallback when WeakImpactDamageByCharge is null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float WeakImpactDamage = 15.0f;

	/** Curve mapping |charge| at impact (X) to weak-impact damage (Y). If null, falls back to WeakImpactDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (EditCondition = "bCanExplode"))
	TObjectPtr<UCurveFloat> WeakImpactDamageByCharge;

	/** Stun duration on weak impact (seconds). Used as fallback when WeakImpactStunDurationByCharge is null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (ClampMin = "0.0", ClampMax = "5.0", EditCondition = "bCanExplode"))
	float WeakImpactStunDuration = 0.5f;

	/** Curve mapping |charge| at impact (X) to stun duration in seconds (Y). If null, falls back to WeakImpactStunDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (EditCondition = "bCanExplode"))
	TObjectPtr<UCurveFloat> WeakImpactStunDurationByCharge;

	/** Optional montage played on stunned NPC. If null, NPC's default knockback montage is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (EditCondition = "bCanExplode"))
	TObjectPtr<UAnimMontage> WeakImpactStunMontage;

	/** Velocity restitution after bounce (0 = stops dead, 1 = elastic, no energy loss). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanExplode"))
	float WeakImpactBounceRestitution = 0.6f;

	/** Fraction of prop's charge transferred to the NPC on weak impact (0..1).
	 *  Default 0.5 = exactly half goes to NPC, half stays on prop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Weak Impact", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanExplode"))
	float WeakImpactChargeShareRatio = 0.5f;

	// ==================== Explosion Impulse (Rocket Boost) ====================

	/** Apply physics impulse to characters and physics bodies within explosion radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Impulse", meta = (EditCondition = "bCanExplode"))
	bool bApplyExplosionImpulse = true;

	/** Base impulse strength (cm/s for characters via LaunchCharacter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Impulse", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float ExplosionImpulseStrength = 1600.0f;

	/** Upward bias for the impulse direction (0 = pure radial, 1 = entirely upward).
	 *  Higher values make rocket boosting easier — the character gets launched up even from side blasts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Impulse", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanExplode"))
	float ExplosionImpulseUpwardBias = 0.45f;

	/** Minimum vertical impulse as fraction of total impulse.
	 *  Guarantees some upward boost even when explosion is directly below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Impulse", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanExplode"))
	float ExplosionMinVerticalRatio = 0.3f;

	/** Impulse strength for physics bodies (Newtons). Separate from character impulse for tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Impulse", meta = (ClampMin = "0.0", EditCondition = "bCanExplode"))
	float ExplosionPhysicsImpulse = 50000.0f;

	// ==================== Explosion Stun ====================

	/** If true, explosion stuns nearby NPCs (puts them in Knockback state) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Stun", meta = (EditCondition = "bCanExplode"))
	bool bApplyExplosionStun = true;

	/** Duration of the stun effect (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Stun", meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bCanExplode"))
	float ExplosionStunDuration = 2.0f;

	/** Animation montage to play on stunned NPCs (if null, uses NPC's default KnockbackMontage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact|Stun", meta = (EditCondition = "bCanExplode"))
	TObjectPtr<UAnimMontage> ExplosionStunMontage;

	// ==================== Charge Overlay Materials ====================

	/** If true, overlay material will be applied based on charge state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay")
	bool bUseChargeOverlay = false;

	/** Overlay material to apply when charge is neutral (near zero) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NeutralChargeOverlayMaterial;

	/** Overlay material to apply when charge is positive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> PositiveChargeOverlayMaterial;

	/** Overlay material to apply when charge is negative */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NegativeChargeOverlayMaterial;

	// ==================== Melee Charge Transfer ====================

	/** Charge added to prop when hit by melee (opposite sign to attacker's charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float ChargeChangeOnMeleeHit = -10.0f;

	/** If true, melee hits grant stable charge to the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee")
	bool bGrantsStableCharge = false;

	/** Amount of stable charge per melee hit (for player) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee", meta = (ClampMin = "0.0", EditCondition = "bGrantsStableCharge"))
	float StableChargePerHit = 1.0f;

	/** Bonus charge on kill (for player) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee", meta = (ClampMin = "0.0", EditCondition = "bGrantsStableCharge"))
	float KillChargeBonus = 0.0f;

	// ==================== Channeling Capture ====================

	/** Can this prop be captured by the channeling plate? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture")
	bool bCanBeCaptured = true;

	/** |charge| a prop must carry before a throwing class may pick it up. Charging it IS the cost of
	 *  the ammunition.
	 *
	 *  Its own field rather than ExplosionReferenceCharge, which is what it used to read. That one is
	 *  an explosion-tuning number, edit-conditioned on bCanExplode, and on a prop with explosions
	 *  switched off there is nothing keeping it meaningful -- set it to zero while tuning and the grab
	 *  gate silently disappears, which is exactly how it "stopped working at max charge only". */
	/** This prop's charge ceiling, as a magnitude. Ionization clamps to it whichever direction it
	 *  drives the charge, and the prop is grabbable only once it is reached.
	 *
	 *  It lives here because the prop is the thing that has a maximum. The ceiling used to be the
	 *  firing weapon's MaxIonizationCharge, which made "is this prop full" depend on what happened to
	 *  be shooting it, and left nothing that reads the prop -- the grab gate, the reticle -- able to
	 *  answer without asking a weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", EditCondition = "bCanBeCaptured"))
	float MaxCharge = 20.0f;

	/** True once this prop sits at its ceiling. What the grab gate and the bracket reticle both ask. */
	UFUNCTION(BlueprintPure, Category = "Channeling Capture")
	bool IsAtMaxCharge() const
	{
		return MaxCharge > KINDA_SMALL_NUMBER && FMath::Abs(GetCharge()) >= MaxCharge - KINDA_SMALL_NUMBER;
	}

	/** Everything about "may this character grab this prop right now" EXCEPT range and angle, which
	 *  the two callers evaluate differently by nature.
	 *
	 *  It lives here because there are two callers: the acquisition scan in UChargeAnimationComponent
	 *  and the bracket reticle in UEMFChargeWidget. When each carried its own copy of the rules, the
	 *  brackets promised grabs the scan then refused -- the reticle was drawn by one set of gates and
	 *  the grab decided by another. Anything added to this function reaches both at once. */
	UFUNCTION(BlueprintPure, Category = "Channeling Capture")
	bool CanBeGrabbedBy(const AActor* Grabber) const;

	/** Viscosity coefficient (damping strength). Higher = faster capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "50.0", EditCondition = "bCanBeCaptured"))
	float ViscosityCoefficient = 10.0f;

	/** Counteract gravity when captured */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (EditCondition = "bCanBeCaptured"))
	bool bCounterGravityWhenCaptured = true;

	/** Gravity counteraction strength (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanBeCaptured"))
	float GravityCounterStrength = 1.0f;

	/** Hooke spring stiffness for pulling prop toward plate center. Force = ToPlate * k * CaptureStrength * Mass (proportional to distance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "50.0", EditCondition = "bCanBeCaptured"))
	float CaptureSpringStiffness = 5.0f;

	/** Minimum CaptureStrength to stay captured */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanBeCaptured"))
	float CaptureMinStrength = 0.05f;

	/** Time below CaptureMinStrength before auto-release */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bCanBeCaptured"))
	float CaptureReleaseTimeout = 0.5f;

	/** Interpolation speed for the initial pull-in to plate center. Higher = faster snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "1.0", ClampMax = "100.0", EditCondition = "bCanBeCaptured"))
	float CapturePullInInterpSpeed = 20.0f;

	/** Distance (cm) at which pull-in lerp snaps to plate and switches to spring/damping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "1.0", ClampMax = "50.0", EditCondition = "bCanBeCaptured"))
	float CapturePullInSnapDistance = 5.0f;

	/** Angular velocity (deg/s) applied to prop when launched. Random axis, this magnitude. 0 = no spin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "3600.0", EditCondition = "bCanBeCaptured"))
	float ReverseLaunchSpinSpeed = 720.0f;

	/** Flight speed of a thrown prop, authored directly.
	 *
	 *  This used to be derived: CaptureRange * DistanceMultiplier / FlightDuration. Capture range is a
	 *  function of the charges involved, so the same prop left the hand at a different speed depending
	 *  on how charged the thrower happened to be -- the throw was never repeatable, and no field said
	 *  so. One number instead, and the throw is the same every time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "100.0", Units = "cm/s", EditCondition = "bCanBeCaptured"))
	float ThrowSpeed = 3600.0f;


	// ==================== Reverse Launch Homing ====================

	/** Steer a thrown prop toward an enemy for as long as it is in flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture|Homing", meta = (EditCondition = "bCanBeCaptured"))
	bool bEnableReverseLaunchHoming = true;

	/** Half-angle (degrees) of the forward detection cone. Only enemies within this cone are considered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture|Homing", meta = (ClampMin = "5.0", ClampMax = "45.0", EditCondition = "bCanBeCaptured && bEnableReverseLaunchHoming"))
	float HomingConeHalfAngle = 15.0f;

	/** Max range (cm) for homing target detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture|Homing", meta = (ClampMin = "500.0", ClampMax = "10000.0", Units = "cm", EditCondition = "bCanBeCaptured && bEnableReverseLaunchHoming"))
	float HomingMaxRange = 3000.0f;

	/** How hard the throw is bent toward its target, in cm/s^2.
	 *
	 *  An acceleration, not a rewrite of the velocity: it is ADDED to whatever physics already did
	 *  that frame, so gravity still pulls, bounces still bounce and the prop still carries its
	 *  momentum. The old rail overwrote velocity outright, which is precisely how it cancelled all
	 *  three. At 4000 a throw crossing a room curves noticeably without ever looking flown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture|Homing", meta = (ClampMin = "0.0", EditCondition = "bCanBeCaptured && bEnableReverseLaunchHoming"))
	float HomingAcceleration = 4000.0f;


	// ==================== Debug ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bDrawDebugForces = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bLogEMForces = false;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropDeath OnPropDeath;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropDamaged OnPropDamaged;

	/** Called when charge value changes (for BP overlay/VFX) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropChargeChanged OnChargeChanged;

	/** Called when prop explodes (for BP camera shake, particles, etc.) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropExploded OnPropExploded;

	/** Called for each NPC stunned by this prop's explosion */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNPCStunnedByExplosion OnNPCStunnedByExplosion;

	/** Called when prop impacts at critical velocity — signals arena-level destruction */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropCriticalVelocityImpact OnCriticalVelocityImpact;

	// ==================== Public API ====================

	/** Get current charge */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Set charge directly */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Get EMF mass */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetPropMass() const;

	/** Set EMF mass (also updates physics body mass) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetPropMass(float NewMass);

	/** Is this prop dead? */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bIsDead; }

	/** Get health percentage (0-1) */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthPercent() const { return MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f; }

	/** Restore prop to a saved checkpoint state (called by CheckpointSubsystem on player respawn) */
	void RestoreFromCheckpointState(const struct FPropCheckpointData& State);

	/** Is this prop currently in reverse channeling flight? */
	UFUNCTION(BlueprintPure, Category = "Explosive Impact")
	bool IsInReverseFlight() const { return bIsInReverseFlight; }

	/** If true, this prop can be destroyed by another prop's explosion (chain reaction).
	 *  By default props ignore damage from each other to prevent unintended chain reactions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Impact")
	bool bAllowChainReaction = false;

	/**
	 * Trigger explosion at current location. Deals radial damage, spawns VFX/SFX, kills the prop.
	 * @param DamageMultiplier  Multiplier for damage (e.g. 2.0 for shot-triggered detonation)
	 * @param RadiusMultiplier  Multiplier for explosion radius
	 * @param VFXScaleMultiplier  Multiplier for VFX scale
	 */
	UFUNCTION(BlueprintCallable, Category = "Explosive Impact")
	void Explode(float DamageMultiplier = 1.0f, float RadiusMultiplier = 1.0f, float VFXScaleMultiplier = 1.0f);

	/** Reset prop to alive state: restore HP, visibility, physics, charge.
	 *  Call SetActorTransform() before this if you need to restore position. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ResetProp();

	// ==================== Channeling Capture API ====================

	/** Mark this prop as captured by the given plate */
	UFUNCTION(BlueprintCallable, Category = "Channeling Capture")
	/** Turn the prop's own physics on or off. Every path that wants to simulate goes through here,
	 *  because off the authority the answer is always no. */
	void ApplyPropPhysicsSimulation(bool bEnable);

	void SetCapturedByPlate(AEMFChannelingPlateActor* Plate);

	/** Release this prop from capture */
	UFUNCTION(BlueprintCallable, Category = "Channeling Capture")
	void ReleasedFromCapture();

	/** Is this prop currently captured? */
	UFUNCTION(BlueprintPure, Category = "Channeling Capture")
	bool IsCapturedByPlate() const { return CapturingPlate.IsValid(); }

	/** Detach from plate without fully releasing (for plate swap during reverse channeling) */
	void DetachFromPlate();

	/** Capture range as it applies to a specific character pulling this prop.
	 *
	 *  Range is a product of both charges, so it is different for each player, and the plain
	 *  CalculateCaptureRange answers for whoever is at THIS screen (see the TODO(COOP) on
	 *  UChargeAnimationComponent::GetCaptureRangeFor). On the server that is the host, so validating
	 *  a client's capture or flying a client's throw with it silently used the host's charge.
	 *  Pass the character who is actually doing the pulling; null falls back to the old answer. */
	UFUNCTION(BlueprintPure, Category = "Channeling Capture")
	float GetCaptureRangeForCharacter(const AShooterCharacter* Character) const;

	// ==================== Remote Hold (client-held capture) ====================
	// Only the authority simulates this prop's physics normally. While a REMOTE client is the one
	// holding it via channeling, that client runs the same spring/damping math locally (kinematically,
	// no physics body) for zero-latency feel and reports its result here; the server stops running its
	// own copy of the capture and just accepts the reported transform until release. The host's own
	// capture is untouched by any of this — it still drives the real physics body directly, because
	// host and server are the same machine and there is no round trip to hide.

	/** Character currently holding this prop over the network (null when nobody remote is holding it).
	 *  Replicated so every client — not just the holder — can tell the prop is spoken for and refuse
	 *  to start their own capture on it. Server-authoritative: only Server_CaptureProp sets it. */
	UFUNCTION(BlueprintPure, Category = "Coop")
	AShooterCharacter* GetHoldingCharacter() const { return HoldingCharacter; }

	/** Server-side: accept Holder as the new remote owner of this prop's transform. Turns the prop's
	 *  own physics off (it becomes a kinematic mirror of whatever BeginRemoteHold's caller reports)
	 *  and switches Pawn collision to Overlap, same as the host's local capture already does. */
	void BeginRemoteHold(AShooterCharacter* Holder, float HolderCaptureRange);

	/** Capture range the current remote holder reported, or 0 when nobody remote is holding.
	 *  See the comment on AShooterCharacter::Server_CaptureProp for why the client has to say. */
	float GetHeldCaptureRange() const { return HeldCaptureRange; }

	/** Server-side: give the prop's physics back, seeded with the last velocity the holder reported
	 *  so a released/dropped prop keeps its momentum instead of freezing then falling straight down. */
	void EndRemoteHold();

	/** Server-side: apply a transform reported by the current holder. Called every tick while a
	 *  remote client holds this prop; the server does not re-derive the spring math itself. */
	void ApplyHeldTransform(const FVector& Location, const FRotator& Rotation, const FVector& LinearVelocity);

	/** True while THIS machine is the one holding the prop without being the server. The holder
	 *  simulates the prop for real and reports the result, which is what lets it be held by a
	 *  constraint and collide with the world properly; everyone else is shown where it ended up. */
	bool IsLocallyHeld() const { return bLocallyHeld; }

	/** Let a stuck prop be pulled through world geometry. A held prop is stopped by walls, which is
	 *  the point, but a prop wedged behind a corner would otherwise stay there while the player walks
	 *  away. After the holder has been unable to reach it for a moment, its world collision is turned
	 *  off so the constraint can yank it back to the hand, and turned straight back on once it
	 *  arrives — the prop passes through the wall for that instant rather than living inside it. */
	void SetHeldPassThrough(bool bPassThrough);

	/** Break the prop apart on every machine. The gibs and the hiding of the intact mesh are both
	 *  plain local calls inside SpawnDestructionGC, so a prop destroyed by the server broke apart
	 *  for the host while every client kept looking at the whole thing sitting there. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayDeathVisuals(FVector DestructionOrigin);

	/** Show the explosion on every machine. Only the authority ever runs Explode, so the blast, the
	 *  sound and the light existed for the host alone and a client saw the prop silently vanish.
	 *  Unreliable: purely cosmetic, and a lost one is a blast nobody can act on anyway. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayExplosionEffects(FVector ExplosionLocation, float VFXScale);

	/** Server-side: the holder threw it. Unlike the hold, the flight is NOT predicted by the
	 *  thrower — the server takes the physics body back and flies the prop itself, so that the hit
	 *  it lands, the damage it deals and the explosion it sets off are all decided in one place.
	 *  The throw is a brief one-shot, so the round trip costs a moment of travel rather than the
	 *  continuous disconnect that made the hold need prediction. */
	void BeginRemoteLaunch(AShooterCharacter* Thrower);

	// ==================== Coop Attribution ====================
	// Whatever this prop does to the world, it does on behalf of the character who charged and
	// spent it. Single player could read that off the one player controller; coop cannot, and the
	// plate is not a substitute because it is destroyed the moment the prop is thrown.

	/** Character this prop acts for. Null for props nobody has captured (world explosions, chain reactions). */
	UFUNCTION(BlueprintPure, Category = "Coop")
	AShooterCharacter* GetSpendingCharacter() const;

	/** Set the character this prop acts for. Called on capture and deliberately NOT cleared on
	 *  release, so a prop stays attributed while it is in the air. */
	UFUNCTION(BlueprintCallable, Category = "Coop")
	void SetSpendingCharacter(AShooterCharacter* InCharacter);

	/** Teammates are immune to this prop's area effects. The spender is always immune regardless.
	 *  Set to false to let a thrown prop catch the rest of the team: friendly fire is an opt-in
	 *  gag here, not the baseline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop")
	bool bTeammatesImmuneToAreaEffects = true;

	/** The one gate for "skip this actor because it is a player". Every area effect of this prop
	 *  goes through here, so turning friendly fire on later stays a single-place change. */
	bool ShouldSkipPlayerForAreaEffect(const AActor* HitActor) const;

	// ==================== Decoy (the Tank's item verb) ====================
	// Every class charges props the same way and spends them differently. The Tank's way is this: the
	// prop it throws does not detonate, it lands and starts making noise, and enemies near it come to
	// fight it instead of the team. The prop is the decoy rather than something spawned in its place,
	// which means the object the players were looking at is the object that lands, it can be shot to
	// pieces to end the distraction early, and none of the networking around held and thrown props
	// needed a second version of itself.

	/** How long the noise lasts once thrown. TEST VALUE: how many seconds of bought attention the
	 *  Tank's item is worth is a balance decision, not a technical one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoy", meta = (ClampMin = "0.0", Units = "s"))
	float DecoyDuration = 6.0f;

	/** How far the noise carries. Enemies outside it are not distracted at all: a decoy is a local
	 *  event, and one that emptied the whole arena would be a different mechanic. TEST VALUE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoy", meta = (ClampMin = "0.0", Units = "cm"))
	float DecoyPullRadius = 2500.0f;

	/** True while this prop is pulling aggression. Replicated, so every machine can draw it. */
	UFUNCTION(BlueprintPure, Category = "Decoy")
	bool IsDecoy() const { return bIsDecoy; }

	/** Turn it on. Authority only — this decides what the AI does. Called from the throw when the
	 *  thrower's class item verb is Decoy; also callable from Blueprint for tests and for anything
	 *  else that should be loud later. */
	UFUNCTION(BlueprintCallable, Category = "Decoy")
	void BecomeDecoy();

	/** Turn it off early: destroyed, or picked up again. Authority only; safe to call when it is not
	 *  a decoy. */
	UFUNCTION(BlueprintCallable, Category = "Decoy")
	void EndDecoy();

	/** Cosmetics live in the Blueprint: the siren, the flashing, the shaking. Both run on every
	 *  machine — on the authority from BecomeDecoy/EndDecoy, on everybody else from OnRep. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Decoy")
	void BP_OnDecoyStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "Decoy")
	void BP_OnDecoyEnded();

	// ==================== IShooterDummyTarget Interface ====================

	virtual bool GrantsStableCharge_Implementation() const override;
	virtual float GetStableChargeAmount_Implementation() const override;
	virtual float GetKillChargeBonus_Implementation() const override;
	virtual bool IsDummyDead_Implementation() const override;

	// ==================== AActor Overrides ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The whole replicated-movement path, refused outright while this machine is the holder.
	 *
	 *  This is not just about the transform. AActor::OnRep_ReplicatedMovement compares the server's
	 *  bRepPhysics against its own and calls SyncReplicatedPhysicsSimulation, which forces the local
	 *  body's simulation to match the server's (ActorReplication.cpp:222). The server deliberately
	 *  goes kinematic while a client carries a prop — so the moment that arrived, the engine switched
	 *  the holder's own simulation OFF and the prop froze in mid-air with the constraint still
	 *  attached to it. Measured, not guessed: the hold trace showed simulating=1 at the grab and
	 *  simulating=0 half a second later, with the prop's position never changing again.
	 *
	 *  It also explains why holding worked now and then: a prop whose server copy was already
	 *  kinematic never changed the flag, so there was nothing to sync. */
	virtual void OnRep_ReplicatedMovement() override;

	/** While this machine is holding the prop it is also the one simulating it, so the transform
	 *  arriving from the server is its own report come back a round trip later. Applying it would
	 *  fight the local simulation several times a second, which is exactly what made a held prop
	 *  jitter. The holder ignores it and keeps its own answer until it lets go. */
	virtual void PostNetReceiveLocationAndRotation() override;

	/** The engine picks this path instead of the one above whenever the SERVER's copy of the prop is
	 *  simulating, which is every prop nobody is carrying. Its normal job is to correct a client that
	 *  is simulating too, by pushing the authority's state into the local body — and our clients
	 *  deliberately do not simulate, so that correction landed on a body that was asleep and did
	 *  nothing at all. That is why a prop the host pushed or blew up never moved on a client while a
	 *  prop a client carried moved fine on the host: only the carried one travelled as a plain
	 *  transform. Here the non-simulating case takes the transform out of the replicated state
	 *  directly, and the holder ignores it for the same reason as above. */
	virtual void PostNetReceivePhysicState() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	/** Spawn the destruction Geometry Collection. Override to implement custom destruction patterns
	 *  (e.g. progressive top-down collapse for a skyscraper instead of an instant universal break). */
	virtual void SpawnDestructionGC(const FVector& DestructionOrigin);

private:
	bool bIsDead = false;
	float LastCollisionDamageTime = -1.0f;

	/** True while prop is being launched by reverse channeling */
	bool bIsInReverseFlight = false;

	/** True if prop has already exploded (prevents double explosion) */
	bool bHasExploded = false;

	/** Cached charge scale from Explode() — used by SpawnDestructionGC to scale gib impulse.
	 *  1.0 for non-explosion deaths. */
	float CachedChargeScale = 1.0f;

	/** Prop's speed cached at end of Tick (before any collision callbacks).
	 *  Used for explosion checks so only the prop's OWN velocity counts. */
	float CachedPreCollisionSpeed = 0.0f;

	/** Full pre-collision velocity vector (same cache point as CachedPreCollisionSpeed).
	 *  Used by the Air Mail bounce for the incidence-angle test — the post-collision
	 *  velocity available inside OnPropHit is already reflected by the solver. */
	FVector CachedPreCollisionVelocity = FVector::ZeroVector;

	/** One-shot guard: a launched prop performs at most one Air Mail bounce per launch.
	 *  Reset when a new reverse-flight launch starts. */
	bool bAirMailBounceConsumed = false;

	/** True from launch until the prop slows down / bounces / explodes / is recaptured.
	 *  Outlives bIsInReverseFlight: the steered reverse-flight window ends by DURATION while
	 *  the prop keeps flying ballistically at full speed — impacts during that tail must
	 *  still count as "thrown by the player" for the Air Mail bounce. */
	bool bAirMailEligibleFlight = false;

	/** Air Mail: if the player owns the upgrade and this impact qualifies (player-launched,
	 *  not exploded, incidence angle within the 60–120° band), redirect the prop toward a
	 *  point at the player's head height and tag it TAG_AirMailIncoming. On success also
	 *  clears bIsInReverseFlight (otherwise UpdateReverseFlight would overwrite the return
	 *  velocity next tick). bCharacterImpact skips the incidence-angle gate (enemy hits).
	 *  Returns true if the bounce was performed. */
	bool TryAirMailBounce(const FVector& ImpactNormal, const FVector& ImpactPoint, bool bCharacterImpact = false);

	// ==================== Charge Tracking State ====================

	float PreviousChargeValue = 0.0f;
	uint8 PreviousPolarity = 0;

	// ==================== Channeling Capture State ====================

	UPROPERTY()
	TWeakObjectPtr<AEMFChannelingPlateActor> CapturingPlate;

	/** See GetSpendingCharacter. Weak on purpose: the prop must outlive its spender dying. */
	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> SpendingCharacter;

	/** See GetHoldingCharacter. Strong + replicated: unlike SpendingCharacter this is server-set
	 *  network-authoritative state, not a local attribution cache, and every machine needs to see it
	 *  to know the prop is already spoken for. */
	UPROPERTY(Replicated)
	TObjectPtr<AShooterCharacter> HoldingCharacter = nullptr;

	/** See IsLocallyHeld. Only ever true on a client that is itself holding this prop. */
	bool bLocallyHeld = false;

	/** See SetHeldPassThrough. */
	bool bHeldPassThrough = false;

	/** See GetHeldCaptureRange. Server-side only, set from the holder's report at capture. */
	float HeldCaptureRange = 0.0f;

	/** World time of the last transform its holder reported, and of the moment a throw began.
	 *  A hold is only ever ended by the holder letting go, or by a thrown prop hitting something.
	 *  Both can go missing — a throw that never lands a blocking hit (an Air Mail bounce clears the
	 *  flight flag, a prop can simply coast to rest) leaves the prop marked as somebody's forever,
	 *  and then nobody else can pick it up, silently. These two stamps are what the watchdog in Tick
	 *  uses to notice that and hand the prop back. */
	float LastHeldReportTime = 0.0f;
	float RemoteLaunchStartTime = 0.0f;

	/** Distance from the hand last tick, so the hold can tell "still reeling it in" from "losing it".
	 *  Reset on capture. See UpdateHeldByHandle. */
	float PreviousHoldDistance = BIG_NUMBER;

	/** The authority's charge, mirrored down to clients.
	 *
	 *  The real value lives in the EMF plugin's field component, which replicates nothing and belongs
	 *  to another repository. Without this a client saw every prop at its DefaultCharge forever: no
	 *  overlay, no HUD, and its own capture attempts measured against a charge that was never true. */
	UPROPERTY(ReplicatedUsing = OnRep_Charge)
	float ReplicatedCharge = 0.0f;

	UFUNCTION()
	void OnRep_Charge();

	/** Last velocity a remote holder reported. Seeds the physics body back to life on release so the
	 *  prop keeps its momentum instead of dropping from a standstill. */
	FVector LastReportedVelocity = FVector::ZeroVector;

	/** See IsDecoy. Server-set; the clients get it to run the Blueprint cosmetics, and for nothing
	 *  else — the pull itself is decided entirely on the authority, where the AI lives. */
	UPROPERTY(ReplicatedUsing = OnRep_IsDecoy)
	bool bIsDecoy = false;

	UFUNCTION()
	void OnRep_IsDecoy();

	FTimerHandle DecoyTimer;

	/** What the thrower's class does with a thrown prop, applied once at launch. Called from BOTH
	 *  throw paths (the plate-driven host throw and BeginRemoteLaunch for a client's), because a
	 *  verb that only worked for the host is exactly the class of bug this project keeps finding. */
	void ApplyItemVerbOnThrow();

	FVector PreviousPlatePosition = FVector::ZeroVector;
	bool bHasPreviousPlatePosition = false;
	float WeakCaptureTimer = 0.0f;
	bool bReverseLaunchInitialized = false;

	// ==================== Internal Methods ====================

	/** Calculate effective capture range based on player and prop charges.
	 *  Formula: BaseRange * max(1, 1 + ln(|q_player * q_prop| / NormCoeff)) */
	float CalculateCaptureRange() const;

	/** Apply electromagnetic forces from all EMF sources */
	void ApplyEMForces(float DeltaTime);

	/** Apply viscous capture forces when held by channeling plate */
	void UpdateCaptureForces(float DeltaTime);

	/** The held (non-throwing) half of the capture. The holder's UPhysicsHandleComponent does the
	 *  actual moving now, so all this does is report the result to the server and give the prop up
	 *  if it somehow ends up far outside capture range. */
	void UpdateHeldByHandle(float DeltaTime);


	/** The throw itself: hand the body a single velocity and a spin, and let physics own the rest.
	 *  Shared by the plate-driven (host) and plateless (remote throw) paths so both leave identically. */
	void LaunchAlongAim(const FVector& AimDir);

	/** One frame of homing. Authority only: this changes where a replicated actor goes, and a client
	 *  steering its own copy would just be corrected back every update. */
	void TickHomingSteer(float DeltaTime);

	/** Locked once acquired, so the throw commits to one enemy instead of flicking between two that
	 *  happen to trade places in the cone mid-flight. */
	TWeakObjectPtr<AShooterNPC> HomingTarget;

	/** Where the throw is aimed: the eyes of the character who spent this prop. Returns false when
	 *  there is no spender to ask, which is the one case a thrown prop cannot happen without. */
	bool GetReverseFlightAimSource(FVector& OutOrigin, FVector& OutDirection) const;

	/** Find best homing target: overlap sphere within HomingMaxRange, filter by cone + alive */
	AShooterNPC* FindHomingTarget(const FVector& Position, const FVector& AimDirection) const;

	/** Handle blocking collision with walls/floors/physics bodies */
	UFUNCTION()
	void OnPropHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Handle overlap with Pawns (NPC damage — Pawns use ECR_Overlap to avoid physics impulse with player) */
	UFUNCTION()
	void OnPropOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Apply the weak-impact path: reduced damage/stun, half-charge transfer, velocity reflection.
	 *  Used when prop hits NPC at speed >= ExplosionSpeedThreshold but |charge| < ExplosionMinCharge. */
	void ApplyWeakImpactToNPC(AShooterNPC* HitNPC, const FVector& ImpactNormal, const FVector& ImpactPoint);

	/** Called when HP reaches zero */
	void Die(AActor* Killer);

	/** Update charge tracking: fire delegates and update overlay */
	void UpdateChargeTracking();

	/** Update overlay material based on current charge polarity */
	void UpdateChargeOverlay(uint8 NewPolarity);

	/** Get force multiplier for a given source owner type */
	float GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const;

	/** Check if source has effectively zero charge/field strength */
	static bool IsSourceEffectivelyZero(const FEMSourceDescription& Source);

	/** Get effective charge sign of source (+1, -1, or 0 for magnetic/neutral) */
	static int32 GetSourceEffectiveChargeSign(const FEMSourceDescription& Source);

	// ==================== Geometry Collection Destruction (Internal) ====================

	/** Settle GC gibs: strip collision to WorldStatic only, apply high damping.
	 *  Pieces fall to rest naturally, Chaos auto-sleeps them (near-zero cost). */
	void FreezeGibs();

	FTimerHandle GCFreezeTimer;
	FTimerHandle GCCleanupTimer;

	/** Cached reference to spawned GC actor (for freeze/cleanup) */
	UPROPERTY()
	TWeakObjectPtr<AGeometryCollectionActor> SpawnedGCActor;
};
