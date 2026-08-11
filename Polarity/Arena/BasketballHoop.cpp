// BasketballHoop.cpp

#include "Arena/BasketballHoop.h"

#include "Arena/ArenaManager.h"
#include "Arena/BasketballBall.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Field/FieldSystemObjects.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "EngineUtils.h"

ABasketballHoop::ABasketballHoop()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	StandFrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandFrameMesh"));
	StandFrameMesh->SetupAttachment(SceneRoot);
	StandFrameMesh->SetCollisionProfileName(TEXT("BlockAll"));

	BackboardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackboardMesh"));
	BackboardMesh->SetupAttachment(SceneRoot);
	BackboardMesh->SetCollisionProfileName(TEXT("BlockAll"));

	RimMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RimMesh"));
	RimMesh->SetupAttachment(SceneRoot);
	RimMesh->SetCollisionProfileName(TEXT("BlockAll"));

	NetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NetMesh"));
	NetMesh->SetupAttachment(SceneRoot);
	NetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NetMesh->SetGenerateOverlapEvents(false);

	ScoreVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ScoreVolume"));
	ScoreVolume->SetupAttachment(SceneRoot);
	ScoreVolume->SetBoxExtent(FVector(45.0f, 45.0f, 35.0f));
	ScoreVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ScoreVolume->SetCollisionObjectType(ECC_WorldDynamic);
	ScoreVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ScoreVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	ScoreVolume->SetGenerateOverlapEvents(true);

	AssistVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("AssistVolume"));
	AssistVolume->SetupAttachment(SceneRoot);
	AssistVolume->SetBoxExtent(FVector(180.0f, 180.0f, 180.0f));
	AssistVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	AssistVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	AssistVolume->SetCollisionObjectType(ECC_WorldDynamic);
	AssistVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	AssistVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	AssistVolume->SetGenerateOverlapEvents(true);

	BackboardSweetSpotVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("BackboardSweetSpotVolume"));
	BackboardSweetSpotVolume->SetupAttachment(SceneRoot);
	BackboardSweetSpotVolume->SetBoxExtent(FVector(18.0f, 70.0f, 55.0f));
	BackboardSweetSpotVolume->SetRelativeLocation(FVector(-70.0f, 0.0f, 130.0f));
	BackboardSweetSpotVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BackboardSweetSpotVolume->SetCollisionObjectType(ECC_WorldDynamic);
	BackboardSweetSpotVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	BackboardSweetSpotVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	BackboardSweetSpotVolume->SetGenerateOverlapEvents(true);
}

void ABasketballHoop::BeginPlay()
{
	Super::BeginPlay();

	if (ScoreVolume)
	{
		ScoreVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ABasketballHoop::OnScoreVolumeBeginOverlap);
	}

	if (AssistVolume)
	{
		AssistVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ABasketballHoop::OnAssistVolumeBeginOverlap);
	}

	if (BackboardSweetSpotVolume)
	{
		BackboardSweetSpotVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ABasketballHoop::OnSweetSpotVolumeBeginOverlap);
	}
}

void ABasketballHoop::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GCImpulseHandle);
		World->GetTimerManager().ClearTimer(GCFreezeHandle);
	}

	Super::EndPlay(EndPlayReason);
}

bool ABasketballHoop::TriggerScore(ABasketballBall* Ball)
{
	if (!Ball)
	{
		return false;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (Now - LastScoreTime < ScoreCooldown)
	{
		return false;
	}

	UPrimitiveComponent* BallPrimitive = Cast<UPrimitiveComponent>(Ball->GetRootComponent());
	const FVector BallVelocity = BallPrimitive ? BallPrimitive->GetPhysicsLinearVelocity() : Ball->GetVelocity();
	if (bRequireDownwardVelocity && BallVelocity.Z > -MinDownwardSpeed)
	{
		return false;
	}

	if (bRequireChargedBall && !Ball->IsCombatScoreReady())
	{
		return false;
	}

	LastScoreTime = Now;
	++ScoreCount;
	LastBallSpeed = BallVelocity.Size();
	LastScoreLocation = BallPrimitive ? BallPrimitive->GetComponentLocation() : Ball->GetActorLocation();

	if (bLogScore)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BasketballHoop] %s scored by %s. Count=%d Speed=%.0f"),
			*GetName(), *Ball->GetName(), ScoreCount, LastBallSpeed);
	}

	PlayScoreFeedback(LastScoreLocation);
	if (bEnableScoreExplosion && !bHasExploded)
	{
		bHasExploded = true;
		TriggerScoreExplosion(LastScoreLocation, CalculateExplosionPower(LastBallSpeed));
	}
	TriggerArenaCompletion();
	OnBasketballHoopScored.Broadcast(this, Ball, LastBallSpeed, LastScoreLocation);

	return true;
}

