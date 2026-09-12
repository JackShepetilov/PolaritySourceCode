// SmokeVisionSubsystem.cpp

#include "SmokeVisionSubsystem.h"
#include "Variant_Shooter/Abilities/SmokeCloud.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void USmokeVisionSubsystem::RegisterCloud(ASmokeCloud* Cloud)
{
	if (Cloud)
	{
		Clouds.AddUnique(Cloud);
		UpdateSweepTimer();
	}
}

void USmokeVisionSubsystem::UnregisterCloud(ASmokeCloud* Cloud)
{
	Clouds.RemoveAll([Cloud](const TWeakObjectPtr<ASmokeCloud>& Held)
	{
		return !Held.IsValid() || Held.Get() == Cloud;
	});
	UpdateSweepTimer();
}

void USmokeVisionSubsystem::UpdateSweepTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	const bool bWanted = Clouds.Num() > 0;
	const bool bRunning = Timers.IsTimerActive(SweepTimer);

	if (bWanted && !bRunning)
	{
		Timers.SetTimer(SweepTimer, this, &USmokeVisionSubsystem::RefreshAllOccupants,
			FMath::Max(0.05f, OccupantRefreshInterval), true);
	}
	else if (!bWanted && bRunning)
	{
		Timers.ClearTimer(SweepTimer);
	}
}

void USmokeVisionSubsystem::RefreshAllOccupants()
{
	UWorld* World = GetWorld();
	if (!World || Clouds.Num() == 0)
	{
		return;
	}

	// Enemies are the server's business, and so is the slow they are given.
	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	TArray<AShooterNPC*> Candidates;

	// The coordinator already keeps the list. Walking the level instead would cost thousands of
	// actor visits several times a second for the whole life of the smoke.
	if (const AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(World))
	{
		const TArray<FRegisteredNPCData>& Registered = Coordinator->GetRegisteredNPCs();
		Candidates.Reserve(Registered.Num());
		for (const FRegisteredNPCData& Entry : Registered)
		{
			if (AShooterNPC* NPC = Cast<AShooterNPC>(Entry.NPC.Get()))
			{
				if (!NPC->IsDead())
				{
					Candidates.Add(NPC);
				}
			}
		}
	}
	else
	{
		// No coordinator on this map -- a bare test level. Correctness over speed, and say so once
		// rather than doing it silently.
		UE_LOG(LogTemp, Verbose, TEXT("[SMOKE] No combat coordinator; falling back to a level sweep"));
		for (TActorIterator<AShooterNPC> It(World); It; ++It)
		{
			if (AShooterNPC* NPC = *It)
			{
				if (!NPC->IsDead())
				{
					Candidates.Add(NPC);
				}
			}
		}
	}

	for (const TWeakObjectPtr<ASmokeCloud>& Held : Clouds)
	{
		if (ASmokeCloud* Cloud = Held.Get())
		{
			Cloud->RefreshOccupantsFrom(Candidates);
		}
	}
}

void USmokeVisionSubsystem::SetSightPenetration(float Centimetres)
{
	SightPenetration = FMath::Max(1.0f, Centimetres);
}

namespace
{
	/** How flat a cloud is: 1.0 is a ball, 0.5 is half as tall as it is wide.
	 *
	 *  MUST match the Non Uniform Scale on ShapeLocation in NS_SmokeCloud. Two numbers describing one
	 *  shape is a trap, and the only reason it is a constant here rather than a property on the
	 *  ability is that a property needs a header change and a full rebuild. @see ASmokeCloud
	 *  TODO: promote to UAbilityDefinition_SmokeScreen::CloudHeightScale on the next rebuild. */
	constexpr float SmokeHeightScale = 0.25f;
}

