// NPCMeleeStrikeComponent.cpp

#include "Variant_Shooter/AI/NPCMeleeStrikeComponent.h"

#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "AI/PolarityTeams.h"
#include "AI/Components/AIAccuracyComponent.h"
#include "Variant_Shooter/AnimNotify_MeleeWindow.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Bench/FightRecorder.h"

// "Melee: Ready For Next Attack" reached an NPC that is striking. Kept here and not on the component
// so the header did not change for it [author, 2026-10-14]: the component picks it up on its next tick.
namespace NPCMeleeStrikeReady
{
	static TArray<TWeakObjectPtr<UNPCMeleeStrikeComponent>> Pending;

	static bool HasReadyNotify(const UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return false;
		}
		auto HasReady = [](const TArray<FAnimNotifyEvent>& Notifies)
		{
			return Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Event)
			{
				return Event.Notify && Event.Notify->IsA<UAnimNotify_MeleeReady>();
			});
		};
		if (HasReady(Montage->Notifies))
		{
			return true;
		}
		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				if (const UAnimSequenceBase* const Sequence = Segment.GetAnimReference(); Sequence && HasReady(Sequence->Notifies))
				{
					return true;
				}
			}
		}
		return false;
	}

	static bool Take(const UNPCMeleeStrikeComponent* Component)
	{
		Pending.RemoveAll([](const TWeakObjectPtr<UNPCMeleeStrikeComponent>& Entry) { return !Entry.IsValid(); });
		return Pending.RemoveAll([Component](const TWeakObjectPtr<UNPCMeleeStrikeComponent>& Entry) { return Entry.Get() == Component; }) > 0;
	}
}

void NPCMeleeStrike_NotifyReady(AActor* Owner)
{
	UNPCMeleeStrikeComponent* const Strike = Owner ? Owner->FindComponentByClass<UNPCMeleeStrikeComponent>() : nullptr;
	if (Strike && Owner->HasAuthority() && Strike->IsBusy())
	{
		NPCMeleeStrikeReady::Pending.AddUnique(Strike);
	}
}

