// MusicPlayerSubsystem.cpp

#include "MusicPlayerSubsystem.h"
#include "MusicTrackDataAsset.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "AudioDevice.h"
#include "Quartz/QuartzSubsystem.h"

DEFINE_LOG_CATEGORY(LogMusicPlayer);

const FName UMusicPlayerSubsystem::MusicClockName = FName("MusicPlayerClock");

void UMusicPlayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LogDebug(TEXT("MusicPlayerSubsystem initialized"));
}

void UMusicPlayerSubsystem::Deinitialize()
{
	// Stop any playing music
	if (AudioComponentA)
	{
		AudioComponentA->Stop();
		AudioComponentA->DestroyComponent();
		AudioComponentA = nullptr;
	}

	if (AudioComponentB)
	{
		AudioComponentB->Stop();
		AudioComponentB->DestroyComponent();
		AudioComponentB = nullptr;
	}

	// Delete Quartz clock
	if (ClockHandle)
	{
		UWorld* World = GetWorldForTimers();
		if (World)
		{
			if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
			{
				QuartzSubsystem->DeleteClockByName(World, MusicClockName);
			}
		}
		ClockHandle = nullptr;
	}

	// Clear timer
	if (UWorld* World = GetWorldForTimers())
	{
		World->GetTimerManager().ClearTimer(StopTimerHandle);
	}

	LogDebug(TEXT("MusicPlayerSubsystem deinitialized"));
	Super::Deinitialize();
}

// ==================== FTickableGameObject ====================

void UMusicPlayerSubsystem::Tick(float DeltaTime)
{
	// Update volume fading
	if (bIsFading)
	{
		UpdateVolumeFade(DeltaTime);
	}

	// Track time remaining and schedule transitions
	if (IsPlaying() && CurrentPartDuration > 0.0f)
	{
		CurrentPartTimeRemaining -= DeltaTime;

		// Prepare next part ahead of time
		if (!bNextPartScheduled && CurrentPartTimeRemaining <= SCHEDULE_AHEAD_TIME && CurrentPartTimeRemaining > 0.0f)
		{
			PrepareNextPart();
		}

		// Transition when time runs out
		if (CurrentPartTimeRemaining <= 0.0f && bNextPartScheduled)
		{
			ExecutePartTransition();
		}
	}
}

TStatId UMusicPlayerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMusicPlayerSubsystem, STATGROUP_Tickables);
}

// ==================== Quartz ====================

UQuartzSubsystem* UMusicPlayerSubsystem::GetQuartzSubsystem() const
{
	UWorld* World = GetWorldForTimers();
	if (!World)
	{
		return nullptr;
	}
	return UQuartzSubsystem::Get(World);
}

void UMusicPlayerSubsystem::EnsureQuartzClock()
{
	if (ClockHandle)
	{
		return;
	}

	UWorld* World = GetWorldForTimers();
	if (!World)
	{
		LogError(TEXT("Cannot create Quartz clock - no world available"));
		return;
	}

	UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem();
	if (!QuartzSubsystem)
	{
		LogError(TEXT("Cannot create Quartz clock - no Quartz subsystem"));
		return;
	}

	// Check if clock already exists
	if (QuartzSubsystem->DoesClockExist(World, MusicClockName))
	{
		ClockHandle = QuartzSubsystem->GetHandleForClock(World, MusicClockName);
		LogDebug(TEXT("Retrieved existing Quartz clock"));
		return;
	}

	// Create clock settings - we use time-based scheduling, not beat-based
	FQuartzClockSettings ClockSettings;
	ClockSettings.TimeSignature.NumBeats = 4;
	ClockSettings.TimeSignature.BeatType = EQuartzTimeSignatureQuantization::QuarterNote;

	// Create the clock
	ClockHandle = QuartzSubsystem->CreateNewClock(World, MusicClockName, ClockSettings);

	if (ClockHandle)
	{
		LogDebug(TEXT("Created Quartz clock for music playback"));
	}
	else
	{
		LogError(TEXT("Failed to create Quartz clock"));
	}
}

// ==================== Public API ====================

