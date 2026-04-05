// EMFPhysicsProp.cpp
// Physics-simulated prop with full EMF system integration

#include "EMFPhysicsProp.h"
#include "Engine/DamageEvents.h"
#include "EMFChannelingPlateActor.h"
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/DamageTypes/DamageType_Wallslam.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFProximity.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "EMFVelocityModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraFunctionLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Variant_Shooter/UI/EMFChargeWidgetSubsystem.h"
#include "Variant_Shooter/ShooterDoor.h"
#include "ShooterCharacter.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "AssetRegistry/AssetRegistryModule.h"

#if WITH_EDITOR
#include "GCBatchCreatorLibrary.h"
#endif

AEMFPhysicsProp::AEMFPhysicsProp()
{
	PrimaryActorTick.bCanEverTick = true;

	// Physics mesh as root (physics body drives actor transform)
	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	SetRootComponent(PropMesh);
	PropMesh->SetSimulatePhysics(true);
	PropMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	PropMesh->BodyInstance.bUseCCD = true;
	PropMesh->BodyInstance.bNotifyRigidBodyCollision = true;

	// Default: Block on Pawn (normal physics collision when free)
	// Switched to Overlap dynamically when captured (see SetCapturedByPlate/ReleasedFromCapture)
	PropMesh->SetGenerateOverlapEvents(true);

	// EMF field component
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
	if (FieldComponent)
	{
		FieldComponent->SetOwnerType(EEMSourceOwnerType::PhysicsProp);
	}
}

// ==================== Editor: Auto-assign GC when PropMesh changes ====================

#if WITH_EDITOR
void AEMFPhysicsProp::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName != GET_MEMBER_NAME_CHECKED(AEMFPhysicsProp, PropMesh))
	{
		return;
	}

	// PropMesh component changed — try to find a matching GC asset
	if (!PropMesh || !PropMesh->GetStaticMesh())
	{
		PropGeometryCollection = nullptr;
		return;
	}

	const UStaticMesh* Mesh = PropMesh->GetStaticMesh();
	const FString MeshName = Mesh->GetName();
	const FString MeshPackagePath = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	const FString GCName = FString::Printf(TEXT("GC_%s"), *MeshName);

	// Search for GC_{MeshName} in the same folder and all subfolders
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> FoundAssets;
	AssetRegistry.GetAssetsByPath(FName(*MeshPackagePath), FoundAssets, /*bRecursive=*/true);

	UGeometryCollection* FoundGC = nullptr;
	for (const FAssetData& Asset : FoundAssets)
	{
		if (Asset.AssetName == FName(*GCName) && Asset.AssetClassPath == UGeometryCollection::StaticClass()->GetClassPathName())
		{
			FoundGC = Cast<UGeometryCollection>(Asset.GetAsset());
			break;
		}
	}

	if (FoundGC)
	{
		PropGeometryCollection = FoundGC;
		UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: Auto-assigned GC '%s'"), *GetName(), *GCName);
		return;
	}

	// Not found — use fallback
	if (FallbackGeometryCollection)
	{
		PropGeometryCollection = FallbackGeometryCollection;
		UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp %s: No GC '%s' found, using fallback"), *GetName(), *GCName);
	}
	else
	{
		PropGeometryCollection = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp %s: No GC '%s' found and no fallback set"), *GetName(), *GCName);
	}
}
#endif

// ==================== AActor Overrides ====================

void AEMFPhysicsProp::BeginPlay()
{
	Super::BeginPlay();

	// Runtime GC auto-lookup: if no GC assigned, search for GC_{MeshName} in mesh folder + subfolders
	if (!PropGeometryCollection && PropMesh && PropMesh->GetStaticMesh())
	{
		const UStaticMesh* Mesh = PropMesh->GetStaticMesh();
		const FString MeshPackagePath = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		const FString GCName = FString::Printf(TEXT("GC_%s"), *Mesh->GetName());

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> FoundAssets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*MeshPackagePath), FoundAssets, /*bRecursive=*/true);

		for (const FAssetData& Asset : FoundAssets)
		{
			if (Asset.AssetName == FName(*GCName) && Asset.AssetClassPath == UGeometryCollection::StaticClass()->GetClassPathName())
			{
				PropGeometryCollection = Cast<UGeometryCollection>(Asset.GetAsset());
				break;
			}
		}

		if (!PropGeometryCollection && FallbackGeometryCollection)
		{
			PropGeometryCollection = FallbackGeometryCollection;
		}
	}

	CurrentHP = MaxHP;

	// Initialize EMF field component
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = DefaultCharge;
		Desc.PhysicsParams.Mass = DefaultMass;
		// OwnerType is NOT overridden here — use whatever is set on the FieldComponent
		// (defaults to PhysicsProp in C++ constructor, but can be changed per-instance in editor)
		FieldComponent->SetSourceDescription(Desc);
	}

	// Sync physics body mass with EMF mass + collision setup
	if (PropMesh)
	{
		PropMesh->SetMassOverrideInKg(NAME_None, DefaultMass, true);
		PropMesh->OnComponentHit.AddDynamic(this, &AEMFPhysicsProp::OnPropHit);
		PropMesh->OnComponentBeginOverlap.AddDynamic(this, &AEMFPhysicsProp::OnPropOverlap);

		// Zero-restitution physics material: prop stops on contact instead of bouncing
		UPhysicalMaterial* PropPhysMat = NewObject<UPhysicalMaterial>(this);
		PropPhysMat->Restitution = 0.0f;
		PropPhysMat->Friction = 0.5f;
		PropPhysMat->RestitutionCombineMode = EFrictionCombineMode::Min;
		PropMesh->SetPhysMaterialOverride(PropPhysMat);
	}

	// Register with charge widget subsystem
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->RegisterProp(this);
	}

	// Uncharged props start with physics and tick disabled (static blockers).
	// Both are enabled when prop receives charge via SetCharge().
	// This prevents NPC depenetration impulses from triggering false explosions.
	if (PropMesh && FMath::IsNearlyZero(DefaultCharge))
	{
		PropMesh->SetSimulatePhysics(false);
		SetActorTickEnabled(false);
	}
}

void AEMFPhysicsProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(GCFreezeTimer);
	GetWorld()->GetTimerManager().ClearTimer(GCCleanupTimer);

	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterProp(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AEMFPhysicsProp::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		return;
	}

	if (bAffectedByExternalFields && FieldComponent && PropMesh && PropMesh->IsSimulatingPhysics())
	{
		ApplyEMForces(DeltaTime);
	}

	if (bCanBeCaptured && CapturingPlate.IsValid())
	{
		UpdateCaptureForces(DeltaTime);
	}

	// Cache speed for explosion checks (before collision callbacks modify velocity)
	if (PropMesh)
	{
		CachedPreCollisionSpeed = PropMesh->GetPhysicsLinearVelocity().Size();
	}

	// Debug: always-visible capture range sphere around this prop
	if (bDrawDebugForces && bCanBeCaptured)
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), CalculateCaptureRange(), 32, FColor::Cyan, false, -1.0f, 0, 1.5f);
	}
}

// ==================== EMF Force Application ====================