float USmokeVisionSubsystem::SegmentInsideSphere(const FVector& A, const FVector& B,
	const FVector& Centre, float Radius)
{
	if (Radius <= 0.0f)
	{
		return 0.0f;
	}

	// The cloud is a squashed ball, so the test is done in a space where it is round again: stretch
	// height by 1/scale and the ellipsoid becomes the sphere the maths below already solves. The
	// stretch is linear, so the t values it produces are the same t values along the ORIGINAL
	// segment -- which is why the length at the end is measured with the original direction.
	const FVector RealDir = B - A;
	const float ZStretch = 1.0f / FMath::Max(0.05f, SmokeHeightScale);
	const FVector SquashedA(A.X, A.Y, A.Z * ZStretch);
	const FVector SquashedB(B.X, B.Y, B.Z * ZStretch);
	const FVector SquashedCentre(Centre.X, Centre.Y, Centre.Z * ZStretch);

	const FVector Dir = SquashedB - SquashedA;
	const double LengthSq = Dir.SizeSquared();
	if (LengthSq <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// |A + t*Dir - Centre|^2 = R^2, solved for t and clamped to the segment.
	const FVector ToStart = SquashedA - SquashedCentre;
	const double QuadA = LengthSq;
	const double QuadB = 2.0 * FVector::DotProduct(ToStart, Dir);
	const double QuadC = ToStart.SizeSquared() - static_cast<double>(Radius) * Radius;

	const double Discriminant = QuadB * QuadB - 4.0 * QuadA * QuadC;
	if (Discriminant <= 0.0)
	{
		return 0.0f;
	}

	const double Root = FMath::Sqrt(Discriminant);
	const double Enter = FMath::Clamp((-QuadB - Root) / (2.0 * QuadA), 0.0, 1.0);
	const double Exit = FMath::Clamp((-QuadB + Root) / (2.0 * QuadA), 0.0, 1.0);
	if (Exit <= Enter)
	{
		return 0.0f;
	}

	// Measured back in real space: the t range is shared between the two spaces, the distance is not.
	return static_cast<float>((Exit - Enter) * RealDir.Size());
}

float USmokeVisionSubsystem::GetSightBlockage(const FVector& From, const FVector& To) const
{
	if (Clouds.Num() == 0)
	{
		return 0.0f;
	}

	float Total = 0.0f;

	for (const TWeakObjectPtr<ASmokeCloud>& Held : Clouds)
	{
		const ASmokeCloud* Cloud = Held.Get();
		if (!Cloud)
		{
			continue;
		}

		// The radius the cloud is at RIGHT NOW, so the blindness grows in with the visuals and
		// shrinks out with them rather than switching on a full-size sphere the moment it lands.
		const float Radius = Cloud->GetCurrentRadius();
		if (Radius <= 0.0f)
		{
			continue;
		}

		for (const FVector& Centre : Cloud->GetPuffCentres())
		{
			Total += SegmentInsideSphere(From, To, Centre, Radius);
		}
	}

	return Total;
}

bool USmokeVisionSubsystem::IsSightBlocked(const FVector& From, const FVector& To) const
{
	if (Clouds.Num() == 0)
	{
		return false;
	}

	return GetSightBlockage(From, To) > SightPenetration;
}

bool USmokeVisionSubsystem::IsInsideSmokeFrom(const FVector& Point, const APawn* Thrower) const
{
	if (Clouds.Num() == 0 || !Thrower)
	{
		return false;
	}

	for (const TWeakObjectPtr<ASmokeCloud>& Held : Clouds)
	{
		const ASmokeCloud* Cloud = Held.Get();
		if (!Cloud || Cloud->GetInstigator() != Thrower)
		{
			continue;
		}

		// One entry per cloud, and a wall is three clouds, so this is three distance checks in the
		// worst case anybody will ever stand in.
		for (const FVector& Centre : Cloud->GetPuffCentres())
		{
			if (FVector::DistSquared(Point, Centre) <= FMath::Square(Cloud->GetFullRadius()))
			{
				return true;
			}
		}
	}

	return false;
}

bool USmokeVisionSubsystem::IsSightBlockedInWorld(const UWorld* World, const FVector& From, const FVector& To)
{
	if (!World)
	{
		return false;
	}

	const USmokeVisionSubsystem* Subsystem = World->GetSubsystem<USmokeVisionSubsystem>();
	return Subsystem && Subsystem->IsSightBlocked(From, To);
}
