// GrenadeProjectile.cpp

#include "Variant_Shooter/Grenades/GrenadeProjectile.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "AI/PolarityTeams.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Components/AudioComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Coop/CoopPlayers.h"
#include "GameFramework/Controller.h"

namespace GrenadeZones
{
	/** Every grenade that is armed or working, for IsPointInDanger. Server only. */
	static TArray<TWeakObjectPtr<AGrenadeProjectile>> Live;
}

AGrenadeProjectile::AGrenadeProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.0f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	// Rolls past legs rather than stopping on them: a grenade that lands on somebody's boot and drops
	// dead there is a throw that did not go where the arc said.
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->bAutoActivate = false;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.35f;
	Movement->Friction = 0.4f;
	Movement->BounceVelocityStopSimulatingThreshold = 40.0f;
	Movement->bRotationFollowsVelocity = false;
	Movement->bSweepCollision = true;
}

void AGrenadeProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGrenadeProjectile, Definition);
}

void AGrenadeProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GrenadeZones::Live.RemoveAll([this](const TWeakObjectPtr<AGrenadeProjectile>& Entry) { return !Entry.IsValid() || Entry.Get() == this; });
	Super::EndPlay(EndPlayReason);
}

float AGrenadeProjectile::GetCurrentDangerRadius() const
{
	if (!Definition || !IsDangerous())
	{
		return 0.0f;
	}
	return bWorking ? Definition->GetWorkingDangerRadius() : Definition->DangerRadius;
}

bool AGrenadeProjectile::IsPointInDanger(const UWorld* World, const FVector& Point)
{
	for (const TWeakObjectPtr<AGrenadeProjectile>& Entry : GrenadeZones::Live)
	{
		const AGrenadeProjectile* const Grenade = Entry.Get();
		if (!Grenade || Grenade->GetWorld() != World || !Grenade->Definition)
		{
			continue;
		}
		const float Radius = Grenade->GetCurrentDangerRadius();
		const FVector Center = Grenade->bWorking ? Grenade->WorkOrigin : Grenade->DangerCenter;
		if (Radius <= 0.0f || FVector::DistSquared(Center, Point) > FMath::Square(Radius))
		{
			continue;
		}
		if (!Grenade->Definition->DoWallsProtect())
		{
			return true;
		}
		// Behind a wall from it, at chest height: the fragments and the flash stop there.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrenadeZone), false, Grenade);
		FHitResult Wall;
		if (!World->LineTraceSingleByChannel(Wall, Center + FVector(0.0f, 0.0f, 40.0f), Point + FVector(0.0f, 0.0f, 100.0f), ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

float AGrenadeProjectile::GetAimHitChanceOverride() const
{
	return bWorking && Definition && Definition->IsShotAtWhileWorking() ? Definition->GetWorkingHitChance() : -1.0f;
}

float AGrenadeProjectile::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const int32 Needed = Definition ? Definition->GetHitsToBreak() : 0;
	if (!HasAuthority() || !bWorking || Needed <= 0 || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	// Only the enemy's side breaks it: the thrower and his team shooting near it do not.
	const APawn* Shooter = EventInstigator ? EventInstigator->GetPawn() : nullptr;
	if (!Shooter && DamageCauser)
	{
		Shooter = DamageCauser->GetInstigator();
	}
	if (!Shooter || CoopPlayers::IsPlayer(Shooter))
	{
		return 0.0f;
	}

	++HitsTaken;
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s hit by %s: %d of %d"), *GetName(), *GetNameSafe(Shooter), HitsTaken, Needed);
	if (HitsTaken >= Needed)
	{
		UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s is broken"), *GetName());
		StopWorking();
	}
	return DamageAmount;
}

void AGrenadeProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (Movement)
	{
		Movement->OnProjectileBounce.AddDynamic(this, &AGrenadeProjectile::OnBounce);
	}

	// Never bounces off the one who threw it on the way out of his hand.
	if (APawn* const Thrower = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Thrower, true);
	}
}

void AGrenadeProjectile::Launch(UGrenadeDefinition* InDefinition, const FVector& Velocity)
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || !InDefinition || !World)
	{
		return;
	}

	Definition = InDefinition;
	if (Movement)
	{
		Movement->ProjectileGravityScale = InDefinition->GravityScale;
		Movement->Velocity = Velocity;
		Movement->Activate();
	}

	DetonateAt = World->GetTimeSeconds() + InDefinition->FuseTime;
	GrenadeZones::Live.AddUnique(this);
	World->GetTimerManager().SetTimer(FuseTimer, this, &AGrenadeProjectile::Detonate, InDefinition->FuseTime, false);

	PredictRest();
	World->GetTimerManager().SetTimer(WarnTimer, this, &AGrenadeProjectile::WarnAround, WarnInterval, true, 0.0f);
}

float AGrenadeProjectile::GetTimeToDetonate() const
{
	const UWorld* const World = GetWorld();
	return World ? FMath::Max(0.0f, DetonateAt - World->GetTimeSeconds()) : 0.0f;
}

