// TacticalDeviceHandlers.cpp

#include "Variant_Shooter/Tactical/TacticalDeviceHandlers.h"
#include "Variant_Shooter/Tactical/TacticalDeviceComponent.h"
#include "Variant_Shooter/Tactical/TacticalDeviceDefinition.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/KamikazeDroneNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/EnemyBeamBoltSubsystem.h"
#include "AI/PolarityTeams.h"
#include "AI/Components/AIAccuracyComponent.h"
#include "Environment/WorldDarknessSubsystem.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace TacticalHandlerUtil
{
	/** Scan cadence for the server's who-is-in-the-cone queries. */
	static constexpr float ScanInterval = 0.1f;

	/** Engine shapes, so a device with no prototype mesh assigned still draws something. */
	static UStaticMesh* LoadEngineShape(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	/** World size of a mesh's bounds, never zero. */
	static FVector MeshSize(const UStaticMesh* Mesh)
	{
		const FVector Size = Mesh ? Mesh->GetBounds().BoxExtent * 2.0f : FVector(100.0f);
		return FVector(FMath::Max(Size.X, 1.0f), FMath::Max(Size.Y, 1.0f), FMath::Max(Size.Z, 1.0f));
	}

	/** Place Shape so its bounds centre lands on Center with the given rotation and world size. */
	static void PlaceByBounds(UStaticMeshComponent* Shape, const FVector& Center, const FQuat& Rotation, const FVector& WorldSize)
	{
		if (!Shape || !Shape->GetStaticMesh())
		{
			return;
		}
		const FVector Scale = WorldSize / MeshSize(Shape->GetStaticMesh());
		const FVector PivotOffset = Rotation.RotateVector(Shape->GetStaticMesh()->GetBounds().Origin * Scale);
		Shape->SetWorldTransform(FTransform(Rotation, Center - PivotOffset, Scale));
	}

	/** Where a hit came from, the same reading the slide block in AShooterCharacter::TakeDamage uses. */
	static bool ResolveDamageSource(FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser, FVector& OutSource)
	{
		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			OutSource = static_cast<const FRadialDamageEvent&>(DamageEvent).Origin;
			return true;
		}
		if (DamageCauser)
		{
			OutSource = DamageCauser->GetActorLocation();
			return true;
		}
		if (EventInstigator && EventInstigator->GetPawn())
		{
			OutSource = EventInstigator->GetPawn()->GetActorLocation();
			return true;
		}
		return false;
	}

	/** Hostile, living enemies within Range of Origin. */
	static void GatherEnemies(const UWorld* World, const AActor* Owner, const FVector& Origin, float Range, TArray<AShooterNPC*>& Out)
	{
		Out.Reset();
		if (!World || !Owner)
		{
			return;
		}
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalGather), false, Owner);
		World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Range), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AShooterNPC* const NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (NPC && !NPC->IsDead() && !Out.Contains(NPC) && PolarityTeams::AreHostile(Owner, NPC))
			{
				Out.Add(NPC);
			}
		}
	}
}

// ==================== Base ====================

void UTacticalDeviceHandler::Init(UTacticalDeviceComponent* InOwner, const UTacticalDeviceDefinition* InDefinition)
{
	Owner = InOwner;
	Definition = const_cast<UTacticalDeviceDefinition*>(InDefinition);
}

bool UTacticalDeviceHandler::IsAuthority() const
{
	return Owner.IsValid() && Owner->IsAuthority();
}

bool UTacticalDeviceHandler::HasScreen() const
{
	const UWorld* const World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	return World && World->GetNetMode() != NM_DedicatedServer;
}

AActor* UTacticalDeviceHandler::GetOwnerActor() const
{
	return Owner.IsValid() ? Owner->GetOwner() : nullptr;
}

float UTacticalDeviceHandler::GetDrainPerSecond() const
{
	return (Definition && Definition->ActiveDuration > 0.0f) ? 1.0f / Definition->ActiveDuration : 0.0f;
}

void UTacticalDeviceHandler::OnActivated()
{
	bOn = true;
	AActor* const OwnerActor = GetOwnerActor();
	if (!Definition || !OwnerActor || !HasScreen())
	{
		return;
	}

	if (Definition->ActivateSound)
	{
		UGameplayStatics::PlaySoundAtLocation(OwnerActor, Definition->ActivateSound, OwnerActor->GetActorLocation());
	}
	if (Definition->ActiveLoopSound)
	{
		LoopAudio = UGameplayStatics::SpawnSoundAttached(Definition->ActiveLoopSound, OwnerActor->GetRootComponent());
	}
	if (Definition->ActiveFX)
	{
		ActiveFXComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(OwnerActor, Definition->ActiveFX,
			Owner->GetVisualOrigin(), FRotator::ZeroRotator, FVector(1.0f), /*bAutoDestroy*/ false);
		if (ActiveFXComponent)
		{
			ActiveFXComponent->SetVariableLinearColor(TEXT("User.Color"), Definition->Color);
		}
	}
}

