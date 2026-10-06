// HealZone.cpp

#include "HealZone.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AHealZone::AHealZone()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(false);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// No collision component at all: who is inside is answered by measuring, not by overlapping.
	ZoneVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ZoneVFX"));
	ZoneVFX->SetupAttachment(RootComponent);
	ZoneVFX->SetAutoActivate(false);
}

void AHealZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHealZone, Radius);
	DOREPLIFETIME(AHealZone, Strength);
	DOREPLIFETIME(AHealZone, Lifetime);
}

void AHealZone::BeginPlay()
{
	Super::BeginPlay();

	if (ZoneVFX)
	{
		if (ZoneFX)
		{
			ZoneVFX->SetAsset(ZoneFX);
		}
		if (ZoneVFX->GetAsset())
		{
			ApplyVisualParams();
			ZoneVFX->Activate(true);
		}
	}

	if (SpawnSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SpawnSound, GetActorLocation());
	}
}

void AHealZone::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
		World->GetTimerManager().ClearTimer(ExpiryTimer);
	}

	Super::EndPlay(Reason);
}

void AHealZone::ApplyVisualParams()
{
	if (ZoneVFX && ZoneVFX->GetAsset())
	{
		ZoneVFX->SetVariableFloat(TEXT("Radius"), Radius);
		ZoneVFX->SetVariableFloat(TEXT("Strength"), Strength);
		ZoneVFX->SetVariableFloat(TEXT("Duration"), Lifetime);
	}
}

void AHealZone::OnRep_VisualParams()
{
	ApplyVisualParams();
}

void AHealZone::Begin(float InHealAmount, float InStrength, float InRadius, float InLifetime)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World)
	{
		return;
	}

	HealAmount = FMath::Max(0.0f, InHealAmount);
	Strength = FMath::Clamp(InStrength, 0.0f, 1.0f);
	Radius = FMath::Max(1.0f, InRadius);
	Lifetime = FMath::Max(0.1f, InLifetime);

	// The listen server is a machine with a screen too, and gets no OnRep for its own writes.
	ApplyVisualParams();

	// Measured once immediately: a teammate the grenade burst on is healed on that frame.
	RefreshOccupants();
	if (bCollected)
	{
		return;
	}

	World->GetTimerManager().SetTimer(RefreshTimer, this, &AHealZone::RefreshOccupants,
		FMath::Max(0.02f, RefreshInterval), true);

	FTimerDelegate Expire;
	Expire.BindLambda([WeakThis = TWeakObjectPtr<AHealZone>(this)]()
	{
		if (AHealZone* Zone = WeakThis.Get())
		{
			Zone->Destroy();
		}
	});
	World->GetTimerManager().SetTimer(ExpiryTimer, Expire, Lifetime, false);
}

void AHealZone::RefreshOccupants()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || bCollected)
	{
		return;
	}

	const FVector Here = GetActorLocation();

	TArray<APawn*> Players;
	CoopPlayers::GetAll(World, Players);

	// The nearest of those inside, so two players arriving in the same refresh are settled by who is
	// closer rather than by their order in a list.
	AShooterCharacter* Taker = nullptr;
	float BestDistSq = Radius * Radius;

	for (APawn* Pawn : Players)
	{
		AShooterCharacter* Player = Cast<AShooterCharacter>(Pawn);
		if (!Player || Player->IsDead() || Player->IsDowned())
		{
			continue;
		}

		// A player with nothing to heal walks through it and leaves it for somebody who needs it.
		if (Player->GetCurrentHP() >= Player->GetMaxHP())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Player->GetActorLocation(), Here);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			Taker = Player;
		}
	}

	if (!Taker)
	{
		return;
	}

	bCollected = true;
	Taker->RestoreHealth(HealAmount);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] HealZone: %s took %.1f (thrown by %s)"),
		*Taker->GetName(), HealAmount, *GetNameSafe(GetInstigator()));

	Multicast_PlayCollected(Here);
	Destroy();
}

void AHealZone::Multicast_PlayCollected_Implementation(FVector Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (CollectedFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, CollectedFX, Location);
	}
	if (CollectedSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, CollectedSound, Location);
	}
}
