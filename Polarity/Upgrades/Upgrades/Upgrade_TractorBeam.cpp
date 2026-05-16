// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_TractorBeam.h"
#include "UpgradeDefinition_TractorBeam.h"
#include "ShooterNPC.h"
#include "HumanoidNPC.h"
#include "SniperTurretNPC.h"
#include "EMFVelocityModifier.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UUpgrade_TractorBeam::UUpgrade_TractorBeam()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_TractorBeam::OnUpgradeActivated()
{
	DefTractorBeam = Cast<UUpgradeDefinition_TractorBeam>(UpgradeDefinition);
	if (!DefTractorBeam.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[TRACTOR_BEAM] UpgradeDefinition is not UUpgradeDefinition_TractorBeam!"));
	}
}

bool UUpgrade_TractorBeam::IsNPCBeingPulled(const AShooterNPC* NPC) const
{
	if (!NPC)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const double* Expiry = ActivePullExpiry.Find(TWeakObjectPtr<AShooterNPC>(const_cast<AShooterNPC*>(NPC)));
	if (!Expiry)
	{
		return false;
	}
	return World->GetTimeSeconds() < *Expiry;
}

void UUpgrade_TractorBeam::MarkNPCPulled(AShooterNPC* NPC)
{
	if (!NPC || !DefTractorBeam.IsValid())
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double ExpiryTime = World->GetTimeSeconds() + DefTractorBeam->ComboWindow;
	ActivePullExpiry.Add(NPC, ExpiryTime);
	NPC->SetBeingPulledByTractorBeam(true);
}

void UUpgrade_TractorBeam::OnHitscanIonized(AActor* Target)
{
	if (!DefTractorBeam.IsValid() || !Target)
	{
		return;
	}

	// Sniper turret immune per design spec
	if (Target->IsA<ASniperTurretNPC>())
	{
		return;
	}

	AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC)
	{
		return;
	}

	AActor* Player = GetOwner();
	if (!Player)
	{
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector NPCLoc = NPC->GetActorLocation();
	const FVector ToPlayer = PlayerLoc - NPCLoc;
	const float DistSq = ToPlayer.SizeSquared();

	// Already on top of the player — nothing to pull
	if (DistSq < 1.0f)
	{
		return;
	}

	const FTractorBeamLevelData& Data = DefTractorBeam->GetLevelData(CurrentLevel);

	// Max-distance gate
	if (Data.MaxPullDistance > 0.0f)
	{
		const float MaxDistSq = Data.MaxPullDistance * Data.MaxPullDistance;
		if (DistSq > MaxDistSq)
		{
			return;
		}
	}

	// Cinematic pull is one-shot — if already in flight on this NPC, ignore the hit entirely.
	if (NPC->IsInCinematicPull())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[TRACTOR_BEAM] %s SKIPPED: cinematic pull already in flight"), *NPC->GetName());
		return;
	}

	// Resolve the NPC's "you're in capture range now" handoff distance from the
	// charge-based capture range (curve evaluated at current player/NPC charges).
	float CaptureHandoffDist = 0.0f;
	if (UEMFVelocityModifier* NPCMod = NPC->FindComponentByClass<UEMFVelocityModifier>())
	{
		CaptureHandoffDist = FMath::Max(0.0f, NPCMod->GetEffectiveCaptureRange() - Data.CaptureRangeBuffer);
	}

	const float CurrentDist = FMath::Sqrt(DistSq);

	// ==================== Lv2 CINEMATIC PATH ====================
	// Trigger when the NPC is ALREADY inside the capture-handoff zone. The pull becomes a one-shot
	// curve-driven drag onto AbsoluteMinDistance from the player, with its own duration.
	// Only applies on Lv2 (bStopAtCaptureRange == false). Skipped if already too close.
	if (!DefTractorBeam->bClassicMode
		&& !Data.bStopAtCaptureRange
		&& CaptureHandoffDist > 0.0f
		&& CurrentDist < CaptureHandoffDist
		&& CurrentDist > Data.AbsoluteMinDistance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TRACTOR_BEAM] %s CINEMATIC: dist=%.0f, target=%.0f, dur=%.2f"),
			*NPC->GetName(), CurrentDist, Data.AbsoluteMinDistance, Data.CinematicPullDuration);

		NPC->ApplyCinematicPull(Player, Data.AbsoluteMinDistance,
			Data.CinematicPullDuration, Data.CinematicPullCurve);
		MarkNPCPulled(NPC);
		return;
	}

	// ==================== Standard Tractor pull ====================
	// Compute pull-stop distance. EASTER EGG: bClassicMode bypasses the gate entirely.
	float MinPullDistance = 0.0f;
	if (!DefTractorBeam->bClassicMode)
	{
		if (Data.bStopAtCaptureRange)
		{
			// Lv1: stop pull when target enters the NPC's BASE capture range, minus a buffer.
			MinPullDistance = CaptureHandoffDist;
		}
		else
		{
			// Lv2 fallback (NPC still outside capture-handoff zone OR already at AbsoluteMinDistance).
			MinPullDistance = Data.AbsoluteMinDistance;
		}

		if (MinPullDistance > 0.0f && DistSq < MinPullDistance * MinPullDistance)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TRACTOR_BEAM] %s SKIPPED: dist=%.0f < MinPull=%.0f (Lv%d, mode=%s)"),
				*NPC->GetName(), CurrentDist, MinPullDistance, CurrentLevel,
				Data.bStopAtCaptureRange ? TEXT("CaptureRange") : TEXT("Absolute"));
			return;
		}
	}

	// Clamp pull distance so NPC stops AT MinPullDistance instead of overshooting past it.
	const float HeadroomToMinDist = FMath::Max(0.0f, CurrentDist - MinPullDistance);
	const float EffectivePullDist = FMath::Min(Data.PullDistance, HeadroomToMinDist);

	if (EffectivePullDist < 1.0f)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[TRACTOR_BEAM] %s at stop-distance (dist=%.0f, MinPull=%.0f)"),
			*NPC->GetName(), CurrentDist, MinPullDistance);
		return;
	}

	const FVector PullDirection = ToPlayer.GetSafeNormal();

	UE_LOG(LogTemp, Warning, TEXT("[TRACTOR_BEAM] %s PULL: dist=%.0f, MinPull=%.0f, EffPull=%.0f (Lv%d, Classic=%d)"),
		*NPC->GetName(), CurrentDist, MinPullDistance, EffectivePullDist, CurrentLevel, DefTractorBeam->bClassicMode);

	// ApplyKnockback with Tractor style — works on regular NPCs AND humanoids (HumanoidNPC's
	// override now forwards Tractor to base instead of no-op'ing). Plays CapturedMontage,
	// suppresses wall/NPC collision damage during the pull.
	NPC->ApplyKnockback(PullDirection, EffectivePullDist, Data.PullDuration, PlayerLoc,
		/*bKeepEMFEnabled=*/ false, EKnockbackStyle::Tractor);

	// Track this NPC for combo-window bonus damage/knockback.
	MarkNPCPulled(NPC);
}