void UTacticalDeviceHandler::OnDeactivated()
{
	bOn = false;
	if (LoopAudio)
	{
		LoopAudio->Stop();
		LoopAudio->DestroyComponent();
		LoopAudio = nullptr;
	}
	DestroyPart(ActiveFXComponent);
	ActiveFXComponent = nullptr;

	AActor* const OwnerActor = GetOwnerActor();
	if (Definition && Definition->DeactivateSound && OwnerActor && HasScreen())
	{
		UGameplayStatics::PlaySoundAtLocation(OwnerActor, Definition->DeactivateSound, OwnerActor->GetActorLocation());
	}
}

void UTacticalDeviceHandler::TickActive(float DeltaTime)
{
	if (ActiveFXComponent && Owner.IsValid())
	{
		FVector Origin, Direction;
		Owner->GetAim(Origin, Direction);
		ActiveFXComponent->SetWorldLocationAndRotation(Owner->GetVisualOrigin(), Direction.Rotation());
	}
}

void UTacticalDeviceHandler::Shutdown()
{
	if (bOn)
	{
		OnDeactivated();
	}
}

UStaticMeshComponent* UTacticalDeviceHandler::MakeShape(UStaticMesh* Mesh, UMaterialInterface* Material, FName Name) const
{
	AActor* const OwnerActor = GetOwnerActor();
	if (!Mesh || !Material || !OwnerActor)
	{
		return nullptr;
	}

	UStaticMeshComponent* const Shape = NewObject<UStaticMeshComponent>(OwnerActor, MakeUniqueObjectName(OwnerActor, UStaticMeshComponent::StaticClass(), Name));
	Shape->SetStaticMesh(Mesh);
	for (int32 Index = 0; Index < FMath::Max(1, Mesh->GetStaticMaterials().Num()); ++Index)
	{
		Shape->SetMaterial(Index, Material);
	}
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCastShadow(false);
	Shape->SetCanEverAffectNavigation(false);
	// Not attached: placed in world space every frame, so it follows the aim without inheriting the
	// first person mesh's render settings.
	Shape->SetUsingAbsoluteLocation(true);
	Shape->SetUsingAbsoluteRotation(true);
	Shape->SetUsingAbsoluteScale(true);
	Shape->RegisterComponent();
	return Shape;
}

void UTacticalDeviceHandler::DestroyPart(UActorComponent* Part)
{
	if (Part)
	{
		Part->DestroyComponent();
	}
}

bool UTacticalDeviceHandler::IsInCone(const FVector& Origin, const FVector& Direction, float CosHalfAngle, float Range, const FVector& Point)
{
	const FVector ToPoint = Point - Origin;
	const float DistSq = ToPoint.SizeSquared();
	if (DistSq > FMath::Square(Range))
	{
		return false;
	}
	if (DistSq < 1.0f)
	{
		return true;
	}
	return FVector::DotProduct(ToPoint / FMath::Sqrt(DistSq), Direction) >= CosHalfAngle;
}

bool UTacticalDeviceHandler::HasClearLine(const FVector& From, const FVector& To, const AActor* Target) const
{
	const UWorld* const World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalLine), false, GetOwnerActor());
	if (Target)
	{
		Params.AddIgnoredActor(Target);
	}
	if (const AShooterWeapon* const Weapon = Owner->GetHeldWeapon())
	{
		Params.AddIgnoredActor(Weapon);
	}
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

// ==================== Shield ====================

const UTacticalDevice_Shield* UTacticalDeviceHandler_Shield::GetShield() const
{
	return Cast<UTacticalDevice_Shield>(Definition);
}

void UTacticalDeviceHandler_Shield::OnActivated()
{
	Super::OnActivated();
	const UTacticalDevice_Shield* const Def = GetShield();
	if (Def && HasScreen())
	{
		UStaticMesh* const Mesh = Def->ShieldMesh ? Def->ShieldMesh.Get() : TacticalHandlerUtil::LoadEngineShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		ShieldShape = MakeShape(Mesh, Def->ShieldMaterial, TEXT("TacticalShield"));
		PlaceShape();
	}
}

void UTacticalDeviceHandler_Shield::OnDeactivated()
{
	DestroyPart(ShieldShape);
	ShieldShape = nullptr;
	Super::OnDeactivated();
}

void UTacticalDeviceHandler_Shield::TickActive(float DeltaTime)
{
	Super::TickActive(DeltaTime);
	PlaceShape();
}

void UTacticalDeviceHandler_Shield::PlaceShape()
{
	const UTacticalDevice_Shield* const Def = GetShield();
	if (!ShieldShape || !Def || !Owner.IsValid())
	{
		return;
	}
	FVector Origin, Direction;
	Owner->GetAim(Origin, Direction);
	TacticalHandlerUtil::PlaceByBounds(ShieldShape, Origin + Direction * Def->ShieldDistance,
		Direction.ToOrientationQuat(), Def->ShieldSize);
}

