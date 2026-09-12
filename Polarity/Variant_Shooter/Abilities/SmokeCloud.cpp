// SmokeCloud.cpp

#include "SmokeCloud.h"
#include "AI/SmokeVisionSubsystem.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ASmokeCloud::ASmokeCloud()
{
	// Ticks, unlike the puddle, and for one reason: the effect is told its opacity every frame from
	// the same clock the blindness uses. @see the class comment.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bReplicates = true;
	SetReplicateMovement(false);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// No collision component at all, deliberately. @see the class comment.
	SmokeVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SmokeVFX"));
	SmokeVFX->SetupAttachment(RootComponent);
	SmokeVFX->SetAutoActivate(false);
}

void ASmokeCloud::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASmokeCloud, Shape);
}

void ASmokeCloud::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (USmokeVisionSubsystem* Vision = World->GetSubsystem<USmokeVisionSubsystem>())
		{
			// Registered for the whole life on every machine. The centre list may still be empty here
			// on a client -- the subsystem asks for it every query, so it reads as no smoke until the
			// shape arrives.
			Vision->RegisterCloud(this);
		}
	}

	// CLIENTS ONLY, and the authority check is the whole point of this branch rather than a detail.
	//
	// On the server BeginPlay runs INSIDE SpawnActor, before the canister gets to call Begin, and at
	// that moment Shape is still the struct's defaults. Applying here latched those defaults and made
	// the real numbers a no-op: every cloud drew itself at the default radius and lifetime no matter
	// what the ability said, and changing CloudRadius appeared to do nothing at all.
	//
	// A client has the opposite problem -- it never calls Begin -- so it still needs this, for the
	// case where Shape replicated in before BeginPlay ran and OnRep therefore fired too early.
	if (!HasAuthority() && Shape.Duration > 0.0f && !bShapeApplied)
	{
		ApplyShape();
	}

	if (SpawnSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SpawnSound, GetActorLocation());
	}
}

void ASmokeCloud::EndPlay(const EEndPlayReason::Type Reason)
{
	// Every exit from the cloud's life comes through here -- expiry, the level tearing down, an
	// early Destroy -- so this is the one place that has to let go.
	ReleaseAll();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpiryTimer);

		if (USmokeVisionSubsystem* Vision = World->GetSubsystem<USmokeVisionSubsystem>())
		{
			Vision->UnregisterCloud(this);
		}
	}

	Super::EndPlay(Reason);
}

void ASmokeCloud::Begin(const FSmokeCloudShape& InShape, float InSightPenetration, float InTurnRateMultiplier)
{
	if (!HasAuthority())
	{
		return;
	}

	Shape = InShape;
	Shape.Radius = FMath::Max(1.0f, Shape.Radius);
	Shape.Duration = FMath::Max(0.1f, Shape.Duration);
	Shape.GrowTime = FMath::Clamp(Shape.GrowTime, 0.0f, Shape.Duration);
	Shape.FadeTime = FMath::Clamp(Shape.FadeTime, 0.0f, Shape.Duration - Shape.GrowTime);

	TurnRateMultiplier = FMath::Clamp(InTurnRateMultiplier, 0.05f, 1.0f);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (USmokeVisionSubsystem* Vision = World->GetSubsystem<USmokeVisionSubsystem>())
	{
		Vision->SetSightPenetration(InSightPenetration);
	}

	ApplyShape();

	// Who is standing in it is measured by USmokeVisionSubsystem, on one timer shared by every cloud
	// in the world. @see RefreshOccupantsFrom

	FTimerDelegate Expire;
	Expire.BindLambda([WeakThis = TWeakObjectPtr<ASmokeCloud>(this)]()
	{
		if (ASmokeCloud* Cloud = WeakThis.Get())
		{
			Cloud->Destroy();
		}
	});
	World->GetTimerManager().SetTimer(ExpiryTimer, Expire, Shape.Duration, false);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeCloud: r=%.0f at %s for %.1fs, turn x%.2f, penetration %.0f"),
		Shape.Radius, *GetActorLocation().ToCompactString(), Shape.Duration, TurnRateMultiplier, InSightPenetration);
}

void ASmokeCloud::OnRep_Shape()
{
	ApplyShape();
}

void ASmokeCloud::ApplyShape()
{
	UWorld* World = GetWorld();
	if (!World || bShapeApplied || Shape.Duration <= 0.0f)
	{
		return;
	}

	bShapeApplied = true;
	StartTime = World->GetTimeSeconds();

	PuffCentres.Reset(1);
	PuffCentres.Add(GetActorLocation());

	if (!SmokeVFX)
	{
		return;
	}

	if (SmokeFX)
	{
		SmokeVFX->SetAsset(SmokeFX);
	}

	if (!SmokeVFX->GetAsset())
	{
		// Not an error worth swallowing silently: the cloud still blinds, and it would be invisible.
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeCloud: no SmokeFX assigned on %s, the smoke is invisible"),
			*GetNameSafe(GetClass()));
		return;
	}

	// Told the same numbers the gameplay is using. Radius sizes the puff; Duration is how long the
	// particles live, so the effect ends with the blindness rather than a moment either side of it.
	SmokeVFX->SetVariableFloat(TEXT("Radius"), Shape.Radius);
	SmokeVFX->SetVariableFloat(TEXT("Duration"), Shape.Duration);
	SmokeVFX->SetVariableFloat(TEXT("GrowTime"), Shape.GrowTime);
	SmokeVFX->SetVariableFloat(TEXT("FadeTime"), Shape.FadeTime);
	SmokeVFX->SetVariableLinearColor(TEXT("SmokeColor"), SmokeColor);
	SmokeVFX->SetVariableFloat(TEXT("Opacity"), GetCurrentOpacity());
	SmokeVFX->Activate(true);
}

