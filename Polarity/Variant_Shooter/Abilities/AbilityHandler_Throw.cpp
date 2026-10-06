// AbilityHandler_Throw.cpp

#include "AbilityHandler_Throw.h"
#include "AbilityDefinition_Throw.h"
#include "AbilityComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void UAbilityHandler_Throw::OnActivate_Implementation()
{
	CachedThrowDef = Cast<UAbilityDefinition_Throw>(Definition);
	if (!CachedThrowDef || !OwningCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Throw::OnActivate ABORT: definition is not UAbilityDefinition_Throw"));
		NotifyAbilityCancelled();
		return;
	}

	bButtonHeld = true;
	bThrown = false;

	// Both hands leave the weapon for the throw.
	OwningCharacter->SetLeftHandIKAlpha(0.0f);

	if (CachedThrowDef->CastStartSound)
	{
		UGameplayStatics::PlaySoundAtLocation(OwningCharacter, CachedThrowDef->CastStartSound,
			OwningCharacter->GetActorLocation());
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Throw::OnActivate '%s'"), *GetNameSafe(CachedThrowDef));
	EnterStart();
}

void UAbilityHandler_Throw::OnButtonReleased_Implementation()
{
	if (!bButtonHeld)
	{
		return;
	}
	bButtonHeld = false;

	if (Phase == EThrowPhase::AimLoop)
	{
		EnterThrow();
		return;
	}

	if (Phase == EThrowPhase::Start && CachedThrowDef && CachedThrowDef->AimStartMontage)
	{
		// A tap: hurry what is left of the wind-up instead of cutting it.
		UAnimInstance* AnimInst = GetFPAnimInstance();
		UAnimMontage* Start = CachedThrowDef->AimStartMontage;
		if (AnimInst && AnimInst->Montage_IsPlaying(Start))
		{
			const float Rate = FMath::Max(0.1f, CachedThrowDef->QuickStartPlayRate);
			const float Left = FMath::Max(0.0f, Start->GetPlayLength() - AnimInst->Montage_GetPosition(Start));
			AnimInst->Montage_SetPlayRate(Start, Rate);
			SchedulePhaseTimer(Left / Rate - CachedThrowDef->MontageOverlap, &UAbilityHandler_Throw::OnStartFinished);
		}
	}
}

void UAbilityHandler_Throw::OnCancelRequested_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Throw::OnCancelRequested phase=%d thrown=%d"), (int32)Phase, bThrown ? 1 : 0);

	if (CachedThrowDef)
	{
		StopFPMontage(CachedThrowDef->AimStartMontage, 0.1f);
		StopFPMontage(CachedThrowDef->AimLoopMontage, 0.1f);
		StopFPMontage(CachedThrowDef->ThrowMontage, 0.1f);
	}

	// Cancelled after the payload left (downed mid-follow-through): the throw happened, so it costs.
	const bool bAlreadyThrown = bThrown;
	ResetToIdle();
	if (bAlreadyThrown)
	{
		NotifyAbilityComplete();
	}
	else
	{
		NotifyAbilityCancelled();
	}
}

// ==================== Pipeline ====================

void UAbilityHandler_Throw::EnterStart()
{
	Phase = EThrowPhase::Start;

	const float Length = PlayFPMontage(CachedThrowDef->AimStartMontage, 1.0f);
	if (Length <= 0.0f)
	{
		// No wind-up authored, or nothing to play it on. The throw still has to happen.
		OnStartFinished();
		return;
	}
	SchedulePhaseTimer(Length - CachedThrowDef->MontageOverlap, &UAbilityHandler_Throw::OnStartFinished);
}

void UAbilityHandler_Throw::OnStartFinished()
{
	if (Phase != EThrowPhase::Start)
	{
		return;
	}

	if (bButtonHeld)
	{
		EnterAimLoop();
	}
	else
	{
		EnterThrow();
	}
}

void UAbilityHandler_Throw::EnterAimLoop()
{
	Phase = EThrowPhase::AimLoop;

	// Played once and told to follow itself, so it wraps inside the montage with no blend. Replaying
	// it on a timer blended the pose into its own first frame every lap, which read as a hitch.
	UAnimMontage* Loop = CachedThrowDef->AimLoopMontage;
	if (PlayFPMontage(Loop, 1.0f) > 0.0f && Loop->CompositeSections.Num() > 0)
	{
		if (UAnimInstance* AnimInst = GetFPAnimInstance())
		{
			const FName Section = Loop->GetSectionName(0);
			AnimInst->Montage_SetNextSection(Section, Section, Loop);
		}
	}
	// With nothing to hold on, the release is still what throws; just wait for it.
}