float UTacticalDeviceHandler_Shield::AbsorbDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const UTacticalDevice_Shield* const Def = GetShield();
	AShooterCharacter* const Character = Owner.IsValid() ? Owner->GetCharacter() : nullptr;
	if (!Def || !Character || !IsAuthority())
	{
		return Damage;
	}

	// Only what comes from in front. A hit with no readable direction is not the shield's.
	FVector Source;
	if (!TacticalHandlerUtil::ResolveDamageSource(DamageEvent, EventInstigator, DamageCauser, Source))
	{
		return Damage;
	}
	FVector FromDirection = Source - Character->GetActorLocation();
	FromDirection.Z = 0.0f;
	FVector Facing = Character->GetBaseAimRotation().Vector();
	Facing.Z = 0.0f;
	if (!FromDirection.Normalize() || !Facing.Normalize()
		|| FVector::DotProduct(Facing, FromDirection) < FMath::Cos(FMath::DegreesToRadians(Def->CoverHalfAngle)))
	{
		return Damage;
	}

	const float Pool = Owner->GetCharge() * Def->ShieldCapacity;
	const float Absorbed = FMath::Min(Damage, Pool);
	if (Absorbed <= 0.0f)
	{
		return Damage;
	}
	Owner->SpendCharge(Absorbed / Def->ShieldCapacity);

	FVector AimOrigin, AimDirection;
	Owner->GetAim(AimOrigin, AimDirection);
	const FVector HitPoint = AimOrigin + AimDirection * Def->ShieldDistance;
	const bool bBroke = Owner->GetCharge() <= 0.0f;
	Owner->BroadcastDeviceEvent(bBroke ? Event_ShieldBreak : Event_ShieldHit, HitPoint);

	UE_LOG(LogTemp, Log, TEXT("[TACTICAL_DEBUG] %s shield took %.1f of %.1f, pool %.1f left%s"),
		*GetNameSafe(Character), Absorbed, Damage, Owner->GetCharge() * Def->ShieldCapacity, bBroke ? TEXT(", BROKEN") : TEXT(""));

	return Damage - Absorbed;
}

void UTacticalDeviceHandler_Shield::PlayEvent(uint8 EventId, const FVector& Location)
{
	const UTacticalDevice_Shield* const Def = GetShield();
	AActor* const OwnerActor = GetOwnerActor();
	if (!Def || !OwnerActor || !HasScreen())
	{
		return;
	}

	const bool bBreak = EventId == Event_ShieldBreak;
	UNiagaraSystem* const FX = bBreak ? Def->ShieldBreakFX.Get() : Def->ShieldHitFX.Get();
	USoundBase* const Sound = bBreak ? Def->ShieldBreakSound.Get() : Def->ShieldHitSound.Get();
	if (FX)
	{
		UNiagaraComponent* const Spawned = UNiagaraFunctionLibrary::SpawnSystemAtLocation(OwnerActor, FX, Location);
		if (Spawned)
		{
			Spawned->SetVariableLinearColor(TEXT("User.Color"), Def->Color);
		}
	}
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(OwnerActor, Sound, Location);
	}
}

// ==================== Light ====================

const UTacticalDevice_Light* UTacticalDeviceHandler_Light::GetLight() const
{
	return Cast<UTacticalDevice_Light>(Definition);
}

void UTacticalDeviceHandler_Light::OnActivated()
{
	Super::OnActivated();
	ScanTimer = 0.0f;

	const UTacticalDevice_Light* const Def = GetLight();
	AActor* const OwnerActor = GetOwnerActor();
	if (!Def || !OwnerActor || !HasScreen())
	{
		return;
	}

	if (Def->LightIntensity > 0.0f)
	{
		Spot = NewObject<USpotLightComponent>(OwnerActor, MakeUniqueObjectName(OwnerActor, USpotLightComponent::StaticClass(), TEXT("TacticalSpot")));
		Spot->SetUsingAbsoluteLocation(true);
		Spot->SetUsingAbsoluteRotation(true);
		Spot->SetMobility(EComponentMobility::Movable);
		Spot->SetIntensityUnits(ELightUnits::Candelas);
		Spot->SetIntensity(Def->LightIntensity);
		Spot->SetLightColor(Def->Color);
		Spot->SetAttenuationRadius(Def->Range);
		Spot->SetOuterConeAngle(FMath::Max(Def->ConeHalfAngle, 1.0f));
		Spot->SetInnerConeAngle(FMath::Max(Def->ConeHalfAngle, 1.0f) * 0.6f);
		// The first person gun right in front of the lamp would throw a shadow over half the screen.
		Spot->SetCastShadows(false);
		Spot->RegisterComponent();
	}

	if (Def->bDrawBeam)
	{
		UStaticMesh* const Mesh = Def->BeamMesh ? Def->BeamMesh.Get() : TacticalHandlerUtil::LoadEngineShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		BeamShape = MakeShape(Mesh, Def->BeamMaterial, TEXT("TacticalBeam"));
	}

	if (Def->EndPointFX)
	{
		EndPointFXComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(OwnerActor, Def->EndPointFX,
			Owner->GetVisualOrigin(), FRotator::ZeroRotator, FVector(1.0f), /*bAutoDestroy*/ false);
		if (EndPointFXComponent)
		{
			EndPointFXComponent->SetVariableLinearColor(TEXT("User.Color"), Def->Color);
		}
	}

	UpdateVisuals();
}