void AEMFPhysicsProp::ApplyEMForces(float DeltaTime)
{
	const float Charge = GetCharge();
	if (FMath::IsNearlyZero(Charge))
	{
		return;
	}

	TArray<FEMSourceDescription> OtherSources = FieldComponent->GetAllOtherSources();
	if (OtherSources.Num() == 0)
	{
		return;
	}

	const FVector Position = GetActorLocation();
	const FVector Velocity = PropMesh->GetPhysicsLinearVelocity();
	const float MaxDistSq = MaxSourceDistance * MaxSourceDistance;
	const float OppositeChargeMinDistSq = OppositeChargeMinDistance * OppositeChargeMinDistance;
	const int32 MyChargeSign = (Charge > KINDA_SMALL_NUMBER) ? 1 : ((Charge < -KINDA_SMALL_NUMBER) ? -1 : 0);

	FVector TotalForce = FVector::ZeroVector;
	bool bShouldApplyProximityDamping = false;

	// Pre-allocate once, reuse in loop (avoids heap allocation per source per tick)
	TArray<FEMSourceDescription> SingleSource;
	SingleSource.Reserve(1);

	for (const FEMSourceDescription& Source : OtherSources)
	{
		if (IsSourceEffectivelyZero(Source))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Position, Source.Position);

		if (DistSq > MaxDistSq)
		{
			continue;
		}

		// Opposite-charge distance cutoff: skip close opposite-charge sources to prevent Coulomb 1/r² singularity
		if (bEnableOppositeChargeDistanceCutoff && DistSq < OppositeChargeMinDistSq)
		{
			const int32 SourceChargeSign = GetSourceEffectiveChargeSign(Source);
			if (SourceChargeSign != 0 && MyChargeSign != 0 && SourceChargeSign != MyChargeSign)
			{
				bShouldApplyProximityDamping = true;
				continue;
			}
		}

		// LOS Shielding: skip sources blocked by geometry
		if (bEnableLOSShielding)
		{
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams(SCENE_QUERY_STAT(EMF_LOS), true, this);
			bool bBlocked = GetWorld()->LineTraceSingleByChannel(
				LOSHit, Position, Source.Position, LOSTraceChannel, LOSParams);

			if (bDrawLOSDebug)
			{
				DrawDebugLine(GetWorld(), Position, Source.Position,
					bBlocked ? FColor::Red : FColor::Green, false, -1.0f, 0, 0.5f);
			}

			if (bBlocked)
			{
				continue;
			}
		}

		const float Multiplier = GetForceMultiplierForOwnerType(Source.OwnerType);
		if (FMath::IsNearlyZero(Multiplier))
		{
			continue;
		}

		// Skip channeling plate forces entirely for capturable props:
		// - If captured: UpdateCaptureForces handles positioning (spring + damping)
		// - If NOT captured: prevent uncaptured props from being attracted by plate's EM field
		//   (mirrors NPC logic in EMFVelocityModifier where non-captured NPCs skip plate forces)
		if (bCanBeCaptured &&
			Source.SourceType == EEMSourceType::FinitePlate &&
			Source.OwnerType == EEMSourceOwnerType::Player)
		{
			continue;
		}

		SingleSource.Reset();
		SingleSource.Add(Source);

		const FVector SourceForce = UEMF_PluginBPLibrary::CalculateLorentzForceComplete(
			Charge, Position, Velocity, SingleSource, true);

		TotalForce += SourceForce * Multiplier;
	}

	// Suppress all EM forces when captured in normal mode (spring + damping handle positioning)
	// In reverse flight: let other forces through with launched multipliers
	if (CapturingPlate.IsValid() && !bIsInReverseFlight)
	{
		TotalForce = FVector::ZeroVector;
	}

	// Clamp
	if (TotalForce.SizeSquared() > MaxEMForce * MaxEMForce)
	{
		TotalForce = TotalForce.GetSafeNormal() * MaxEMForce;
	}

	// EMF-specific Coulomb friction: subtract friction force from tangential EMF component
	// when prop rests on a surface. Does NOT affect normal physics interactions.
	if (bApplyEMFSurfaceFriction && EMFSurfaceFriction > 0.0f && !TotalForce.IsNearlyZero())
	{
		FHitResult GroundHit;
		FCollisionQueryParams GroundParams;
		GroundParams.AddIgnoredActor(this);

		const FVector TraceStart = Position;
		const FVector TraceEnd = Position - FVector(0.0f, 0.0f, EMFGroundTraceDistance);

		if (GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, GroundParams))
		{
			const float GravityZ = FMath::Abs(GetWorld()->GetGravityZ());
			const float PhysMass = PropMesh->GetMass();

			// Normal force = mass * gravity * cos(surface angle)
			const float NormalForce = PhysMass * GravityZ * FMath::Abs(GroundHit.Normal.Z);

			// Tangential component of EMF force (parallel to surface)
			const FVector NormalComponent = GroundHit.Normal * FVector::DotProduct(TotalForce, GroundHit.Normal);
			const FVector TangentForce = TotalForce - NormalComponent;
			const float TangentMag = TangentForce.Size();

			const float FrictionMag = EMFSurfaceFriction * NormalForce;

			if (FrictionMag >= TangentMag)
			{
				// Static friction: EMF force too weak to overcome — cancel tangential component
				TotalForce = NormalComponent;
			}
			else
			{
				// Kinetic friction: reduce tangential EMF force by friction amount
				TotalForce -= TangentForce.GetSafeNormal() * FrictionMag;
			}

			if (bDrawDebugForces && FrictionMag > 0.0f)
			{
				const FVector FrictionDir = TangentMag > KINDA_SMALL_NUMBER ? -TangentForce.GetSafeNormal() : FVector::ZeroVector;
				DrawDebugDirectionalArrow(
					GetWorld(), Position,
					Position + FrictionDir * FMath::Min(FrictionMag * 0.01f, 100.0f),
					6.0f, FColor::Yellow, false, -1.0f, 0, 2.0f);
			}
		}
	}

	// Apply as continuous force to physics body
	if (!TotalForce.IsNearlyZero())
	{
		PropMesh->AddForce(TotalForce);
	}

	// Proximity damping: viscous braking when inside opposite-charge cutoff distance
	// Prevents prop from passing through the source after EM force is suppressed
	if (bShouldApplyProximityDamping && OppositeChargeProximityDamping > 0.0f)
	{
		const float PhysMass = PropMesh->GetMass();
		const FVector DampingForce = -Velocity * OppositeChargeProximityDamping * PhysMass;
		PropMesh->AddForce(DampingForce);

		if (bDrawDebugForces)
		{
			DrawDebugDirectionalArrow(
				GetWorld(), Position,
				Position + DampingForce.GetSafeNormal() * FMath::Min(DampingForce.Size() * 0.01f, 100.0f),
				8.0f, FColor::Orange, false, -1.0f, 0, 2.0f);
		}
	}

	// Debug
	if (bDrawDebugForces && !TotalForce.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(
			GetWorld(), Position,
			Position + TotalForce.GetSafeNormal() * FMath::Min(TotalForce.Size() * 0.01f, 200.0f),
			10.0f, FColor::Cyan, false, -1.0f, 0, 2.0f);
	}

	if (bLogEMForces && !TotalForce.IsNearlyZero())
	{
		UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: Charge=%.2f Force=(%.0f, %.0f, %.0f) Sources=%d"),
			*GetName(), Charge, TotalForce.X, TotalForce.Y, TotalForce.Z, OtherSources.Num());
	}
}

// ==================== Channeling Capture ====================