void UMusicPlayerSubsystem::StartTrack(UMusicTrackDataAsset* Track, bool bFadeIn)
{
	if (!Track)
	{
		LogError(TEXT("StartTrack called with null track"));
		return;
	}

	if (!Track->IsValid())
	{
		LogError(FString::Printf(TEXT("StartTrack called with invalid track: %s"), *Track->TrackName));
		return;
	}

	// If already playing this track, just update intensity
	if (CurrentTrack == Track && IsPlaying())
	{
		LogDebug(FString::Printf(TEXT("Track '%s' already playing, ignoring StartTrack"), *Track->TrackName));
		return;
	}

	// Stop current track if any (without fade, we'll fade the new one)
	if (AudioComponentA && AudioComponentA->IsPlaying())
	{
		AudioComponentA->Stop();
	}
	if (AudioComponentB && AudioComponentB->IsPlaying())
	{
		AudioComponentB->Stop();
	}

	CurrentTrack = Track;
	bIsInIntenseZone = true; // Starting means we're in MIB
	bNextPartScheduled = false;
	ScheduledNextPartID = NAME_None;

	LogDebug(FString::Printf(TEXT("=== Starting track: %s ==="), *Track->TrackName));
	LogDebug(FString::Printf(TEXT("  FadeIn: %s"), bFadeIn ? TEXT("YES") : TEXT("NO")));
	LogDebug(FString::Printf(TEXT("  StartPart: %s"), *Track->DefaultStartPart.ToString()));

	// Ensure Quartz clock exists
	EnsureQuartzClock();

	// Start the clock if not running
	if (ClockHandle)
	{
		UWorld* World = GetWorldForTimers();
		if (World && !ClockHandle->IsClockRunning(World))
		{
			UQuartzClockHandle* ClockHandlePtr = ClockHandle.Get();
			ClockHandle->StartClock(World, ClockHandlePtr);
			LogDebug(TEXT("Started Quartz clock"));
		}
	}

	// Start playing the default part
	if (bFadeIn)
	{
		SetState(EMusicPlayerState::FadingIn);

		// Start at zero volume, fade to target
		CurrentVolume = 0.0f;
		ApplyVolume();

		PlayPart(Track->DefaultStartPart);

		// Start fade in
		StartVolumeFade(CalculateTargetVolume(), Track->FadeInDuration);
	}
	else
	{
		SetState(EMusicPlayerState::Playing);
		CurrentVolume = CalculateTargetVolume();
		PlayPart(Track->DefaultStartPart);
		ApplyVolume();
	}
}

void UMusicPlayerSubsystem::StopTrack()
{
	if (CurrentState == EMusicPlayerState::Stopped)
	{
		LogDebug(TEXT("StopTrack called but already stopped"));
		return;
	}

	if (CurrentState == EMusicPlayerState::FadingOut)
	{
		LogDebug(TEXT("StopTrack called but already fading out"));
		return;
	}

	LogDebug(TEXT("=== Stopping track (fade out) ==="));

	SetState(EMusicPlayerState::FadingOut);

	// Fade out to zero
	float FadeOutDuration = CurrentTrack ? CurrentTrack->FadeOutDuration : 2.0f;
	StartVolumeFade(0.0f, FadeOutDuration);

	// Set timer to fully stop after fade
	if (UWorld* World = GetWorldForTimers())
	{
		World->GetTimerManager().SetTimer(
			StopTimerHandle,
			[this]()
			{
				if (AudioComponentA)
				{
					AudioComponentA->Stop();
				}
				if (AudioComponentB)
				{
					AudioComponentB->Stop();
				}

				// Stop the Quartz clock
				if (ClockHandle)
				{
					UWorld* World = GetWorldForTimers();
					if (World)
					{
						UQuartzClockHandle* ClockHandlePtr = ClockHandle.Get();
						ClockHandle->StopClock(World, true, ClockHandlePtr);
					}
				}

				CurrentTrack = nullptr;
				CurrentPartID = NAME_None;
				bIsInIntenseZone = false;
				bNextPartScheduled = false;
				ScheduledNextPartID = NAME_None;
				CurrentPartDuration = 0.0f;
				CurrentPartTimeRemaining = 0.0f;
				SetState(EMusicPlayerState::Stopped);
				LogDebug(TEXT("Track fully stopped after fade out"));
			},
			FadeOutDuration,
			false
		);
	}
}

