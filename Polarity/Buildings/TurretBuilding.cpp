// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "TurretBuilding.h"
#include "Variant_Shooter/AI/KamikazeDroneNPC.h"
#include "Engine/TargetPoint.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

ATurretBuilding::ATurretBuilding()
{
	// Building defaults: never explodes (drone collapse is the only death path),
	// no scattering impulse on the gibs so the GC falls under gravity (collapse feel).
	bCanExplode = false;
	DestructionImpulse = 0.0f;
	DestructionAngularImpulse = 0.0f;

	// One-shot from the drone — no incidental damage should chip the building down.
	MaxHP = 1.0f;
	CurrentHP = 1.0f;

	// Buildings are static — disable surface friction tick path that prop uses.
	bApplyEMFSurfaceFriction = false;

	// Never flip PropMesh to SimulatePhysics automatically. The building stays kinematic
	// until our overridden SpawnDestructionGC hides the mesh and spawns the GC.
	// Without this, any EMF charge or damage transfer from the drone's explosion would
	// unfreeze PropMesh and the building would start falling mid-air as a single rigid body.
	bKeepPropMeshStatic = true;
}

void ATurretBuilding::OnMarked_Implementation(FVector HitLocation, FVector HitNormal)
{
	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ENTER | HitLoc=%s Normal=%s | IsDead=%d bCollapsing=%d SpawnedDroneValid=%d DroneClassSet=%d SpawnPointsCount=%d"),
		*GetName(), *HitLocation.ToCompactString(), *HitNormal.ToCompactString(),
		IsDead() ? 1 : 0, bCollapsing ? 1 : 0,
		SpawnedDrone.IsValid() ? 1 : 0,
		DroneClass ? 1 : 0,
		DroneSpawnPoints.Num());

	if (IsDead() || bCollapsing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ABORTED — building is dead or already collapsing"), *GetName());
		return;
	}

	// Already marked and the drone is en route — ignore further marks until it impacts.
	if (SpawnedDrone.IsValid())
	{
		AKamikazeDroneNPC* ExistingDrone = SpawnedDrone.Get();
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ABORTED — drone already en route (%s, at %s). Wait for it to impact (or be destroyed) before marking again."),
			*GetName(),
			ExistingDrone ? *ExistingDrone->GetName() : TEXT("?"),
			ExistingDrone ? *ExistingDrone->GetActorLocation().ToCompactString() : TEXT("?"));
		return;
	}

	if (!DroneClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ABORTED — DroneClass is null (configure Building|Drone|DroneClass in BP defaults)"), *GetName());
		return;
	}

	if (DroneSpawnPoints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ABORTED — DroneSpawnPoints array is empty (place ATargetPoint actors in the level and drag them into this instance's DroneSpawnPoints array)"), *GetName());
		return;
	}

	ATargetPoint* SpawnPoint = PickBestDroneSpawnPoint(HitLocation, HitNormal);
	if (!SpawnPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ABORTED — PickBestDroneSpawnPoint returned null. DroneSpawnPoints has %d entries but all are null refs (stale references in the array after level changes?)"),
			*GetName(), DroneSpawnPoints.Num());
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	AKamikazeDroneNPC* Drone = GetWorld()->SpawnActor<AKamikazeDroneNPC>(
		DroneClass,
		SpawnPoint->GetActorLocation(),
		SpawnPoint->GetActorRotation(),
		SpawnParams);

	if (!Drone)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked ABORTED — SpawnActor returned null for class %s at %s (check DroneClass is abstract? collision handling?)"),
			*GetName(), *DroneClass->GetName(), *SpawnPoint->GetActorLocation().ToCompactString());
		return;
	}

	// Pass the hit point as the drone's target — NOT the building's bounds center (would be empty
	// air inside the mesh's AABB for tall thin skyscrapers, drone would fly past without touching).
	Drone->InitiateDirectAttack(this, HitLocation);
	SpawnedDrone = Drone;

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::OnMarked OK — drone %s spawned at %s (location=%s), aiming at hit point %s"),
		*GetName(), *Drone->GetName(), *SpawnPoint->GetName(),
		*SpawnPoint->GetActorLocation().ToCompactString(), *HitLocation.ToCompactString());
}