void AGrenadeProjectile::PredictRest()
{
	// Where the flight comes down, by the same simulation the thrower's arc uses. The bounces after
	// it are left out: they are short, and each one asks again.
	DangerCenter = GetActorLocation();
	const UWorld* const World = GetWorld();
	if (!World || !Movement || Movement->Velocity.SizeSquared() < FMath::Square(150.0f))
	{
		return;
	}

	FPredictProjectilePathParams Params(Collision ? Collision->GetScaledSphereRadius() : 8.0f, GetActorLocation(),
		Movement->Velocity, FMath::Max(0.5f, GetTimeToDetonate()), ECC_Visibility, this);
	Params.OverrideGravityZ = World->GetGravityZ() * Movement->ProjectileGravityScale;
	Params.SimFrequency = 15.0f;
	if (APawn* const Thrower = GetInstigator())
	{
		Params.ActorsToIgnore.Add(Thrower);
	}

	FPredictProjectilePathResult Result;
	if (UGameplayStatics::PredictProjectilePath(this, Params, Result))
	{
		DangerCenter = Result.HitResult.ImpactPoint;
	}
	else if (Result.PathData.Num() > 0)
	{
		DangerCenter = Result.PathData.Last().Location;
	}
}

void AGrenadeProjectile::OnBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}

	if (const UWorld* const World = GetWorld())
	{
		LastBounceTime = World->GetTimeSeconds();
	}
	LastBounceLocation = ImpactResult.ImpactPoint;
	PredictRest();

	const float Strength = FMath::Clamp(ImpactVelocity.Size() / 800.0f, 0.15f, 1.0f);
	if (Strength > 0.15f)
	{
		Multicast_Bounce(ImpactResult.ImpactPoint, Strength);
	}
}

void AGrenadeProjectile::Multicast_Bounce_Implementation(FVector Location, float Strength)
{
	if (Definition && Definition->BounceSound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Definition->BounceSound, Location, Strength);
	}
}

bool AGrenadeProjectile::WasHeardBy(const FVector& ListenerLocation) const
{
	// Working: everybody inside its reach feels it, the hum and the head both.
	if (bWorking && Definition)
	{
		return FVector::DistSquared(ListenerLocation, WorkOrigin) <= FMath::Square(Definition->GetWorkingDangerRadius());
	}

	const UWorld* const World = GetWorld();
	const float HearRadius = Definition ? Definition->BounceHearRadius : 400.0f;
	return World && World->GetTimeSeconds() - LastBounceTime <= HeardWindow
		&& FVector::DistSquared(ListenerLocation, LastBounceLocation) <= FMath::Square(HearRadius);
}

void AGrenadeProjectile::ReportNoticedBy(const AActor* Noticer)
{
	const UWorld* const World = GetWorld();
	if (!World || !Noticer)
	{
		return;
	}
	const uint8 Side = PolarityTeams::GetTeam(Noticer);
	if (!NoticedAtBySide.Contains(Side))
	{
		NoticedAtBySide.Add(Side, World->GetTimeSeconds());
	}
}

bool AGrenadeProjectile::WasShoutedTo(const AActor* Listener) const
{
	const UWorld* const World = GetWorld();
	const float* const At = Listener ? NoticedAtBySide.Find(PolarityTeams::GetTeam(Listener)) : nullptr;
	return World && At && World->GetTimeSeconds() - *At >= ShoutDelay;
}

void AGrenadeProjectile::WarnAround()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || !IsDangerous() || !World || !Definition)
	{
		return;
	}

	// Working where it went off: the danger is all of its reach, until it stops.
	if (bWorking)
	{
		const float Reach = Definition->GetWorkingDangerRadius();
		const float Left = FMath::Max(0.0f, WorkEndAt - World->GetTimeSeconds());
		FCollisionObjectQueryParams WorkObjects(ECC_Pawn);
		FCollisionQueryParams WorkParams(SCENE_QUERY_STAT(GrenadeWorkWarn), false, this);
		TArray<FOverlapResult> WorkOverlaps;
		World->OverlapMultiByObjectType(WorkOverlaps, WorkOrigin, FQuat::Identity, WorkObjects, FCollisionShape::MakeSphere(Reach + 300.0f), WorkParams);
		TSet<AShooterNPC*> WorkTold;
		for (const FOverlapResult& Overlap : WorkOverlaps)
		{
			AShooterNPC* const NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (NPC && !WorkTold.Contains(NPC))
			{
				WorkTold.Add(NPC);
				NPC->NotifyGrenadeThreat(this, WorkOrigin, Reach, Left);
			}
		}
		return;
	}

	// Lying still: the danger is where it lies.
	if (!Movement || !Movement->IsActive() || Movement->Velocity.SizeSquared() < FMath::Square(150.0f))
	{
		DangerCenter = GetActorLocation();
	}

	// A little wider than the danger: one standing just outside it still sees it coming at him.
	const float Radius = Definition->DangerRadius + 300.0f;
	FCollisionObjectQueryParams Objects(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrenadeWarn), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, DangerCenter, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);

	TSet<AShooterNPC*> Told;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShooterNPC* const NPC = Cast<AShooterNPC>(Overlap.GetActor());
		if (NPC && !Told.Contains(NPC))
		{
			Told.Add(NPC);
			NPC->NotifyGrenadeThreat(this, DangerCenter, Definition->DangerRadius, GetTimeToDetonate());
		}
	}
}

