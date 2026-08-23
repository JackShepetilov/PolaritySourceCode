// HitMarkerComponent.h
// Hit marker and kill confirmation feedback system

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Variant_Shooter/Feedback/HitFeedbackSet.h"
#include "HitMarkerComponent.generated.h"

class USoundBase;
class UMaterialParameterCollection;
class APlayerCameraManager;

/**
 * Type of hit for different visual/audio feedback.
 *
 * New entries are appended, never reordered or renamed: tagged property serialization stores the
 * NAME of an enum value, so appending is free while a rename silently resets every saved asset.
 */
UENUM(BlueprintType)
enum class EHitMarkerType : uint8
{
	Normal,			// Regular body hit
	Ionized,		// Zero-damage hit that successfully transferred charge
	Headshot,		// Headshot/critical hit
	Kill,			// Killing blow
	HeadshotKill,	// Headshot that killed
	ShieldHit,		// Landed on a shield that is still holding
	ShieldBreak		// This hit is the one that took the shield down
};

/**
 * Hit marker event data for UI
 */
USTRUCT(BlueprintType)
struct FHitMarkerEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EHitMarkerType HitType = EHitMarkerType::Normal;

	UPROPERTY(BlueprintReadOnly)
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector HitDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bIsKill = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIsHeadshot = false;

	/** The shield was still holding when this shot arrived. */
	UPROPERTY(BlueprintReadOnly)
	bool bIsShieldHit = false;

	/** This shot is the one that took the shield down. True for exactly one hit per shield. */
	UPROPERTY(BlueprintReadOnly)
	bool bIsShieldBreak = false;

	/** Time when this hit occurred (for expiration) */
	float EventTime = 0.0f;
};

/**
 * Hit marker visual settings
 */
USTRUCT(BlueprintType)
struct FHitMarkerSettings
{
	GENERATED_BODY()

	// ==================== Visual ====================

	/** Duration hit marker stays on screen */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float HitMarkerDuration = 0.15f;

	/** Duration for kill marker */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float KillMarkerDuration = 0.4f;

	/** Hit marker size (screen percentage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0.01", ClampMax = "0.1"))
	float HitMarkerSize = 0.03f;

	/** Kill marker size multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float KillMarkerSizeMultiplier = 1.5f;

	/** Normal hit color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor NormalHitColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Ionization confirmation color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor IonizedHitColor = FLinearColor(0.05f, 0.8f, 1.0f, 1.0f);

	/** Headshot hit color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor HeadshotColor = FLinearColor(1.0f, 0.3f, 0.3f, 1.0f);

	/** Kill confirm color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor KillColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	/** Colour for a hit that landed on a still-holding shield. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor ShieldHitColor = FLinearColor(0.6f, 0.85f, 1.0f, 1.0f);

	/** Colour for the hit that took a shield down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor ShieldBreakColor = FLinearColor(0.2f, 1.0f, 1.0f, 1.0f);

	/** Duration for the shield break marker. Longer than an ordinary hit because it is the one
	 *  moment in a fight that changes what the player should do next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float ShieldBreakMarkerDuration = 0.3f;

	// ==================== Audio ====================

	/** Enable hit sounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bEnableHitSounds = true;

	/** Normal hit sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HitSound;

	/** Zero-damage ionization confirmation sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> IonizedHitSound;

	/** Headshot sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HeadshotSound;

	/** Kill confirmation sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> KillSound;

	/** Headshot kill sound (plays instead of regular kill) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HeadshotKillSound;

	/** Hit sound volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HitSoundVolume = 0.5f;

	/** Ionization confirmation sound volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float IonizedHitSoundVolume = 0.5f;

	/** Kill sound volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float KillSoundVolume = 0.8f;

	/** Fallback for the shield break confirmation, used when the firing weapon's UHitFeedbackSet
	 *  leaves that cue empty or the weapon has no set at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> ShieldBreakSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ShieldBreakSoundVolume = 0.9f;

	/** Floor on the gap between two confirmations of equal or lower rank, in seconds, used when the
	 *  firing weapon's set does not set its own. Without it a fast automatic plays a hit sound on
	 *  every bullet and the confirmations blur into a buzz. 0 disables the limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultMinCueInterval = 0.05f;

	// ==================== Screen Effects ====================

	/** Enable screen effects on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Effects")
	bool bEnableScreenEffects = true;

	/** Chromatic aberration intensity on kill */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Effects", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float KillChromaticAberration = 0.5f;