void UAbilityHandler_Throw::EnterThrow()
{
	Phase = EThrowPhase::Throw;

	UWorld* World = OwningCharacter ? OwningCharacter->GetWorld() : nullptr;
	if (!World)
	{
		ReleasePayload();
		FinishThrow();
		return;
	}
	World->GetTimerManager().ClearTimer(PhaseTimer);

	const float Rate = FMath::Max(0.1f, CachedThrowDef->ThrowPlayRate);
	const float Length = PlayFPMontage(CachedThrowDef->ThrowMontage, Rate);
	if (Length <= 0.0f)
	{
		ReleasePayload();
		FinishThrow();
		return;
	}

	const float Duration = Length / Rate;
	const float ReleaseAt = FMath::Clamp(CachedThrowDef->ThrowReleaseTime / Rate, 0.0f, Duration);
	if (ReleaseAt <= KINDA_SMALL_NUMBER)
	{
		// SetTimer drops a zero delay without a word.
		ReleasePayload();
	}
	else
	{
		World->GetTimerManager().SetTimer(ReleaseTimer, this, &UAbilityHandler_Throw::ReleasePayload, ReleaseAt, false);
	}

	SchedulePhaseTimer(Duration, &UAbilityHandler_Throw::FinishThrow);
}

void UAbilityHandler_Throw::ReleasePayload()
{
	if (bThrown || !CachedThrowDef || !OwningCharacter)
	{
		return;
	}
	bThrown = true;

	FVector Location;
	FRotator Rotation;
	CachedThrowDef->GetThrowStart(OwningCharacter, Location, Rotation);

	if (CachedThrowDef->ThrowSound)
	{
		UGameplayStatics::PlaySoundAtLocation(OwningCharacter, CachedThrowDef->ThrowSound, Location);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Throw: '%s' released at %s facing %s"),
		*GetNameSafe(CachedThrowDef), *Location.ToCompactString(), *Rotation.ToCompactString());

	SpawnThrown(Location, Rotation);
}

void UAbilityHandler_Throw::FinishThrow()
{
	if (Phase != EThrowPhase::Throw)
	{
		return;
	}

	// The montage ending before its own release time would otherwise throw nothing.
	ReleasePayload();

	ResetToIdle();
	NotifyAbilityComplete();
}

void UAbilityHandler_Throw::ResetToIdle()
{
	Phase = EThrowPhase::Idle;
	bButtonHeld = false;

	if (OwningCharacter)
	{
		if (UWorld* World = OwningCharacter->GetWorld())
		{
			World->GetTimerManager().ClearTimer(PhaseTimer);
			World->GetTimerManager().ClearTimer(ReleaseTimer);
		}
		OwningCharacter->SetLeftHandIKAlpha(1.0f);
	}
}

void UAbilityHandler_Throw::SchedulePhaseTimer(float SecondsLeft, void (UAbilityHandler_Throw::*Callback)())
{
	UWorld* World = OwningCharacter ? OwningCharacter->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	World->GetTimerManager().SetTimer(PhaseTimer, this, Callback, FMath::Max(0.05f, SecondsLeft), false);
}

// ==================== Preview ====================

void UAbilityHandler_Throw::DrawAimPreview(const AShooterCharacter* Caster, const UAbilityDefinition_Throw* Def)
{
	const UWorld* World = Caster ? Caster->GetWorld() : nullptr;
	if (!World || !Def)
	{
		return;
	}

	TArray<FVector> Points;
	FHitResult Hit;
	const bool bLands = Def->PredictThrowPath(Caster, Points, Hit);

	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		DrawDebugLine(World, Points[Index - 1], Points[Index], FColor::Cyan, false, 0.0f, 0, 2.0f);
	}
	if (bLands)
	{
		DrawDebugSphere(World, Hit.ImpactPoint, 20.0f, 12, FColor::Cyan, false, 0.0f, 0, 1.5f);
	}
}