void UMusicPlayerSubsystem::SetIntenseZone(bool bIntense)
{
	if (bIsInIntenseZone == bIntense)
	{
		return;
	}

	bIsInIntenseZone = bIntense;

	LogDebug(FString::Printf(TEXT("IntenseZone changed to: %s"), bIntense ? TEXT("INTENSE") : TEXT("CALM")));

	// Start fade to new target volume if playing
	if (IsPlaying() && CurrentTrack)
	{
		float NewTarget = CalculateTargetVolume();
		StartVolumeFade(NewTarget, CurrentTrack->IntensityChangeDuration);
	}
}

void UMusicPlayerSubsystem::OnEnemiesCleared()
{
	LogDebug(TEXT("Enemies cleared - switching to calm mode"));
	SetIntenseZone(false);
}

// ==================== Audio Components ====================

void UMusicPlayerSubsystem::EnsureAudioComponents()
{
	UWorld* World = GetWorldForTimers();
	if (!World)
	{
		LogError(TEXT("Cannot create AudioComponents - no world available"));
		return;
	}

	auto CreateComponent = [World](const TCHAR* Name) -> UAudioComponent*
	{
		UAudioComponent* AC = NewObject<UAudioComponent>(World);
		if (AC)
		{
			AC->bAutoActivate = false;
			AC->bAutoDestroy = false;
			AC->bIsUISound = true; // 2D, ignores listener position
			AC->RegisterComponent();
		}
		return AC;
	};

	if (!AudioComponentA)
	{
		AudioComponentA = CreateComponent(TEXT("MusicComponentA"));
		LogDebug(TEXT("AudioComponentA created"));
	}

	if (!AudioComponentB)
	{
		AudioComponentB = CreateComponent(TEXT("MusicComponentB"));
		LogDebug(TEXT("AudioComponentB created"));
	}
}

UAudioComponent* UMusicPlayerSubsystem::GetActiveComponent() const
{
	return bUsingComponentA ? AudioComponentA.Get() : AudioComponentB.Get();
}

UAudioComponent* UMusicPlayerSubsystem::GetInactiveComponent() const
{
	return bUsingComponentA ? AudioComponentB.Get() : AudioComponentA.Get();
}

void UMusicPlayerSubsystem::SwapComponents()
{
	bUsingComponentA = !bUsingComponentA;
}

// ==================== Part Playback ====================

void UMusicPlayerSubsystem::PlayPart(FName PartID)
{
	if (!CurrentTrack)
	{
		LogError(TEXT("PlayPart called with no current track"));
		return;
	}

	const FMusicPart* Part = CurrentTrack->FindPart(PartID);
	if (!Part)
	{
		LogError(FString::Printf(TEXT("Part '%s' not found in track '%s'"),
			*PartID.ToString(), *CurrentTrack->TrackName));
		return;
	}

	if (!Part->Sound)
	{
		LogError(FString::Printf(TEXT("Part '%s' has no sound assigned"), *PartID.ToString()));
		return;
	}

	EnsureAudioComponents();

	UAudioComponent* AC = GetActiveComponent();
	if (!AC)
	{
		return;
	}

	// Stop if playing
	if (AC->IsPlaying())
	{
		AC->Stop();
	}

	CurrentPartID = PartID;
	CurrentPartDuration = Part->Sound->GetDuration();
	CurrentPartTimeRemaining = CurrentPartDuration;
	bNextPartScheduled = false;
	ScheduledNextPartID = NAME_None;

	// Set the sound and play
	AC->SetSound(Part->Sound);
	AC->SetVolumeMultiplier(CurrentVolume);
	AC->Play();

	LogDebug(FString::Printf(TEXT("Now playing part: %s (Volume: %.2f, Duration: %.1fs)"),
		*PartID.ToString(),
		Part->Volume,
		CurrentPartDuration));

	OnMusicPartChanged.Broadcast(PartID);
}

