// KamikazeDroneSpawner.cpp

#include "KamikazeDroneSpawner.h"
#include "KamikazeDroneNPC.h"
#include "Variant_Shooter/ShooterDummy.h"
#include "Components/SphereComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"

AKamikazeDroneSpawner::AKamikazeDroneSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	DetectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionSphere"));
	DetectionSphere->SetupAttachment(SceneRoot);
	// Explicit collision setup — do NOT rely on profile names that may not exist in the project
	DetectionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DetectionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	DetectionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	DetectionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
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

			// ShooterDummy uses OnDummyDeath (does NOT call Destroy on death)
			// Other actors use OnDestroyed
			if (AShooterDummy* Dummy = Cast<AShooterDummy>(Guardian))
			{
				Dummy->OnDummyDeath.AddDynamic(this, &AKamikazeDroneSpawner::OnGuardianDummyDied);
			}
			else
			{
				Guardian->OnDestroyed.AddDynamic(this, &AKamikazeDroneSpawner::OnGuardianDestroyed);
			}
		}
	}

	GuardiansAlive = ResolvedGuardians.Num();
	GuardiansTotal = GuardiansAlive;

	UE_LOG(LogTemp, Warning, TEXT("[Spawner] BeginPlay: %d guardians resolved"), GuardiansAlive);

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
	if (bIsShieldActive || bIsDead)
	{
		return 0.f;
	}

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	CurrentHP -= ActualDamage;

	UE_LOG(LogTemp, Warning, TEXT("[Spawner] Took %.1f damage, HP now: %.1f/%.1f"),
		ActualDamage, CurrentHP, MaxHP);

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
	// Drone enters Launching state: FPV PID stabilization from launch impulse,
	// then transitions to orbit when speed settles near CruiseSpeed.
	if (LaunchSpeed > 0.f)
	{
		FVector WorldLaunchDir = GetActorTransform().TransformVectorNoScale(LaunchDirection.GetSafeNormal());

		if (LaunchSpreadAngle > 0.f)
		{
			WorldLaunchDir = FMath::VRandCone(WorldLaunchDir, FMath::DegreesToRadians(LaunchSpreadAngle));
		}

		Drone->InitiateLaunch(WorldLaunchDir * LaunchSpeed);
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
	HandleGuardianKilled(DestroyedActor);
}

void AKamikazeDroneSpawner::OnGuardianDummyDied(AShooterDummy* Dummy, AActor* Killer)
{
	UE_LOG(LogTemp, Warning, TEXT("[Spawner] Guardian Dummy died: %s"),
		Dummy ? *Dummy->GetName() : TEXT("null"));
	HandleGuardianKilled(Dummy);
}

void AKamikazeDroneSpawner::HandleGuardianKilled(AActor* Guardian)
{
	GuardiansAlive = FMath::Max(0, GuardiansAlive - 1);

	UE_LOG(LogTemp, Warning, TEXT("[Spawner] Guardian killed! Remaining: %d/%d"),
		GuardiansAlive, GuardiansTotal);

	OnGuardianKilled.Broadcast(Guardian, GuardiansAlive, GuardiansTotal);

	if (GuardiansAlive <= 0)
	{
		EnterPanicMode();
	}
}

void AKamikazeDroneSpawner::EnterPanicMode()
{
	CurrentMode = ESpawnerMode::Panic;
	bIsShieldActive = false;

	UE_LOG(LogTemp, Warning, TEXT("[Spawner] PANIC MODE — shield down, spawner damageable"));

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
	bIsDead = true;
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

	// Spawn GC destruction if assigned
	if (DestructionGeometryCollection)
	{
		SpawnDestructionGC();
	}

	OnSpawnerDestroyed.Broadcast(this);

	// Drones are NOT killed — they become orphans
	SetLifeSpan(5.f);
}

// ── Geometry Collection Destruction ───────────────────────────

void AKamikazeDroneSpawner::SpawnDestructionGC()
{
	if (!DestructionGeometryCollection || !GetWorld())
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	const FRotator Rotation = GetActorRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		Origin, Rotation, SpawnParams);

	if (!GCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		GCActor->Destroy();
		return;
	}

	// Match spawner scale
	GCActor->SetActorScale3D(GetActorScale3D());

	// Collision: gibs should not push pawns or block camera
	GCComp->SetCollisionProfileName(GibCollisionProfile);
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	// Assign GC asset and initialize physics
	GCComp->SetRestCollection(DestructionGeometryCollection);

	// Copy materials from first found StaticMeshComponent (added in BP)
	TArray<UStaticMeshComponent*> MeshComponents;
	GetComponents<UStaticMeshComponent>(MeshComponents);
	if (MeshComponents.Num() > 0)
	{
		UStaticMeshComponent* SourceMesh = MeshComponents[0];
		const int32 NumMats = SourceMesh->GetNumMaterials();
		for (int32 i = 0; i < NumMats; i++)
		{
			if (UMaterialInterface* Mat = SourceMesh->GetMaterial(i))
			{
				GCComp->SetMaterial(i, Mat);
			}
		}
	}

	GCComp->SetSimulatePhysics(true);
	GCComp->RecreatePhysicsState();

	// Break all clusters
	UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Scatter pieces radially from destruction origin
	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = DestructionImpulse;
	RadialVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	// Angular velocity for tumbling
	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = DestructionAngularImpulse;
	AngularVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVelocity);

	// GC actor self-destructs after lifetime
	GCActor->SetLifeSpan(GibLifetime);

	// Hide all mesh components (GC gibs replace them visually)
	TArray<UStaticMeshComponent*> AllMeshes;
	GetComponents<UStaticMeshComponent>(AllMeshes);
	for (UStaticMeshComponent* Mesh : AllMeshes)
	{
		Mesh->SetVisibility(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
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