ATargetPoint* ATurretBuilding::PickBestDroneSpawnPoint(const FVector& HitLocation, const FVector& HitNormal) const
{
	ATargetPoint* Best = nullptr;
	float BestDot = -FLT_MAX;
	int32 NullCount = 0;
	int32 ValidCount = 0;

	for (int32 Idx = 0; Idx < DroneSpawnPoints.Num(); ++Idx)
	{
		ATargetPoint* SP = DroneSpawnPoints[Idx];
		if (!SP)
		{
			++NullCount;
			UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING]   DroneSpawnPoints[%d] = NULL (stale ref)"), Idx);
			continue;
		}

		const FVector ToSpawn = (SP->GetActorLocation() - HitLocation).GetSafeNormal();
		const float Dot = FVector::DotProduct(HitNormal, ToSpawn);
		const float Dist = FVector::Dist(SP->GetActorLocation(), HitLocation);
		++ValidCount;

		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING]   DroneSpawnPoints[%d] %s | Loc=%s | Dist=%.0f | Dot(Normal,ToSpawn)=%.3f"),
			Idx, *SP->GetName(), *SP->GetActorLocation().ToCompactString(), Dist, Dot);

		if (Dot > BestDot)
		{
			BestDot = Dot;
			Best = SP;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] PickBestDroneSpawnPoint result: Best=%s BestDot=%.3f | ValidPoints=%d NullPoints=%d"),
		Best ? *Best->GetName() : TEXT("NONE"), BestDot, ValidCount, NullCount);

	if (Best && BestDot < 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] WARNING: chosen spawn point has NEGATIVE dot (%.3f) — it's on the OPPOSITE side of the wall from the player. Drone will approach from the wrong side. Place more ATargetPoint actors on other sides of the building to give the picker a better choice."),
			BestDot);
	}

	return Best;
}

float ATurretBuilding::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const FString CauserName = DamageCauser ? DamageCauser->GetName() : TEXT("NULL");
	const FString CauserClass = DamageCauser ? DamageCauser->GetClass()->GetName() : TEXT("NULL");
	const FString DmgTypeName = DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetName() : TEXT("NULL");
	const bool bDroneCast = DamageCauser && Cast<AKamikazeDroneNPC>(DamageCauser) != nullptr;

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::TakeDamage | Damage=%.1f Causer=%s (class=%s) DmgType=%s | bCollapsing=%d IsDead=%d DroneCast=%d"),
		*GetName(), Damage, *CauserName, *CauserClass, *DmgTypeName,
		bCollapsing ? 1 : 0, IsDead() ? 1 : 0, bDroneCast ? 1 : 0);

	// During Collapse() we re-enter via Super::TakeDamage with a fatal damage event — let it through.
	if (bCollapsing)
	{
		const float Result = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::TakeDamage (collapsing path) | Super returned %.1f | post-call IsDead=%d CurrentHP=%.1f"),
			*GetName(), Result, IsDead() ? 1 : 0, CurrentHP);
		return Result;
	}

	if (IsDead())
	{
		return 0.0f;
	}

	// Pre-collapse: only a kamikaze drone impact triggers collapse. Everything else (stray
	// projectiles, EMF prop chain reactions, the player accidentally shooting it) is ignored —
	// the building is invulnerable except to the dedicated mechanic.
	if (bDroneCast)
	{
		const FVector ImpactLoc = DamageCauser->GetActorLocation();
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::TakeDamage → Collapse(%s) — drone hit recognized"),
			*GetName(), *ImpactLoc.ToCompactString());
		Collapse(ImpactLoc);
		return Damage;
	}

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::TakeDamage IGNORED — causer is not AKamikazeDroneNPC"), *GetName());
	return 0.0f;
}