void ABasketballHoop::TriggerArenaCompletion()
{
	if (!bKillArenaNPCsOnScore)
	{
		return;
	}

	AArenaManager* ArenaManager = ResolveArenaManager();
	if (!ArenaManager)
	{
		if (bLogScore)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BasketballHoop] %s could not find ArenaManager for score completion."), *GetName());
		}
		return;
	}

	ArenaManager->CompleteArenaAfterKillingAllNPCs(
		bSequentialArenaKills,
		ArenaKillDelayBetweenNPCs,
		ArenaKillDeathVFX,
		bSuppressArenaKillDrops,
		bGrantUpgradeAfterArenaClear);
}

AArenaManager* ABasketballHoop::ResolveArenaManager() const
{
	if (TargetArenaManager)
	{
		return TargetArenaManager;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AArenaManager* FirstManager = nullptr;
	ULevel* HoopLevel = GetLevel();
	for (TActorIterator<AArenaManager> It(World); It; ++It)
	{
		AArenaManager* Manager = *It;
		if (!FirstManager)
		{
			FirstManager = Manager;
		}
		if (HoopLevel && Manager && Manager->GetLevel() == HoopLevel)
		{
			return Manager;
		}
	}

	return FirstManager;
}

void ABasketballHoop::OnScoreVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ABasketballBall* Ball = Cast<ABasketballBall>(OtherActor))
	{
		TriggerScore(Ball);
		return;
	}

	if (OtherActor)
	{
		if (ABasketballBall* BallOwner = Cast<ABasketballBall>(OtherActor->GetOwner()))
		{
			TriggerScore(BallOwner);
		}
	}
}

void ABasketballHoop::OnAssistVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryStartAssistForActor(OtherActor, false);
}

void ABasketballHoop::OnSweetSpotVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryStartAssistForActor(OtherActor, true);
}

void ABasketballHoop::TryStartAssistForActor(AActor* OtherActor, bool bSweetSpot)
{
	if (!bEnableAssist || !OtherActor)
	{
		return;
	}

	ABasketballBall* Ball = Cast<ABasketballBall>(OtherActor);
	if (!Ball)
	{
		Ball = Cast<ABasketballBall>(OtherActor->GetOwner());
	}

	if (!Ball || (bAssistRequiresChargedBall && !Ball->IsCombatScoreReady()))
	{
		return;
	}

	const float Duration = bSweetSpot ? SweetSpotAssistDuration : AssistDuration;
	const float Strength = bSweetSpot ? SweetSpotAssistStrength : AssistStrength;
	const float VelocityBlend = bSweetSpot ? SweetSpotVelocityBlend : AssistVelocityBlend;
	Ball->StartHoopAssist(GetAssistTargetLocation(), Duration, Strength, VelocityBlend);

	if (bLogAssist)
	{
		UE_LOG(LogTemp, Log, TEXT("[BasketballHoop] %s assist %s for %s. Duration=%.2f Strength=%.2f"),
			*GetName(), bSweetSpot ? TEXT("sweet spot") : TEXT("near rim"), *Ball->GetName(), Duration, Strength);
	}
}

FVector ABasketballHoop::GetAssistTargetLocation() const
{
	const FVector BaseLocation = ScoreVolume ? ScoreVolume->GetComponentLocation() : GetActorLocation();
	return BaseLocation + GetActorTransform().TransformVectorNoScale(AssistTargetLocalOffset);
}

void ABasketballHoop::PlayScoreFeedback(const FVector& Location)
{
	if (ScoreVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ScoreVFX, Location);
	}

	if (ScoreSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ScoreSound, Location);
	}
}

float ABasketballHoop::CalculateExplosionPower(float BallSpeed) const
{
	const float SafeFullSpeed = FMath::Max(FullExplosionSpeed, SlowExplosionSpeed + 1.0f);
	return FMath::Clamp((BallSpeed - SlowExplosionSpeed) / (SafeFullSpeed - SlowExplosionSpeed), 0.0f, 1.0f);
}

