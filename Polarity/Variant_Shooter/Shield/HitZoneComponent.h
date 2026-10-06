// HitZoneComponent.h
// Where on an enemy a hit landed, and what that part of the body does with it.
//
// Docs/Damage_Model_Rework_Plan_2026-10-03.md §3-4. A zone is a box in the space of one bone of the
// owner's mesh (boxes, not bones, because a mesh like the tankette has no bone per track or sight).
// The first box the shot's line enters is the zone; nothing entered means the fallback zone. A zone
// may refuse light damage (hull plate), scale the hit, or keep it in a pool of its own (a track, the
// sights) that breaks once and tells the owner. Server decides; broken zones replicate for the VFX.
//
// The NPC damage funnel asks it first (AShooterNPC::TakeDamage), before armour and health.
// Debug: polarity.debug.hitzones 1 draws the boxes (green whole, red broken).

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Components/ActorComponent.h"
#include "HitZoneComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class USkeletalMeshComponent;
class UBoxComponent;
class UHitZoneComponent;
struct FDamageEvent;

/** Which damage class a zone lets through. */
UENUM(BlueprintType)
enum class EHitZoneAccepts : uint8
{
	/** Any hit. */
	Any,
	/** Only heavy damage (UDamageType_Heavy); a light round does nothing at all. */
	HeavyOnly
};

/** One part of the body. */
USTRUCT(BlueprintType)
struct FHitZone
{
	GENERATED_BODY()

	/** What the owner calls this part (TrackLeft, Optics, Hull). Code identifier, not a display name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FName Name;

	/** The box lives in this bone's space (it turns with a turret). None = the mesh's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FName Bone;

	/** Box centre, in the bone's space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FVector Center = FVector::ZeroVector;

	/** Box half size, in the bone's space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FVector Extent = FVector(50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	EHitZoneAccepts Accepts = EHitZoneAccepts::Any;

	/** Scales every hit that lands here (above 1 a weak spot, below 1 a limb). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone", meta = (ClampMin = "0.0"))
	float Multiplier = 1.0f;

	/** This part's own durability. 0 = none: hits go straight on to armour and health. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Pool", meta = (ClampMin = "0.0"))
	float Pool = 0.0f;

	/** With a pool: hits also reach the owner's health. Off = the part takes them alone (a track, the
	 *  sights), and once broken further hits there do nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Pool", meta = (EditCondition = "Pool > 0"))
	bool bHealthTakesHits = false;

	/** With a pool the health does not share: heavy damage reaches the health anyway. A rocket on
	 *  the tracks still wrecks the whole machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Pool", meta = (EditCondition = "Pool > 0 && !bHealthTakesHits"))
	bool bHeavyReachesHealth = true;

	/** What a bullet landing here looks and sounds like (the weapon's impact table by surface).
	 *  Default = leave it to the enemy's own surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Presentation")
	TEnumAsByte<EPhysicalSurface> Surface = SurfaceType_Default;

	/** The same once the part is broken. Default = keep Surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Presentation")
	TEnumAsByte<EPhysicalSurface> BrokenSurface = SurfaceType_Default;

	/** Played on the part when its pool runs out (attached, every machine). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Presentation")
	TObjectPtr<UNiagaraSystem> BrokenEffect;

	/** Offset from the zone centre in the same bone space. Place the break burst outside the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Presentation")
	FVector BrokenEffectOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Presentation")
	TObjectPtr<USoundBase> BrokenSound;
};

/** What one hit did. */
USTRUCT(BlueprintType)
struct FHitZoneOutcome
{
	GENERATED_BODY()

	/** Index into Zones, INDEX_NONE when the owner has no zone there. */
	UPROPERTY(BlueprintReadOnly, Category = "Zone")
	int32 Zone = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Zone")
	FName Name;

	/** What goes on to armour and health. */
	UPROPERTY(BlueprintReadOnly, Category = "Zone")
	float BodyDamage = 0.0f;

	/** The zone refused it outright (light on heavy plate): not a hit at all. */
	UPROPERTY(BlueprintReadOnly, Category = "Zone")
	bool bBlocked = false;

	/** Went into the zone's pool. */
	UPROPERTY(BlueprintReadOnly, Category = "Zone")
	bool bPoolHit = false;

	/** This hit broke the zone. */
	UPROPERTY(BlueprintReadOnly, Category = "Zone")
	bool bBrokeZone = false;
};

/** The little picture beside a row of the enemy bar. */
UENUM(BlueprintType)
enum class EHitZoneBarIcon : uint8
{
	None,
	Turret,
	Tracks
};

/** One row of the enemy bar: the pools of these zones side by side (two tracks = two halves). */
USTRUCT(BlueprintType)
struct FHitZoneBarRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bar")
	EHitZoneBarIcon Icon = EHitZoneBarIcon::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bar")
	TArray<FName> Zones;
};