void ATurretBuilding::Collapse(FVector ImpactLocation)
{
	if (IsDead() || bCollapsing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::Collapse called but skipped (IsDead=%d, bCollapsing=%d)"),
			*GetName(), IsDead() ? 1 : 0, bCollapsing ? 1 : 0);
		return;
	}
	bCollapsing = true;

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::Collapse begin | ImpactLoc=%s | MaxHP=%.1f CurrentHP=%.1f bCanExplode=%d HasGC=%d"),
		*GetName(), *ImpactLocation.ToString(), MaxHP, CurrentHP, bCanExplode ? 1 : 0, PropGeometryCollection ? 1 : 0);

	// Crush everything in radius (turrets, the player if they were standing too close).
	TArray<AActor*> Ignore;
	Ignore.Add(this);
	UGameplayStatics::ApplyRadialDamage(
		this,
		CollapseDamage,
		GetActorLocation(),
		CollapseDamageRadius,
		CollapseDamageType ? CollapseDamageType : TSubclassOf<UDamageType>(UDamageType::StaticClass()),
		Ignore,
		this,
		nullptr,
		true /*bDoFullDamage*/);

	// Trigger parent's death pipeline — spawns the GC, hides the static mesh, broadcasts OnPropDeath,
	// and (via CheckpointSubsystem snapshot/restore) handles checkpoint respawn for free.
	FDamageEvent FatalEvent;
	FatalEvent.DamageTypeClass = CollapseDamageType ? CollapseDamageType.Get() : UDamageType::StaticClass();
	const float FatalDamage = MaxHP * 1000.0f;
	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::Collapse → calling Super::TakeDamage(%.1f) to trigger parent Die()"),
		*GetName(), FatalDamage);
	Super::TakeDamage(FatalDamage, FatalEvent, nullptr, this);

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::Collapse end | post-Super: IsDead=%d CurrentHP=%.1f (expected: IsDead=1, CurrentHP=0)"),
		*GetName(), IsDead() ? 1 : 0, CurrentHP);

	bCollapsing = false;
}

// ==================== Cascade Collapse (9/11-style) ====================

void ATurretBuilding::SpawnDestructionGC(const FVector& DestructionOrigin)
{
	// Bypass parent's "instant max-strain everywhere" pattern. Instead:
	//   1. Spawn the GC, anchor every chunk so the building stays standing.
	//   2. Hide the static mesh.
	//   3. Start a timer that progressively unanchors a horizontal slice from top to bottom
	//      over CollapseDuration seconds. Released chunks fall under gravity, hit anchored
	//      chunks below, and the next slice gets released — pancake collapse.

	if (!PropGeometryCollection || !PropMesh || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::SpawnDestructionGC SKIPPED | GC=%d Mesh=%d World=%d"),
			*GetName(), PropGeometryCollection ? 1 : 0, PropMesh ? 1 : 0, GetWorld() ? 1 : 0);
		return;
	}

	const FTransform MeshTransform = PropMesh->GetComponentTransform();

	// Apply local-frame rotation offset to compensate for Fracture Editor's baked orientation.
	const FQuat MeshQuat = MeshTransform.GetRotation();
	const FQuat OffsetQuat = GCRotationOffset.Quaternion();
	const FQuat FinalQuat = MeshQuat * OffsetQuat;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		MeshTransform.GetLocation(), FinalQuat.Rotator(), SpawnParams);

	if (!GCActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::SpawnDestructionGC failed to spawn AGeometryCollectionActor"), *GetName());
		return;
	}

	// Match the scale of the static mesh — handles buildings that are scaled in the level
	// (parent's pattern leaves GC at scale 1.0, which only works if PropMesh is also at scale 1.0).
	GCActor->SetActorScale3D(MeshTransform.GetScale3D());

	UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		GCActor->Destroy();
		return;
	}

	GCComp->SetCollisionProfileName(GibCollisionProfile);
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GCComp->SetRestCollection(PropGeometryCollection);

	// Copy materials from the static mesh so the GC inherits the building's look.
	const int32 NumMats = PropMesh->GetNumMaterials();
	for (int32 i = 0; i < NumMats; i++)
	{
		if (UMaterialInterface* Mat = PropMesh->GetMaterial(i))
		{
			GCComp->SetMaterial(i, Mat);
		}
	}

	GCComp->SetSimulatePhysics(true);
	GCComp->RecreatePhysicsState();

	// CRITICAL: break ALL clusters first. Without this, the GC stays as one rigid body at the root
	// cluster level and per-chunk anchor changes have no visible effect. Same field the parent class
	// uses — applies to all particles via null metadata filter.
	UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Compute world-space Z bounds of the building.
	FVector BoundsOrigin, BoundsExtent;
	GetActorBounds(/*bOnlyCollidingComponents=*/true, BoundsOrigin, BoundsExtent);
	CollapseTopZ = BoundsOrigin.Z + BoundsExtent.Z;
	CollapseBottomZ = BoundsOrigin.Z - BoundsExtent.Z;
	const float Height = CollapseTopZ - CollapseBottomZ;

	// Anchor everything BELOW the initial release line — bottom holds, top is free.
	// Free top chunks (above InitialReleaseZ) fall under gravity at t=0.
	CurrentAnchorTopZ = CollapseTopZ - Height * InitialReleaseHeightFraction;

	{
		// Wide horizontal box, vertical from far below the building up to CurrentAnchorTopZ.
		const FBox AnchorBox(
			FVector(-1e7f, -1e7f, CollapseBottomZ - 1000.0f),
			FVector( 1e7f,  1e7f, CurrentAnchorTopZ));
		GCComp->SetAnchoredByBox(AnchorBox, true);
	}

	// Hide the static mesh — the GC takes over visually.
	PropMesh->SetVisibility(false);
	PropMesh->SetSimulatePhysics(false);
	PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CollapseGCComp = GCComp;
	CollapseStartTime = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Warning, TEXT("[TURRET_BUILDING] %s::SpawnDestructionGC (cascade) | TopZ=%.0f BottomZ=%.0f Height=%.0f | InitialReleaseZ=%.0f | Duration=%.2fs"),
		*GetName(), CollapseTopZ, CollapseBottomZ, Height, CurrentAnchorTopZ, CollapseDuration);

	// Start the cascade timer.
	GetWorld()->GetTimerManager().SetTimer(
		CascadeTimerHandle, this, &ATurretBuilding::TickProgressiveCollapse,
		CascadeTickInterval, /*bLoop=*/true);
}

