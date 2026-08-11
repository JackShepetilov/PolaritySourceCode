// SportsGoal.cpp

#include "Arena/SportsGoal.h"

#include "Arena/ArenaManager.h"
#include "Arena/SportsBall.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Field/FieldSystemObjects.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "EngineUtils.h"

ASportsGoal::ASportsGoal()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(SceneRoot);
	FrameMesh->SetCollisionProfileName(TEXT("BlockAll"));

	NetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NetMesh"));
	NetMesh->SetupAttachment(SceneRoot);
	NetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GoalVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("GoalVolume"));
	GoalVolume->SetupAttachment(SceneRoot);
	GoalVolume->SetBoxExtent(FVector(90.0f, 220.0f, 160.0f));
	GoalVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GoalVolume->SetCollisionObjectType(ECC_WorldDynamic);
	GoalVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	GoalVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	GoalVolume->SetGenerateOverlapEvents(true);

	ExplosionOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("ExplosionOrigin"));
	ExplosionOrigin->SetupAttachment(SceneRoot);
}

void ASportsGoal::BeginPlay()
{
	Super::BeginPlay();

	if (GoalVolume)
	{
		GoalVolume->OnComponentBeginOverlap.AddDynamic(this, &ASportsGoal::OnGoalVolumeBeginOverlap);
	}
}

void ASportsGoal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GCImpulseHandle);
		World->GetTimerManager().ClearTimer(GCFreezeHandle);
	}

	Super::EndPlay(EndPlayReason);
}

bool ASportsGoal::TriggerGoal(ASportsBall* Ball)
{
	if (!Ball || (bOneShot && bHasScored))
	{
		return false;
	}

	UPrimitiveComponent* BallPrimitive = Cast<UPrimitiveComponent>(Ball->GetRootComponent());
	const FVector BallVelocity = BallPrimitive ? BallPrimitive->GetPhysicsLinearVelocity() : Ball->GetVelocity();
	const float BallSpeed = BallVelocity.Size();
	const float GoalPower = CalculateGoalPower(BallSpeed);
	const FVector Origin = BallPrimitive ? BallPrimitive->GetComponentLocation()
		: (ExplosionOrigin ? ExplosionOrigin->GetComponentLocation() : GetActorLocation());

	bHasScored = true;
	LastBallSpeed = BallSpeed;
	LastGoalPower = GoalPower;
	LastExplosionOrigin = Origin;

	if (bLogGoal)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SportsGoal] %s scored by %s. Speed=%.0f Power=%.2f Origin=(%.0f, %.0f, %.0f)"),
			*GetName(),
			*Ball->GetName(),
			BallSpeed,
			GoalPower,
			Origin.X,
			Origin.Y,
			Origin.Z);
	}

	PlayGoalFeedback(Origin, GoalPower);
	HideNet();
	ResolveBallAfterGoal(Ball);
	ApplyExplosionImpulse(Origin, GoalPower);
	SpawnAndBreakFrameGC(Origin, GoalPower);
	TriggerArenaManagerKillEvent();

	OnSportsGoalScored.Broadcast(this, Ball, BallSpeed, GoalPower, Origin);
	return true;
}

float ASportsGoal::CalculateGoalPower(float BallSpeed) const
{
	if (SpeedToPowerCurve)
	{
		return FMath::Clamp(SpeedToPowerCurve->GetFloatValue(BallSpeed), 0.0f, 1.0f);
	}

	const float SafeFullSpeed = FMath::Max(1.0f, FullPowerSpeed);
	const float NormalizedSpeed = FMath::Clamp(BallSpeed / SafeFullSpeed, 0.0f, 1.0f);
	return FMath::Clamp(FMath::Lerp(SlowGoalPower, 1.0f, NormalizedSpeed), 0.0f, 1.0f);
}

void ASportsGoal::OnGoalVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ASportsBall* Ball = Cast<ASportsBall>(OtherActor))
	{
		TriggerGoal(Ball);
		return;
	}

	if (OtherActor)
	{
		if (ASportsBall* BallOwner = Cast<ASportsBall>(OtherActor->GetOwner()))
		{
			TriggerGoal(BallOwner);
		}
	}
}

void ASportsGoal::TriggerArenaManagerKillEvent()
{
	if (!bKillArenaNPCsOnGoal)
	{
		return;
	}

	AArenaManager* ArenaManager = ResolveArenaManager();
	if (!ArenaManager)
	{
		if (bLogGoal)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SportsGoal] %s could not find ArenaManager for goal kill event."),
				*GetName());
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

AArenaManager* ASportsGoal::ResolveArenaManager() const
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
	ULevel* GoalLevel = GetLevel();
	for (TActorIterator<AArenaManager> It(World); It; ++It)
	{
		AArenaManager* Manager = *It;
		if (!Manager)
		{
			continue;
		}

		if (!FirstManager)
		{
			FirstManager = Manager;
		}

		if (GoalLevel && Manager->GetLevel() == GoalLevel)
		{
			return Manager;
		}
	}

	return FirstManager;
}

void ASportsGoal::HideNet()
{
	if (!NetMesh)
	{
		return;
	}

	if (bHideNetOnGoal)
	{
		NetMesh->SetVisibility(false, true);
	}

	if (bDisableNetCollisionOnGoal)
	{
		NetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NetMesh->SetGenerateOverlapEvents(false);
	}
}

