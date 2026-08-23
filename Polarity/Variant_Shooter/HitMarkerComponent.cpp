// HitMarkerComponent.cpp
// Hit marker and kill confirmation feedback system implementation

#include "HitMarkerComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UHitMarkerComponent::UHitMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	static ConstructorHelpers::FObjectFinder<USoundBase> IonizedSoundFinder(
		TEXT("/Game/SFX/universfield-new-notification-08-352461.universfield-new-notification-08-352461"));
	if (IonizedSoundFinder.Succeeded())
	{
		Settings.IonizedHitSound = IonizedSoundFinder.Object;
	}
}

void UHitMarkerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache owner controller
	if (AActor* Owner = GetOwner())
	{
		if (APawn* Pawn = Cast<APawn>(Owner))
		{
			OwnerController = Cast<APlayerController>(Pawn->GetController());
		}
	}
}

void UHitMarkerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update hit marker timer
	if (bHitMarkerActive)
	{
		HitMarkerTimeRemaining -= DeltaTime;
		if (HitMarkerTimeRemaining <= 0.0f)
		{
			bHitMarkerActive = false;
			HitMarkerTimeRemaining = 0.0f;
		}
	}

	// Update screen effects
	UpdateScreenEffects(DeltaTime);
}

// ==================== API ====================

void UHitMarkerComponent::RegisterHitFeedback(const FHitFeedbackContext& Context)
{
	// Confirmation belongs to the person who pulled the trigger and to nobody else. Without this
	// guard a listen server host hears -- and gets a camera punch from -- every hit every client
	// lands, because their pawns all live in the host's world too.
	if (!IsLocalFeedback())
	{
		return;
	}

	UHitFeedbackSet* Set = Context.FeedbackSet;
	if (Set)
	{
		LastFeedbackSet = Set;
	}

	const EHitFeedbackCue Cue = UHitFeedbackSet::ResolveCue(Context);

	// What you HEAR and what you SEE are two vocabularies, and they do not have to agree.
	//
	// A held shield absorbs the damage completely, so charging one up is both "I hit a shield" and
	// "I transferred charge". The ear wants the first, because that is what the shot landed on. The
	// HUD wants the second, because the electric marker is what tells the player the meter moved.
	// Deriving the visual from the audio cue would have silently retired that marker.
	EHitMarkerType HitType = CueToMarkerType(Cue);
	if (Context.bZeroDamage && !Context.bKilled && !Context.bShieldBroken)
	{
		HitType = EHitMarkerType::Ionized;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// Another pellet of the same volley: it adds to the number already on screen instead of
	// replacing it, so a shotgun reads as one hit for its full damage rather than eight small ones.
	const bool bSameVolley = Set && Set->bAccumulatePerFrame
		&& bHitMarkerActive && GFrameCounter == LastEventFrame;

	CurrentHitEvent.HitType = HitType;
	CurrentHitEvent.Damage = bSameVolley ? CurrentHitEvent.Damage + Context.Damage : Context.Damage;
	CurrentHitEvent.HitLocation = Context.HitLocation;
	CurrentHitEvent.HitDirection = Context.HitDirection;
	CurrentHitEvent.bIsKill = Context.bKilled;
	CurrentHitEvent.bIsHeadshot = Context.bHeadshot;
	CurrentHitEvent.bIsShieldHit = Context.bShieldHit;
	CurrentHitEvent.bIsShieldBreak = Context.bShieldBroken;
	CurrentHitEvent.EventTime = Now;

	// A marker that says something bigger stays up longer.
	if (Context.bKilled)
	{
		HitMarkerTimeRemaining = Settings.KillMarkerDuration;
	}
	else if (Context.bShieldBroken)
	{
		HitMarkerTimeRemaining = Settings.ShieldBreakMarkerDuration;
	}
	else
	{
		HitMarkerTimeRemaining = Settings.HitMarkerDuration;
	}

	bHitMarkerActive = true;
	LastEventFrame = GFrameCounter;

	// Two delegates, not one: the legacy Blueprint HUD animation is bound to OnHitMarker and would
	// otherwise overwrite the electric ionization marker with an ordinary one.
	if (HitType == EHitMarkerType::Ionized)
	{
		OnIonizedHitMarker.Broadcast(CurrentHitEvent);
	}
	else
	{
		OnHitMarker.Broadcast(CurrentHitEvent);
	}

	if (Context.bShieldBroken)
	{
		OnShieldBreakConfirmed.Broadcast(CurrentHitEvent);
	}

	if (Context.bKilled)
	{
		OnKillConfirmed.Broadcast();
	}

	PlayCue(Cue, Set);

	// The screen and the camera answer to the same pacing as the sound: a shotgun that punched the
	// camera once per pellet shook it eight times for one trigger pull.
	if (!bSameVolley)
	{
		ApplyScreenEffects(HitType);
		ApplyCameraEffects(HitType);
	}
}

void UHitMarkerComponent::RegisterHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled)
{
	FHitFeedbackContext Context;
	Context.HitLocation = HitLocation;
	Context.HitDirection = HitDirection;
	Context.Damage = Damage;
	Context.bHeadshot = bHeadshot;
	Context.bKilled = bKilled;

	RegisterHitFeedback(Context);
}

