// HitMarkerComponent.cpp
// Hit marker and kill confirmation feedback system implementation

#include "HitMarkerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Sound/SoundBase.h"

UHitMarkerComponent::UHitMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
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

void UHitMarkerComponent::RegisterHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled)
{
	// Determine hit type
	EHitMarkerType HitType;
	if (bKilled && bHeadshot)
	{
		HitType = EHitMarkerType::HeadshotKill;
	}
	else if (bKilled)
	{
		HitType = EHitMarkerType::Kill;
	}
	else if (bHeadshot)
	{
		HitType = EHitMarkerType::Headshot;
	}
	else
	{
		HitType = EHitMarkerType::Normal;
	}

	// Fill event data
	CurrentHitEvent.HitType = HitType;
	CurrentHitEvent.Damage = Damage;
	CurrentHitEvent.HitLocation = HitLocation;
	CurrentHitEvent.HitDirection = HitDirection;
	CurrentHitEvent.bIsKill = bKilled;
	CurrentHitEvent.bIsHeadshot = bHeadshot;
	CurrentHitEvent.EventTime = GetWorld()->GetTimeSeconds();

	// Set duration based on type
	if (bKilled)
	{
		HitMarkerTimeRemaining = Settings.KillMarkerDuration;
	}
	else
	{
		HitMarkerTimeRemaining = Settings.HitMarkerDuration;
	}

	bHitMarkerActive = true;

	// Broadcast event for UI
	OnHitMarker.Broadcast(CurrentHitEvent);

	if (bKilled)
	{
		OnKillConfirmed.Broadcast();
	}

	// Play sound
	PlayHitSound(HitType);

	// Apply effects
	ApplyScreenEffects(HitType);
	ApplyCameraEffects(HitType);

	UE_LOG(LogTemp, Log, TEXT("HitMarker: Type=%d, Damage=%.1f, Headshot=%d, Kill=%d"),
		(int32)HitType, Damage, bHeadshot, bKilled);
}

void UHitMarkerComponent::RegisterKill()
{
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

	// Play kill sound
	PlayHitSound(CurrentHitEvent.HitType);

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

	// Calculate based on remaining time
	float Duration = CurrentHitEvent.bIsKill ? Settings.KillMarkerDuration : Settings.HitMarkerDuration;
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