UNPCMeleeStrikeComponent::UNPCMeleeStrikeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// The author's two for the enemy (2026-10-14): ground and kick.
	StrikeMontages.Add(TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Variant_Shooter/Anims/Melee/ThirdPerson/AM_Melee_TP_Grounded.AM_Melee_TP_Grounded"))));
	StrikeMontages.Add(TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Variant_Shooter/Anims/Melee/ThirdPerson/AM_Melee_TP_Kick.AM_Melee_TP_Kick"))));
}

AShooterNPC* UNPCMeleeStrikeComponent::GetNPC() const
{
	return Cast<AShooterNPC>(GetOwner());
}

// ==================== The fork ====================

bool UNPCMeleeStrikeComponent::WantsToStrike(const AActor* Threat) const
{
	const AShooterNPC* const NPC = GetNPC();
	if (!bEnabled || !NPC || NPC->IsDead() || NPC->IsKnockedDown() || !Threat || !Threat->IsA<APawn>()
		|| StrikeMontages.Num() == 0 || !PolarityTeams::AreHostile(NPC, Threat))
	{
		return false;
	}

	// Far: the run there is the time a reload takes. Back off and reload.
	if (FVector::Dist2D(Threat->GetActorLocation(), NPC->GetActorLocation()) > ChargeDistance)
	{
		return false;
	}

	// Shaken: it does not go in.
	if (const UAIAccuracyComponent* const Accuracy = NPC->GetAccuracyComponent(); Accuracy && Accuracy->GetStress() > MaxStressToCharge)
	{
		return false;
	}

	// Worth it: he cannot shoot back right now, or this one can take more than he can.
	bool bHeIsReloading = false;
	float HisHealth = 100.0f;
	if (const AShooterCharacter* const Player = Cast<AShooterCharacter>(Threat))
	{
		const AShooterWeapon* const Gun = Player->GetCurrentWeapon();
		bHeIsReloading = Gun && Gun->IsReloading();
		HisHealth = Player->GetCurrentHP();
	}
	else if (const AShooterNPC* const Other = Cast<AShooterNPC>(Threat))
	{
		bHeIsReloading = Other->IsReloadingWeapon();
		HisHealth = Other->CurrentHP;
	}

	return bHeIsReloading || NPC->CurrentHP >= HisHealth;
}

bool UNPCMeleeStrikeComponent::JustChoseTheGun() const
{
	const UWorld* const World = GetWorld();
	return World && World->GetTimeSeconds() - ChoseGunTime < 0.1f;
}

// ==================== Going in ====================

void UNPCMeleeStrikeComponent::Begin(AActor* Threat)
{
	AShooterNPC* const NPC = GetNPC();
	UWorld* const World = GetWorld();
	if (!NPC || !World || !Threat || !NPC->HasAuthority())
	{
		return;
	}

	Target = Threat;
	SetComponentTickEnabled(true);

	if (FVector::Dist2D(Threat->GetActorLocation(), NPC->GetActorLocation()) > StrikeStartDistance)
	{
		Phase = EPhase::Charging;
		ChargeStartTime = World->GetTimeSeconds();
		if (AAIController* const AI = Cast<AAIController>(NPC->GetController()))
		{
			AI->MoveToActor(Threat, StrikeStartDistance * 0.5f);
		}
		NPC->SayTactic(TEXT("MELEE: out of rounds, going in"));
		return;
	}

	StartStrike();
}

void UNPCMeleeStrikeComponent::TickCharge()
{
	AShooterNPC* const NPC = GetNPC();
	const AActor* const Threat = Target.Get();
	const UWorld* const World = GetWorld();
	if (!NPC || !World || !Threat || (Threat->IsA<AShooterNPC>() && Cast<AShooterNPC>(Threat)->IsDead()))
	{
		ChooseGun();
		return;
	}

	if (FVector::Dist2D(Threat->GetActorLocation(), NPC->GetActorLocation()) <= StrikeStartDistance)
	{
		StartStrike();
		return;
	}
	if (World->GetTimeSeconds() - ChargeStartTime > ChargeTimeout)
	{
		NPC->SayTactic(TEXT("MELEE: could not get to him, back to the gun"));
		ChooseGun();
	}
}

void UNPCMeleeStrikeComponent::StartStrike()
{
	AShooterNPC* const NPC = GetNPC();
	UWorld* const World = GetWorld();
	UAnimMontage* const Strike = PickStrikeMontage();
	if (!NPC || !World || !Strike)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] %s: no strike montage loaded, back to the gun"), *GetNameSafe(NPC));
		ChooseGun();
		return;
	}

	Phase = EPhase::Striking;
	CurrentStrike = Strike;
	PulledSoFar = 0.0f;
	bWindowOpen = false;
	bHitThisStrike = false;
	ClearTimers();

	if (AAIController* const AI = Cast<AAIController>(NPC->GetController()))
	{
		AI->StopMovement();
		if (AActor* const Threat = Target.Get())
		{
			AI->SetFocus(Threat);
		}
	}

	// The gun is put away under the strike: its holster montage starts at full weight and hands over.
	UAnimMontage* Holster = nullptr;
	if (!bGunAway)
	{
		if (AShooterWeapon* const Gun = NPC->GetWeapon())
		{
			Holster = Gun->GetHolsterMontageTP();
		}
		bGunAway = true;
	}
	NPC->Multicast_PlayMeleeStrike(Holster, Strike, HolsterBlendSeconds);
	NPC->SayTactic(Holster ? TEXT("MELEE: gun away, strike") : TEXT("MELEE: strike again"));

	// The window the montage marks. A montage the author has not marked yet gets one by its length.
	const float Length = Strike->GetPlayLength();
	if (!HasDamageWindowNotify(Strike) && FallbackWindowEnd > FallbackWindowStart)
	{
		World->GetTimerManager().SetTimer(FallbackOpenTimer, this, &UNPCMeleeStrikeComponent::ActivateDamageWindow,
			FMath::Max(Length * FallbackWindowStart, 0.01f), false);
		World->GetTimerManager().SetTimer(FallbackCloseTimer, this, &UNPCMeleeStrikeComponent::DeactivateDamageWindow,
			FMath::Max(Length * FallbackWindowEnd, 0.02f), false);
	}

	// The end. A montage marked "Melee: Ready For Next Attack" decides there (the timer is only the
	// safety net, at its very end); an unmarked one as it starts blending out.
	NPCMeleeStrikeReady::Take(this);
	const bool bMarkedReady = NPCMeleeStrikeReady::HasReadyNotify(Strike);
	const float BlendOut = Strike->GetDefaultBlendOutTime();
	World->GetTimerManager().SetTimer(StrikeEndTimer, this, &UNPCMeleeStrikeComponent::OnStrikeFinished,
		FMath::Max(bMarkedReady ? Length : Length - BlendOut, 0.05f), false);
}