void UHitMarkerComponent::RegisterIonizedHit(const FVector& HitLocation, const FVector& HitDirection)
{
	FHitFeedbackContext Context;
	Context.HitLocation = HitLocation;
	Context.HitDirection = HitDirection;
	Context.bZeroDamage = true;

	RegisterHitFeedback(Context);
}

void UHitMarkerComponent::RegisterKill()
{
	// Same rule as every other confirmation: it is a readout for one player.
	if (!IsLocalFeedback())
	{
		return;
	}

	// Upgrade current hit to kill if active
	if (bHitMarkerActive)
	{
		if (CurrentHitEvent.bIsHeadshot)
		{
			CurrentHitEvent.HitType = EHitMarkerType::HeadshotKill;
		}
		else
		{
			CurrentHitEvent.HitType = EHitMarkerType::Kill;
		}
		CurrentHitEvent.bIsKill = true;

		// Extend duration
		HitMarkerTimeRemaining = Settings.KillMarkerDuration;
	}
	else
	{
		// Create new kill event
		CurrentHitEvent.HitType = EHitMarkerType::Kill;
		CurrentHitEvent.bIsKill = true;
		CurrentHitEvent.EventTime = GetWorld()->GetTimeSeconds();
		HitMarkerTimeRemaining = Settings.KillMarkerDuration;
		bHitMarkerActive = true;
	}

	OnKillConfirmed.Broadcast();

	// Play kill sound. Through the pacing so a kill arriving right behind the hit that caused it
	// does not double up, and so the weapon's own kill cue is used when there is one.
	const EHitFeedbackCue KillCue = CurrentHitEvent.bIsHeadshot
		? EHitFeedbackCue::HeadshotKill : EHitFeedbackCue::Kill;
	PlayCue(KillCue, LastFeedbackSet);

	// Apply kill effects
	ApplyScreenEffects(CurrentHitEvent.HitType);
	ApplyCameraEffects(CurrentHitEvent.HitType);
}

bool UHitMarkerComponent::GetActiveHitMarker(FHitMarkerEvent& OutEvent) const
{
	if (bHitMarkerActive)
	{
		OutEvent = CurrentHitEvent;
		return true;
	}
	return false;
}

float UHitMarkerComponent::GetHitMarkerAlpha() const
{
	if (!bHitMarkerActive)
	{
		return 0.0f;
	}

	// Calculate based on remaining time. Same three-way choice RegisterHitFeedback made when it set
	// the timer, or the fade would be measured against a duration the marker never had.
	float Duration = Settings.HitMarkerDuration;
	if (CurrentHitEvent.bIsKill)
	{
		Duration = Settings.KillMarkerDuration;
	}
	else if (CurrentHitEvent.bIsShieldBreak)
	{
		Duration = Settings.ShieldBreakMarkerDuration;
	}
	if (Duration <= 0.0f) return 0.0f;

	// Quick fade in, slow fade out
	float Progress = HitMarkerTimeRemaining / Duration;

	// First 20% of duration: full alpha
	// Remaining 80%: fade out
	if (Progress > 0.8f)
	{
		return 1.0f;
	}
	else
	{
		return Progress / 0.8f;
	}
}

FLinearColor UHitMarkerComponent::GetHitMarkerColor() const
{
	switch (CurrentHitEvent.HitType)
	{
	case EHitMarkerType::HeadshotKill:
	case EHitMarkerType::Kill:
		return Settings.KillColor;

	case EHitMarkerType::Headshot:
		return Settings.HeadshotColor;

	case EHitMarkerType::ShieldBreak:
		return Settings.ShieldBreakColor;

	case EHitMarkerType::ShieldHit:
		return Settings.ShieldHitColor;

	case EHitMarkerType::Ionized:
		return Settings.IonizedHitColor;

	case EHitMarkerType::Normal:
	default:
		return Settings.NormalHitColor;
	}
}

float UHitMarkerComponent::GetHitMarkerSize() const
{
	float BaseSize = Settings.HitMarkerSize;

	if (CurrentHitEvent.bIsKill)
	{
		BaseSize *= Settings.KillMarkerSizeMultiplier;
	}

	// Slight pulse effect based on alpha
	float Alpha = GetHitMarkerAlpha();
	float Pulse = 1.0f + (1.0f - Alpha) * 0.2f; // Grows slightly as it fades

	return BaseSize * Pulse;
}