float AEMFPhysicsProp::CalculateCaptureRange() const
{
	// Get player charge from the plate (if captured) or from player pawn
	float PlayerCharge = 0.0f;
	if (CapturingPlate.IsValid())
	{
		PlayerCharge = CapturingPlate.Get()->GetPlateChargeDensity();
	}
	else if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0))
	{
		if (UEMFVelocityModifier* PlayerMod = PlayerChar->FindComponentByClass<UEMFVelocityModifier>())
		{
			PlayerCharge = PlayerMod->GetCharge();
		}
	}

	const float PropChargeAbs = FMath::Abs(GetCharge());
	const float PlayerChargeAbs = FMath::Abs(PlayerCharge);

	// No capture possible without charge on both sides
	if (PropChargeAbs < KINDA_SMALL_NUMBER || PlayerChargeAbs < KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float ChargeProduct = PlayerChargeAbs * PropChargeAbs;
	const float Ratio = ChargeProduct / FMath::Max(CaptureChargeNormCoeff, 0.01f);
	const float RangeMultiplier = FMath::Max(1.0f, 1.0f + FMath::Loge(Ratio));

	return CaptureBaseRange * RangeMultiplier;
}

void AEMFPhysicsProp::SetCapturedByPlate(AEMFChannelingPlateActor* Plate)
{
	if (!Plate || !bCanBeCaptured)
	{
		return;
	}

	CapturingPlate = Plate;
	WeakCaptureTimer = 0.0f;
	bHasPreviousPlatePosition = false;
	bReverseLaunchInitialized = false;

	// Let spring/damping pull the prop smoothly to plate center (no teleport).
	// Only zero out velocity so the prop doesn't overshoot on first capture.
	if (PropMesh && PropMesh->IsSimulatingPhysics())
	{
		PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);

		// Switch to Overlap with Pawns: no physics impulse while captured near player
		PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
}

void AEMFPhysicsProp::ReleasedFromCapture()
{
	CapturingPlate.Reset();
	bHasPreviousPlatePosition = false;
	WeakCaptureTimer = 0.0f;
	bReverseLaunchInitialized = false;

	// Restore Block with Pawns: normal physics collision when free
	if (PropMesh)
	{
		PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

void AEMFPhysicsProp::DetachFromPlate()
{
	CapturingPlate.Reset();
	bHasPreviousPlatePosition = false;
	bReverseLaunchInitialized = false;
}

AShooterNPC* AEMFPhysicsProp::FindHomingTarget(const FVector& Position, const FVector& AimDirection) const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(HomingMaxRange);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->OverlapMultiByChannel(
		Overlaps, Position, FQuat::Identity,
		ECC_Pawn, Sphere, QueryParams);

	const float ConeThreshold = FMath::Cos(FMath::DegreesToRadians(HomingConeHalfAngle));

	AShooterNPC* BestTarget = nullptr;
	float BestScore = -1.0f;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
		if (!NPC || NPC->IsDead())
		{
			continue;
		}

		const FVector ToNPC = NPC->GetActorLocation() - Position;
		const float Distance = ToNPC.Size();
		if (Distance < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector DirToNPC = ToNPC / Distance;
		const float Dot = FVector::DotProduct(AimDirection, DirToNPC);

		if (Dot < ConeThreshold)
		{
			continue;
		}

		// Score: prefer centered (high dot) and close (low distance)
		const float Score = Dot / FMath::Max(Distance / HomingMaxRange, 0.01f);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = NPC;
		}
	}

	return BestTarget;
}

void AEMFPhysicsProp::UpdateCaptureForces(float DeltaTime)
{
	AEMFChannelingPlateActor* Plate = CapturingPlate.Get();
	if (!Plate || !PropMesh || !PropMesh->IsSimulatingPhysics())
	{
		return;
	}

	const FVector Position = GetActorLocation();
	const FVector PlatePos = Plate->GetActorLocation();
	const float Distance = FVector::Dist(Position, PlatePos);

	// Wall check: if there's a wall between prop and plate, don't apply capture forces
	{
		FHitResult WallCheck;
		FCollisionQueryParams WallParams;
		WallParams.AddIgnoredActor(this);
		WallParams.AddIgnoredActor(Plate);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			WallCheck, Position, PlatePos, ECC_Visibility, WallParams);

		if (bBlocked)
		{
			WeakCaptureTimer += DeltaTime;
			if (WeakCaptureTimer >= CaptureReleaseTimeout)
			{
				ReleasedFromCapture();
			}
			return;
		}
	}

	// Dynamic capture range based on charge product
	const float EffectiveCaptureRange = CalculateCaptureRange();

	// Smoothstep capture strength
	float CaptureStrength = 0.0f;
	if (Distance < EffectiveCaptureRange)
	{
		const float T = Distance / EffectiveCaptureRange;
		CaptureStrength = 1.0f - T * T * (3.0f - 2.0f * T);
	}

	// Auto-release check
	if (CaptureStrength < CaptureMinStrength)
	{
		WeakCaptureTimer += DeltaTime;
		if (WeakCaptureTimer >= CaptureReleaseTimeout)
		{
			ReleasedFromCapture();
			return;
		}
	}
	else
	{
		WeakCaptureTimer = 0.0f;
	}

	// Plate velocity via finite difference
	FVector PlateVelocity = FVector::ZeroVector;
	if (bHasPreviousPlatePosition && DeltaTime > SMALL_NUMBER)
	{
		PlateVelocity = (PlatePos - PreviousPlatePosition) / DeltaTime;
	}
	PreviousPlatePosition = PlatePos;
	bHasPreviousPlatePosition = true;

	const FVector PropVelocity = PropMesh->GetPhysicsLinearVelocity();
	const FVector RelativeVelocity = PropVelocity - PlateVelocity;
	const float PhysMass = PropMesh->GetMass();

	if (Plate->IsInReverseMode())
	{
		// === REVERSE MODE: aim-line convergence ===
		// Prop flies forward at constant speed + converges laterally onto camera aim line.
		// SetPhysicsLinearVelocity each frame: full control, gravity doesn't accumulate.

		if (!bReverseLaunchInitialized)
		{
			bReverseLaunchInitialized = true;
			bIsInReverseFlight = true;
			bHasExploded = false;
			ReverseLaunchElapsed = 0.0f;

			const float LaunchDistance = EffectiveCaptureRange * ReverseLaunchDistanceMultiplier;
			ReverseLaunchSpeed = LaunchDistance / FMath::Max(ReverseLaunchFlightDuration, 0.01f);

			// Apply spin on launch
			if (ReverseLaunchSpinSpeed > 0.0f && PropMesh)
			{
				const FVector RandomAxis = FMath::VRand();
				PropMesh->SetPhysicsAngularVelocityInDegrees(RandomAxis * ReverseLaunchSpinSpeed);
			}
		}

		ReverseLaunchElapsed += DeltaTime;

		// Aim line from camera position along plate normal (= camera forward)
		const FVector PropPos = GetActorLocation();
		const APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
		const FVector AimOrigin = CamMgr ? CamMgr->GetCameraLocation() : Plate->GetActorLocation();
		FVector AimDir = Plate->GetPlateNormal();

		// Soft homing: bias aim direction toward nearest valid target in cone
		if (bEnableReverseLaunchHoming && HomingStrength > 0.0f)
		{
			if (AShooterNPC* Target = FindHomingTarget(PropPos, AimDir))
			{
				const FVector DirToTarget = (Target->GetActorLocation() - PropPos).GetSafeNormal();
				const float RampAlpha = HomingRampUpTime > 0.0f
					? FMath::Clamp(ReverseLaunchElapsed / HomingRampUpTime, 0.0f, 1.0f)
					: 1.0f;
				AimDir = FMath::Lerp(AimDir, DirToTarget, HomingStrength * RampAlpha).GetSafeNormal();
			}
		}

		// Project prop position onto aim line
		const FVector ToTarget = PropPos - AimOrigin;
		const float ForwardDist = FVector::DotProduct(ToTarget, AimDir);
		const FVector NearestOnLine = AimOrigin + ForwardDist * AimDir;
		const FVector LateralOffset = PropPos - NearestOnLine;

		// Desired velocity: forward at constant speed + lateral convergence toward aim line
		const FVector DesiredVelocity = AimDir * ReverseLaunchSpeed
			- LateralOffset * ReverseLaunchConvergenceRate;

		// Direct velocity set: bypasses gravity/physics artifacts, collision detection still works
		PropMesh->SetPhysicsLinearVelocity(DesiredVelocity);
	}
	else
	{
		// Pull-in phase: smooth interp to plate center before switching to spring/damping
		if (!bReverseLaunchInitialized)
		{
			const float DistToPlate = FVector::Dist(Position, PlatePos);
			if (DistToPlate < CapturePullInSnapDistance)
			{
				// Close enough — snap and switch to spring/damping
				bReverseLaunchInitialized = true;
				PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
				SetActorLocation(PlatePos);
			}
			else
			{
				// Exponential ease toward plate center
				const FVector NewPos = FMath::VInterpTo(Position, PlatePos, DeltaTime, CapturePullInInterpSpeed);
				SetActorLocation(NewPos);
				PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
				return;
			}
		}

		// Normal capture: damp all relative velocity
		const float DampingFactor = 1.0f - FMath::Exp(-ViscosityCoefficient * CaptureStrength * DeltaTime);
		const FVector DampingForce = -RelativeVelocity * DampingFactor * PhysMass / FMath::Max(DeltaTime, SMALL_NUMBER);

		PropMesh->AddForce(DampingForce);

		// Gravity counteraction
		if (bCounterGravityWhenCaptured)
		{
			const float GravityZ = GetWorld()->GetGravityZ();
			const float CounterForceZ = -GravityZ * GravityCounterStrength * CaptureStrength * PhysMass;
			PropMesh->AddForce(FVector(0.0f, 0.0f, CounterForceZ));
		}

		// Hooke spring: force proportional to distance (stronger pull when far, gentle near center)
		if (CaptureSpringStiffness > 0.0f)
		{
			const FVector ToPlate = PlatePos - Position;
			const FVector SpringForce = ToPlate * CaptureSpringStiffness * CaptureStrength * PhysMass;
			PropMesh->AddForce(SpringForce);
		}
	}
}