	/** Chromatic aberration duration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Effects", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ChromaticAberrationDuration = 0.15f;

	/** Vignette intensity on kill */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Effects", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KillVignetteIntensity = 0.3f;

	/** Time slowdown on kill (1.0 = no slowdown) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Effects", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float KillTimeSlowdown = 0.9f;

	/** Duration of time slowdown effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Effects", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float TimeSlowdownDuration = 0.05f;

	// ==================== Camera Effects ====================

	/** Enable camera punch on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bEnableCameraEffects = true;

	/** Camera punch intensity on hit confirmation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HitCameraPunch = 0.2f;

	/** Camera punch intensity on kill */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float KillCameraPunch = 0.5f;

	/** Minimum interval between camera punches (seconds). Prevents continuous-fire weapons (laser) from applying punch every frame. 0 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CameraPunchCooldown = 0.1f;
};

// Delegate for UI to bind to
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHitMarkerEvent, const FHitMarkerEvent&, HitEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnKillConfirmed);

/**
 * Component that handles hit marker display and kill confirmation feedback.
 * Provides visual, audio, and screen effects for combat feedback.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UHitMarkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHitMarkerComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Settings ====================

	/** Hit marker settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FHitMarkerSettings Settings;

	// ==================== Events ====================

	/** Called for damaging hits. Legacy Blueprint HUD animation binds here. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnHitMarkerEvent OnHitMarker;

	/** Called only for zero-damage ionization confirmations. Kept separate so legacy normal-hit
	 *  Blueprint animation cannot overwrite the electric marker. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnHitMarkerEvent OnIonizedHitMarker;

	/** Called when a kill is confirmed */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnKillConfirmed OnKillConfirmed;

	/** Called on the shot that takes a target's shield down. Separate from OnHitMarker (which also
	 *  fires for the same hit) so the HUD can show the break without having to inspect the event. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnHitMarkerEvent OnShieldBreakConfirmed;

	// ==================== API ====================

	/**
	 * The one entry point. Everything else here is a convenience wrapper around it.
	 *
	 * Confirmation is a readout for the person who pulled the trigger and for nobody else, so this
	 * plays flat 2D audio and only on the machine that owns this pawn. The world half of the hit --
	 * the impact everyone can hear -- is the weapon's job, not this component's.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hit Marker")
	void RegisterHitFeedback(const FHitFeedbackContext& Context);

	/**
	 * Register a hit on an enemy
	 * @param HitLocation World location of the hit
	 * @param HitDirection Direction of the shot
	 * @param Damage Amount of damage dealt
	 * @param bHeadshot Was this a headshot
	 * @param bKilled Did this kill the target
	 */
	UFUNCTION(BlueprintCallable, Category = "Hit Marker")
	void RegisterHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled);

	/** Register a successful charge transfer that dealt no damage. */
	UFUNCTION(BlueprintCallable, Category = "Hit Marker")
	void RegisterIonizedHit(const FVector& HitLocation, const FVector& HitDirection);

	/**
	 * Register a kill (called separately if kill happens after hit)
	 */
	UFUNCTION(BlueprintCallable, Category = "Hit Marker")
	void RegisterKill();

	/**
	 * Get the current active hit marker (for UI rendering)
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	bool GetActiveHitMarker(FHitMarkerEvent& OutEvent) const;

	/**
	 * Get current hit marker alpha (for fade out)
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	float GetHitMarkerAlpha() const;

	/**
	 * Get current hit marker color (based on type)
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	FLinearColor GetHitMarkerColor() const;

	/**
	 * Get current hit marker size
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	float GetHitMarkerSize() const;

	/**
	 * Check if a hit marker is currently active
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	bool IsHitMarkerActive() const { return bHitMarkerActive; }

	/**
	 * Get chromatic aberration intensity (for post-process)
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	float GetChromaticAberrationIntensity() const { return CurrentChromaticAberration; }

	/**
	 * Get vignette intensity (for post-process)
	 */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	float GetVignetteIntensity() const { return CurrentVignetteIntensity; }