void ASportsGoal::ResolveBallAfterGoal(ASportsBall* Ball)
{
	if (!Ball)
	{
		return;
	}

	if (UPrimitiveComponent* BallPrimitive = Cast<UPrimitiveComponent>(Ball->GetRootComponent()))
	{
		BallPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
		BallPrimitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		BallPrimitive->SetSimulatePhysics(false);

		if (bDisableBallCollisionOnGoal)
		{
			BallPrimitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BallPrimitive->SetGenerateOverlapEvents(false);
		}
	}

	if (bHideBallOnGoal)
	{
		Ball->SetActorHiddenInGame(true);
	}
}

void ASportsGoal::PlayGoalFeedback(const FVector& Origin, float GoalPower)
{
	const float ClampedPower = FMath::Clamp(GoalPower, 0.0f, 1.0f);

	if (GoalVFX)
	{
		const float VFXScale = FMath::Lerp(SlowVFXScale, FullVFXScale, ClampedPower);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			GoalVFX,
			Origin,
			FRotator::ZeroRotator,
			FVector(VFXScale),
			true,
			true,
			ENCPoolMethod::None,
			true);
	}

	if (GoalSound)
	{
		const float Volume = FMath::Lerp(SlowSoundVolume, FullSoundVolume, ClampedPower);
		const float Pitch = FMath::Lerp(SlowSoundPitch, FullSoundPitch, ClampedPower);
		UGameplayStatics::PlaySoundAtLocation(this, GoalSound, Origin, Volume, Pitch);
	}
}

void ASportsGoal::ApplyExplosionImpulse(const FVector& Origin, float GoalPower)
{
	const float Radius = FMath::Lerp(SlowImpulseRadius, FullImpulseRadius, GoalPower);
	const float Impulse = FMath::Lerp(SlowRadialImpulse, FullRadialImpulse, GoalPower);

	if (Radius <= 0.0f || Impulse <= 0.0f)
	{
		return;
	}

	auto ApplyToPrimitive = [&](UPrimitiveComponent* Primitive)
	{
		if (Primitive && Primitive->IsSimulatingPhysics())
		{
			Primitive->AddRadialImpulse(Origin, Radius, Impulse, ImpulseFalloff, bImpulseVelocityChange);
		}
	};

	ApplyToPrimitive(FrameMesh);
	for (UPrimitiveComponent* Piece : ExtraImpulsePieces)
	{
		ApplyToPrimitive(Piece);
	}
}

void ASportsGoal::SpawnAndBreakFrameGC(const FVector& Origin, float GoalPower)
{
	if (!FrameGeometryCollection || !FrameMesh || !GetWorld())
	{
		return;
	}

	const FTransform FrameTransform = FrameMesh->GetComponentTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedFrameGCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		FrameTransform.GetLocation(),
		FrameTransform.GetRotation().Rotator(),
		SpawnParams);

	if (!SpawnedFrameGCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = SpawnedFrameGCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		SpawnedFrameGCActor->Destroy();
		SpawnedFrameGCActor = nullptr;
		return;
	}

	SpawnedFrameGCActor->SetActorScale3D(FrameTransform.GetScale3D());
	GCComp->SetRestCollection(FrameGeometryCollection);

	const int32 NumMaterials = FrameMesh->GetNumMaterials();
	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		if (UMaterialInterface* Material = FrameMesh->GetMaterial(Index))
		{
			GCComp->SetMaterial(Index, Material);
		}
	}

	GCComp->SetCollisionProfileName(FrameGibCollisionProfile);
	GCComp->SetSimulatePhysics(true);
	GCComp->SetEnableGravity(true);
	GCComp->RecreatePhysicsState();

	if (bHideFrameMeshWhenGCSpawns)
	{
		FrameMesh->SetVisibility(false, true);
		FrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	UUniformScalar* StrainField = NewObject<UUniformScalar>(SpawnedFrameGCActor);
	StrainField->Magnitude = FMath::Max(0.0f, GCExternalStrain);
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr,
		StrainField);

	const float Radius = FMath::Lerp(SlowImpulseRadius, FullImpulseRadius, GoalPower);
	const float Impulse = FMath::Lerp(SlowRadialImpulse, FullRadialImpulse, GoalPower);
	GetWorldTimerManager().SetTimer(GCImpulseHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, Origin, Radius, Impulse]()
		{
			if (!SpawnedFrameGCActor)
			{
				return;
			}

			if (UGeometryCollectionComponent* GC = SpawnedFrameGCActor->GetGeometryCollectionComponent())
			{
				GC->AddRadialImpulse(Origin, Radius, Impulse, ImpulseFalloff, bImpulseVelocityChange);
			}
		}),
		0.05f,
		false);

	if (GCGibLifetime > 0.0f)
	{
		SpawnedFrameGCActor->SetLifeSpan(GCGibLifetime);
	}

	if (GCGibFreezeTime > 0.0f)
	{
		GetWorldTimerManager().SetTimer(GCFreezeHandle, this, &ASportsGoal::FreezeSpawnedGC, GCGibFreezeTime, false);
	}
}

void ASportsGoal::FreezeSpawnedGC()
{
	if (!SpawnedFrameGCActor)
	{
		return;
	}

	if (UGeometryCollectionComponent* GC = SpawnedFrameGCActor->GetGeometryCollectionComponent())
	{
		GC->SetSimulatePhysics(false);
	}
}