// ==================== Collision Callbacks ====================

void AEMFPhysicsProp::OnPropHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Early exit: nothing to do for slow/resting props that aren't in flight
	if (!bIsInReverseFlight && CachedPreCollisionSpeed < ExplosionSpeedThreshold)
	{
		return;
	}

	// Explosive impact: launched props use lower threshold, collateral uses higher
	if (bCanExplode && !bHasExploded && !bIsDead && PropMesh)
	{
		const float Speed = CachedPreCollisionSpeed;
		const float Threshold = bIsInReverseFlight ? ExplosionSpeedThreshold : CollateralExplosionSpeedThreshold;

		if (Speed >= Threshold)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
					FString::Printf(TEXT("[%s] BOOM! Speed=%.0f | Threshold=%.0f (%s) | Hit=%s"),
						*GetName(), Speed, Threshold,
						bIsInReverseFlight ? TEXT("Launch") : TEXT("Collateral"),
						OtherActor ? *OtherActor->GetName() : TEXT("NULL")));
			}
			if (CriticalVelocity > 0.0f && Speed >= CriticalVelocity)
			{
				OnCriticalVelocityImpact.Broadcast(this, GetActorLocation(), Speed);
			}
			Explode(1.0f, 1.0f, 1.0f);
			return;
		}
	}

	// End reverse flight state on any blocking collision (wall, floor, etc.)
	if (bIsInReverseFlight)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s: bIsInReverseFlight set to FALSE due to collision with %s"),
			*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("NULL"));
		bIsInReverseFlight = false;
	}
}

void AEMFPhysicsProp::OnPropOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || bIsDead)
	{
		return;
	}

	if (!bDealCollisionDamage)
	{
		return;
	}

	// Cooldown check
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastCollisionDamageTime < CollisionDamageCooldown)
	{
		return;
	}

	// Only damage ShooterNPC targets
	AShooterNPC* HitNPC = Cast<AShooterNPC>(OtherActor);
	if (!HitNPC || HitNPC->IsDead())
	{
		return;
	}

	// Explosive props: detonate on NPC contact — launched props use lower threshold, collateral uses higher
	if (bCanExplode && !bHasExploded)
	{
		const float OverlapThreshold = bIsInReverseFlight ? ExplosionSpeedThreshold : CollateralExplosionSpeedThreshold;
		if (CachedPreCollisionSpeed >= OverlapThreshold)
		{
			if (CriticalVelocity > 0.0f && CachedPreCollisionSpeed >= CriticalVelocity)
			{
				OnCriticalVelocityImpact.Broadcast(this, GetActorLocation(), CachedPreCollisionSpeed);
			}
			Explode(1.0f, 1.0f, 1.0f);
			return;
		}
	}

	// Impact speed from prop's velocity
	const FVector PropVelocity = PropMesh ? PropMesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const float ImpactSpeed = PropVelocity.Size();

	// Kinetic damage
	float KineticDamage = 0.0f;
	if (ImpactSpeed >= CollisionVelocityThreshold)
	{
		const float Excess = ImpactSpeed - CollisionVelocityThreshold;
		KineticDamage = (Excess / 100.0f) * CollisionDamagePerVelocity;
	}

	// EMF discharge damage (opposite charges)
	float EMFDamage = 0.0f;
	const float PropCharge = GetCharge();
	UEMFVelocityModifier* NPCModifier = HitNPC->FindComponentByClass<UEMFVelocityModifier>();
	if (NPCModifier && !FMath::IsNearlyZero(PropCharge))
	{
		const float NPCCharge = NPCModifier->GetCharge();
		if (PropCharge * NPCCharge < 0.0f) // Opposite charges
		{
			const float TotalMag = FMath::Abs(PropCharge) + FMath::Abs(NPCCharge);
			EMFDamage = EMFProximityDamage * (TotalMag / 100.0f);
			EMFDamage = FMath::Max(EMFDamage, EMFProximityDamage);
		}
	}

	const FVector ImpactPoint = bFromSweep ? FVector(SweepResult.ImpactPoint) : OtherActor->GetActorLocation();

	// Apply kinetic damage
	if (KineticDamage > 0.0f)
	{
		FDamageEvent KineticEvent;
		KineticEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
		HitNPC->TakeDamage(KineticDamage, KineticEvent, nullptr, this);
	}

	// Apply EMF damage
	if (EMFDamage > 0.0f)
	{
		FDamageEvent EMFEvent;
		EMFEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
		HitNPC->TakeDamage(EMFDamage, EMFEvent, nullptr, this);

		// EMF discharge VFX
		if (EMFDischargeVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), EMFDischargeVFX, ImpactPoint,
				FRotator::ZeroRotator, FVector(EMFDischargeVFXScale),
				true, true, ENCPoolMethod::None);
		}
	}

	// Impact sound
	if (ImpactSound && (KineticDamage > 0.0f || EMFDamage > 0.0f))
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, ImpactPoint);
	}

	LastCollisionDamageTime = CurrentTime;

	if (bLogEMForces)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp %s overlap NPC %s: Speed=%.0f, KineticDmg=%.1f, EMFDmg=%.1f"),
			*GetName(), *HitNPC->GetName(), ImpactSpeed, KineticDamage, EMFDamage);
	}
}