void UNPCMeleeStrikeComponent::TickPull(float DeltaTime)
{
	AShooterNPC* const NPC = GetNPC();
	const AActor* const Threat = Target.Get();
	if (!NPC || !Threat || bHitThisStrike || PulledSoFar >= PullMaxDistance)
	{
		return;
	}

	const FVector From = NPC->GetActorLocation();
	const float Distance = FVector::Dist2D(Threat->GetActorLocation(), From);
	if (Distance <= PullStopDistance)
	{
		return;
	}

	const float Step = FMath::Min3(PullSpeed * DeltaTime, Distance - PullStopDistance, PullMaxDistance - PulledSoFar);
	const FVector Direction = (Threat->GetActorLocation() - From).GetSafeNormal2D();
	NPC->SetActorLocation(From + Direction * Step, /*bSweep*/ true);
	PulledSoFar += Step;
}

// ==================== The hit ====================

void UNPCMeleeStrikeComponent::ActivateDamageWindow()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Phase != EPhase::Striking)
	{
		return;
	}
	bWindowOpen = true;
}

void UNPCMeleeStrikeComponent::DeactivateDamageWindow()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	bWindowOpen = false;
}

void UNPCMeleeStrikeComponent::SweepForHit()
{
	AShooterNPC* const NPC = GetNPC();
	UWorld* const World = GetWorld();
	if (!NPC || !World || bHitThisStrike)
	{
		return;
	}

	const FVector Forward = NPC->GetActorForwardVector().GetSafeNormal2D();
	const FVector Start = NPC->GetActorLocation() + FVector(0.0f, 0.0f, TraceHeight);
	const FVector End = Start + Forward * TraceReach;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NPCMeleeStrike), false, NPC);
	if (AShooterWeapon* const Gun = NPC->GetWeapon())
	{
		Params.AddIgnoredActor(Gun);
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(TraceRadius), Params);

	for (const FHitResult& Hit : Hits)
	{
		AActor* const Struck = Hit.GetActor();
		if (!Struck || !Struck->IsA<APawn>() || !PolarityTeams::AreHostile(NPC, Struck) || !Struck->CanBeDamaged())
		{
			continue;
		}

		bHitThisStrike = true;
		UGameplayStatics::ApplyDamage(Struck, Damage, NPC->GetController(), NPC, UDamageType::StaticClass());

		const FVector Away = (Struck->GetActorLocation() - NPC->GetActorLocation()).GetSafeNormal2D();
		UMeleeAttackComponent::ApplyKnockbackOnAuthority(Struck, Away.IsNearlyZero() ? Forward : Away,
			KnockbackDistance, KnockbackDuration, NPC->GetActorLocation());

		const FString Note = FString::Printf(TEXT("MELEE: hit %s for %.0f"), *Struck->GetName(), Damage);
		UFightRecorderSubsystem::NoteTactic(NPC, FName(*Note));
		UE_LOG(LogTemp, Log, TEXT("[MELEE_DEBUG] %s %s"), *NPC->GetName(), *Note);
		return;
	}
}

