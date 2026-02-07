// MusicPlayerSubsystem.h
// GameInstance subsystem that manages dynamic music playback using Quartz for sample-accurate timing

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "MusicTypes.h"
#include "MusicPlayerSubsystem.generated.h"

class UMusicTrackDataAsset;
class UAudioComponent;
class UQuartzClockHandle;

// Debug log category
DECLARE_LOG_CATEGORY_EXTERN(LogMusicPlayer, Log, All);

/**
 * Delegate broadcast when music state changes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMusicStateChanged, EMusicPlayerState, NewState, FName, CurrentPartID);

/**
 * Delegate broadcast when a new part starts playing
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMusicPartChanged, FName, NewPartID);

/**
 * GameInstance subsystem that handles dynamic music playback.
 * Uses Quartz for sample-accurate gapless transitions between parts.
 * Responds to MusicIntensityBox and MusicExitBox triggers.
 */
UCLASS()
class POLARITY_API UMusicPlayerSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ==================== USubsystem Interface ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== FTickableGameObject Interface ====================

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ==================== Public API ====================

	/**
	 * Start playing a track. Called when player first enters any MIB.
	 * @param Track The track data asset to play
	 * @param bFadeIn If true, fade in from zero (first MIB entry). If false, start at current volume.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void StartTrack(UMusicTrackDataAsset* Track, bool bFadeIn = true);

	/**
	 * Stop the current track with fade out. Called when player enters EMB.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void StopTrack();

	/**
	 * Set whether player is in an intense zone (MIB).
	 * Immediately changes volume; next part selection happens when current part ends.
	 * @param bIntense True if entering MIB, false if exiting
	 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void SetIntenseZone(bool bIntense);

	/**
	 * Called when all enemies in MIB are eliminated.
	 * Equivalent to SetIntenseZone(false).
	 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void OnEnemiesCleared();

	// ==================== Getters ====================

	UFUNCTION(BlueprintPure, Category = "Music")
	EMusicPlayerState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Music")
	bool IsPlaying() const { return CurrentState == EMusicPlayerState::Playing || CurrentState == EMusicPlayerState::FadingIn; }

	UFUNCTION(BlueprintPure, Category = "Music")
	bool IsInIntenseZone() const { return bIsInIntenseZone; }

	UFUNCTION(BlueprintPure, Category = "Music")
	FName GetCurrentPartID() const { return CurrentPartID; }

	UFUNCTION(BlueprintPure, Category = "Music")
	UMusicTrackDataAsset* GetCurrentTrack() const { return CurrentTrack; }

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Music|Events")
	FOnMusicStateChanged OnMusicStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Music|Events")
	FOnMusicPartChanged OnMusicPartChanged;

protected:
	// ==================== State ====================

	UPROPERTY()
	EMusicPlayerState CurrentState = EMusicPlayerState::Stopped;

	UPROPERTY()
	bool bIsInIntenseZone = false;

	UPROPERTY()
	TObjectPtr<UMusicTrackDataAsset> CurrentTrack;

	UPROPERTY()
	FName CurrentPartID;

	// ==================== Quartz ====================

	/** Quartz clock handle for precise timing */
	UPROPERTY()
	TObjectPtr<UQuartzClockHandle> ClockHandle;

	/** Name of our Quartz clock */
	static const FName MusicClockName;

	// ==================== Double-buffered Audio ====================

	/** Primary audio component (currently playing) */
	UPROPERTY()
	TObjectPtr<UAudioComponent> AudioComponentA;

	/** Secondary audio component (for gapless transition) */
	UPROPERTY()
	TObjectPtr<UAudioComponent> AudioComponentB;

	/** Which component is currently active (true = A, false = B) */
	bool bUsingComponentA = true;

	/** Next part that's been scheduled for gapless playback */
	FName ScheduledNextPartID;

	/** Has next part been scheduled? */
	bool bNextPartScheduled = false;

	/** Time remaining in current part (tracked for scheduling) */
	float CurrentPartTimeRemaining = 0.0f;

	/** Total duration of current part */
	float CurrentPartDuration = 0.0f;

	/** How far ahead to schedule next part (seconds) - Quartz needs advance notice */
	static constexpr float SCHEDULE_AHEAD_TIME = 0.5f;

	// ==================== Volume Fading ====================

	/** Current actual volume */
	float CurrentVolume = 1.0f;

	/** Target volume we're fading towards */
	float TargetVolume = 1.0f;

	/** Volume at start of fade */
	float FadeStartVolume = 1.0f;

	/** Total fade duration */
	float FadeDuration = 0.0f;

	/** Time elapsed in current fade */
	float FadeElapsed = 0.0f;

	/** Is volume currently fading? */
	bool bIsFading = false;

	/** Timer handle for stop after fade out */
	FTimerHandle StopTimerHandle;

private:
	// ==================== Quartz Setup ====================

	/** Create Quartz clock if it doesn't exist */
	void EnsureQuartzClock();

	/** Get the Quartz subsystem */
	class UQuartzSubsystem* GetQuartzSubsystem() const;

	// ==================== Audio Components ====================

	/** Create audio components if they don't exist */
	void EnsureAudioComponents();

	/** Get the currently active audio component */
	UAudioComponent* GetActiveComponent() const;

	/** Get the inactive audio component (for preparing next part) */
	UAudioComponent* GetInactiveComponent() const;

	/** Swap active/inactive components */
	void SwapComponents();

	// ==================== Part Playback ====================

	/** Play a specific part by ID on the active component */
	void PlayPart(FName PartID);

	/** Prepare the next part on inactive component (called ahead of time) */
	void PrepareNextPart();

	/** Execute transition to prepared next part (called when time runs out) */
	void ExecutePartTransition();

	/** Choose the next part based on current intensity state */
	FName ChooseNextPart(const FMusicPart* CurrentPart) const;

	// ==================== Volume ====================

	/** Start a volume fade */
	void StartVolumeFade(float NewTargetVolume, float Duration);

	/** Update volume during tick */
	void UpdateVolumeFade(float DeltaTime);

	/** Apply current volume to active component */
	void ApplyVolume();

	/** Calculate target volume based on state */
	float CalculateTargetVolume() const;

	// ==================== State ====================

	/** Set state and broadcast event */
	void SetState(EMusicPlayerState NewState);

	/** Get world for timers (from game instance) */
	UWorld* GetWorldForTimers() const;

	// ==================== Debug ====================

	void LogDebug(const FString& Message) const;
	void LogWarning(const FString& Message) const;
	void LogError(const FString& Message) const;
	FString StateToString(EMusicPlayerState State) const;

	/**
	 * Called when a world is being cleaned up (level transition).
	 * Resets audio components and timing state for the new level.
	 */
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
};