// ==================== Internal ====================

bool UHitMarkerComponent::IsLocalFeedback() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	// Not a pawn at all: an upgrade or a test actor carrying the component. Nothing to disambiguate
	// there, so keep the pre-coop behaviour rather than falling silent.
	if (!OwnerPawn)
	{
		return true;
	}

	return OwnerPawn->IsLocallyControlled();
}

EHitMarkerType UHitMarkerComponent::CueToMarkerType(EHitFeedbackCue Cue)
{
	switch (Cue)
	{
	case EHitFeedbackCue::HeadshotKill:	return EHitMarkerType::HeadshotKill;
	case EHitFeedbackCue::Kill:			return EHitMarkerType::Kill;
	case EHitFeedbackCue::ShieldBreak:	return EHitMarkerType::ShieldBreak;
	case EHitFeedbackCue::Headshot:		return EHitMarkerType::Headshot;
	case EHitFeedbackCue::HitShield:	return EHitMarkerType::ShieldHit;
	case EHitFeedbackCue::ZeroDamage:	return EHitMarkerType::Ionized;
	case EHitFeedbackCue::HitFlesh:
	default:							return EHitMarkerType::Normal;
	}
}

bool UHitMarkerComponent::ShouldPlayCue(EHitFeedbackCue Cue, const UHitFeedbackSet* Set)
{
	const int32 Rank = UHitFeedbackSet::GetCueRank(Cue);
	if (Rank < 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// A set that exists speaks for itself, including when it deliberately asks for no limit at all.
	// Only a weapon with no set falls back on the component's own default.
	const float MinInterval = Set ? Set->MinCueInterval : Settings.DefaultMinCueInterval;

	const bool bSameFrame = Set && Set->bAccumulatePerFrame && GFrameCounter == LastCueFrame;
	const bool bWithinInterval = (MinInterval > 0.0f) && ((Now - LastCueTime) < MinInterval);

	// Suppression only holds against something no more important than what is already sounding.
	// A pellet that kills still gets heard through the pellet that merely connected before it.
	if ((bSameFrame || bWithinInterval) && Rank <= LastCueRank)
	{
		return false;
	}

	LastCueTime = Now;
	LastCueFrame = GFrameCounter;
	LastCueRank = Rank;
	return true;
}

void UHitMarkerComponent::PlayCue(EHitFeedbackCue Cue, const UHitFeedbackSet* Set)
{
	if (!Settings.bEnableHitSounds)
	{
		return;
	}

	if (!ShouldPlayCue(Cue, Set))
	{
		return;
	}

	// The firing weapon's own voice first: a shotgun and a pistol are not supposed to confirm the
	// same way.
	if (Set)
	{
		if (const FHitFeedbackCue* Configured = Set->FindCue(Cue))
		{
			const float Pitch = FMath::FRandRange(
				FMath::Min(Configured->PitchMin, Configured->PitchMax),
				FMath::Max(Configured->PitchMin, Configured->PitchMax));

			UGameplayStatics::PlaySound2D(this, Configured->Sound, Configured->Volume, Pitch);
			return;
		}
	}

	// No set, or this set leaves that cue empty: whatever this component played before sets existed.
	PlayHitSound(CueToMarkerType(Cue));
}

void UHitMarkerComponent::PlayHitSound(EHitMarkerType HitType)
{
	if (!Settings.bEnableHitSounds)
	{
		return;
	}

	USoundBase* SoundToPlay = nullptr;
	float Volume = Settings.HitSoundVolume;

	switch (HitType)
	{
	case EHitMarkerType::HeadshotKill:
		SoundToPlay = Settings.HeadshotKillSound ? Settings.HeadshotKillSound : Settings.KillSound;
		Volume = Settings.KillSoundVolume;
		break;

	case EHitMarkerType::Kill:
		SoundToPlay = Settings.KillSound;
		Volume = Settings.KillSoundVolume;
		break;

	case EHitMarkerType::Headshot:
		SoundToPlay = Settings.HeadshotSound ? Settings.HeadshotSound : Settings.HitSound;
		break;

	case EHitMarkerType::ShieldBreak:
		SoundToPlay = Settings.ShieldBreakSound ? Settings.ShieldBreakSound : Settings.HitSound;
		Volume = Settings.ShieldBreakSound ? Settings.ShieldBreakSoundVolume : Settings.HitSoundVolume;
		break;

	case EHitMarkerType::Ionized:
		SoundToPlay = Settings.IonizedHitSound;
		Volume = Settings.IonizedHitSoundVolume;
		break;

	// A shield hit with no set to distinguish it falls back on the ordinary hit sound rather than
	// on silence: the difference between shield and flesh is the set's job, not this fallback's.
	case EHitMarkerType::ShieldHit:
	case EHitMarkerType::Normal:
	default:
		SoundToPlay = Settings.HitSound;
		break;
	}

	if (SoundToPlay)
	{
		UGameplayStatics::PlaySound2D(this, SoundToPlay, Volume);
	}
}

void UHitMarkerComponent::ApplyScreenEffects(EHitMarkerType HitType)
{
	if (!Settings.bEnableScreenEffects)
	{
		return;
	}

	bool bIsKill = (HitType == EHitMarkerType::Kill || HitType == EHitMarkerType::HeadshotKill);

	if (bIsKill)
	{
		// Set chromatic aberration
		CurrentChromaticAberration = Settings.KillChromaticAberration;
		CurrentVignetteIntensity = Settings.KillVignetteIntensity;
		ScreenEffectTimeRemaining = Settings.ChromaticAberrationDuration;

		// Apply time dilation for kill emphasis
		if (Settings.KillTimeSlowdown < 1.0f)
		{
			ApplyTimeDilation(Settings.KillTimeSlowdown, Settings.TimeSlowdownDuration);
		}
	}
	else if (HitType == EHitMarkerType::Headshot)
	{
		// Lighter effect for headshots
		CurrentChromaticAberration = Settings.KillChromaticAberration * 0.3f;
		CurrentVignetteIntensity = Settings.KillVignetteIntensity * 0.2f;
		ScreenEffectTimeRemaining = Settings.ChromaticAberrationDuration * 0.5f;
	}
}

void UHitMarkerComponent::ApplyCameraEffects(EHitMarkerType HitType)
{
	if (!Settings.bEnableCameraEffects || !OwnerController)
	{
		return;
	}

	// Cooldown check — prevents continuous-fire weapons (laser) from applying punch every frame
	if (Settings.CameraPunchCooldown > 0.0f && GetWorld())
	{
		const float CurrentTime = GetWorld()->GetTimeSeconds();
		if (CurrentTime - LastCameraPunchTime < Settings.CameraPunchCooldown)
		{
			return;
		}
		LastCameraPunchTime = CurrentTime;
	}

	// Get camera manager for camera shake
	APlayerCameraManager* CameraManager = OwnerController->PlayerCameraManager;
	if (!CameraManager)
	{
		return;
	}

	// Determine intensity based on hit type
	float PunchIntensity = 0.0f;

	switch (HitType)
	{
	case EHitMarkerType::HeadshotKill:
		PunchIntensity = Settings.KillCameraPunch * 1.2f;
		break;

	case EHitMarkerType::Kill:
		PunchIntensity = Settings.KillCameraPunch;
		break;

	case EHitMarkerType::Headshot:
		PunchIntensity = Settings.HitCameraPunch * 1.5f;
		break;

	case EHitMarkerType::Ionized:
		// Confirmation only: charge transfer deals no damage and should not punch the camera.
		PunchIntensity = 0.0f;
		break;

	case EHitMarkerType::Normal:
	default:
		PunchIntensity = Settings.HitCameraPunch;
		break;
	}

	if (PunchIntensity > 0.0f)
	{
		// Apply as a small pitch/roll kick
		// Negative = slight upward kick on hit confirm (satisfying feeling)
		OwnerController->AddPitchInput(-PunchIntensity * 0.5f);
		OwnerController->AddYawInput(FMath::RandRange(-PunchIntensity, PunchIntensity) * 0.3f);
	}
}

void UHitMarkerComponent::UpdateScreenEffects(float DeltaTime)
{
	if (ScreenEffectTimeRemaining > 0.0f)
	{
		ScreenEffectTimeRemaining -= DeltaTime;

		// Calculate fade
		float FadeAlpha = FMath::Clamp(ScreenEffectTimeRemaining / Settings.ChromaticAberrationDuration, 0.0f, 1.0f);

		// Apply easing for smooth fade out
		FadeAlpha = FMath::Pow(FadeAlpha, 2.0f); // Quadratic ease out

		CurrentChromaticAberration *= FadeAlpha;
		CurrentVignetteIntensity *= FadeAlpha;

		if (ScreenEffectTimeRemaining <= 0.0f)
		{
			CurrentChromaticAberration = 0.0f;
			CurrentVignetteIntensity = 0.0f;
		}
	}
}

void UHitMarkerComponent::ApplyTimeDilation(float TimeDilation, float Duration)
{
	if (!OwnerController)
	{
		return;
	}

	// Apply global time dilation
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), TimeDilation);

	// Schedule reset
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(
		TimerHandle,
		[this]()
		{
			UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.0f);
		},
		Duration / TimeDilation, // Adjust for time dilation
		false
	);
}
