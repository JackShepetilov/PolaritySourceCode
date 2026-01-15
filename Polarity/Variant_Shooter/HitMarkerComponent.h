// HitMarkerComponent.h
// Hit marker and kill confirmation feedback system

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HitMarkerComponent.generated.h"

class USoundBase;
class UMaterialParameterCollection;
class APlayerCameraManager;

/**
 * Type of hit for different visual/audio feedback
 */
UENUM(BlueprintType)
enum class EHitMarkerType : uint8
{
	Normal,			// Regular body hit
	Headshot,		// Headshot/critical hit  
	Kill,			// Killing blow
	HeadshotKill	// Headshot that killed
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

	/** Headshot hit color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor HeadshotColor = FLinearColor(1.0f, 0.3f, 0.3f, 1.0f);

	/** Kill confirm color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor KillColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	// ==================== Audio ====================

	/** Enable hit sounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bEnableHitSounds = true;

	/** Normal hit sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HitSound;

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

	/** Kill sound volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float KillSoundVolume = 0.8f;

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

	/** Called when a hit is confirmed (for UI) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnHitMarkerEvent OnHitMarker;

	/** Called when a kill is confirmed */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnKillConfirmed OnKillConfirmed;

	// ==================== API ====================

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

	// ==================== Internal ====================

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