// ==================== After a strike ====================

void UNPCMeleeStrikeComponent::OnStrikeFinished()
{
	AShooterNPC* const NPC = GetNPC();
	ClearTimers();
	bWindowOpen = false;
	if (!NPC || NPC->IsDead())
	{
		Phase = EPhase::None;
		SetComponentTickEnabled(false);
		return;
	}

	if (!bHitThisStrike)
	{
		UFightRecorderSubsystem::NoteTactic(NPC, TEXT("MELEE: missed"));
	}

	// The fork again, the magazine still empty: in again with the gun away, or back to it.
	AActor* const Threat = Target.Get();
	if (Threat && WantsToStrike(Threat))
	{
		Begin(Threat);
		return;
	}
	ChooseGun();
}

void UNPCMeleeStrikeComponent::ChooseGun()
{
	AShooterNPC* const NPC = GetNPC();
	ClearTimers();
	Phase = EPhase::None;
	bWindowOpen = false;
	SetComponentTickEnabled(false);
	if (const UWorld* const World = GetWorld())
	{
		ChoseGunTime = World->GetTimeSeconds();
	}
	if (!NPC)
	{
		bGunAway = false;
		return;
	}

	const bool bWasAway = bGunAway;
	bGunAway = false;
	if (bWasAway)
	{
		UAnimMontage* const Draw = NPC->GetWeapon() ? NPC->GetWeapon()->GetDrawMontageTP() : nullptr;
		NPC->Multicast_PlayMeleeDraw(Draw);
	}
	NPC->ReturnToGunAfterMelee();
}

void UNPCMeleeStrikeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	switch (Phase)
	{
	case EPhase::Charging:
		TickCharge();
		break;
	case EPhase::Striking:
		if (NPCMeleeStrikeReady::Take(this))
		{
			OnStrikeFinished();
			break;
		}
		TickPull(DeltaTime);
		if (bWindowOpen)
		{
			SweepForHit();
		}
		break;
	default:
		SetComponentTickEnabled(false);
		break;
	}
}

void UNPCMeleeStrikeComponent::ClearTimers()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallbackOpenTimer);
		World->GetTimerManager().ClearTimer(FallbackCloseTimer);
		World->GetTimerManager().ClearTimer(StrikeEndTimer);
	}
}

void UNPCMeleeStrikeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTimers();
	Super::EndPlay(EndPlayReason);
}

UAnimMontage* UNPCMeleeStrikeComponent::PickStrikeMontage() const
{
	TArray<UAnimMontage*, TInlineAllocator<4>> Loaded;
	for (const TSoftObjectPtr<UAnimMontage>& Soft : StrikeMontages)
	{
		if (UAnimMontage* const Montage = Soft.LoadSynchronous())
		{
			Loaded.Add(Montage);
		}
	}
	return Loaded.Num() > 0 ? Loaded[FMath::RandRange(0, Loaded.Num() - 1)] : nullptr;
}

bool UNPCMeleeStrikeComponent::HasDamageWindowNotify(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return false;
	}
	auto HasWindow = [](const TArray<FAnimNotifyEvent>& Notifies)
	{
		return Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Event)
		{
			return Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAnimNotifyState_MeleeDamageWindow>();
		});
	};

	if (HasWindow(Montage->Notifies))
	{
		return true;
	}
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			if (const UAnimSequenceBase* const Sequence = Segment.GetAnimReference(); Sequence && HasWindow(Sequence->Notifies))
			{
				return true;
			}
		}
	}
	return false;
}
