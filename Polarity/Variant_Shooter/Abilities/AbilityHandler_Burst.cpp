// AbilityHandler_Burst.cpp

#include "AbilityHandler_Burst.h"
#include "AbilityComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

void UAbilityHandler_Burst::OnActivate_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::OnActivate"));

	CachedBurstDef = Cast<UAbilityDefinition_Burst>(Definition);
	if (!CachedBurstDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::OnActivate ABORT: Definition is not UAbilityDefinition_Burst"));
		NotifyAbilityCancelled();
		return;
	}

	CachedStats = CachedBurstDef->GetBurstStatsAtLevel(CurrentLevel);
	ShotsFired = 0;

	// Free left hand from weapon IK so the LeftArm slot drives the arm cleanly.
	if (OwningCharacter)
	{
		OwningCharacter->SetLeftHandIKAlpha(0.0f);
	}

	if (CachedBurstDef->CastStartSound)
	{
		UGameplayStatics::PlaySound2D(OwningCharacter ? OwningCharacter->GetWorld() : GetWorld(), CachedBurstDef->CastStartSound);
	}

	EnterCastStart();
}

void UAbilityHandler_Burst::OnCancelRequested_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::OnCancelRequested phase=%d"), (int32)Phase);

	// Clear pending timer so it doesn't fire after cancel.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransitionTimerHandle);
	}

	// Stop whatever montage is currently playing.
	if (CachedBurstDef)
	{
		StopFPMontage(CachedBurstDef->CastStartMontage, 0.1f);
		StopFPMontage(CachedBurstDef->CastFinishMontage, 0.1f);
	}
	if (ActiveLoopMontage)
	{
		StopFPMontage(ActiveLoopMontage, 0.1f);
	}

	ResetToIdle();
	NotifyAbilityCancelled();
}

void UAbilityHandler_Burst::NotifyPerShotFromAnimNotify()
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::PerShotNotify phase=%d ShotsFired=%d"), (int32)Phase, ShotsFired);

	if (Phase != EBurstPhase::Loop)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::PerShotNotify SKIPPED: not in Loop phase"));
		return;
	}

	const float ChargeBefore = GetPlayerChargeModule();
	if (!TryDeductCharge(CachedStats.ChargePerShot))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::PerShotNotify OUT OF CHARGE (%.2f < %.2f) → CastFinish"),
			ChargeBefore, CachedStats.ChargePerShot);
		EnterCastFinish();
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::PerShotNotify deducted %.2f (was %.2f, now %.2f)"),
		CachedStats.ChargePerShot, ChargeBefore, GetPlayerChargeModule());

	if (CachedBurstDef && CachedBurstDef->PerShotSound && OwningCharacter)
	{
		UGameplayStatics::PlaySoundAtLocation(OwningCharacter, CachedBurstDef->PerShotSound, OwningCharacter->GetActorLocation());
	}

	OnPerShotEffect();
}

// ==================== Pipeline ====================

void UAbilityHandler_Burst::EnterCastStart()
{
	Phase = EBurstPhase::CastStart;

	if (!CachedBurstDef->CastStartMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::EnterCastStart: no CastStartMontage, jumping to Loop"));
		EnterLoopFirst();
		return;
	}

	const float Length = PlayFPMontage(CachedBurstDef->CastStartMontage, 1.0f);
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::EnterCastStart: montage='%s' len=%.3f"),
		*CachedBurstDef->CastStartMontage->GetName(), Length);

	// Schedule overlap-into-Loop. Native length, native rate.
	ScheduleOverlapTimer(Length, 1.0f, GET_FUNCTION_NAME_CHECKED(UAbilityHandler_Burst, OnCastStartOverlapTimer));
}

void UAbilityHandler_Burst::OnCastStartOverlapTimer()
{
	if (Phase != EBurstPhase::CastStart)
	{
		return;
	}
	EnterLoopFirst();
}

void UAbilityHandler_Burst::EnterLoopFirst()
{
	Phase = EBurstPhase::Loop;
	PlayNextLoopIteration();
}

void UAbilityHandler_Burst::PlayNextLoopIteration()
{
	if (!CachedBurstDef || CachedStats.NumProjectiles <= 0)
	{
		EnterCastFinish();
		return;
	}
	UAnimMontage* Picked = CachedBurstDef->PickRandomLoopMontage();
	if (!Picked)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::PlayNextLoopIteration: PickRandomLoopMontage returned null → CastFinish"));
		EnterCastFinish();
		return;
	}

	// PlayRate so EffectiveDuration = TimePerShot + Overlap → stagger between consecutive
	// notifies = TimePerShot. (EffectiveDuration = NativeLength / PlayRate.)
	const float TimePerShot = FMath::Max(KINDA_SMALL_NUMBER, CachedStats.CastDuration / CachedStats.NumProjectiles);
	const float NativeLen = Picked->GetPlayLength();
	const float TargetEffectiveDuration = TimePerShot + MontageOverlap;
	const float PlayRate = (TargetEffectiveDuration > KINDA_SMALL_NUMBER && NativeLen > KINDA_SMALL_NUMBER)
		? (NativeLen / TargetEffectiveDuration)
		: 1.0f;

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::Loop[%d/%d] montage='%s' nativeLen=%.3f TimePerShot=%.3f PlayRate=%.3f"),
		ShotsFired + 1, CachedStats.NumProjectiles, *Picked->GetName(), NativeLen, TimePerShot, PlayRate);

	ActiveLoopMontage = Picked;
	PlayFPMontage(Picked, PlayRate);

	ScheduleOverlapTimer(NativeLen, PlayRate, GET_FUNCTION_NAME_CHECKED(UAbilityHandler_Burst, OnLoopOverlapTimer));
}