void ATurretBuilding::TickProgressiveCollapse()
{
	UGeometryCollectionComponent* GCComp = CollapseGCComp.Get();
	if (!GCComp)
	{
		GetWorld()->GetTimerManager().ClearTimer(CascadeTimerHandle);
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - CollapseStartTime;
	const float Progress = FMath::Clamp(Elapsed / CollapseDuration, 0.0f, 1.0f);

	// As progress goes 0→1, CurrentAnchorTopZ descends from the initial release line down to the foundation
	// (or all the way to the bottom if bReleaseFoundation).
	const float StartZ = CollapseTopZ - (CollapseTopZ - CollapseBottomZ) * InitialReleaseHeightFraction;
	const float EndZ = bReleaseFoundation ? (CollapseBottomZ - 100.0f) : CollapseBottomZ;
	const float NewAnchorTopZ = FMath::Lerp(StartZ, EndZ, Progress);

	if (NewAnchorTopZ < CurrentAnchorTopZ)
	{
		// Unanchor the slice between NewAnchorTopZ and CurrentAnchorTopZ.
		// Use a wide horizontal box so we definitely catch all bones in this band.
		const FBox SliceBox(
			FVector(-1e7f, -1e7f, NewAnchorTopZ),
			FVector( 1e7f,  1e7f, CurrentAnchorTopZ + 1.0f)); // small overlap with the previous step
		GCComp->SetAnchoredByBox(SliceBox, false);
		CurrentAnchorTopZ = NewAnchorTopZ;
	}

	if (Progress >= 1.0f)
	{
		// Cascade done. Stop the timer.
		GetWorld()->GetTimerManager().ClearTimer(CascadeTimerHandle);
		UE_LOG(LogTemp, Log, TEXT("[TURRET_BUILDING] %s cascade complete (foundation %s)"),
			*GetName(), bReleaseFoundation ? TEXT("released") : TEXT("kept anchored as rubble"));
	}
}