void UMusicPlayerSubsystem::PrepareNextPart()
{
	if (!CurrentTrack || bNextPartScheduled)
	{
		return;
	}

	const FMusicPart* CurrentPart = CurrentTrack->FindPart(CurrentPartID);
	if (!CurrentPart)
	{
		LogError(FString::Printf(TEXT("Current part '%s' not found for preparation"), *CurrentPartID.ToString()));
		return;
	}

	// Choose next part based on current intensity
	FName NextPartID = ChooseNextPart(CurrentPart);
	if (NextPartID.IsNone())
	{
		LogWarning(FString::Printf(TEXT("No next part found after '%s'"), *CurrentPartID.ToString()));
		ScheduledNextPartID = NAME_None;
		bNextPartScheduled = true;
		return;
	}

	const FMusicPart* NextPart = CurrentTrack->FindPart(NextPartID);
	if (!NextPart || !NextPart->Sound)
	{
		LogError(FString::Printf(TEXT("Next part '%s' invalid"), *NextPartID.ToString()));
		ScheduledNextPartID = NAME_None;
		bNextPartScheduled = true;
		return;
	}

	EnsureAudioComponents();

	// Prepare sound on inactive component
	UAudioComponent* InactiveAC = GetInactiveComponent();
	if (!InactiveAC)
	{
		LogError(TEXT("No inactive audio component for preparation"));
		return;
	}

	// Set sound but don't play yet
	InactiveAC->SetSound(NextPart->Sound);
	InactiveAC->SetVolumeMultiplier(CurrentVolume);

	ScheduledNextPartID = NextPartID;
	bNextPartScheduled = true;

	LogDebug(FString::Printf(TEXT("Prepared next part: %s (TimeRemaining: %.3fs)"),
		*NextPartID.ToString(),
		CurrentPartTimeRemaining));
}

void UMusicPlayerSubsystem::ExecutePartTransition()
{
	// Don't continue if stopped or fading out
	if (CurrentState == EMusicPlayerState::Stopped || CurrentState == EMusicPlayerState::FadingOut)
	{
		LogDebug(TEXT("Transition skipped - stopped or fading out"));
		return;
	}

	// If we were fading in, now we're playing
	if (CurrentState == EMusicPlayerState::FadingIn)
	{
		SetState(EMusicPlayerState::Playing);
	}

	LogDebug(FString::Printf(TEXT("Part '%s' finished"), *CurrentPartID.ToString()));

	// Check if we have a prepared part
	if (ScheduledNextPartID.IsNone())
	{
		LogWarning(TEXT("No prepared part - stopping track"));
		StopTrack();
		return;
	}

	EnsureAudioComponents();

	// Get the prepared inactive component
	UAudioComponent* InactiveAC = GetInactiveComponent();
	UAudioComponent* ActiveAC = GetActiveComponent();

	if (!InactiveAC)
	{
		LogError(TEXT("No inactive component for transition"));
		return;
	}

	// Start the new part FIRST (before stopping old one for minimal gap)
	InactiveAC->SetVolumeMultiplier(CurrentVolume);
	InactiveAC->Play();

	// Stop the old part immediately after starting new one
	// The slight overlap helps mask any buffer-boundary gaps
	if (ActiveAC && ActiveAC->IsPlaying())
	{
		ActiveAC->Stop();
	}

	// Swap components
	SwapComponents();

	// Update state
	const FMusicPart* NextPart = CurrentTrack ? CurrentTrack->FindPart(ScheduledNextPartID) : nullptr;
	CurrentPartID = ScheduledNextPartID;
	CurrentPartDuration = NextPart && NextPart->Sound ? NextPart->Sound->GetDuration() : 0.0f;
	CurrentPartTimeRemaining = CurrentPartDuration;
	bNextPartScheduled = false;
	ScheduledNextPartID = NAME_None;

	LogDebug(FString::Printf(TEXT("Transitioned to part: %s (Duration: %.1fs)"),
		*CurrentPartID.ToString(),
		CurrentPartDuration));

	OnMusicPartChanged.Broadcast(CurrentPartID);
}

FName UMusicPlayerSubsystem::ChooseNextPart(const FMusicPart* CurrentPart) const
{
	if (!CurrentPart)
	{
		return NAME_None;
	}

	const TArray<FName>* PartsArray = nullptr;

	if (bIsInIntenseZone)
	{
		// Use intense parts
		PartsArray = &CurrentPart->NextPartsIntense;
	}
	else
	{
		// Use calm parts, or fall back to intense if calm is empty
		if (CurrentPart->NextPartsCalm.Num() > 0)
		{
			PartsArray = &CurrentPart->NextPartsCalm;
		}
		else
		{
			PartsArray = &CurrentPart->NextPartsIntense;
			LogDebug(TEXT("NextPartsCalm is empty, using NextPartsIntense as fallback"));
		}
	}

	if (!PartsArray || PartsArray->Num() == 0)
	{
		return NAME_None;
	}

	// Random selection
	int32 Index = FMath::RandRange(0, PartsArray->Num() - 1);
	return (*PartsArray)[Index];
}