void UTacticalDeviceHandler_Light::OnDeactivated()
{
	// Nothing to undo on the enemies: each dazzle runs its own clock (AShooterNPC::ApplyDazzle).
	LitRecords.Reset();
	DestroyPart(Spot);
	Spot = nullptr;
	DestroyPart(BeamShape);
	BeamShape = nullptr;
	DestroyPart(EndPointFXComponent);
	EndPointFXComponent = nullptr;
	Super::OnDeactivated();
}

void UTacticalDeviceHandler_Light::TickActive(float DeltaTime)
{
	Super::TickActive(DeltaTime);

	if (IsAuthority())
	{
		ScanTimer -= DeltaTime;
		if (ScanTimer <= 0.0f)
		{
			ScanTargets(TacticalHandlerUtil::ScanInterval - ScanTimer);
			ScanTimer = TacticalHandlerUtil::ScanInterval;
		}
	}

	UpdateVisuals();
}

float UTacticalDeviceHandler_Light::GetDrainPerSecond() const
{
	const UTacticalDevice_Light* const Def = GetLight();
	if (!Def || !Owner.IsValid())
	{
		return Super::GetDrainPerSecond();
	}
	const float Seconds = Owner->GetMode() == ETacticalDeviceMode::Active ? Def->ActiveDuration : Def->NormalDuration;
	return Seconds > 0.0f ? 1.0f / Seconds : 0.0f;
}

bool UTacticalDeviceHandler_Light::FindBurnPoint(const AShooterNPC* Enemy, const FVector& Origin, const FVector& Direction,
	float CosHalf, FVector& OutPoint) const
{
	const UTacticalDevice_Light* const Def = GetLight();
	const USkeletalMeshComponent* const Mesh = Enemy ? Enemy->GetMesh() : nullptr;
	if (!Def || !Mesh || Mesh->GetBoneIndex(Def->BurnBone) == INDEX_NONE)
	{
		return false;
	}
	const FVector Point = Mesh->GetBoneLocation(Def->BurnBone);
	if (IsInCone(Origin, Direction, CosHalf, FMath::Min(Def->BurnRange, Def->Range), Point) && HasClearLine(Origin, Point, Enemy))
	{
		OutPoint = Point;
		return true;
	}
	return false;
}

void UTacticalDeviceHandler_Light::ScanPainted(float ScanDeltaTime, const FVector& Origin, const FVector& End)
{
	const UTacticalDevice_Light* const Def = GetLight();
	AActor* const OwnerActor = GetOwnerActor();
	if (!Def || !OwnerActor || Def->PaintRadius <= 0.0f)
	{
		return;
	}

	// Whoever the beam passes by or the dot lands by: it knows it is the one aimed at.
	TArray<AShooterNPC*> Candidates;
	TacticalHandlerUtil::GatherEnemies(OwnerActor->GetWorld(), OwnerActor, Origin, FVector::Dist(Origin, End) + Def->PaintRadius, Candidates);
	for (AShooterNPC* const Enemy : Candidates)
	{
		UAIAccuracyComponent* const Accuracy = Enemy->GetAccuracyComponent();
		const FVector Chest = Enemy->GetActorLocation();
		if (!Accuracy || FVector::Dist(FMath::ClosestPointOnSegment(Chest, Origin, End), Chest) > Def->PaintRadius)
		{
			continue;
		}
		if (!Accuracy->IsPainted())
		{
			UE_LOG(LogTemp, Log, TEXT("[TACTICAL_DEBUG] %s painted by %s"), *Enemy->GetName(), *OwnerActor->GetName());
		}
		Accuracy->NotePainted(0.3f);
		Accuracy->AddStress(Def->PaintStressPerSecond * ScanDeltaTime);
	}
}