void ASmokeCloud::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// The only per-frame work: hand the effect the same age curve the gameplay is reading. Runs on
	// every machine because it is what makes the smoke visible, and costs one parameter write.
	if (bShapeApplied && SmokeVFX && SmokeVFX->GetAsset())
	{
		SmokeVFX->SetVariableFloat(TEXT("Opacity"), GetCurrentOpacity());
	}
}

float ASmokeCloud::GetCurrentRadius() const
{
	const UWorld* World = GetWorld();
	if (!World || !bShapeApplied)
	{
		return 0.0f;
	}

	const float Age = World->GetTimeSeconds() - StartTime;
	if (Age <= 0.0f)
	{
		return 0.0f;
	}

	if (Shape.GrowTime > 0.0f && Age < Shape.GrowTime)
	{
		return Shape.Radius * (Age / Shape.GrowTime);
	}

	// Full for the whole rest of the life. The cloud stops blocking when it is destroyed, not while
	// it is visibly thinning: an enemy should not be able to shoot through smoke the player can
	// still see.
	return Shape.Radius;
}

float ASmokeCloud::GetCurrentOpacity() const
{
	const UWorld* World = GetWorld();
	if (!World || !bShapeApplied)
	{
		return 0.0f;
	}

	const float Age = World->GetTimeSeconds() - StartTime;
	if (Age <= 0.0f)
	{
		return 0.0f;
	}

	if (Shape.GrowTime > 0.0f && Age < Shape.GrowTime)
	{
		return FMath::Clamp(Age / Shape.GrowTime, 0.0f, 1.0f);
	}

	const float FadeStart = Shape.Duration - Shape.FadeTime;
	if (Shape.FadeTime > 0.0f && Age > FadeStart)
	{
		const float Left = FMath::Clamp(1.0f - (Age - FadeStart) / Shape.FadeTime, 0.0f, 1.0f);

		// Squared rather than linear. Translucent sprites stacked on top of each other still read as
		// solid at a per-sprite alpha of 0.4, so a straight ramp spends most of the fade looking like
		// nothing is happening and then vanishes. The curve puts the visible part of the fade where
		// the smoke is actually about to expire.
		return Left * Left;
	}

	return 1.0f;
}

void ASmokeCloud::RefreshOccupantsFrom(const TArray<AShooterNPC*>& Candidates)
{
	if (!HasAuthority())
	{
		return;
	}

	const float Radius = GetCurrentRadius();
	const float RadiusSq = Radius * Radius;
	const FVector Centre = GetActorLocation();

	TArray<TWeakObjectPtr<AShooterNPC>> StillInside;

	if (Radius > 0.0f)
	{
		// The cloud is squashed flat, so height counts double against the radius. Must match the Non
		// Uniform Scale on ShapeLocation in NS_SmokeCloud and the constant in USmokeVisionSubsystem.
		constexpr float SmokeHeightScale = 0.25f;
		const float ZStretch = 1.0f / SmokeHeightScale;

		for (AShooterNPC* Enemy : Candidates)
		{
			if (!Enemy)
			{
				continue;
			}

			// The body's centre against the volume, in three dimensions: unlike a puddle, smoke has
			// height, and an enemy on a walkway above it is genuinely not in it.
			FVector Offset = Enemy->GetActorLocation() - Centre;
			Offset.Z *= ZStretch;

			if (Offset.SizeSquared() <= RadiusSq)
			{
				StillInside.Add(Enemy);
			}
		}
	}

	// Release whoever left before slowing whoever arrived, so an enemy in both lists is never
	// briefly restored to full turn rate.
	for (const TWeakObjectPtr<AShooterNPC>& Was : Occupants)
	{
		AShooterNPC* Enemy = Was.Get();
		if (Enemy && !StillInside.Contains(Was))
		{
			Enemy->RemoveTurnSlow(this);
		}
	}

	for (const TWeakObjectPtr<AShooterNPC>& Now : StillInside)
	{
		if (AShooterNPC* Enemy = Now.Get())
		{
			// Re-applied every refresh rather than only on entry: the value is stored per source, so
			// writing the same number again is free, and it heals the case where something else
			// reset the enemy's rotation rate while it stood here.
			Enemy->AddTurnSlow(this, TurnRateMultiplier);
		}
	}

	Occupants = MoveTemp(StillInside);
}

void ASmokeCloud::ReleaseAll()
{
	for (const TWeakObjectPtr<AShooterNPC>& Held : Occupants)
	{
		if (AShooterNPC* Enemy = Held.Get())
		{
			Enemy->RemoveTurnSlow(this);
		}
	}
	Occupants.Reset();
}