// ==================== Volume Fading ====================

void UMusicPlayerSubsystem::StartVolumeFade(float NewTargetVolume, float Duration)
{
	FadeStartVolume = CurrentVolume;
	TargetVolume = NewTargetVolume;
	FadeDuration = Duration;
	FadeElapsed = 0.0f;

	if (Duration <= 0.0f)
	{
		// Immediate
		CurrentVolume = TargetVolume;
		bIsFading = false;
		ApplyVolume();
	}
	else
	{
		bIsFading = true;
	}

	LogDebug(FString::Printf(TEXT("StartVolumeFade: %.2f -> %.2f over %.2fs"),
		FadeStartVolume, TargetVolume, Duration));
}

void UMusicPlayerSubsystem::UpdateVolumeFade(float DeltaTime)
{
	if (!bIsFading)
	{
		return;
	}

	FadeElapsed += DeltaTime;

	if (FadeElapsed >= FadeDuration)
	{
		// Fade complete
		CurrentVolume = TargetVolume;
		bIsFading = false;
	}
	else
	{
		// Interpolate
		float Alpha = FadeElapsed / FadeDuration;
		CurrentVolume = FMath::Lerp(FadeStartVolume, TargetVolume, Alpha);
	}

	ApplyVolume();
}

void UMusicPlayerSubsystem::ApplyVolume()
{
	// Apply to both components (only active one is playing, but keeps them in sync)
	if (AudioComponentA)
	{
		AudioComponentA->SetVolumeMultiplier(CurrentVolume);
	}
	if (AudioComponentB)
	{
		AudioComponentB->SetVolumeMultiplier(CurrentVolume);
	}
}

float UMusicPlayerSubsystem::CalculateTargetVolume() const
{
	if (!CurrentTrack)
	{
		return 1.0f;
	}

	// Get base volume multiplier based on intensity
	float ZoneMultiplier = bIsInIntenseZone ? CurrentTrack->IntenseVolumeMultiplier : CurrentTrack->CalmVolumeMultiplier;

	// Get part-specific volume
	float PartVolume = 1.0f;
	const FMusicPart* Part = CurrentTrack->FindPart(CurrentPartID);
	if (Part)
	{
		PartVolume = Part->Volume;
	}

	return ZoneMultiplier * PartVolume;
}

// ==================== State ====================

void UMusicPlayerSubsystem::SetState(EMusicPlayerState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	EMusicPlayerState OldState = CurrentState;
	CurrentState = NewState;

	LogDebug(FString::Printf(TEXT("State: %s -> %s"),
		*StateToString(OldState), *StateToString(NewState)));

	OnMusicStateChanged.Broadcast(NewState, CurrentPartID);
}

UWorld* UMusicPlayerSubsystem::GetWorldForTimers() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetWorld() : nullptr;
}

// ==================== Debug ====================

void UMusicPlayerSubsystem::LogDebug(const FString& Message) const
{
	UE_LOG(LogMusicPlayer, Log, TEXT("[MusicPlayer] %s"), *Message);
}

void UMusicPlayerSubsystem::LogWarning(const FString& Message) const
{
	UE_LOG(LogMusicPlayer, Warning, TEXT("[MusicPlayer] %s"), *Message);
}

void UMusicPlayerSubsystem::LogError(const FString& Message) const
{
	UE_LOG(LogMusicPlayer, Error, TEXT("[MusicPlayer] %s"), *Message);
}

FString UMusicPlayerSubsystem::StateToString(EMusicPlayerState State) const
{
	switch (State)
	{
	case EMusicPlayerState::Stopped:   return TEXT("Stopped");
	case EMusicPlayerState::Playing:   return TEXT("Playing");
	case EMusicPlayerState::FadingIn:  return TEXT("FadingIn");
	case EMusicPlayerState::FadingOut: return TEXT("FadingOut");
	default:                           return TEXT("Unknown");
	}
}