void UAbilityHandler_Burst::OnLoopOverlapTimer()
{
	if (Phase != EBurstPhase::Loop)
	{
		return;
	}

	++ShotsFired;
	if (ShotsFired >= CachedStats.NumProjectiles)
	{
		EnterCastFinish();
		return;
	}
	PlayNextLoopIteration();
}

void UAbilityHandler_Burst::EnterCastFinish()
{
	Phase = EBurstPhase::CastFinish;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransitionTimerHandle);
	}

	if (!CachedBurstDef || !CachedBurstDef->CastFinishMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::EnterCastFinish: no CastFinishMontage → complete immediately"));
		ResetToIdle();
		NotifyAbilityComplete();
		return;
	}

	const float Length = PlayFPMontage(CachedBurstDef->CastFinishMontage, 1.0f);
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::EnterCastFinish: montage='%s' len=%.3f"),
		*CachedBurstDef->CastFinishMontage->GetName(), Length);

	// If the finish montage failed to play (no AnimInstance, length 0, etc.) the end delegate
	// below would never fire and the cast would deadlock with bIsCasting stuck true. Complete now.
	if (Length <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::EnterCastFinish: montage failed to play (len<=0) -> complete immediately"));
		ResetToIdle();
		NotifyAbilityComplete();
		return;
	}

	// Use natural end for the final montage (nothing to crossfade into).
	BindFPMontageEnd(CachedBurstDef->CastFinishMontage,
		GET_FUNCTION_NAME_CHECKED(UAbilityHandler_Burst, OnCastFinishMontageEnded));

	// Deadlock safety net. The whole cast's completion hinges on this single montage end
	// delegate. If it never fires -- e.g. a weapon swap / mesh re-init replaces the FP
	// AnimInstance while CastFinish is playing, or the montage is force-stopped without an end
	// callback -- bIsCasting would stay true forever (firing / ability / weapon-swap all locked).
	// Schedule a fallback that force-completes shortly after the montage's expected end. Reuses
	// TransitionTimerHandle (idle during CastFinish); the normal OnCastFinishMontageEnded path
	// runs ResetToIdle() which clears this timer, so the fallback only fires if the delegate didn't.
	if (UWorld* World = GetWorld())
	{
		const float FallbackDelay = Length + 0.5f;
		TWeakObjectPtr<UAbilityHandler_Burst> WeakThis(this);
		FTimerDelegate FallbackDelegate = FTimerDelegate::CreateLambda([WeakThis]()
		{
			UAbilityHandler_Burst* Self = WeakThis.Get();
			if (Self && Self->Phase == EBurstPhase::CastFinish)
			{
				UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::CastFinish watchdog fired -- end delegate never came, force-completing"));
				Self->ResetToIdle();
				Self->NotifyAbilityComplete();
			}
		});
		World->GetTimerManager().SetTimer(TransitionTimerHandle, FallbackDelegate, FallbackDelay, false);
	}
}

void UAbilityHandler_Burst::OnCastFinishMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Burst::OnCastFinishMontageEnded interrupted=%d"), bInterrupted);

	if (Phase != EBurstPhase::CastFinish)
	{
		return;
	}
	ResetToIdle();
	if (bInterrupted)
	{
		NotifyAbilityCancelled();
	}
	else
	{
		NotifyAbilityComplete();
	}
}

void UAbilityHandler_Burst::ResetToIdle()
{
	Phase = EBurstPhase::Idle;
	ShotsFired = 0;
	ActiveLoopMontage = nullptr;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransitionTimerHandle);
	}

	if (OwningCharacter)
	{
		OwningCharacter->SetLeftHandIKAlpha(1.0f);
	}
}

void UAbilityHandler_Burst::ScheduleOverlapTimer(float MontageNativeLength, float PlayRate, FName CallbackName)
{
	if (!OwningCharacter)
	{
		return;
	}
	UWorld* World = OwningCharacter->GetWorld();
	if (!World)
	{
		return;
	}

	const float Effective = (PlayRate > KINDA_SMALL_NUMBER) ? (MontageNativeLength / PlayRate) : MontageNativeLength;
	const float Overlap = FMath::Min(MontageOverlap, Effective * 0.5f);
	const float Delay = FMath::Max(0.05f, Effective - Overlap);

	World->GetTimerManager().ClearTimer(TransitionTimerHandle);

	FTimerDelegate Delegate;
	Delegate.BindUFunction(this, CallbackName);
	World->GetTimerManager().SetTimer(TransitionTimerHandle, Delegate, Delay, false);
}