// ==================== Damage & Health ====================

float AEMFPhysicsProp::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return 0.0f;
	}

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	// DEBUG: Log ALL incoming damage to this prop
	UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s::TakeDamage: Damage=%.1f, DamageCauser=%s, bCanExplode=%d, bIsInReverseFlight=%d, bHasExploded=%d, bIsDead=%d, DamageType=%s"),
		*GetName(), Damage,
		DamageCauser ? *DamageCauser->GetName() : TEXT("NULL"),
		bCanExplode, bIsInReverseFlight, bHasExploded, bIsDead,
		DamageEvent.DamageTypeClass ? *DamageEvent.DamageTypeClass->GetName() : TEXT("NULL"));

	// No chain reaction: ignore all damage from other props (charge distribution happens in Explode)
	// Exception: bAllowChainReaction lets specific props be destroyed by nearby explosions
	if (Cast<AEMFPhysicsProp>(DamageCauser) && DamageCauser != this && !bAllowChainReaction)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s: Ignoring damage from prop %s (no chain reaction)"), *GetName(), *DamageCauser->GetName());
		return 0.0f;
	}

	// Immunity during reverse flight: only player damage gets through (enemy shots ignored)
	if (bIsInReverseFlight && !bHasExploded)
	{
		const bool bIsPlayerDamage = EventInstigator && EventInstigator->IsPlayerController();
		if (!bIsPlayerDamage)
		{
			UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s: Ignoring non-player damage during reverse flight (DamageCauser=%s)"),
				*GetName(), DamageCauser ? *DamageCauser->GetName() : TEXT("NULL"));
			return 0.0f;
		}
	}

	// Shot-triggered detonation: player hit (non-melee) during reverse flight = 2x explosion
	if (bCanExplode && bIsInReverseFlight && !bHasExploded)
	{
		const bool bIsMeleeDamage = DamageEvent.DamageTypeClass &&
			DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass());

		UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s: Shot-detonation check PASSED. bIsMeleeDamage=%d"),
			*GetName(), bIsMeleeDamage);

		if (!bIsMeleeDamage)
		{
			UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s: >>> TRIGGERING EXPLOSION 2x! <<<"), *GetName());
			Explode(2.0f, 2.0f, 2.0f);
			return ActualDamage;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[EMFProp DEBUG] %s: Shot-detonation check FAILED. bCanExplode=%d, bIsInReverseFlight=%d, bHasExploded=%d"),
			*GetName(), bCanExplode, bIsInReverseFlight, bHasExploded);
	}

	// Melee charge transfer
	if (DamageEvent.DamageTypeClass &&
		DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		if (FieldComponent && EventInstigator)
		{
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker)
			{
				float ChargeToAdd = ChargeChangeOnMeleeHit;

				// Read attacker's charge sign
				UEMFVelocityModifier* AttackerEMF = Attacker->FindComponentByClass<UEMFVelocityModifier>();
				if (AttackerEMF)
				{
					const float AttackerCharge = AttackerEMF->GetCharge();
					if (FMath::Abs(AttackerCharge) >= KINDA_SMALL_NUMBER)
					{
						ChargeToAdd = -FMath::Abs(ChargeChangeOnMeleeHit) * FMath::Sign(AttackerCharge);
					}
				}

				const float OldCharge = GetCharge();
				SetCharge(OldCharge + ChargeToAdd);
			}
		}
	}

	CurrentHP = FMath::Max(0.0f, CurrentHP - ActualDamage);
	OnPropDamaged.Broadcast(this, ActualDamage, DamageCauser);

	if (CurrentHP <= 0.0f)
	{
		Die(DamageCauser);
	}

	return ActualDamage;
}

void AEMFPhysicsProp::Die(AActor* Killer)
{
	if (bIsDead)
	{
		return;
	}

	// If prop can explode and hasn't yet — explode instead of just dying
	if (bCanExplode && !bHasExploded)
	{
		Explode(1.0f, 1.0f, 1.0f);
		return;
	}

	bIsDead = true;
	SetCharge(0.0f);
	SetActorTickEnabled(false);
	OnPropDeath.Broadcast(this, Killer);

	// Release from capture if held
	if (CapturingPlate.IsValid())
	{
		ReleasedFromCapture();
	}

	// Spawn GC destruction if assigned
	if (PropGeometryCollection)
	{
		SpawnDestructionGC(GetActorLocation());
	}
}

// ==================== Geometry Collection Destruction ====================

void AEMFPhysicsProp::SpawnDestructionGC(const FVector& DestructionOrigin)
{
	UE_LOG(LogTemp, Warning, TEXT("SpawnDestructionGC [%s]: GC=%s, Mesh=%s"),
		*GetName(),
		PropGeometryCollection ? *PropGeometryCollection->GetName() : TEXT("NULL"),
		(PropMesh && PropMesh->GetStaticMesh()) ? *PropMesh->GetStaticMesh()->GetName() : TEXT("NULL"));

	if (!PropGeometryCollection || !PropMesh || !GetWorld())
	{
		return;
	}

	// Spawn GC actor at PropMesh's exact world transform
	const FTransform MeshTransform = PropMesh->GetComponentTransform();
	const FVector Origin = MeshTransform.GetLocation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		Origin, MeshTransform.GetRotation().Rotator(), SpawnParams);

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

	// Note: GC geometry is at native mesh scale from AppendStaticMesh.
	// Actor scale is (1,1,1) — no additional scaling needed.

	// Collision: gibs should not push pawns or block camera
	GCComp->SetCollisionProfileName(GibCollisionProfile);
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	// Assign GC asset and initialize physics
	GCComp->SetRestCollection(PropGeometryCollection);

	// Copy materials from PropMesh to GC gibs — allows generic GC with prop's material
	if (PropMesh)
	{
		const int32 NumMats = PropMesh->GetNumMaterials();
		for (int32 i = 0; i < NumMats; i++)
		{
			if (UMaterialInterface* Mat = PropMesh->GetMaterial(i))
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

	// Scatter pieces radially from destruction origin (scaled by charge for explosions)
	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = DestructionImpulse * CachedChargeScale;
	RadialVelocity->Position = DestructionOrigin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	// Angular velocity for tumbling (scaled by charge for explosions)
	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = DestructionAngularImpulse * CachedChargeScale;
	AngularVelocity->Position = DestructionOrigin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVelocity);

	// Cache reference for freeze/cleanup
	SpawnedGCActor = GCActor;

	// Phase 1: after GibPhysicsLifetime, freeze gibs (disable physics + collision)
	GetWorld()->GetTimerManager().SetTimer(GCFreezeTimer, this, &AEMFPhysicsProp::FreezeGibs, GibPhysicsLifetime, false);

	// Phase 2: if GibVisualLifetime > 0, destroy the frozen gibs after additional time
	if (GibVisualLifetime > 0.0f)
	{
		GCActor->SetLifeSpan(GibPhysicsLifetime + GibVisualLifetime);
	}
	// else: gibs persist forever as cheap static visuals

	// Hide PropMesh (GC gibs replace it visually)
	PropMesh->SetVisibility(false);
	PropMesh->SetSimulatePhysics(false);
	PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: GC destruction spawned, impulse=%.0f (chargeScale=%.2f), physicsTime=%.1fs, visualTime=%.1fs"),
		*GetName(), DestructionImpulse * CachedChargeScale, CachedChargeScale, GibPhysicsLifetime,
		GibVisualLifetime > 0.0f ? GibVisualLifetime : -1.0f);
}