void UTacticalDeviceHandler_Light::ScanSeen(const FVector& Origin, const FVector& Direction, const FVector& End, bool bActive)
{
	const UTacticalDevice_Light* const Def = GetLight();
	AActor* const OwnerActor = GetOwnerActor();
	UWorld* const World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (!Def || !World)
	{
		return;
	}

	// How far it is seen: by day hardly, by night from far off, the active use further still. The
	// plain laser is a thin line: only the dark shows it.
	const bool bLaser = Def->Kind == ETacticalLightKind::Laser;
	const float Dark = UWorldDarknessSubsystem::GetDarkness(World);
	float Range = FMath::Lerp(Def->SeenRangeDay, Def->SeenRangeNight, Dark);
	if (bLaser && !bActive)
	{
		Range = Def->SeenRangeNight * Dark;
	}
	if (bActive)
	{
		Range *= Def->ActiveSeenScale;
	}
	if (Range <= 0.0f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const FVector Shooter = OwnerActor->GetActorLocation();
	const float CosLamp = FMath::Cos(FMath::DegreesToRadians(80.0f));
	TArray<AShooterNPC*> Candidates;
	TacticalHandlerUtil::GatherEnemies(World, OwnerActor, Origin, Range, Candidates);
	for (AShooterNPC* const Enemy : Candidates)
	{
		AShooterAIController* const Brain = Cast<AShooterAIController>(Enemy->GetController());
		if (!Brain)
		{
			continue;
		}
		const FVector Eyes = Enemy->GetPawnViewLocation();

		// A lamp is seen from in front of it; a beam, wherever along it comes nearest the eyes.
		FVector Look = Origin;
		if (bLaser)
		{
			Look = FMath::ClosestPointOnSegment(Eyes, Origin, End);
		}
		else if (FVector::DotProduct((Eyes - Origin).GetSafeNormal(), Direction) < CosLamp)
		{
			continue;
		}
		if (!Brain->IsInViewCone(Look))
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalSeen), false, Enemy);
		Params.AddIgnoredActor(OwnerActor);
		if (const AShooterWeapon* const Weapon = Owner.IsValid() ? Owner->GetHeldWeapon() : nullptr)
		{
			Params.AddIgnoredActor(Weapon);
		}
		FHitResult Blocked;
		if (World->LineTraceSingleByChannel(Blocked, Eyes, Look, ECC_Visibility, Params))
		{
			continue;
		}

		// Seen: the gun comes up and turns that way (and holds there: a light that appears where the
		// gun is waiting meets the first volley). The burner gives the place away to the whole side.
		if (UAIAccuracyComponent* const Accuracy = Enemy->GetAccuracyComponent())
		{
			Accuracy->NoteAlert();
		}
		Brain->LookTowards(Shooter, 3.0f);
		if (bLaser && bActive)
		{
			Brain->BroadcastContactAt(OwnerActor, Shooter, /*bSeenNow*/ true);
		}

		float& Said = SeenLogTime.FindOrAdd(Enemy);
		if (Now - Said > 2.0f)
		{
			Said = Now;
			UE_LOG(LogTemp, Log, TEXT("[TACTICAL_DEBUG] %s sees %s's %s (%s, darkness %.2f, range %.0f)"), *Enemy->GetName(), *OwnerActor->GetName(),
				bLaser ? TEXT("laser") : TEXT("light"), bActive ? TEXT("active") : TEXT("normal"), Dark, Range);
		}
	}
}

float UTacticalDeviceHandler_Light::DurationForLitTime(float LitSeconds) const
{
	const UTacticalDevice_Light* const Def = GetLight();
	if (!Def)
	{
		return 0.0f;
	}
	if (Def->DazzleDurationByLitTime)
	{
		return FMath::Max(0.0f, Def->DazzleDurationByLitTime->GetFloatValue(LitSeconds));
	}
	return FMath::Min(Def->MaxDazzleDuration, LitSeconds * Def->DazzleSecondsPerLitSecond);
}

bool UTacticalDeviceHandler_Light::FindLightPoint(const AShooterNPC* Enemy, const FVector& Origin, const FVector& Direction,
	float CosHalf, float Range, FVector& OutPoint) const
{
	const UTacticalDevice_Light* const Def = GetLight();
	if (!Enemy || !Def)
	{
		return false;
	}

	// A person is lit through the face, nowhere else (author's call, 2026-10-01): any face bone the
	// mesh has, in the cone and in sight.
	bool bHasFace = false;
	if (const USkeletalMeshComponent* const Mesh = Enemy->GetMesh())
	{
		for (const FName& Bone : Def->FaceBoneNames)
		{
			if (Mesh->GetBoneIndex(Bone) == INDEX_NONE)
			{
				continue;
			}
			bHasFace = true;
			const FVector Point = Mesh->GetBoneLocation(Bone);
			if (IsInCone(Origin, Direction, CosHalf, Range, Point) && HasClearLine(Origin, Point, Enemy))
			{
				OutPoint = Point;
				return true;
			}
		}
	}
	if (bHasFace)
	{
		return false;
	}

	// No face to find (a drone): the body.
	const FVector Body = Enemy->GetActorLocation();
	if (IsInCone(Origin, Direction, CosHalf, Range, Body) && HasClearLine(Origin, Body, Enemy))
	{
		OutPoint = Body;
		return true;
	}
	return false;
}

