// HitFeedbackSet.h
// Everything one class of weapon sounds and looks like when it lands a hit, in one asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Chaos/ChaosEngineInterface.h"
#include "HitFeedbackSet.generated.h"

class USoundBase;
class USoundAttenuation;
class UNiagaraSystem;
class UHitFeedbackSet;

/**
 * Which confirmation a hit earned. Derived from the hit's context in exactly one place
 * (UHitFeedbackSet::ResolveCue) so the shooter's ears and the shooter's screen can never disagree
 * about what just happened.
 */
UENUM(BlueprintType)
enum class EHitFeedbackCue : uint8
{
	None,
	/** Landed on an exposed body. */
	HitFlesh,
	/** Landed on a shield that is still holding. */
	HitShield,
	/** Landed on the head. */
	Headshot,
	/** This hit is the one that took the shield down. */
	ShieldBreak,
	/** This hit killed. */
	Kill,
	/** This hit killed, through the head. */
	HeadshotKill,
	/** Connected but dealt no health damage -- the ionizer charging a target up. */
	ZeroDamage
};

/**
 * The 2D half of the feedback: a flat sound for the person who pulled the trigger.
 *
 * Deliberately not spatialised and deliberately not attenuated. It is a readout, not an event in
 * the world -- distance, walls and the shape of the room must not be able to hide it.
 */
USTRUCT(BlueprintType)
struct FHitFeedbackCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Volume = 1.0f;

	/** Pitch is randomised inside this range on every play. Leave both at 1.0 for a cue that has to
	 *  be recognised instantly and identically every time -- the kill, above all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMax = 1.0f;

	bool IsSet() const { return Sound != nullptr; }
};

/**
 * The 3D half: what a bullet landing on one kind of surface sounds and looks like, in the world,
 * for everybody who can hear it.
 */
USTRUCT(BlueprintType)
struct FImpactFeedback
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<UNiagaraSystem> VFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Volume = 1.0f;

	bool HasSound() const { return Sound != nullptr; }
	bool HasVFX() const { return VFX != nullptr; }
};

/**
 * Everything the feedback layer needs to know about a single hit.
 *
 * One struct rather than a growing argument list because this travels through four layers (weapon
 * -> weapon holder -> character -> hit marker), and every parameter added to a six-argument call
 * had to be threaded through eleven call sites by hand.
 */
USTRUCT(BlueprintType)
struct FHitFeedbackContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FVector HitDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	bool bHeadshot = false;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	bool bKilled = false;

	/** The shield was still holding when this shot arrived. */
	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	bool bShieldHit = false;

	/** This shot is the one that took the shield down. Set for exactly one hit per shield. */
	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	bool bShieldBroken = false;

	/** Connected but wrote no health -- the ionizer filling a target's meter. */
	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	bool bZeroDamage = false;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	TObjectPtr<AActor> HitActor = nullptr;

	/** The firing weapon's set. Null falls the hit marker back onto its own legacy sounds. */
	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	TObjectPtr<UHitFeedbackSet> FeedbackSet = nullptr;
};

/**
 * One asset per class of weapon -- light, heavy, shotgun, energy, melee -- holding both halves of
 * what a hit feels like: the world impact and the shooter's confirmation.
 *
 * Per class rather than per weapon on purpose. A player can learn five hit sounds and read them
 * instantly; thirty is just noise, and every new gun would need a trip to a sound designer instead
 * of a pointer to an existing set.
 */
UCLASS(BlueprintType)
class POLARITY_API UHitFeedbackSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==================== Confirmation, 2D, shooter only ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue HitFlesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue HitShield;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue Headshot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue ShieldBreak;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue Kill;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue HeadshotKill;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Confirmation (2D)")
	FHitFeedbackCue ZeroDamage;

	// ==================== Impacts, 3D, everyone ====================

	/** Keyed by the SurfaceType from Project Settings -> Physics -> Physical Surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impacts (3D)")
	TMap<TEnumAsByte<EPhysicalSurface>, FImpactFeedback> Impacts;

	/** Used when the surface that was hit has no entry above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impacts (3D)")
	FImpactFeedback DefaultImpact;

	// ==================== Pacing ====================

	/** Fold every hit landed in the same frame into one confirmation, summing the damage.
	 *
	 *  For a shotgun this is the difference between one heavy thump and eight overlapping ticks
	 *  that read as a rattle. A cue that outranks what already played this frame still gets through
	 *  -- pellet three killing must not be swallowed because pellet one merely connected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pacing")
	bool bAccumulatePerFrame = false;

	/** Floor on the gap between two confirmations of equal or lower rank, in seconds. Stops a
	 *  fast automatic from firing a hit sound on every single bullet. 0 disables the limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pacing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinCueInterval = 0.0f;

	// ==================== Lookup ====================

	/** Which cue a hit earned. The single place that decision is made. */
	UFUNCTION(BlueprintPure, Category = "Hit Feedback")
	static EHitFeedbackCue ResolveCue(const FHitFeedbackContext& Context);

	/** How important a cue is. Used to decide what may interrupt or outrank what during pacing. */
	UFUNCTION(BlueprintPure, Category = "Hit Feedback")
	static int32 GetCueRank(EHitFeedbackCue Cue);

	/** The configured cue, or null if this set leaves that one empty (the caller then falls back). */
	const FHitFeedbackCue* FindCue(EHitFeedbackCue Cue) const;

	/** The entry for a surface, or DefaultImpact when the surface is not listed. */
	const FImpactFeedback& FindImpact(EPhysicalSurface Surface) const;
};
