// SlowPuddle.cpp

#include "SlowPuddle.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ASlowPuddle::ASlowPuddle()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(false);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// No collision component at all. Nothing is meant to bump into a puddle, and who is standing in
	// it is answered by measuring, not by overlapping.
	PuddleVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PuddleVFX"));
	PuddleVFX->SetupAttachment(RootComponent);
	PuddleVFX->SetAutoActivate(false);
}

void ASlowPuddle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASlowPuddle, Radius);
}

void ASlowPuddle::BeginPlay()
{
	Super::BeginPlay();

	if (PuddleVFX)
	{
		if (PuddleFX)
		{
			PuddleVFX->SetAsset(PuddleFX);
		}
		if (PuddleVFX->GetAsset())
		{
			// The one user parameter the effect is given, so a Blueprint's visuals grow with the
			// gameplay radius instead of being authored to match one particular number.
			PuddleVFX->SetVariableFloat(TEXT("Radius"), Radius);
			PuddleVFX->Activate(true);
		}
	}

	if (SpawnSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SpawnSound, GetActorLocation());
	}
}

void ASlowPuddle::OnRep_Radius()
{
	if (PuddleVFX && PuddleVFX->GetAsset())
	{
		PuddleVFX->SetVariableFloat(TEXT("Radius"), Radius);
	}
}

void ASlowPuddle::EndPlay(const EEndPlayReason::Type Reason)
{
	// Every exit from the puddle's life comes through here -- the expiry timer, the level tearing
	// down, somebody destroying it early -- so this is the one place that has to let go, and letting
	// go anywhere else would be a second path that can be missed.
	ReleaseAll();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
		World->GetTimerManager().ClearTimer(ExpiryTimer);
	}

	Super::EndPlay(Reason);
}

void ASlowPuddle::Begin(float InRadius, float InDuration, float InSlowMultiplier)
{
	if (!HasAuthority())
	{
		return;
	}

	Radius = FMath::Max(1.0f, InRadius);
	SlowMultiplier = FMath::Clamp(InSlowMultiplier, 0.05f, 1.0f);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Measured once immediately: an enemy already standing where the puddle lands is slowed on the
	// frame it lands, not up to RefreshInterval later.
	RefreshOccupants();

	World->GetTimerManager().SetTimer(RefreshTimer, this, &ASlowPuddle::RefreshOccupants,
		FMath::Max(0.05f, RefreshInterval), true);

	if (InDuration > 0.0f)
	{
		FTimerDelegate Expire;
		Expire.BindLambda([WeakThis = TWeakObjectPtr<ASlowPuddle>(this)]()
		{
			if (ASlowPuddle* Puddle = WeakThis.Get())
			{
				Puddle->Destroy();
			}
		});
		World->GetTimerManager().SetTimer(ExpiryTimer, Expire, InDuration, false);
	}
}

void ASlowPuddle::RefreshOccupants()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	const FVector Here = GetActorLocation();
	const float RadiusSq = Radius * Radius;

	TArray<TWeakObjectPtr<AShooterNPC>> StillInside;

	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* Enemy = *It;
		if (!Enemy || Enemy->IsDead())
		{
			continue;
		}

		// Feet, not the actor origin: the capsule's centre of a tall enemy standing on the puddle is
		// most of a metre up, and a height window measured from there would need to be so generous
		// that a floor above would fall inside it.
		FVector Feet = Enemy->GetActorLocation();
		if (const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
		{
			Feet.Z -= Capsule->GetScaledCapsuleHalfHeight();
		}

		const float Height = Feet.Z - Here.Z;
		if (Height > HeightTolerance || Height < -HeightTolerance)
		{
			continue;
		}

		if (FVector::DistSquared2D(Feet, Here) > RadiusSq)
		{
			continue;
		}

		StillInside.Add(Enemy);
	}

	// Release whoever left before slowing whoever arrived, so an enemy that is in both lists is
	// never briefly un-slowed.
	for (const TWeakObjectPtr<AShooterNPC>& Was : Occupants)
	{
		AShooterNPC* Enemy = Was.Get();
		if (Enemy && !StillInside.Contains(Was))
		{
			Enemy->RemoveMovementSlow(this);
		}
	}

	for (const TWeakObjectPtr<AShooterNPC>& Now : StillInside)
	{
		if (AShooterNPC* Enemy = Now.Get())
		{
			// Re-applied every refresh rather than only on entry: the value is stored per source, so
			// writing the same number again is free, and it heals the case where something else reset
			// the enemy's speed while it stood here.
			Enemy->AddMovementSlow(this, SlowMultiplier);
		}
	}

	Occupants = MoveTemp(StillInside);
}

void ASlowPuddle::ReleaseAll()
{
	for (const TWeakObjectPtr<AShooterNPC>& Held : Occupants)
	{
		if (AShooterNPC* Enemy = Held.Get())
		{
			Enemy->RemoveMovementSlow(this);
		}
	}
	Occupants.Reset();
}