void UTacticalDeviceHandler_Light::ScanTargets(float ScanDeltaTime)
{
	const UTacticalDevice_Light* const Def = GetLight();
	AActor* const OwnerActor = GetOwnerActor();
	if (!Def || !OwnerActor || !Owner.IsValid())
	{
		return;
	}

	FVector Origin, Direction;
	Owner->GetDeviceAim(Origin, Direction);
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(Def->ConeHalfAngle));
	UWorld* const World = OwnerActor->GetWorld();
	const float Now = World->GetTimeSeconds();
	const bool bActive = Owner->GetMode() == ETacticalDeviceMode::Active;
	const bool bLaser = Def->Kind == ETacticalLightKind::Laser;

	// Where the light lands: the laser's dot, the far end of what the soldiers may see of the beam.
	FVector End = Origin + Direction * Def->Range;
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalLightEnd), false, OwnerActor);
		if (const AShooterWeapon* const Weapon = Owner->GetHeldWeapon())
		{
			Params.AddIgnoredActor(Weapon);
		}
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params))
		{
			End = Hit.ImpactPoint;
		}
	}

	// Seen from about, four times a second. The burner pins whoever it is held on or by.
	SeenTimer -= ScanDeltaTime;
	if (SeenTimer <= 0.0f)
	{
		SeenTimer = 0.25f;
		ScanSeen(Origin, Direction, End, bActive);
	}
	if (bLaser && bActive)
	{
		ScanPainted(ScanDeltaTime, Origin, End);
	}

	TArray<AShooterNPC*> Candidates;
	TacticalHandlerUtil::GatherEnemies(World, OwnerActor, Origin, Def->Range, Candidates);

	for (AShooterNPC* const Enemy : Candidates)
	{
		// In the face. The burner only counts on the head bone, and only up close.
		FVector Point;
		const bool bInFace = (bLaser && bActive)
			? FindBurnPoint(Enemy, Origin, Direction, CosHalf, Point)
			: FindLightPoint(Enemy, Origin, Direction, CosHalf, Def->Range, Point);
		if (!bInFace)
		{
			continue;
		}

		FLitRecord& Record = LitRecords.FindOrAdd(Enemy);
		Record.LitSeconds += ScanDeltaTime;
		Record.LastLitTime = Now;

		// A kamikaze already in its run is blinded by the first touch: there is no "holding it on"
		// a drone coming at you at twelve metres a second.
		const AKamikazeDroneNPC* const Kamikaze = Cast<AKamikazeDroneNPC>(Enemy);
		const bool bStriking = Kamikaze && Kamikaze->GetKamikazeState() == EKamikazeState::Attacking;
		if (Record.LitSeconds < Def->MinLitTimeToDazzle && !bStriking)
		{
			continue;
		}

		UAIAccuracyComponent* const Accuracy = Enemy->GetAccuracyComponent();
		if (bActive || bStriking)
		{
			// The strobe, the burner: dazzled, driven off and kept off, stress piling up.
			const float Duration = FMath::Max(DurationForLitTime(Record.LitSeconds), bStriking ? 1.0f : 0.0f);
			Enemy->ApplyDazzle(OwnerActor, Duration, Def->DazzleSpreadMultiplier, Def->AffectedOverlayMaterial);
			Enemy->ApplyKeepAway(OwnerActor, Def->ActiveKeepAwaySeconds);
			if (Accuracy)
			{
				Accuracy->AddStress(Def->ActiveStressPerSecond * ScanDeltaTime);
			}
		}
		else
		{
			// The plain light: put off a little, nothing more.
			Enemy->ApplyGlare(OwnerActor, Def->NormalGlareSeconds, Def->NormalSpreadMultiplier);
			if (Accuracy)
			{
				Accuracy->AddStress(Def->NormalStressPerSecond * ScanDeltaTime);
			}
		}

		if (!Record.bAffectedFXPlayed && Def->AffectedFX)
		{
			Record.bAffectedFXPlayed = true;
			UNiagaraFunctionLibrary::SpawnSystemAttached(Def->AffectedFX, Enemy->GetRootComponent(), NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, /*bAutoDestroy*/ true);
		}
	}

	// Light that moved off an enemy long enough forgets how long it had been held there.
	for (auto It = LitRecords.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value().LastLitTime > Def->LitTimeMemory)
		{
			It.RemoveCurrent();
		}
	}
}