void AEMFPhysicsProp::FreezeGibs()
{
	AGeometryCollectionActor* GCActor = SpawnedGCActor.Get();
	if (!GCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		return;
	}

	// Don't hard-freeze (SetSimulatePhysics(false)) — that leaves airborne pieces floating.
	// Instead: strip all collision except WorldStatic so pieces can only rest on floors/walls.
	// High linear damping makes them settle quickly. Chaos auto-sleeps stationary bodies,
	// and sleeping rigid bodies cost near-zero (no solver iterations, no broadphase).
	GCComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	GCComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	GCComp->SetAngularDamping(5.0f);

	UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: Gibs settling (collision stripped to WorldStatic, high damping)"), *GetName());
}

// ==================== Explosive Impact ====================

void AEMFPhysicsProp::Explode(float DamageMultiplier, float RadiusMultiplier, float VFXScaleMultiplier)
{
	if (bHasExploded || bIsDead)
	{
		return;
	}

	bHasExploded = true;
	bIsInReverseFlight = false;

	// Charge-proportionate scaling: scale = |charge| / referenceCharge, clamped
	float ChargeScale = 1.0f;
	if (bScaleExplosionWithCharge)
	{
		const float AbsCharge = FMath::Abs(GetCharge());
		ChargeScale = FMath::Clamp(AbsCharge / ExplosionReferenceCharge, MinChargeScale, MaxChargeScale);
	}
	CachedChargeScale = ChargeScale;

	const FVector ExplosionLocation = GetActorLocation();
	const float FinalDamage = ExplosionDamage * DamageMultiplier * ChargeScale;
	const float FinalRadius = ExplosionRadius * RadiusMultiplier;
	const float FinalVFXScale = ExplosionVFXScale * VFXScaleMultiplier * ChargeScale;

	// LOS check: trace from explosion origin to target, blocking on static world geometry only
	FCollisionQueryParams LOSParams;
	LOSParams.AddIgnoredActor(this);
	LOSParams.bTraceComplex = false;
	auto HasLineOfSight = [&](const AActor* Target) -> bool
	{
		if (!Target)
		{
			return false;
		}
		FHitResult LOSHit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			LOSHit, ExplosionLocation, Target->GetActorLocation(),
			ECC_Visibility, LOSParams);
		// Not blocked, or the trace hit the target itself = has LOS
		return !bBlocked || LOSHit.GetActor() == Target;
	};

	// Radial damage (manual per-actor with LOS) + impact tracking for delegate
	float ImpactTotalDamage = 0.0f;
	int32 ImpactKillCount = 0;

	if (FinalDamage > 0.0f && FinalRadius > 0.0f)
	{
		TSubclassOf<UDamageType> DamageClass = ExplosionDamageType;
		if (!DamageClass)
		{
			DamageClass = UDamageType::StaticClass();
		}

		TArray<FOverlapResult> DamageOverlaps;
		FCollisionShape DamageSphere = FCollisionShape::MakeSphere(FinalRadius);
		FCollisionQueryParams DamageQueryParams;
		DamageQueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(
			DamageOverlaps, ExplosionLocation, FQuat::Identity,
			ECC_Pawn, DamageSphere, DamageQueryParams);

		TSet<AActor*> DamagedActors;
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

		for (const FOverlapResult& Overlap : DamageOverlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || HitActor == PlayerPawn || DamagedActors.Contains(HitActor))
			{
				continue;
			}
			DamagedActors.Add(HitActor);

			if (!HasLineOfSight(HitActor))
			{
				continue;
			}

			const float Distance = FVector::Dist(ExplosionLocation, HitActor->GetActorLocation());
			const float InnerRadius = FinalRadius * 0.3f;

			// Falloff: full damage within inner radius, then power-curve falloff to edge
			float DamageAlpha;
			if (Distance <= InnerRadius)
			{
				DamageAlpha = 1.0f;
			}
			else
			{
				const float T = FMath::Clamp((Distance - InnerRadius) / (FinalRadius - InnerRadius), 0.0f, 1.0f);
				DamageAlpha = FMath::Lerp(1.0f, 0.1f, FMath::Pow(T, ExplosionDamageFalloff));
			}

			const float ActorDamage = FinalDamage * DamageAlpha;

			// Track NPC state before damage for kill detection
			AShooterNPC* HitNPC = Cast<AShooterNPC>(HitActor);
			const bool bWasAlive = HitNPC && !HitNPC->IsDead();

			UGameplayStatics::ApplyDamage(HitActor, ActorDamage, nullptr, this, DamageClass);

			if (bWasAlive)
			{
				ImpactTotalDamage += ActorDamage;
				if (HitNPC->IsDead())
				{
					ImpactKillCount++;
				}
			}
		}
	}

	// Broadcast prop impact delegate to player character
	if (ImpactTotalDamage > 0.0f)
	{
		if (AShooterCharacter* Player = Cast<AShooterCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0)))
		{
			Player->OnPropImpact.Broadcast(this, ImpactTotalDamage, ImpactKillCount);
		}
	}

	// Explosion impulse: push characters and physics bodies (with LOS)
	if (bApplyExplosionImpulse && FinalRadius > 0.0f)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(FinalRadius);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(
			Overlaps, ExplosionLocation, FQuat::Identity,
			ECC_Pawn, Sphere, QueryParams);

		// Also sweep WorldDynamic for physics bodies
		TArray<FOverlapResult> PhysicsOverlaps;
		GetWorld()->OverlapMultiByChannel(
			PhysicsOverlaps, ExplosionLocation, FQuat::Identity,
			ECC_WorldDynamic, Sphere, QueryParams);
		Overlaps.Append(PhysicsOverlaps);

		// Track already-processed actors to avoid double impulse
		TSet<AActor*> ProcessedActors;

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || ProcessedActors.Contains(HitActor))
			{
				continue;
			}
			ProcessedActors.Add(HitActor);

			if (!HasLineOfSight(HitActor))
			{
				continue;
			}

			const FVector ToTarget = HitActor->GetActorLocation() - ExplosionLocation;
			const float Distance = ToTarget.Size();

			// Linear falloff: full strength at center, zero at edge
			const float FalloffAlpha = FMath::Clamp(1.0f - Distance / FinalRadius, 0.0f, 1.0f);

			// Radial direction (away from explosion)
			FVector ImpulseDir = Distance > KINDA_SMALL_NUMBER
				? ToTarget.GetSafeNormal()
				: FVector::UpVector;

			// Apply upward bias: blend radial direction toward Up
			ImpulseDir = FMath::Lerp(ImpulseDir, FVector::UpVector, ExplosionImpulseUpwardBias).GetSafeNormal();

			// Guarantee minimum vertical component for reliable rocket boost
			if (ImpulseDir.Z < ExplosionMinVerticalRatio)
			{
				ImpulseDir.Z = ExplosionMinVerticalRatio;
				ImpulseDir.Normalize();
			}

			// Character impulse via LaunchCharacter (velocity override — feels like a rocket boost)
			ACharacter* HitCharacter = Cast<ACharacter>(HitActor);
			if (HitCharacter)
			{
				const FVector LaunchVelocity = ImpulseDir * ExplosionImpulseStrength * FalloffAlpha * DamageMultiplier * ChargeScale;
				HitCharacter->LaunchCharacter(LaunchVelocity, false, true);
				continue;
			}

			// Physics body impulse
			UPrimitiveComponent* HitComp = Overlap.GetComponent();
			if (HitComp && HitComp->IsSimulatingPhysics())
			{
				const FVector Impulse = ImpulseDir * ExplosionPhysicsImpulse * FalloffAlpha * DamageMultiplier * ChargeScale;
				HitComp->AddImpulse(Impulse);
			}
		}
	}

	// Stun nearby NPCs (with LOS)
	if (bApplyExplosionStun && FinalRadius > 0.0f)
	{
		TArray<FOverlapResult> StunOverlaps;
		FCollisionShape StunSphere = FCollisionShape::MakeSphere(FinalRadius);
		FCollisionQueryParams StunQueryParams;
		StunQueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(
			StunOverlaps, ExplosionLocation, FQuat::Identity,
			ECC_Pawn, StunSphere, StunQueryParams);

		TSet<AShooterNPC*> StunnedNPCs;

		for (const FOverlapResult& Overlap : StunOverlaps)
		{
			AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (!NPC || StunnedNPCs.Contains(NPC) || NPC->IsDead())
			{
				continue;
			}
			StunnedNPCs.Add(NPC);

			if (!HasLineOfSight(NPC))
			{
				continue;
			}

			const float FinalStunDuration = ExplosionStunDuration * ChargeScale;
			NPC->ApplyExplosionStun(FinalStunDuration, ExplosionStunMontage);
			OnNPCStunnedByExplosion.Broadcast(NPC, this, FinalStunDuration);
		}
	}

	// Distribute charge among nearby props (instead of chain reaction destruction)
	if (FinalRadius > 0.0f)
	{
		const float MyCharge = GetCharge();
		if (!FMath::IsNearlyZero(MyCharge))
		{
			TArray<FOverlapResult> PropOverlaps;
			FCollisionShape PropSphere = FCollisionShape::MakeSphere(FinalRadius);
			FCollisionQueryParams PropQueryParams;
			PropQueryParams.AddIgnoredActor(this);

			GetWorld()->OverlapMultiByChannel(
				PropOverlaps, ExplosionLocation, FQuat::Identity,
				ECC_WorldDynamic, PropSphere, PropQueryParams);

			TArray<AEMFPhysicsProp*> NearbyProps;
			for (const FOverlapResult& Overlap : PropOverlaps)
			{
				AEMFPhysicsProp* OtherProp = Cast<AEMFPhysicsProp>(Overlap.GetActor());
				if (OtherProp && OtherProp != this && !OtherProp->IsDead())
				{
					NearbyProps.AddUnique(OtherProp);
				}
			}

			if (NearbyProps.Num() > 0)
			{
				const float ChargePerProp = MyCharge / static_cast<float>(NearbyProps.Num());
				for (AEMFPhysicsProp* Prop : NearbyProps)
				{
					Prop->SetCharge(Prop->GetCharge() + ChargePerProp);
				}

				UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: Distributed charge %.1f among %d nearby props (%.1f each)"),
					*GetName(), MyCharge, NearbyProps.Num(), ChargePerProp);
			}
		}
	}

	// VFX
	if (ExplosionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), ExplosionVFX, ExplosionLocation,
			FRotator::ZeroRotator, FVector(FinalVFXScale),
			true, true, ENCPoolMethod::None);
	}

	// SFX
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, ExplosionLocation);
	}

	if (bLogEMForces)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp %s EXPLODED: Damage=%.0f Radius=%.0f VFXScale=%.1f ChargeScale=%.2f (multipliers: %.1fx/%.1fx/%.1fx)"),
			*GetName(), FinalDamage, FinalRadius, FinalVFXScale, ChargeScale, DamageMultiplier, RadiusMultiplier, VFXScaleMultiplier);
	}

	// Check for breakable doors within explosion radius
	{
		TArray<AActor*> Doors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShooterDoor::StaticClass(), Doors);
		for (AActor* DoorActor : Doors)
		{
			AShooterDoor* Door = Cast<AShooterDoor>(DoorActor);
			if (!Door || !Door->bCanBeBrokenByDrop)
			{
				continue;
			}

			const float Dist = FVector::Dist(ExplosionLocation, Door->GetActorLocation());
			if (Dist <= FinalRadius)
			{
				UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp::Explode - Breaking door %s (dist=%.1f, radius=%.1f)"),
					*Door->GetName(), Dist, FinalRadius);
				Door->BreakDoor(ExplosionLocation);
			}
		}
	}

	OnPropExploded.Broadcast(this, ExplosionLocation, DamageMultiplier);

	// Kill the prop
	Die(this);
}