protected:
	// ==================== State ====================

	/** Current active hit event */
	FHitMarkerEvent CurrentHitEvent;

	/** Is hit marker currently showing */
	bool bHitMarkerActive = false;

	/** Time remaining for current hit marker */
	float HitMarkerTimeRemaining = 0.0f;

	/** Current chromatic aberration value */
	float CurrentChromaticAberration = 0.0f;

	/** Current vignette intensity */
	float CurrentVignetteIntensity = 0.0f;

	/** Time remaining for screen effects */
	float ScreenEffectTimeRemaining = 0.0f;

	/** Cached owner controller */
	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	/** World time of last camera punch application (for cooldown) */
	float LastCameraPunchTime = -100.0f;

	// ==================== Pacing state ====================

	/** World time the last confirmation sound actually played. */
	float LastCueTime = -100.0f;

	/** Engine frame the last hit was registered on, whether or not it was heard. Lets a shotgun's
	 *  pellets add up into one number on screen instead of overwriting each other. */
	uint64 LastEventFrame = 0;

	/** Engine frame the last confirmation sound played on. Frames rather than seconds because
	 *  per-frame accumulation asks "was this the same volley", and a shotgun's pellets share a
	 *  frame exactly while their timestamps do not have to. */
	uint64 LastCueFrame = 0;

	/** Rank of the last confirmation that played, so a louder event can still cut through a
	 *  suppression window that a quieter one opened. @see UHitFeedbackSet::GetCueRank */
	int32 LastCueRank = -1;

	/** The set the last registered hit came in with. A kill confirmed separately -- a client
	 *  learning the outcome from replicated health a round trip later -- has no weapon to ask, and
	 *  would otherwise fall back to the generic kill sound for every gun in the game. */
	UPROPERTY()
	TObjectPtr<UHitFeedbackSet> LastFeedbackSet;

	// ==================== Internal ====================

	/** True only on the machine whose player owns this pawn. Confirmation is a readout for one
	 *  person; on a listen server an unguarded 2D sound plays the host every client's hits. */
	bool IsLocalFeedback() const;

	/** Which EHitMarkerType a resolved cue draws as. Visuals are a separate vocabulary from audio
	 *  because the legacy Blueprint HUD binds to the marker type. */
	static EHitMarkerType CueToMarkerType(EHitFeedbackCue Cue);

	/** Decide whether this cue is allowed to be heard right now, and remember it if so. */
	bool ShouldPlayCue(EHitFeedbackCue Cue, const UHitFeedbackSet* Set);

	/** Play a confirmation: the weapon's set first, this component's own sounds as the fallback. */
	void PlayCue(EHitFeedbackCue Cue, const UHitFeedbackSet* Set);

	/** Play hit sound based on type */
	void PlayHitSound(EHitMarkerType HitType);

	/** Apply screen effects */
	void ApplyScreenEffects(EHitMarkerType HitType);

	/** Apply camera effects */
	void ApplyCameraEffects(EHitMarkerType HitType);

	/** Update screen effects (fade out) */
	void UpdateScreenEffects(float DeltaTime);

	/** Apply time dilation effect */
	void ApplyTimeDilation(float TimeDilation, float Duration);
};