void ABasketballHoop::TriggerScoreExplosion(const FVector& Origin, float ExplosionPower)
{
	const float ClampedPower = FMath::Clamp(ExplosionPower, 0.0f, 1.0f);
	PlayExplosionFeedback(Origin, ClampedPower);

	// Each GC inherits its source component's world transform, including any per-mesh BP edits.
	// If no GC is assigned, the source mesh is still hidden so the exploded part disappears.
	SpawnAndBreakGeometryCollection(StandFrameMesh, StandFrameGeometryCollection);
	SpawnAndBreakGeometryCollection(BackboardMesh, BackboardGeometryCollection);
	SpawnAndBreakGeometryCollection(RimMesh, RimGeometryCollection);
	SpawnAndBreakGeometryCollection(NetMesh, NetGeometryCollection);

	if (SpawnedGeometryCollectionActors.IsEmpty() || !GetWorld())
	{
		return;
	}

	const float Radius = FMath::Lerp(SlowImpulseRadius, FullImpulseRadius, ClampedPower);
	const float Impulse = FMath::Lerp(SlowRadialImpulse, FullRadialImpulse, ClampedPower);
	GetWorldTimerManager().SetTimer(GCImpulseHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, Origin, Radius, Impulse]()
		{
			for (AGeometryCollectionActor* Actor : SpawnedGeometryCollectionActors)
			{
				if (Actor)
				{
					if (UGeometryCollectionComponent* GC = Actor->GetGeometryCollectionComponent())
					{
						GC->AddRadialImpulse(Origin, Radius, Impulse, ImpulseFalloff, bImpulseVelocityChange);
					}
				}
			}
		}),
		0.05f,
		false);

	if (GCGibFreezeTime > 0.0f)
	{
		GetWorldTimerManager().SetTimer(GCFreezeHandle, this,
			&ABasketballHoop::FreezeSpawnedGeometryCollections, GCGibFreezeTime, false);
	}
}

void ABasketballHoop::PlayExplosionFeedback(const FVector& Origin, float ExplosionPower) const
{
	if (ExplosionVFX)
	{
		const float VFXScale = FMath::Lerp(SlowExplosionVFXScale, FullExplosionVFXScale, ExplosionPower);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ExplosionVFX, Origin, FRotator::ZeroRotator,
			FVector(VFXScale), true, true, ENCPoolMethod::None, true);
	}

	if (ExplosionSound)
	{
		const float Volume = FMath::Lerp(SlowExplosionSoundVolume, FullExplosionSoundVolume, ExplosionPower);
		const float Pitch = FMath::Lerp(SlowExplosionSoundPitch, FullExplosionSoundPitch, ExplosionPower);
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, Origin, Volume, Pitch);
	}
}

void ABasketballHoop::SpawnAndBreakGeometryCollection(UStaticMeshComponent* SourceMesh,
	UGeometryCollection* GeometryCollection)
{
	if (!SourceMesh)
	{
		return;
	}

	const FTransform SourceTransform = SourceMesh->GetComponentTransform();
	SourceMesh->SetVisibility(false, true);
	SourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (!GeometryCollection || !GetWorld())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGeometryCollectionActor* SpawnedActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		SourceTransform.GetLocation(), SourceTransform.GetRotation().Rotator(), SpawnParams);
	if (!SpawnedActor)
	{
		return;
	}

	UGeometryCollectionComponent* GC = SpawnedActor->GetGeometryCollectionComponent();
	if (!GC)
	{
		SpawnedActor->Destroy();
		return;
	}

	SpawnedActor->SetActorTransform(SourceTransform, false, nullptr, ETeleportType::TeleportPhysics);
	GC->SetRestCollection(GeometryCollection);
	for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
	{
		if (UMaterialInterface* Material = SourceMesh->GetMaterial(MaterialIndex))
		{
			GC->SetMaterial(MaterialIndex, Material);
		}
	}

	GC->SetCollisionProfileName(GCGibCollisionProfile);
	GC->SetSimulatePhysics(true);
	GC->SetEnableGravity(true);
	GC->RecreatePhysicsState();

	UUniformScalar* StrainField = NewObject<UUniformScalar>(SpawnedActor);
	StrainField->Magnitude = FMath::Max(0.0f, GCExternalStrain);
	GC->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr,
		StrainField);

	if (GCGibLifetime > 0.0f)
	{
		SpawnedActor->SetLifeSpan(GCGibLifetime);
	}

	SpawnedGeometryCollectionActors.Add(SpawnedActor);
}

void ABasketballHoop::FreezeSpawnedGeometryCollections()
{
	for (AGeometryCollectionActor* Actor : SpawnedGeometryCollectionActors)
	{
		if (Actor)
		{
			if (UGeometryCollectionComponent* GC = Actor->GetGeometryCollectionComponent())
			{
				GC->SetSimulatePhysics(false);
			}
		}
	}
}