// ==================== Charge API ====================

float AEMFPhysicsProp::GetCharge() const
{
	if (!FieldComponent)
	{
		return 0.0f;
	}
	return FieldComponent->GetSourceDescription().PointChargeParams.Charge;
}

void AEMFPhysicsProp::ResetProp()
{
	bIsDead = false;
	bHasExploded = false;
	bIsInReverseFlight = false;
	CachedPreCollisionSpeed = 0.0f;
	CachedChargeScale = 1.0f;
	CurrentHP = MaxHP;
	SetActorTickEnabled(true);

	// Clean up any existing GC gibs from previous death
	GetWorld()->GetTimerManager().ClearTimer(GCFreezeTimer);
	GetWorld()->GetTimerManager().ClearTimer(GCCleanupTimer);
	if (AGeometryCollectionActor* OldGC = SpawnedGCActor.Get())
	{
		OldGC->Destroy();
		SpawnedGCActor.Reset();
	}

	// Release from capture if held
	if (CapturingPlate.IsValid())
	{
		ReleasedFromCapture();
	}

	// Restore visibility
	SetActorHiddenInGame(false);
	if (PropMesh)
	{
		PropMesh->SetVisibility(true);
		PropMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		// Match BeginPlay logic: uncharged props start with physics off
		if (FMath::IsNearlyZero(DefaultCharge))
		{
			PropMesh->SetSimulatePhysics(false);
		}
		else
		{
			PropMesh->SetSimulatePhysics(true);
		}

		PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	// Reset charge to default
	SetCharge(DefaultCharge);

	// Match BeginPlay: uncharged props don't need tick
	if (FMath::IsNearlyZero(DefaultCharge))
	{
		SetActorTickEnabled(false);
	}

	// Re-register with charge widget subsystem (OnPropDied unregistered us)
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->RegisterProp(this);
	}

	UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp: %s reset to alive state"), *GetName());
}

void AEMFPhysicsProp::SetCharge(float NewCharge)
{
	if (!FieldComponent)
	{
		return;
	}

	const float OldCharge = GetCharge();

	FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
	Desc.PointChargeParams.Charge = NewCharge;
	FieldComponent->SetSourceDescription(Desc);

	// Enable physics and tick when prop transitions from uncharged to charged
	if (PropMesh && FMath::IsNearlyZero(OldCharge) && !FMath::IsNearlyZero(NewCharge))
	{
		PropMesh->SetSimulatePhysics(true);
		SetActorTickEnabled(true);
	}

	UpdateChargeTracking();
}