void AGrenadeProjectile::Detonate()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || bDetonated || !World)
	{
		return;
	}
	bDetonated = true;

	World->GetTimerManager().ClearTimer(FuseTimer);
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}

	// A little off the floor it lies on, so the lines drawn from it to whoever is near do not start
	// inside the ground.
	const FVector Origin = GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);

	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s (%s) goes off at %s"), *GetName(), *GetNameSafe(Definition), *Origin.ToCompactString());

	if (Definition)
	{
		Definition->Detonate(this, Origin);
	}
	Multicast_Detonate(Origin);

	// One that works on: stays where it lies, pulses, and the enemies keep being told to get away.
	const float Active = Definition ? Definition->GetActiveSeconds() : 0.0f;
	if (Active > 0.0f)
	{
		bWorking = true;
		WorkOrigin = Origin;
		WorkEndAt = World->GetTimeSeconds() + Active;
		const float Interval = FMath::Max(0.05f, Definition->GetPulseInterval());
		World->GetTimerManager().SetTimer(PulseTimer, this, &AGrenadeProjectile::PulseOnce, Interval, true);
		World->GetTimerManager().SetTimer(WorkEndTimer, this, &AGrenadeProjectile::StopWorking, Active, false);
		UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s works for %.1f s, a pulse every %.2f s"), *GetName(), Active, Interval);
		return;
	}

	World->GetTimerManager().ClearTimer(WarnTimer);

	// Gone from the picture now; the actor lives a moment longer so the multicast gets out with it.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(1.0f);
}

void AGrenadeProjectile::PulseOnce()
{
	if (!HasAuthority() || !bWorking || !Definition)
	{
		return;
	}
	Definition->Pulse(this, WorkOrigin);
	Multicast_Pulse(WorkOrigin);
}

void AGrenadeProjectile::StopWorking()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || !bWorking || !World)
	{
		return;
	}
	bWorking = false;
	World->GetTimerManager().ClearTimer(PulseTimer);
	World->GetTimerManager().ClearTimer(WorkEndTimer);
	World->GetTimerManager().ClearTimer(WarnTimer);
	Multicast_StopWorking(WorkOrigin);
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s stops working"), *GetName());

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(1.0f);
}

void AGrenadeProjectile::Multicast_Pulse_Implementation(FVector Origin)
{
	if (Definition && Definition->PulseFX && GetNetMode() != NM_DedicatedServer)
	{
		if (UNiagaraComponent* const FX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Definition->PulseFX, Origin))
		{
			FX->SetVariableFloat(FName("User.Radius"), Definition->GetWorkingEffectRadius());
		}
	}
}

void AGrenadeProjectile::Multicast_StopWorking_Implementation(FVector Origin)
{
	if (WorkingLoop)
	{
		WorkingLoop->FadeOut(0.3f, 0.0f);
		WorkingLoop = nullptr;
	}
	if (Mesh)
	{
		Mesh->SetVisibility(false);
	}
	if (Definition && Definition->WorkingEndSound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Definition->WorkingEndSound, Origin, FRotator::ZeroRotator,
			1.0f, 1.0f, 0.0f, Definition->DetonationAttenuation);
	}
}

void AGrenadeProjectile::Multicast_Detonate_Implementation(FVector Origin)
{
	// One that works on stays in sight until it stops. @see Multicast_StopWorking
	const bool bWorksOn = Definition && Definition->GetActiveSeconds() > 0.0f;
	if (Mesh && !bWorksOn)
	{
		Mesh->SetVisibility(false);
	}

	if (!Definition || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (bWorksOn && Definition->WorkingLoopSound)
	{
		WorkingLoop = UGameplayStatics::SpawnSoundAttached(Definition->WorkingLoopSound, RootComponent, NAME_None,
			FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 1.0f, 1.0f, 0.0f, Definition->DetonationAttenuation);
	}

	if (Definition->DetonationFX)
	{
		// A grenade that works on gets its reach and its time handed to the effect (User.Radius,
		// User.Duration), so the dome is as big and lasts as long as the asset says.
		if (UNiagaraComponent* const FX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Definition->DetonationFX, Origin);
			FX && bWorksOn)
		{
			FX->SetVariableFloat(FName("User.Radius"), Definition->GetWorkingEffectRadius());
			FX->SetVariableFloat(FName("User.Duration"), Definition->GetActiveSeconds());
		}
	}
	if (Definition->DetonationSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Definition->DetonationSound, Origin, FRotator::ZeroRotator,
			1.0f, 1.0f, 0.0f, Definition->DetonationAttenuation);
	}
}
