// KamikazeDroneSpawner.cpp

#include "KamikazeDroneSpawner.h"
#include "KamikazeDroneNPC.h"
#include "Components/SphereComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

AKamikazeDroneSpawner::AKamikazeDroneSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	DetectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionSphere"));
	DetectionSphere->SetupAttachment(SceneRoot);
	DetectionSphere->SetCollisionProfileName(FName("OverlapOnlyPawn"));
	DetectionSphere->SetGenerateOverlapEvents(true);
	DetectionSphere->SetSphereRadius(ReactionRadius);

	CurrentHP = MaxHP;
}

void AKamikazeDroneSpawner::BeginPlay()
{
	Super::BeginPlay();

	CurrentHP = MaxHP;
	DetectionSphere->SetSphereRadius(ReactionRadius);

	// Bind detection overlap
	DetectionSphere->OnComponentBeginOverlap.AddDynamic(this, &AKamikazeDroneSpawner::OnDetectionBeginOverlap);

	// Resolve soft references to shield guardians and bind their death
	for (const TSoftObjectPtr<AActor>& SoftRef : ShieldGuardians)
	{
		AActor* Guardian = SoftRef.Get();
		if (Guardian)
		{
			ResolvedGuardians.Add(Guardian);
			Guardian->OnDestroyed.AddDynamic(this, &AKamikazeDroneSpawner::OnGuardianDestroyed);
		}
	}

	GuardiansAlive = ResolvedGuardians.Num();

	// No guardians assigned → skip straight to Panic mode
	if (GuardiansAlive <= 0)
	{
		EnterPanicMode();
	}
	else
	{
		// Spawn shield VFX
		if (ShieldVFX)
		{
			ShieldVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				ShieldVFX, SceneRoot, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset,
				true // bAutoActivate
			);
		}
	}
}

float AKamikazeDroneSpawner::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsShieldActive)
	{
		return 0.f;
	}

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	CurrentHP -= ActualDamage;

	if (CurrentHP <= 0.f)
	{
		CurrentHP = 0.f;
		Die();
	}

	return ActualDamage;
}

// ── Detection ──────────────────────────────────────────────────

void AKamikazeDroneSpawner::OnDetectionBeginOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (bIsActivated) return;

	// Only activate for the player character
	ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0);
	if (OtherActor && OtherActor == PlayerChar)
	{
		Activate();
	}
}

void AKamikazeDroneSpawner::Activate()
{
	bIsActivated = true;
	StartSpawnTimer();
}

// ── Spawning ───────────────────────────────────────────────────

void AKamikazeDroneSpawner::StartSpawnTimer()
{
	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&AKamikazeDroneSpawner::SpawnDrone,
		GetSpawnIntervalForCurrentMode(),
		true, // bLoop
		0.f   // first spawn immediately
	);
}

void AKamikazeDroneSpawner::SpawnDrone()
{
	if (!DroneClass) return;

	// Cap check
	if (ActiveDrones.Num() >= GetMaxDronesForCurrentMode()) return;

	// Random point on a circle around spawner
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const FVector SpawnOffset(
		FMath::Cos(Angle) * SpawnRadius,
		FMath::Sin(Angle) * SpawnRadius,
		SpawnHeight
	);
	const FVector SpawnLocation = GetActorLocation() + SpawnOffset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AKamikazeDroneNPC* Drone = GetWorld()->SpawnActor<AKamikazeDroneNPC>(
		DroneClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);

	if (!Drone) return;

	ActiveDrones.Add(Drone);
	Drone->OnDestroyed.AddDynamic(this, &AKamikazeDroneSpawner::OnDroneDestroyed);

	// ── Launch velocity ───────────────────────────────────────
	// Convert LaunchDirection from local to world space, apply random spread cone
	if (LaunchSpeed > 0.f)
	{
		FVector WorldLaunchDir = GetActorTransform().TransformVectorNoScale(LaunchDirection.GetSafeNormal());

		// Apply random spread within cone
		if (LaunchSpreadAngle > 0.f)
		{
			WorldLaunchDir = FMath::VRandCone(WorldLaunchDir, FMath::DegreesToRadians(LaunchSpreadAngle));
		}

		// Set initial velocity directly on CMC — orbit system will naturally stabilize
		if (UCharacterMovementComponent* DroneCMC = Drone->GetCharacterMovement())
		{
			DroneCMC->Velocity = WorldLaunchDir * LaunchSpeed;
		}

		// Orient drone to face launch direction
		Drone->SetActorRotation(WorldLaunchDir.Rotation());
	}

	// VFX
	if (SpawnVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, SpawnVFX, SpawnLocation);
	}

	// SFX
	if (SpawnSFX)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SpawnSFX, SpawnLocation);
	}
}

void AKamikazeDroneSpawner::OnDroneDestroyed(AActor* DestroyedActor)
{
	AKamikazeDroneNPC* Drone = Cast<AKamikazeDroneNPC>(DestroyedActor);
	if (Drone)
	{
		ActiveDrones.Remove(Drone);
	}
}

// ── Shield Guardians ───────────────────────────────────────────

void AKamikazeDroneSpawner::OnGuardianDestroyed(AActor* DestroyedActor)
{
	GuardiansAlive = FMath::Max(0, GuardiansAlive - 1);

	if (GuardiansAlive <= 0)
	{
		EnterPanicMode();
	}
}

void AKamikazeDroneSpawner::EnterPanicMode()
{
	CurrentMode = ESpawnerMode::Panic;
	bIsShieldActive = false;

	// Kill shield VFX
	if (ShieldVFXComponent)
	{
		ShieldVFXComponent->Deactivate();
		ShieldVFXComponent->DestroyComponent();
		ShieldVFXComponent = nullptr;
	}

	// Shield break SFX
	if (ShieldBreakSFX)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ShieldBreakSFX, GetActorLocation());
	}

	// Restart timer with panic rate (if already activated)
	if (bIsActivated)
	{
		GetWorldTimerManager().ClearTimer(SpawnTimerHandle);
		StartSpawnTimer();
	}
}

// ── Death ──────────────────────────────────────────────────────

void AKamikazeDroneSpawner::Die()
{
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);

	if (DeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, DeathVFX, GetActorLocation());
	}

	if (DeathSFX)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSFX, GetActorLocation());
	}

	OnSpawnerDestroyed.Broadcast(this);

	// Drones are NOT killed — they become orphans
	SetLifeSpan(5.f);
}

// ── Helpers ────────────────────────────────────────────────────

int32 AKamikazeDroneSpawner::GetMaxDronesForCurrentMode() const
{
	return (CurrentMode == ESpawnerMode::Panic) ? MaxDrones_Panic : MaxDrones_Normal;
}

float AKamikazeDroneSpawner::GetSpawnIntervalForCurrentMode() const
{
	return (CurrentMode == ESpawnerMode::Panic) ? SpawnInterval_Panic : SpawnInterval_Normal;
}