float AEMFPhysicsProp::GetPropMass() const
{
	if (!FieldComponent)
	{
		return DefaultMass;
	}
	return FieldComponent->GetSourceDescription().PhysicsParams.Mass;
}

void AEMFPhysicsProp::SetPropMass(float NewMass)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PhysicsParams.Mass = NewMass;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Keep physics body mass in sync
	if (PropMesh)
	{
		PropMesh->SetMassOverrideInKg(NAME_None, NewMass, true);
	}
}

// ==================== IShooterDummyTarget ====================

bool AEMFPhysicsProp::GrantsStableCharge_Implementation() const
{
	return bGrantsStableCharge;
}

float AEMFPhysicsProp::GetStableChargeAmount_Implementation() const
{
	return StableChargePerHit;
}

float AEMFPhysicsProp::GetKillChargeBonus_Implementation() const
{
	return KillChargeBonus;
}

bool AEMFPhysicsProp::IsDummyDead_Implementation() const
{
	return bIsDead;
}

// ==================== Charge Tracking & Overlay ====================

void AEMFPhysicsProp::UpdateChargeTracking()
{
	const float Charge = GetCharge();

	uint8 CurrentPolarity = 0;
	if (Charge > KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 1;
	}
	else if (Charge < -KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 2;
	}

	if (!FMath::IsNearlyEqual(Charge, PreviousChargeValue, 0.001f))
	{
		OnChargeChanged.Broadcast(Charge, CurrentPolarity);
		PreviousChargeValue = Charge;
	}

	if (CurrentPolarity != PreviousPolarity)
	{
		UpdateChargeOverlay(CurrentPolarity);
		PreviousPolarity = CurrentPolarity;
	}
}

void AEMFPhysicsProp::UpdateChargeOverlay(uint8 NewPolarity)
{
	if (!bUseChargeOverlay || !PropMesh)
	{
		return;
	}

	UMaterialInterface* TargetMaterial = nullptr;

	switch (NewPolarity)
	{
	case 0:
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	case 1:
		TargetMaterial = PositiveChargeOverlayMaterial;
		break;
	case 2:
		TargetMaterial = NegativeChargeOverlayMaterial;
		break;
	}

	PropMesh->SetOverlayMaterial(TargetMaterial);
}

// ==================== Force Filtering ====================

float AEMFPhysicsProp::GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const
{
	if (bIsInReverseFlight)
	{
		switch (OwnerType)
		{
		case EEMSourceOwnerType::Player:
			return LaunchedPlayerForceMultiplier;
		case EEMSourceOwnerType::NPC:
			return LaunchedNPCForceMultiplier;
		case EEMSourceOwnerType::Projectile:
			return LaunchedProjectileForceMultiplier;
		case EEMSourceOwnerType::Environment:
			return LaunchedEnvironmentForceMultiplier;
		case EEMSourceOwnerType::PhysicsProp:
			return LaunchedPhysicsPropForceMultiplier;
		case EEMSourceOwnerType::None:
		default:
			return LaunchedUnknownForceMultiplier;
		}
	}

	switch (OwnerType)
	{
	case EEMSourceOwnerType::Player:
		return PlayerForceMultiplier;
	case EEMSourceOwnerType::NPC:
		return NPCForceMultiplier;
	case EEMSourceOwnerType::Projectile:
		return ProjectileForceMultiplier;
	case EEMSourceOwnerType::Environment:
		return EnvironmentForceMultiplier;
	case EEMSourceOwnerType::PhysicsProp:
		return PhysicsPropForceMultiplier;
	case EEMSourceOwnerType::None:
	default:
		return UnknownForceMultiplier;
	}
}

// ==================== Source Zero Check ====================

bool AEMFPhysicsProp::IsSourceEffectivelyZero(const FEMSourceDescription& Source)
{
	switch (Source.SourceType)
	{
	case EEMSourceType::PointCharge:
		return FMath::IsNearlyZero(Source.PointChargeParams.Charge);
	case EEMSourceType::LineCharge:
		return FMath::IsNearlyZero(Source.LineChargeParams.LinearChargeDensity);
	case EEMSourceType::ChargedRing:
		return FMath::IsNearlyZero(Source.RingParams.TotalCharge);
	case EEMSourceType::ChargedSphere:
		return FMath::IsNearlyZero(Source.SphereParams.TotalCharge);
	case EEMSourceType::ChargedBall:
		return FMath::IsNearlyZero(Source.BallParams.TotalCharge);
	case EEMSourceType::InfinitePlate:
	case EEMSourceType::FinitePlate:
		return FMath::IsNearlyZero(Source.PlateParams.SurfaceChargeDensity);
	case EEMSourceType::Dipole:
		return Source.DipoleParams.DipoleMoment.IsNearlyZero();
	case EEMSourceType::CurrentWire:
		return FMath::IsNearlyZero(Source.WireParams.Current);
	case EEMSourceType::CurrentLoop:
		return FMath::IsNearlyZero(Source.LoopParams.Current);
	case EEMSourceType::Solenoid:
		return FMath::IsNearlyZero(Source.SolenoidParams.Current);
	case EEMSourceType::MagneticDipole:
		return Source.MagneticDipoleParams.MagneticMoment.IsNearlyZero();
	case EEMSourceType::SectorMagnet:
		return FMath::IsNearlyZero(Source.SectorMagnetParams.FieldStrength);
	case EEMSourceType::PlateMagnet:
		return FMath::IsNearlyZero(Source.PlateMagnetParams.FieldStrength);
	case EEMSourceType::DielectricSphere:
		return FMath::IsNearlyEqual(Source.DielectricSphereParams.RelativePermittivity, 1.0f);
	case EEMSourceType::DielectricSlab:
		return FMath::IsNearlyEqual(Source.DielectricSlabParams.RelativePermittivity, 1.0f);
	case EEMSourceType::GroundedConductor:
	case EEMSourceType::GroundedPlate:
		return false;
	default:
		return FMath::IsNearlyZero(Source.PointChargeParams.Charge);
	}
}

int32 AEMFPhysicsProp::GetSourceEffectiveChargeSign(const FEMSourceDescription& Source)
{
	float EffectiveCharge = 0.0f;

	switch (Source.SourceType)
	{
	case EEMSourceType::PointCharge:
		EffectiveCharge = Source.PointChargeParams.Charge;
		break;
	case EEMSourceType::LineCharge:
		EffectiveCharge = Source.LineChargeParams.LinearChargeDensity;
		break;
	case EEMSourceType::ChargedRing:
		EffectiveCharge = Source.RingParams.TotalCharge;
		break;
	case EEMSourceType::ChargedSphere:
		EffectiveCharge = Source.SphereParams.TotalCharge;
		break;
	case EEMSourceType::ChargedBall:
		EffectiveCharge = Source.BallParams.TotalCharge;
		break;
	case EEMSourceType::InfinitePlate:
	case EEMSourceType::FinitePlate:
		EffectiveCharge = Source.PlateParams.SurfaceChargeDensity;
		break;
	default:
		// Magnetic sources, dielectrics, grounded conductors — no charge sign concept
		return 0;
	}

	if (EffectiveCharge > KINDA_SMALL_NUMBER) return 1;
	if (EffectiveCharge < -KINDA_SMALL_NUMBER) return -1;
	return 0;
}