/** What a hit will meet, read before it lands, on whichever machine fired (no side effects). */
struct FHitZoneVerdict
{
	int32 Zone = INDEX_NONE;
	/** The zone refuses this damage class: nothing will happen. */
	bool bDeflected = false;
	/** The zone is already broken and does not pass hits on: nothing will happen. */
	bool bDeadPart = false;
	/** Surface for the impact effect; SurfaceType_Default = no opinion. */
	EPhysicalSurface Surface = SurfaceType_Default;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHitZoneBroken, UHitZoneComponent*, Zones, FName, ZoneName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnHitZonePoolHit, UHitZoneComponent*, Zones, FName, ZoneName, float, Damage, AActor*, Attacker);

UCLASS(ClassGroup = (Damage), meta = (BlueprintSpawnableComponent))
class POLARITY_API UHitZoneComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UHitZoneComponent();

	/** The parts. The first box a shot's line enters wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Zones")
	TArray<FHitZone> Zones;

	/** Build query-only shot colliders from these boxes on every machine. Movement stays separate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Zones")
	bool bUseZoneCollision = false;

	/** The zone for a hit that enters no box, and for blasts (no line). None = no zone: the hit goes
	 *  on as if this component were not there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Zones")
	FName FallbackZone;

	/** Rows the enemy bar draws for this body, top to bottom, above its health. Empty = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Zones")
	TArray<FHitZoneBarRow> BarRows;

	/** Server: sort one hit out. Damage is the full hit, before armour. */
	FHitZoneOutcome ProcessHit(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser);

	/** Which zone the hit is in (no side effects). */
	int32 ResolveZone(const FDamageEvent& DamageEvent, AActor* DamageCauser) const;

	/** Which zone a hit with this line lands in. Blast (no line) = fallback. */
	int32 ResolveZoneForHit(const FHitResult& Hit, const AActor* DamageCauser) const;

	/** What a hit will meet, before it lands. Any machine: what it reads (config, broken mask) is
	 *  the same everywhere. For the shooter's feedback and the impact effect. */
	FHitZoneVerdict PreviewHit(const FHitResult& Hit, const AActor* DamageCauser, bool bHeavy) const;

	/** Is this damage type heavy (UDamageType_Heavy or a child)? */
	static bool IsHeavyDamageType(const UClass* DamageType);

	int32 FindZone(FName ZoneName) const;

	/** Centre of an existing zone in world space, including the current bone transform. */
	bool GetZoneWorldCenter(FName ZoneName, FVector& Out) const;

	UFUNCTION(BlueprintPure, Category = "Hit Zones")
	bool IsZoneBroken(FName ZoneName) const;

	/** What is left of the zone's pool, 0..1 (1 for a zone without a pool). */
	UFUNCTION(BlueprintPure, Category = "Hit Zones")
	float GetZonePoolFraction(FName ZoneName) const;

	/** Whole again: pools full, nothing broken. For the NPC pool's recycle. Server. */
	void ResetZones();

	void SetZoneCollisionEnabled(bool bEnabled);

	/** Is this hit heavy damage (UDamageType_Heavy or a child)? */
	static bool IsHeavyDamage(const FDamageEvent& DamageEvent);

	/** A zone broke. Every machine (clients through the replicated mask). */
	UPROPERTY(BlueprintAssignable, Category = "Hit Zones")
	FOnHitZoneBroken OnZoneBroken;

	/** A hit went into a zone's pool. Server. */
	UPROPERTY(BlueprintAssignable, Category = "Hit Zones")
	FOnHitZonePoolHit OnZonePoolHit;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void BeginPlay() override;

private:

	/** Bit per zone index: broken. */
	UPROPERTY(ReplicatedUsing = OnRep_BrokenMask)
	int32 BrokenMask = 0;

	UFUNCTION()
	void OnRep_BrokenMask();

	/** What OnRep last saw, so a client plays each break once. */
	int32 SeenBrokenMask = 0;
	/** Attached loops must stop when their zone is restored for another pooled spawn. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> BrokenEffects;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ZoneColliders;

	void BuildZoneCollision();

	/** Pool left per zone. Server writes; replicated for the enemy bar. */
	UPROPERTY(Replicated)
	TArray<float> PoolLeft;

	void BreakZone(int32 Index);

	/** Effects and the event for zones that broke since SeenBrokenMask. */
	void PresentBrokenZones();

	USkeletalMeshComponent* GetOwnerMesh() const;

	/** World transform of a zone's box space. */
	FTransform GetZoneTransform(const FHitZone& Zone) const;

	void DrawZones(float Duration) const;
};