void UTacticalDeviceHandler_Light::UpdateVisuals()
{
	const UTacticalDevice_Light* const Def = GetLight();
	const UWorld* const World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	if (!Def || !World || !HasScreen())
	{
		return;
	}

	FVector AimOrigin, Direction;
	Owner->GetDeviceAim(AimOrigin, Direction);
	// Where the owner sees the rail, not where its first person bones are: else the beam hangs in the air.
	const FVector Start = Owner->GetApparentVisualOrigin();

	// Where the light lands: down the aim, to the first thing in the way.
	FVector End = AimOrigin + Direction * Def->Range;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalBeam), false, GetOwnerActor());
	if (const AShooterWeapon* const Weapon = Owner->GetHeldWeapon())
	{
		Params.AddIgnoredActor(Weapon);
	}
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, AimOrigin, End, ECC_Visibility, Params))
	{
		End = Hit.ImpactPoint;
	}

	const bool bActive = Owner->GetMode() == ETacticalDeviceMode::Active;
	if (Spot)
	{
		// The lamp sits a little past the end of the barrel, not at the rail: at the rail it is two
		// centimetres behind the front sight, and three thousand candelas that close burn a white hole
		// in the middle of the screen on the gun's own metal (author's screenshot, 2026-10-15).
		static constexpr float LampAheadOfMuzzle = 25.0f;
		FVector LampAt = Start;
		if (const AShooterWeapon* const Weapon = Owner->GetHeldWeapon())
		{
			LampAt = Weapon->GetMuzzleWorldLocation() + Direction * LampAheadOfMuzzle;
		}
		// Aimed at the end of the beam, so the lit spot is where the beam lands and not a lamp's width beside it.
		Spot->SetWorldLocationAndRotation(LampAt, (End - LampAt).GetSafeNormal().Rotation());

		// The strobe: on and off StrobeRate times a second, brighter when on.
		const bool bStrobe = bActive && Def->Kind == ETacticalLightKind::Flashlight;
		const bool bLit = !bStrobe || FMath::Frac(World->GetTimeSeconds() * Def->StrobeRate) < 0.5f;
		Spot->SetIntensity(bLit ? Def->LightIntensity * (bStrobe ? 2.5f : 1.0f) : 0.0f);
	}

	if (BeamShape)
	{
		const FVector Beam = End - Start;
		const float Length = Beam.Size();
		BeamShape->SetVisibility(Length > 1.0f);
		if (Length > 1.0f)
		{
			// The engine cylinder stands on Z: point Z down the beam.
			const FQuat Rotation = FRotationMatrix::MakeFromZ(Beam / Length).ToQuat();
			const float Width = Def->BeamWidth * (bActive ? 2.0f : 1.0f);
			TacticalHandlerUtil::PlaceByBounds(BeamShape, Start + Beam * 0.5f, Rotation, FVector(Width, Width, Length));
		}
	}

	if (EndPointFXComponent)
	{
		EndPointFXComponent->SetWorldLocation(End);
	}
}

// ==================== Freeze ====================

const UTacticalDevice_Freeze* UTacticalDeviceHandler_Freeze::GetFreeze() const
{
	return Cast<UTacticalDevice_Freeze>(Definition);
}

void UTacticalDeviceHandler_Freeze::OnActivated()
{
	Super::OnActivated();
	ScanTimer = 0.0f;

	const UTacticalDevice_Freeze* const Def = GetFreeze();
	if (Def && HasScreen())
	{
		UStaticMesh* const Mesh = Def->ConeMesh ? Def->ConeMesh.Get() : TacticalHandlerUtil::LoadEngineShape(TEXT("/Engine/BasicShapes/Cone.Cone"));
		ConeShape = MakeShape(Mesh, Def->ConeMaterial, TEXT("TacticalFreezeCone"));
		PlaceShape();
	}
}

void UTacticalDeviceHandler_Freeze::OnDeactivated()
{
	ReleaseAll();
	DestroyPart(ConeShape);
	ConeShape = nullptr;
	Super::OnDeactivated();
}

void UTacticalDeviceHandler_Freeze::TickActive(float DeltaTime)
{
	Super::TickActive(DeltaTime);

	if (IsAuthority())
	{
		ScanTimer -= DeltaTime;
		if (ScanTimer <= 0.0f)
		{
			ScanTimer = TacticalHandlerUtil::ScanInterval;
			ScanEnemies();
		}
	}

	// Every machine slows its own copies: a client draws its own cosmetic rounds and bolts.
	UpdateProjectiles();
	UpdateBoltZone();
	PlaceShape();
}