void UUpgrade_TractorBeam::OnOwnerDealtDamage(AActor* Target, float /*Damage*/, bool bKilled)
{
	if (!bKilled || !Target)
	{
		return;
	}
	AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC)
	{
		return;
	}
	if (IsNPCBeingPulled(NPC))
	{
		// Mark the kill so death-attribution / scoring code can branch on tractor-combo specifically.
		NPC->SetKilledByTractorBeamCombo(true);
	}
	// Drop the entry — NPC is dead, no further combo damage applies.
	ActivePullExpiry.Remove(NPC);
}

float UUpgrade_TractorBeam::GetMeleeDamageMultiplier(AActor* Target) const
{
	if (!DefTractorBeam.IsValid())
	{
		return 1.0f;
	}
	const AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TRACTOR_BEAM] GetMeleeDmg: target not AShooterNPC (%s)"),
			Target ? *Target->GetName() : TEXT("null"));
		return 1.0f;
	}
	const bool bPulled = IsNPCBeingPulled(NPC);
	const float Mult = bPulled ? DefTractorBeam->GetLevelData(CurrentLevel).MeleeDamageMultiplier : 1.0f;
	UE_LOG(LogTemp, Warning, TEXT("[TRACTOR_BEAM] GetMeleeDmg %s: Pulled=%d, Lv=%d, Mult=%.2f (LevelDataNum=%d)"),
		*NPC->GetName(), bPulled, CurrentLevel, Mult, DefTractorBeam->LevelData.Num());
	return Mult;
}

float UUpgrade_TractorBeam::GetMeleeKnockbackDistanceMultiplier(AActor* Target) const
{
	if (!DefTractorBeam.IsValid())
	{
		return 1.0f;
	}
	const AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC || !IsNPCBeingPulled(NPC))
	{
		return 1.0f;
	}
	const float Mult = DefTractorBeam->GetLevelData(CurrentLevel).MeleeKnockbackMultiplier;
	UE_LOG(LogTemp, Warning, TEXT("[TRACTOR_BEAM] GetMeleeKnock %s: Lv=%d, Mult=%.2f"),
		*NPC->GetName(), CurrentLevel, Mult);
	return Mult;
}