void UTacticalDeviceHandler_Freeze::ScanEnemies()
{
	const UTacticalDevice_Freeze* const Def = GetFreeze();
	AActor* const OwnerActor = GetOwnerActor();
	if (!Def || !OwnerActor || !Owner.IsValid())
	{
		return;
	}

	FVector Origin, Direction;
	Owner->GetAim(Origin, Direction);
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(Def->ConeHalfAngle));

	TArray<AShooterNPC*> Candidates;
	TacticalHandlerUtil::GatherEnemies(OwnerActor->GetWorld(), OwnerActor, Origin, Def->Range, Candidates);

	TSet<TWeakObjectPtr<AShooterNPC>> NowFrozen;
	for (AShooterNPC* const Enemy : Candidates)
	{
		const FVector Body = Enemy->GetActorLocation();
		if (!IsInCone(Origin, Direction, CosHalf, Def->Range, Body) || !HasClearLine(Origin, Body, Enemy))
		{
			continue;
		}
		Enemy->ApplyTacticalFreeze(OwnerActor, Def->EnemyMoveMultiplier, Def->EnemyTurnMultiplier,
			Def->FlyerTimeMultiplier, Def->AffectedOverlayMaterial);
		NowFrozen.Add(Enemy);
	}

	for (const TWeakObjectPtr<AShooterNPC>& Was : Frozen)
	{
		if (!NowFrozen.Contains(Was))
		{
			if (AShooterNPC* const Enemy = Was.Get())
			{
				Enemy->RemoveTacticalFreeze(OwnerActor);
			}
		}
	}
	Frozen = MoveTemp(NowFrozen);
}

void UTacticalDeviceHandler_Freeze::UpdateProjectiles()
{
	const UTacticalDevice_Freeze* const Def = GetFreeze();
	UWorld* const World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	if (!Def || !World)
	{
		return;
	}

	FVector Origin, Direction;
	Owner->GetAim(Origin, Direction);
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(Def->ConeHalfAngle));

	// The round's whole clock runs slower, so its path (gravity arc included) stays the same and
	// only takes longer. A round that leaves the cone is let go; the pool resets one that died in it.
	TSet<TWeakObjectPtr<AActor>> NowSlowed;
	for (TActorIterator<AShooterProjectile> It(World); It; ++It)
	{
		AShooterProjectile* const Projectile = *It;
		if (!Projectile || Projectile->IsHidden())
		{
			continue;
		}
		if (IsInCone(Origin, Direction, CosHalf, Def->Range, Projectile->GetActorLocation()))
		{
			Projectile->CustomTimeDilation = Def->ProjectileSpeedMultiplier;
			NowSlowed.Add(Projectile);
		}
	}
	for (const TWeakObjectPtr<AActor>& Was : SlowedProjectiles)
	{
		if (!NowSlowed.Contains(Was))
		{
			if (AActor* const Projectile = Was.Get())
			{
				Projectile->CustomTimeDilation = 1.0f;
			}
		}
	}
	SlowedProjectiles = MoveTemp(NowSlowed);
}

void UTacticalDeviceHandler_Freeze::UpdateBoltZone()
{
	const UTacticalDevice_Freeze* const Def = GetFreeze();
	UWorld* const World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	if (!Def || !World)
	{
		return;
	}
	if (UEnemyBeamBoltSubsystem* const Bolts = World->GetSubsystem<UEnemyBeamBoltSubsystem>())
	{
		FVector Origin, Direction;
		Owner->GetAim(Origin, Direction);
		Bolts->SetSlowZone(this, Origin, Direction, Def->ConeHalfAngle, Def->Range, Def->ProjectileSpeedMultiplier);
	}
}

void UTacticalDeviceHandler_Freeze::ReleaseAll()
{
	AActor* const OwnerActor = GetOwnerActor();
	for (const TWeakObjectPtr<AShooterNPC>& Was : Frozen)
	{
		if (AShooterNPC* const Enemy = Was.Get())
		{
			Enemy->RemoveTacticalFreeze(OwnerActor);
		}
	}
	Frozen.Reset();

	for (const TWeakObjectPtr<AActor>& Was : SlowedProjectiles)
	{
		if (AActor* const Projectile = Was.Get())
		{
			Projectile->CustomTimeDilation = 1.0f;
		}
	}
	SlowedProjectiles.Reset();

	UWorld* const World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	if (UEnemyBeamBoltSubsystem* const Bolts = World ? World->GetSubsystem<UEnemyBeamBoltSubsystem>() : nullptr)
	{
		Bolts->ClearSlowZone(this);
	}
}

void UTacticalDeviceHandler_Freeze::PlaceShape()
{
	const UTacticalDevice_Freeze* const Def = GetFreeze();
	if (!ConeShape || !Def || !Owner.IsValid())
	{
		return;
	}
	FVector AimOrigin, Direction;
	Owner->GetAim(AimOrigin, Direction);
	const FVector Start = Owner->GetVisualOrigin();
	const float Radius = Def->Range * FMath::Tan(FMath::DegreesToRadians(Def->ConeHalfAngle));

	// The engine cone has its point up (+Z) and its base down: point -Z down the aim, so the tip
	// sits at the device and the base opens away from it.
	const FQuat Rotation = FRotationMatrix::MakeFromZ(-Direction).ToQuat();
	TacticalHandlerUtil::PlaceByBounds(ConeShape, Start + Direction * (Def->Range * 0.5f), Rotation,
		FVector(Radius * 2.0f, Radius * 2.0f, Def->Range));
}
