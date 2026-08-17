// EMFPhysicsProp.cpp
// Physics-simulated prop with full EMF system integration

#include "EMFPhysicsProp.h"
#include "Curves/CurveFloat.h"
#include "ChargeAnimationComponent.h"
#include "Engine/DamageEvents.h"
#include "EMFChannelingPlateActor.h"
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "Variant_Shooter/DamageTypes/DamageType_Wallslam.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFProximity.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Upgrades/Upgrades/Upgrade_AirKick.h"
#include "Upgrades/Upgrades/AirMailSpear.h"
#include "EMFVelocityModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
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
#include "Checkpoint/CheckpointSubsystem.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "Net/UnrealNetwork.h"

#if WITH_EDITOR
#include "GCBatchCreatorLibrary.h"
#endif

AEMFPhysicsProp::AEMFPhysicsProp()
{
	PrimaryActorTick.bCanEverTick = true;

	// Physics mesh as root (physics body drives actor transform)
	// One prop, one simulation. Every machine used to run its own physics for every prop, so the
	// same crate ended up somewhere different on each screen and the crate that killed you on your
	// screen never moved on anybody else's. The server simulates and everyone else is shown the
	// result. See ApplyPropPhysicsSimulation for the other half: clients must not simulate at all,
	// or their own physics fights the transforms coming in.
	bReplicates = true;
	SetReplicateMovement(true);

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

void AEMFPhysicsProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEMFPhysicsProp, HoldingCharacter);
	DOREPLIFETIME(AEMFPhysicsProp, ReplicatedCharge);
	DOREPLIFETIME(AEMFPhysicsProp, DecoyPhase);
}

void AEMFPhysicsProp::OnRep_Charge()
{
	// Straight into the normal setter, so the overlay, the delegates and the physics-on-first-charge
	// rule all fire on the client exactly as they do on the server.
	SetCharge(ReplicatedCharge);
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

	// Register with checkpoint subsystem for state tracking
	if (UCheckpointSubsystem* CheckpointSub = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		CheckpointSub->RegisterProp(this);
	}

	// Uncharged props (or static-mode subclasses) start with physics and tick disabled (static blockers).
	// Normally both are enabled when prop receives charge via SetCharge() — but static-mode subclasses
	// keep PropMesh kinematic regardless.
	// This prevents NPC depenetration impulses from triggering false explosions.
	if (PropMesh && (FMath::IsNearlyZero(DefaultCharge) || bKeepPropMeshStatic))
	{
		ApplyPropPhysicsSimulation(false);
		SetActorTickEnabled(false);
	}

	// The constructor turns simulation on for everybody, because there is no authority to ask yet.
	// This is where a client takes it back off and settles for being shown where the prop went.
	if (!HasAuthority())
	{
		ApplyPropPhysicsSimulation(false);
	}
}

void AEMFPhysicsProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(GCFreezeTimer);
	GetWorld()->GetTimerManager().ClearTimer(GCCleanupTimer);

	// A decoy leaving the world has to hand its enemies back while its pointer is still valid: after
	// this the coordinator would hold a weak pointer to nothing and could not tell whom to release.
	EndDecoy();

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

	// Air Mail: while the prop is flying back to the player or has been kicked, EMF forces are
	// suppressed entirely — the player's field otherwise sucks the returning prop onto them,
	// ruining both the incoming flight and the kick geometry.
	const bool bAirMailFlightActive =
		ActorHasTag(UUpgrade_AirKick::TAG_AirMailIncoming) ||
		ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked);

	if (bAffectedByExternalFields && !bAirMailFlightActive && FieldComponent && PropMesh && PropMesh->IsSimulatingPhysics())
	{
		ApplyEMForces(DeltaTime);
	}

	// Air Mail spear: while kicked, drive orientation kinematically (nose along velocity + roll)
	// so the asymmetric body can't precess/tumble. Self-stops once the prop slows down.
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this);
		AirMailTickSpear(PropMesh, AirMail ? AirMail->GetKickSpinSpeed() : 720.0f);
	}

	// Belt and braces for the stranding described in DetachFromPlate: whatever route the hold ended
	// by, the flag that makes this machine ignore the server dies with the plate that justified it.
	if (bLocallyHeld && !CapturingPlate.IsValid())
	{
		bLocallyHeld = false;
		ApplyPropPhysicsSimulation(false);
	}

	// The authority is the only one who knows the real charge, and it is not replicated by the field
	// component itself (that lives in the plugin). Mirror it so clients can see a prop light up,
	// judge whether it can be grabbed, and show it on the HUD.
	if (HasAuthority() && !FMath::IsNearlyEqual(ReplicatedCharge, GetCharge()))
	{
		ReplicatedCharge = GetCharge();
	}

	// Watchdog: a prop must never stay marked as somebody's when nobody is holding it. See the
	// comment on LastHeldReportTime. Without this the prop is quietly un-grabbable for everyone
	// else, with nothing in any log to say why — the capture simply refuses.
	if (HasAuthority() && HoldingCharacter)
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		static constexpr float HoldSilenceTimeout = 1.0f;
		static constexpr float MaxFlightSeconds = 6.0f;

		const bool bHolderGone = !IsValid(HoldingCharacter);
		const bool bHolderSilent = !bIsInReverseFlight && (Now - LastHeldReportTime) > HoldSilenceTimeout;
		const bool bFlightOverran = bIsInReverseFlight && (Now - RemoteLaunchStartTime) > MaxFlightSeconds;

		if (bHolderGone || bHolderSilent || bFlightOverran)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s released by watchdog (gone=%d silent=%d flightOverran=%d)"),
				*GetName(), bHolderGone ? 1 : 0, bHolderSilent ? 1 : 0, bFlightOverran ? 1 : 0);
			bIsInReverseFlight = false;
			EndRemoteHold();
		}
	}

	if (bCanBeCaptured && CapturingPlate.IsValid())
	{
		UpdateCaptureForces(DeltaTime);
	}
	// Homing runs for as long as the prop is in the air, and outside the capture branch above on
	// purpose: a prop thrown by the host is still held by its plate, one thrown by a remote client
	// has no plate on this machine at all, and both have to steer.
	if (bIsInReverseFlight && HasAuthority())
	{
		TickHomingSteer(DeltaTime);
	}

	// Cache speed for explosion checks (before collision callbacks modify velocity)
	if (PropMesh)
	{
		CachedPreCollisionVelocity = PropMesh->GetPhysicsLinearVelocity();
		CachedPreCollisionSpeed = CachedPreCollisionVelocity.Size();

		// Air Mail: the launch eligibility survives the steered reverse-flight window (it ends
		// by duration mid-air) but expires once the prop slows out of "flight" speeds.
		if (bAirMailEligibleFlight && !bIsInReverseFlight && CachedPreCollisionSpeed < 350.0f)
		{
			bAirMailEligibleFlight = false;
		}
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

		// Close-range cutoff: skip any charged source inside the cutoff radius to prevent the Coulomb
		// 1/r^2 singularity.
		//
		// This used to fire only when the signs DIFFERED, though the singularity it exists to prevent
		// does not care about sign at all. Same-sign pairs therefore kept the full force at contact
		// range, and once the weapons started driving both props and enemies to the same polarity, an
		// enemy brushing past a prop launched it across the level. Only MaxEMForce stood between the
		// two, and that ceiling is far above what a light prop can absorb.
		if (bEnableOppositeChargeDistanceCutoff && DistSq < OppositeChargeMinDistSq)
		{
			const int32 SourceChargeSign = GetSourceEffectiveChargeSign(Source);
			if (SourceChargeSign != 0 && MyChargeSign != 0)
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
	return UChargeAnimationComponent::GetCaptureRangeFor(this, FMath::Abs(GetCharge()));
}

float AEMFPhysicsProp::GetCaptureRangeForCharacter(const AShooterCharacter* Character) const
{
	if (Character)
	{
		if (const UChargeAnimationComponent* Charge = Character->FindComponentByClass<UChargeAnimationComponent>())
		{
			return Charge->EvaluateCaptureRange(FMath::Abs(GetCharge()));
		}
	}

	return CalculateCaptureRange();
}

void AEMFPhysicsProp::ApplyPropPhysicsSimulation(bool bEnable)
{
	if (!PropMesh)
	{
		return;
	}

	// The authority simulates, and so does a client while it is the one holding the prop: a held
	// prop has to be a real physics body on the holder's machine, or the constraint holding it has
	// nothing to pull and it cannot be stopped by walls. Every other machine is shown where the
	// prop ended up, because a client simulating a prop nobody there is holding would push its own
	// copy around and then be yanked back by the server's transform, which reads as twitching.
	PropMesh->SetSimulatePhysics(bEnable && (HasAuthority() || bLocallyHeld));
}

void AEMFPhysicsProp::OnRep_ReplicatedMovement()
{
	if (bLocallyHeld)
	{
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AEMFPhysicsProp::PostNetReceiveLocationAndRotation()
{
	if (bLocallyHeld)
	{
		return;
	}

	Super::PostNetReceiveLocationAndRotation();
}

void AEMFPhysicsProp::PostNetReceivePhysicState()
{
	if (bLocallyHeld)
	{
		return;
	}

	if (PropMesh && !PropMesh->IsSimulatingPhysics())
	{
		// Show where the server put it. Handing this to Super instead would try to correct a physics
		// body that is not running, which is silently nothing.
		const FRepMovement& RepMove = GetReplicatedMovement();
		SetActorLocationAndRotation(
			FRepMovement::RebaseOntoLocalOrigin(RepMove.Location, this), RepMove.Rotation);
		return;
	}

	Super::PostNetReceivePhysicState();
}

void AEMFPhysicsProp::Multicast_PlayExplosionEffects_Implementation(FVector ExplosionLocation, float VFXScale)
{
	if (ExplosionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), ExplosionVFX, ExplosionLocation,
			FRotator::ZeroRotator, FVector(VFXScale),
			true, true, ENCPoolMethod::None);
	}

	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, ExplosionLocation);
	}
}

void AEMFPhysicsProp::SetHeldPassThrough(bool bPassThrough)
{
	if (bHeldPassThrough == bPassThrough || !PropMesh)
	{
		return;
	}

	bHeldPassThrough = bPassThrough;

	const ECollisionResponse Response = bPassThrough ? ECR_Ignore : ECR_Block;
	PropMesh->SetCollisionResponseToChannel(ECC_WorldStatic, Response);
	PropMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, Response);
}

void AEMFPhysicsProp::SetCapturedByPlate(AEMFChannelingPlateActor* Plate)
{
	if (!Plate || !bCanBeCaptured)
	{
		return;
	}

	CapturingPlate = Plate;
	WeakCaptureTimer = 0.0f;
	PreviousHoldDistance = BIG_NUMBER;
	bHasPreviousPlatePosition = false;
	bReverseLaunchInitialized = false;

	// The plate is spawned with the channeling character as its Owner (UChargeAnimationComponent::
	// SpawnPlate), which is the only link back to who is holding this prop.
	SetSpendingCharacter(Cast<AShooterCharacter>(Plate->GetOwner()));

	// BeginLaunch re-captures the prop onto a fresh plate already flipped to reverse mode. Same
	// call, opposite meaning: that one is the throw, this one is the grab.
	const bool bIsThrow = Plate->IsInReverseMode();

	// A client holding a prop simulates it itself from here on, so that the constraint on its
	// character has a real body to hold and the prop is stopped by walls like it would be for the
	// host. A throw hands the prop back instead: the server flies it, so that the hit it lands and
	// the explosion it sets off are decided in one place.
	if (!HasAuthority())
	{
		bLocallyHeld = !bIsThrow;
		ApplyPropPhysicsSimulation(bLocallyHeld);
	}

	if (PropMesh && PropMesh->IsSimulatingPhysics())
	{
		PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);

		// Switch to Overlap with Pawns: no physics impulse while captured near player
		PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	UE_LOG(LogTemp, Warning, TEXT("[HOLD_DEBUG] %s SetCapturedByPlate: authority=%d throw=%d locallyHeld=%d simulating=%d mobility=%d charge=%.1f"),
		*GetName(), HasAuthority() ? 1 : 0, bIsThrow ? 1 : 0, bLocallyHeld ? 1 : 0,
		(PropMesh && PropMesh->IsSimulatingPhysics()) ? 1 : 0,
		PropMesh ? (int32)PropMesh->Mobility.GetValue() : -1, GetCharge());

	// The server's own copy of this prop has no plate at all and needs to be told what just
	// happened, so it can either start mirroring this client's reported transform, or fly the throw.
	if (!HasAuthority())
	{
		if (AShooterCharacter* Spender = SpendingCharacter.Get())
		{
			if (bIsThrow)
			{
				Spender->Server_LaunchProp(this);
			}
			else
			{
				// Our range travels with the request: only this machine knows this player's charge.
				Spender->Server_CaptureProp(this, GetCaptureRangeForCharacter(Spender));
			}
		}
	}
}

void AEMFPhysicsProp::ReleasedFromCapture()
{
	// A remote hold is ending — tell the server before the local state that identifies it is cleared.
	// A throw is excluded: the throw animation finishing on the thrower's machine says nothing about
	// where the prop is, and releasing the server's hold here would stop the flight mid-air and
	// overwrite its live velocity with the stale one from just before the throw. A thrown prop is
	// finished by hitting something, in OnPropHit, which is where that has always been decided.
	const AEMFChannelingPlateActor* ReleasingPlate = CapturingPlate.Get();
	const bool bWasRemoteHold = !HasAuthority() && ReleasingPlate && !ReleasingPlate->IsInReverseMode();

	CapturingPlate.Reset();
	bHasPreviousPlatePosition = false;
	WeakCaptureTimer = 0.0f;
	bReverseLaunchInitialized = false;

	// Never leave a released prop able to sink through the world.
	SetHeldPassThrough(false);

	// Restore Block with Pawns: normal physics collision when free
	if (PropMesh)
	{
		PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	// Hand the prop back: this machine stops simulating it and goes back to being shown where the
	// server says it is.
	if (bLocallyHeld)
	{
		bLocallyHeld = false;
		ApplyPropPhysicsSimulation(false);
	}

	if (bWasRemoteHold)
	{
		if (AShooterCharacter* Spender = SpendingCharacter.Get())
		{
			Spender->Server_ReleaseProp(this);
		}
	}
}

void AEMFPhysicsProp::BeginRemoteHold(AShooterCharacter* Holder, float HolderCaptureRange)
{
	if (!Holder || !bCanBeCaptured)
	{
		return;
	}

	HoldingCharacter = Holder;
	HeldCaptureRange = HolderCaptureRange;
	LastHeldReportTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	SetSpendingCharacter(Holder);

	// The server stops driving this prop itself — it becomes a kinematic mirror of whatever the
	// holder reports via ApplyHeldTransform, same as the host's own capture already takes the prop
	// off the "resting/falling" path while held.
	ApplyPropPhysicsSimulation(false);

	if (PropMesh)
	{
		PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
}

void AEMFPhysicsProp::EndRemoteHold()
{
	HoldingCharacter = nullptr;
	HeldCaptureRange = 0.0f;

	if (PropMesh)
	{
		PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	// Hand the body back to physics. A fresh SetSimulatePhysics(true) wakes the body at rest, so
	// without this a released prop would freeze for a frame and then just fall, instead of keeping
	// the momentum the holder was visibly carrying it with.
	ApplyPropPhysicsSimulation(true);
	if (PropMesh)
	{
		PropMesh->SetPhysicsLinearVelocity(LastReportedVelocity);
	}
}

void AEMFPhysicsProp::ApplyHeldTransform(const FVector& Location, const FRotator& Rotation, const FVector& LinearVelocity)
{
	SetActorLocationAndRotation(Location, Rotation);
	LastReportedVelocity = LinearVelocity;

	// Proof the holder is still there. Silence past a second is how the watchdog above notices a
	// hold that ended without anybody saying so.
	LastHeldReportTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

AShooterCharacter* AEMFPhysicsProp::GetSpendingCharacter() const
{
	return SpendingCharacter.Get();
}

void AEMFPhysicsProp::SetSpendingCharacter(AShooterCharacter* InCharacter)
{
	if (!InCharacter)
	{
		return;
	}

	SpendingCharacter = InCharacter;
}

// ==================== Decoy ====================

void AEMFPhysicsProp::ApplyItemVerbOnThrow()
{
	// Only the authority: what follows is a decision about what the AI does, and the client that
	// threw the prop runs this same launch code locally for its own feel.
	if (!HasAuthority())
	{
		return;
	}

	const AShooterCharacter* Thrower = GetSpendingCharacter();
	if (!Thrower)
	{
		return;
	}

	switch (Thrower->GetItemVerb())
	{
	case EClassItemVerb::Decoy:
		BecomeDecoy();
		break;

	// Throw is the Wizard's, and it is not implemented here: it is the absence of everything else.
	// Detonate and Heal have no implementation yet at all; when they do, this is where they attach,
	// so that "what my class does with a charged object" is answered in one switch instead of being
	// spread across the prop as a set of unrelated conditions.
	default:
		break;
	}
}

bool AEMFPhysicsProp::CanDetonate() const
{
	// A prop spent by a class that does something else with it never detonates. For the Wizard a
	// fully charged object is ammunition, not a bomb; for the Tank it is a decoy that has to survive
	// landing in order to be one. Letting either blow up on the first thing it touches would delete
	// that class's whole verb.
	//
	// Nobody's prop, nobody's verb: a world explosion or a chain reaction is free to go off.
	const AShooterCharacter* Spender = GetSpendingCharacter();
	if (!Spender)
	{
		return true;
	}

	const EClassItemVerb Verb = Spender->GetItemVerb();
	return Verb != EClassItemVerb::Throw && Verb != EClassItemVerb::Decoy;
}

void AEMFPhysicsProp::BecomeDecoy()
{
	if (!HasAuthority() || DecoyPhase != EPropDecoyPhase::Inactive || bIsDead || !GetWorld())
	{
		return;
	}

	if (DecoyDuration <= 0.0f || DecoyPullRadius <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s cannot become a decoy: duration=%.1f radius=%.0f"),
			*GetName(), DecoyDuration, DecoyPullRadius);
		return;
	}

	// The fuse. Nothing is shown, nothing sounds and nobody is pulled until it runs out: the phase is
	// still set and replicated, because it is what stops a second throw from arming the same prop
	// twice and what tells a joining client this prop is already spoken for.
	DecoyPhase = EPropDecoyPhase::Arming;
	ApplyDecoyPresentation(DecoyPhase);

	// A zero arm delay is allowed and means "goes off on landing": the timer manager fires a
	// zero-length timer on the next tick, so activation still goes through the same path.
	GetWorld()->GetTimerManager().SetTimer(DecoyArmTimer, this, &AEMFPhysicsProp::ActivateDecoy,
		FMath::Max(KINDA_SMALL_NUMBER, DecoyArmDelay), false);

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s armed as a decoy: goes off in %.1fs, will reach %.0f cm (thrown by %s)"),
		*GetName(), DecoyArmDelay, DecoyPullRadius, *GetNameSafe(GetSpendingCharacter()));
}

void AEMFPhysicsProp::ActivateDecoy()
{
	if (!HasAuthority() || DecoyPhase != EPropDecoyPhase::Arming || !GetWorld())
	{
		return;
	}

	// Destroyed mid-arming: the prop is gone, and there is nothing to be loud with.
	if (bIsDead)
	{
		EndDecoy();
		return;
	}

	DecoyPhase = EPropDecoyPhase::Active;
	ApplyDecoyPresentation(DecoyPhase);
	BP_OnDecoyStarted();

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		Coordinator->RegisterDecoy(this, DecoyPullRadius, DecoyDuration);
	}

	GetWorld()->GetTimerManager().SetTimer(DecoyTimer, this, &AEMFPhysicsProp::OnDecoyExpired, DecoyDuration, false);

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s is a decoy for %.1fs, radius %.0f"),
		*GetName(), DecoyDuration, DecoyPullRadius);
}

void AEMFPhysicsProp::EndDecoy()
{
	// Authority only, in both directions: the phase is server-owned, and a client clearing its copy
	// would be overwritten by the next update anyway. Clients end their cosmetics from OnRep.
	if (!HasAuthority() || DecoyPhase == EPropDecoyPhase::Inactive)
	{
		return;
	}

	DecoyPhase = EPropDecoyPhase::Inactive;

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(DecoyArmTimer);
		GetWorld()->GetTimerManager().ClearTimer(DecoyTimer);
	}

	// Unregistering is what releases the enemies holding it, so it has to happen even when the prop
	// is being destroyed — that is the case where they would otherwise keep facing a dead pointer
	// until the coordinator's next sweep.
	//
	// Not while the world is going away, though: GetCoordinator SPAWNS one when there is none, and
	// EndPlay reaches here on level teardown, where spawning an actor is a warning at best.
	UWorld* World = GetWorld();
	if (World && !World->bIsTearingDown)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
		{
			Coordinator->UnregisterDecoy(this);
		}
	}

	ApplyDecoyPresentation(EPropDecoyPhase::Inactive);
	BP_OnDecoyEnded();
}

void AEMFPhysicsProp::OnDecoyExpired()
{
	if (!HasAuthority() || DecoyPhase != EPropDecoyPhase::Active)
	{
		return;
	}

	const FVector Where = GetActorLocation();

	// Release the enemies first, then announce it. The other order would leave a frame in which the
	// prop is invisible and still being fought.
	EndDecoy();
	Multicast_PlayDecoyExpiry(Where);

	// Dead without dying: no OnPropDeath, no gibs, no damage anywhere. The prop is finished as a
	// gameplay object, which is what stops it being recharged, recaptured or shot at, and
	// ResetProp puts it back for the checkpoint system exactly as it does after a real death.
	bIsDead = true;
	SetActorTickEnabled(false);
}

void AEMFPhysicsProp::Multicast_PlayDecoyExpiry_Implementation(FVector Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (DecoyExpiryVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, DecoyExpiryVFX, Location,
			FRotator::ZeroRotator, FVector(DecoyExpiryVFXScale),
			true, true, ENCPoolMethod::None);
	}

	if (DecoyExpirySound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, DecoyExpirySound, Location);
	}

	// On a client this also covers the case where the phase has not replicated down yet: the siren
	// has to stop with the prop it belongs to whichever message arrives first.
	ApplyDecoyPresentation(EPropDecoyPhase::Inactive);

	// And it is gone. Hidden rather than destroyed, because a destroyed prop cannot be restored by
	// the checkpoint system, and because the actor is what carries the charge bar registration.
	if (PropMesh)
	{
		ApplyPropPhysicsSimulation(false);
		PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PropMesh->SetVisibility(false);
	}
	SetActorHiddenInGame(true);

	// The overhead charge bar outlives the mesh otherwise, and floats there over nothing.
	if (UEMFChargeWidgetSubsystem* WidgetSub = World->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterProp(this);
	}
}

void AEMFPhysicsProp::ApplyDecoyPresentation(EPropDecoyPhase Phase)
{
	if (!PropMesh || PresentedPhase == Phase)
	{
		return;
	}

	const EPropDecoyPhase Previous = PresentedPhase;
	PresentedPhase = Phase;

	// ---- Leaving the phase we were in ----

	if (Previous == EPropDecoyPhase::Active)
	{
		if (DecoyRadiusVFXComponent)
		{
			// Deactivate, not destroy: the sphere is allowed to finish the fade it is in the middle of.
			// Normally the system has already ended by itself, having been told to last exactly as long
			// as the pull; this matters for a decoy cut short, shot to pieces or picked back up.
			DecoyRadiusVFXComponent->Deactivate();
			DecoyRadiusVFXComponent = nullptr;
		}

		if (DecoyAudio)
		{
			DecoyAudio->FadeOut(DecoyLoopFadeOutSeconds, 0.0f);
			DecoyAudio = nullptr;
		}

		// Back to whatever the charge is worth. UpdateChargeOverlay no-ops when the charge overlay is
		// switched off on this prop, so the material is cleared first rather than left on.
		PropMesh->SetOverlayMaterial(nullptr);
		UpdateChargeOverlay(PreviousPolarity);
	}

	// ---- Entering the new one ----

	if (Phase == EPropDecoyPhase::Active)
	{
		if (DecoyRadiusVFX)
		{
			// Attached to the mesh, not fired at a location: the prop can still be rolling when the
			// fuse runs out, and a sphere left where it was would mark the wrong ground.
			DecoyRadiusVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				DecoyRadiusVFX, PropMesh, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true);

			// The asset is told the radius rather than knowing it, so the drawn sphere and the radius
			// the coordinator uses cannot drift apart. Duration is the whole active phase: the sphere
			// snaps out to full size and then stands there for as long as the pull lasts, which is what
			// makes "am I inside it" answerable while it matters. Harmless when the system has no such
			// parameters.
			if (DecoyRadiusVFXComponent)
			{
				DecoyRadiusVFXComponent->SetFloatParameter(FName("Radius"), DecoyPullRadius);
				DecoyRadiusVFXComponent->SetFloatParameter(FName("Duration"), FMath::Max(KINDA_SMALL_NUMBER, DecoyDuration));
			}
		}

		if (DecoyOverlayMaterial)
		{
			PropMesh->SetOverlayMaterial(DecoyOverlayMaterial);
		}

		// Attached, not fired at a location, for the same reason as the sphere. Returns null on a
		// dedicated server, which is the correct amount of noise for a machine with no ears.
		if (DecoyLoopSound && !DecoyAudio)
		{
			DecoyAudio = UGameplayStatics::SpawnSoundAttached(DecoyLoopSound, PropMesh);
		}
	}
}

void AEMFPhysicsProp::OnRep_DecoyPhase()
{
	// Cosmetics only. Everything the decoy DOES is decided on the server.
	//
	// A client that misses the Arming update entirely (relevancy, packet loss) gets Active on its own
	// and loses nothing by it: the fuse is silent and invisible, so there is no announcement to miss,
	// and ApplyDecoyPresentation goes straight to what is true now.
	const bool bWasActive = PresentedPhase == EPropDecoyPhase::Active;

	ApplyDecoyPresentation(DecoyPhase);

	if (DecoyPhase == EPropDecoyPhase::Active)
	{
		BP_OnDecoyStarted();
	}
	else if (DecoyPhase == EPropDecoyPhase::Inactive && bWasActive)
	{
		BP_OnDecoyEnded();
	}
}

bool AEMFPhysicsProp::ShouldSkipPlayerForAreaEffect(const AActor* HitActor) const
{
	const APawn* HitPawn = Cast<APawn>(HitActor);
	if (!HitPawn || !HitPawn->IsPlayerControlled())
	{
		// Not a player: NPCs and props always take the effect.
		return false;
	}

	// Nobody blows themselves up with their own prop.
	if (HitPawn == SpendingCharacter.Get())
	{
		return true;
	}

	// Everyone else on the team. Flip bTeammatesImmuneToAreaEffects to let props hit teammates.
	return bTeammatesImmuneToAreaEffects;
}

void AEMFPhysicsProp::DetachFromPlate()
{
	CapturingPlate.Reset();
	bHasPreviousPlatePosition = false;
	bReverseLaunchInitialized = false;

	// A local hold cannot outlive the local plate. While bLocallyHeld is set this machine refuses
	// every replicated update for the prop, so leaving it set after a detach that is not followed by
	// a re-capture strands the prop: it sits wherever this client last simulated it while the server
	// and everyone else see it somewhere else entirely. That is the crate that existed on one screen
	// and not the other.
	if (bLocallyHeld)
	{
		bLocallyHeld = false;
		ApplyPropPhysicsSimulation(false);
	}
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
	if (!Plate || !PropMesh)
	{
		return;
	}

	// Being held and being thrown are two different jobs sharing one plate. The hold is done by a
	// constraint on the holder's character now, not by forces from here, so it gets its own short
	// path; everything below stays the throw.
	if (!Plate->IsInReverseMode())
	{
		UpdateHeldByHandle(DeltaTime);
		return;
	}

	// Only the machine actually flying the prop runs the throw. Everyone else is shown the result.
	if (!PropMesh->IsSimulatingPhysics())
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

	PreviousPlatePosition = PlatePos;
	bHasPreviousPlatePosition = true;

	// === REVERSE MODE: one impulse, then honest physics ===
	//
	// The throw used to be a rail: velocity re-set every frame onto the line through the crosshair,
	// which cancelled gravity and let the camera steer the prop in mid-air. Now it leaves the hand
	// once and the physics engine owns everything after -- arc, bounce, spin, the momentum it carries
	// into what it hits. There is nothing left to tune here but the speed.
	if (bReverseLaunchInitialized)
	{
		return;   // already thrown; the plate no longer has anything to do with it
	}
	bReverseLaunchInitialized = true;
	bIsInReverseFlight = true;
	bHasExploded = false;

	// Air Mail: fresh player launch — eligible for one new bounce.
	bAirMailEligibleFlight = true;
	bAirMailBounceConsumed = false;

	// What the thrower's class turns a charged prop into. This is the host's throw; the client's
	// equivalent is in BeginRemoteLaunch.
	ApplyItemVerbOnThrow();

	// Aim from the thrower's eyes. The plate's own normal is the same direction, but asking the
	// spending character works for a prop thrown by any player, where the plate does not exist on
	// this machine at all.
	FVector AimOrigin;
	FVector AimDir;
	if (!GetReverseFlightAimSource(AimOrigin, AimDir))
	{
		// Deliberately NOT Plate->GetPlateNormal(): a freshly spawned launch plate still holds
		// FVector::ForwardVector there until its first camera update, so on the one frame this reads
		// it the answer would be world +X. Same trap that sent every thrown enemy the same way.
		const APawn* ThrowerPawn = Cast<APawn>(Plate->GetOwner());
		AimDir = ThrowerPawn ? ThrowerPawn->GetBaseAimRotation().Vector() : GetActorForwardVector();
	}

	LaunchAlongAim(AimDir);

	// NOT released here. ReleasedFromCapture restores ECR_Block against pawns, and a thrown prop has
	// to stay Overlap until it lands: the damage, the stun and the charge transfer all arrive through
	// the overlap path. Releasing on launch turned the throw into a prop that bounced off enemies and
	// did nothing to them. The flight ends where it always ended -- on contact, in OnPropHit.
}

void AEMFPhysicsProp::LaunchAlongAim(const FVector& InAimDir)
{
	if (!PropMesh)
	{
		return;
	}

	const FVector AimDir = InAimDir.GetSafeNormal();

	// Leaves exactly where it was aimed. Bending toward an enemy is TickHomingSteer's job now, and
	// doing it here as well would double the correction on the first frame.
	HomingTarget.Reset();
	PropMesh->SetPhysicsLinearVelocity(AimDir * ThrowSpeed);

	if (ReverseLaunchSpinSpeed > 0.0f)
	{
		const FVector RandomAxis = FMath::VRand();
		PropMesh->SetPhysicsAngularVelocityInDegrees(RandomAxis * ReverseLaunchSpinSpeed);
	}
}

void AEMFPhysicsProp::UpdateHeldByHandle(float DeltaTime)
{
	const AEMFChannelingPlateActor* Plate = CapturingPlate.Get();
	if (!Plate || !PropMesh || !PropMesh->IsSimulatingPhysics())
	{
		return;
	}

	// Range is still what decides whether the hold survives, exactly as before — a prop dragged
	// beyond what the charges can hold falls. What is gone is the old wall check: a wall between
	// hand and prop used to drop it after half a second, and now the prop is expected to be stopped
	// by that wall and pulled around it, with the holder's stuck timer resolving the hopeless cases.
	const float EffectiveCaptureRange = GetCaptureRangeForCharacter(GetSpendingCharacter());
	const float Distance = FVector::Dist(GetActorLocation(), Plate->GetActorLocation());

	float CaptureStrength = 0.0f;
	if (Distance < EffectiveCaptureRange)
	{
		const float T = Distance / EffectiveCaptureRange;
		CaptureStrength = 1.0f - T * T * (3.0f - 2.0f * T);
	}

	// Losing the prop is for a hold that is failing, not for one that is still working. A prop being
	// reeled in from the far edge of range starts below CaptureMinStrength by definition, and the
	// old timer fired mid-pull: the prop dropped, snapped back to the server's copy and was
	// immediately re-captured, which is the "pulled in, jerked back, pulled again" double grab.
	// While the gap is closing the hold is doing its job, however weak the number looks.
	const bool bClosing = Distance < PreviousHoldDistance - 1.0f;
	PreviousHoldDistance = Distance;

	if (CaptureStrength < CaptureMinStrength && !bClosing)
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

	// The holder is simulating this prop, and the server is not: tell it where the prop ended up.
	if (bLocallyHeld)
	{
		if (AShooterCharacter* Spender = GetSpendingCharacter())
		{
			Spender->Server_UpdateHeldPropTransform(this, GetActorLocation(), GetActorRotation(),
				PropMesh->GetPhysicsLinearVelocity());
		}
	}
}

bool AEMFPhysicsProp::GetReverseFlightAimSource(FVector& OutOrigin, FVector& OutDirection) const
{
	AShooterCharacter* Thrower = GetSpendingCharacter();
	if (!Thrower)
	{
		return false;
	}

	// The camera, not the pawn's nominal eye height. The throw converges onto the line from the eye
	// through the crosshair, so an origin a few centimetres off bends the whole trajectory — this is
	// the same camera the old code read, just asked of the thrower instead of of player zero.
	if (const APlayerController* PC = Cast<APlayerController>(Thrower->GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			OutOrigin = PC->PlayerCameraManager->GetCameraLocation();
			OutDirection = PC->PlayerCameraManager->GetCameraRotation().Vector();
			return true;
		}
	}

	FRotator EyeRotation;
	Thrower->GetActorEyesViewPoint(OutOrigin, EyeRotation);
	OutDirection = EyeRotation.Vector();
	return true;
}

void AEMFPhysicsProp::TickHomingSteer(float DeltaTime)
{
	if (!bEnableReverseLaunchHoming || HomingAcceleration <= 0.0f || !PropMesh || !PropMesh->IsSimulatingPhysics())
	{
		return;
	}

	const FVector Velocity = PropMesh->GetPhysicsLinearVelocity();
	const float Speed = Velocity.Size();
	if (Speed < 1.0f)
	{
		return;   // nothing to steer; direction would be meaningless
	}

	// Acquire once, then commit. Re-picking every frame makes a throw flick between two enemies that
	// trade places in the cone and arrive at neither.
	if (!HomingTarget.IsValid() || HomingTarget->IsDead())
	{
		HomingTarget = FindHomingTarget(GetActorLocation(), Velocity / Speed);
		if (!HomingTarget.IsValid())
		{
			return;
		}
	}

	// Turn the velocity toward the target while keeping its magnitude, and clamp how much of that
	// turn one frame is allowed to buy. ADDED to what physics already did this frame, so gravity and
	// any bounce it just took are still in there -- that is the whole difference from the old rail.
	const FVector ToTarget = (HomingTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	const FVector Desired = ToTarget * Speed;
	const FVector Steer = (Desired - Velocity).GetClampedToMaxSize(HomingAcceleration * DeltaTime);

	PropMesh->SetPhysicsLinearVelocity(Velocity + Steer);
}

void AEMFPhysicsProp::BeginRemoteLaunch(AShooterCharacter* Thrower)
{
	if (!Thrower || !PropMesh)
	{
		return;
	}

	SetSpendingCharacter(Thrower);

	// The same launch state the plate-driven path sets up on its first reverse tick. There is no
	// plate here to trigger that, so the throw is armed directly.
	bIsInReverseFlight = true;
	bHasExploded = false;
	bAirMailEligibleFlight = true;
	bAirMailBounceConsumed = false;
	RemoteLaunchStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// Same verb dispatch as the host's throw above, after SetSpendingCharacter so there is somebody
	// to ask.
	ApplyItemVerbOnThrow();

	// The physics body back that BeginRemoteHold took away, BEFORE the impulse: velocity set on a
	// body that is not simulating goes nowhere.
	ApplyPropPhysicsSimulation(true);

	// One impulse and the server is done steering. The old path re-derived a speed here from the
	// thrower's charge, which the server does not have for a client, and flew the prop by hand every
	// tick; both of those are gone with the rail.
	FVector AimOrigin;
	FVector AimDir;
	if (!GetReverseFlightAimSource(AimOrigin, AimDir))
	{
		AimDir = Thrower->GetActorForwardVector();
	}

	LaunchAlongAim(AimDir);
}

// ==================== Collision Callbacks ====================

void AEMFPhysicsProp::OnPropHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// A prop thrown by a remote client has landed. This is the end of that throw, and it has to be
	// resolved here rather than on a timer: hitting something is what "the throw is over" means, and
	// this runs before every early exit below so a prop that lands without exploding still frees up.
	// Whatever the impact does next (damage, detonation) is unaffected — it is decided further down,
	// on this same authority, exactly as it always was.
	//
	// Only while actually in flight. A prop being carried scrapes the floor and the walls constantly,
	// and without this gate the first of those contacts ended the hold server-side while the player
	// was still holding it — the throw that followed was then rejected as coming from someone who
	// was not holding anything.
	if (HoldingCharacter && bIsInReverseFlight && HasAuthority())
	{
		HoldingCharacter = nullptr;
		if (PropMesh)
		{
			PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		}
	}

	// Everything past this point changes the world: damage, stun, knockback, detonation, the Air Mail
	// bounce. A client simulates the prop it is carrying, so this callback fires there too now, and
	// each of those would otherwise happen on that one machine and nowhere else. Same rule as
	// AShooterProjectile::CanAffectWorld: the impact is resolved where it can be resolved once.
	if (!HasAuthority())
	{
		return;
	}

	// Air Mail state resolution — BEFORE the slow-prop early exit so a gently landing
	// returned prop still clears its tag. A kicked prop resolves on this impact (the
	// weak-impact path below carries the primed KickDamage); an un-kicked returning
	// prop just lands.
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailKicked);
	}
	else if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailIncoming))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailIncoming);
	}

	// Early exit: nothing to do for slow/resting props that aren't in flight
	if (!bIsInReverseFlight && CachedPreCollisionSpeed < ExplosionSpeedThreshold)
	{
		return;
	}

	// Impact check. NOT gated on bCanExplode: a prop that cannot detonate still has to land its hit.
	// Gating the whole block was why switching explosions off made thrown props pass through enemies
	// instead of staggering them -- the weak-impact branch lived inside the explosive one.
	if (!bHasExploded && !bIsDead && PropMesh)
	{
		const float Speed = CachedPreCollisionSpeed;
		const float Threshold = bIsInReverseFlight ? ExplosionSpeedThreshold : CollateralExplosionSpeedThreshold;

		if (Speed >= Threshold)
		{
			// A prop a player threw NEVER detonates on an enemy, whatever bCanExplode says and whatever
			// it is charged to. That flag being left on is what made a fully charged throw pass through
			// an enemy doing nothing: it cleared ExplosionMinCharge, took the detonation branch instead
			// of the weak-impact one, and the detonation had nothing to show. Half-charged props went
			// on working, which is what made it look like charge broke the damage.
			const bool bChargeBelowThreshold = !bCanExplode || bIsInReverseFlight
				|| FMath::Abs(GetCharge()) <= ExplosionMinCharge;

			// Direct NPC hit
			AShooterNPC* HitNPC = Cast<AShooterNPC>(OtherActor);
			if (HitNPC && !HitNPC->IsDead())
			{
				if (bChargeBelowThreshold)
				{
					UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Direct NPC hit → %s (Speed=%.0f), |charge|=%.1f < %.1f → weak impact"),
						*GetName(), *OtherActor->GetName(), Speed, FMath::Abs(GetCharge()), ExplosionMinCharge);
					ApplyWeakImpactToNPC(HitNPC, Hit.ImpactNormal, Hit.ImpactPoint);
					// Air Mail: the prop survived (no explosion) — override the weak-impact
					// reflection with the return-to-player flight. Character impact → no angle gate.
					TryAirMailBounce(Hit.ImpactNormal, Hit.ImpactPoint, /*bCharacterImpact=*/ true);
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Direct NPC hit → %s, Speed=%.0f, |charge|=%.1f >= %.1f → EXPLODE"),
					*GetName(), *OtherActor->GetName(), Speed, FMath::Abs(GetCharge()), ExplosionMinCharge);
				if (CriticalVelocity > 0.0f && Speed >= CriticalVelocity)
				{
					OnCriticalVelocityImpact.Broadcast(this, GetActorLocation(), Speed);
				}
				Explode(1.0f, 1.0f, 1.0f);
				return;
			}

			// Environment hit — low charge means no detonation regardless of geometry
			if (bChargeBelowThreshold)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Env hit on %s (Speed=%.0f), |charge|=%.1f < %.1f → no explosion"),
					*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("NULL"), Speed, FMath::Abs(GetCharge()), ExplosionMinCharge);
				// Fall through to "End reverse flight" code below — physics handles the bounce naturally
			}
			else
			{
				// Environment hit — check if prop center is facing the impact point
				const FVector PropCenter = GetActorLocation();
				const FVector VelDir = PropMesh->GetPhysicsLinearVelocity().GetSafeNormal();
				const FVector ToImpact = (Hit.ImpactPoint - PropCenter).GetSafeNormal();
				const float CenterDot = FVector::DotProduct(VelDir, ToImpact);
				bool bShouldDetonate = false;

				// High dot = impact point is ahead of center along velocity = head-on hit
				if (CenterDot >= CenterHitDotThreshold)
				{
					bShouldDetonate = true;
					UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Center hit (dot=%.2f >= %.2f, |charge|=%.1f > %.1f) → detonate"),
						*GetName(), CenterDot, CenterHitDotThreshold, FMath::Abs(GetCharge()), ExplosionMinCharge);
				}

				// Fallback: check if any NPC is within explosion radius
				if (!bShouldDetonate)
				{
					FCollisionQueryParams OverlapParams;
					OverlapParams.AddIgnoredActor(this);
					TArray<FOverlapResult> NearbyOverlaps;
					GetWorld()->OverlapMultiByChannel(
						NearbyOverlaps, PropCenter, FQuat::Identity,
						ECC_Pawn, FCollisionShape::MakeSphere(ExplosionRadius), OverlapParams);

					for (const FOverlapResult& Overlap : NearbyOverlaps)
					{
						AShooterNPC* NearbyNPC = Cast<AShooterNPC>(Overlap.GetActor());
						if (NearbyNPC && !NearbyNPC->IsDead())
						{
							bShouldDetonate = true;
							UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: NPC %s in explosion radius (|charge|=%.1f > %.1f) → detonate"),
								*GetName(), *NearbyNPC->GetName(), FMath::Abs(GetCharge()), ExplosionMinCharge);
							break;
						}
					}
				}

				if (bShouldDetonate)
				{
					if (CriticalVelocity > 0.0f && Speed >= CriticalVelocity)
					{
						OnCriticalVelocityImpact.Broadcast(this, GetActorLocation(), Speed);
					}
					Explode(1.0f, 1.0f, 1.0f);
					return;
				}

				// Edge graze with no NPC nearby — prop continues flying
				UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Edge graze on %s (dot=%.2f), no NPC nearby → skip"),
					*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("NULL"), CenterDot);
				return;
			}
		}
	}

	// End reverse flight state on any blocking collision (wall, floor, etc.)
	if (bIsInReverseFlight)
	{
		// Air Mail: launched prop hit something without detonating — bounce it back toward
		// the player (must run BEFORE bIsInReverseFlight is cleared: the bounce only applies
		// to player-launched props). Non-explosive props reach here even for direct NPC hits
		// (the explosive block above is bCanExplode-gated) — character hits skip the angle gate.
		TryAirMailBounce(Hit.ImpactNormal, Hit.ImpactPoint,
			/*bCharacterImpact=*/ OtherActor && OtherActor->IsA<APawn>());

		UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: bIsInReverseFlight set to FALSE due to collision with %s"),
			*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("NULL"));
		bIsInReverseFlight = false;
	}
}

bool AEMFPhysicsProp::TryAirMailBounce(const FVector& ImpactNormal, const FVector& ImpactPoint, bool bCharacterImpact)
{
	// Only player-launched props return (the eligibility flag outlives the steered
	// reverse-flight window), once per launch, and only if the prop survived.
	if (bAirMailBounceConsumed || !(bIsInReverseFlight || bAirMailEligibleFlight) || bHasExploded || bIsDead || !PropMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] prop %s bounce skipped: consumed=%d reverseFlight=%d eligible=%d exploded=%d dead=%d"),
			*GetName(), bAirMailBounceConsumed ? 1 : 0, bIsInReverseFlight ? 1 : 0, bAirMailEligibleFlight ? 1 : 0,
			bHasExploded ? 1 : 0, bIsDead ? 1 : 0);
		return false;
	}

	UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this);
	if (!AirMail)
	{
		return false;
	}

	FVector ReturnVelocity;
	if (!AirMail->TryComputeBounce(GetActorLocation(), CachedPreCollisionVelocity, ImpactNormal, ReturnVelocity, bCharacterImpact))
	{
		return false;
	}

	bAirMailBounceConsumed = true;
	bAirMailEligibleFlight = false;

	// End the reverse flight HERE: UpdateReverseFlight steers the prop along the aim line every
	// tick and would overwrite the return velocity on the very next frame (this was why bounces
	// off NPCs appeared to do nothing — the NPC weak-impact path returns without clearing it).
	bIsInReverseFlight = false;

	PropMesh->SetPhysicsLinearVelocity(ReturnVelocity);
	// Spear-orient toward the player (long axis along the return velocity) instead of the old
	// random VRand() tumble, so the incoming object reads as a spear, not a chaotic spin.
	AirMailOrientSpear(PropMesh, ReturnVelocity);
	Tags.Add(UUpgrade_AirKick::TAG_AirMailIncoming);
	AirMail->PlayBounceFeedback(ImpactPoint);

	UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] launched prop %s bounced toward player (pre-impact speed=%.0f)"),
		*GetName(), CachedPreCollisionSpeed);
	return true;
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

	// NPC contact — launched props use the lower threshold, collateral the higher one. Same reason as
	// the sweep path above for not gating on bCanExplode: a non-explosive prop must still connect.
	if (!bHasExploded)
	{
		const float OverlapThreshold = bIsInReverseFlight ? ExplosionSpeedThreshold : CollateralExplosionSpeedThreshold;
		if (CachedPreCollisionSpeed >= OverlapThreshold)
		{
			// Same rule as the sweep path: a thrown prop always lands a weak impact and never detonates,
			// so a full charge cannot turn a working throw into one that quietly does nothing.
			if (!bCanExplode || bIsInReverseFlight || FMath::Abs(GetCharge()) <= ExplosionMinCharge)
			{
				const FVector OverlapNormal = bFromSweep
					? FVector(SweepResult.ImpactNormal)
					: (GetActorLocation() - HitNPC->GetActorLocation()).GetSafeNormal();
				const FVector OverlapPoint = bFromSweep ? FVector(SweepResult.ImpactPoint) : HitNPC->GetActorLocation();

				UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Overlap with %s (Speed=%.0f), |charge|=%.1f < %.1f → weak impact"),
					*GetName(), *HitNPC->GetName(), CachedPreCollisionSpeed, FMath::Abs(GetCharge()), ExplosionMinCharge);

				ApplyWeakImpactToNPC(HitNPC, OverlapNormal, OverlapPoint);

				// Air Mail: kicked-flight resolves on this NPC contact; an un-kicked launched
				// prop that survived bounces back to the player instead of the weak reflect.
				if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
				{
					Tags.Remove(UUpgrade_AirKick::TAG_AirMailKicked);
				}
				else
				{
					TryAirMailBounce(OverlapNormal, OverlapPoint, /*bCharacterImpact=*/ true);
				}
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("[PROP_DETONATION] %s: Overlap with %s (Speed=%.0f), |charge|=%.1f >= %.1f → EXPLODE"),
				*GetName(), *HitNPC->GetName(), CachedPreCollisionSpeed, FMath::Abs(GetCharge()), ExplosionMinCharge);
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

	// Air Mail: NON-explosive launched props reach this point on NPC contact (the explosive
	// block above only runs with bCanExplode) — they survived by definition, so bounce back
	// to the player. Kicked-flight props resolve their flight here instead.
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailKicked);
	}
	else
	{
		const FVector OverlapNormal = bFromSweep
			? FVector(SweepResult.ImpactNormal)
			: (GetActorLocation() - HitNPC->GetActorLocation()).GetSafeNormal();
		TryAirMailBounce(OverlapNormal, ImpactPoint, /*bCharacterImpact=*/ true);
	}

	if (bLogEMForces)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp %s overlap NPC %s: Speed=%.0f, KineticDmg=%.1f, EMFDmg=%.1f"),
			*GetName(), *HitNPC->GetName(), ImpactSpeed, KineticDamage, EMFDamage);
	}
}

void AEMFPhysicsProp::ApplyWeakImpactToNPC(AShooterNPC* HitNPC, const FVector& ImpactNormal, const FVector& ImpactPoint)
{
	if (!HitNPC || HitNPC->IsDead() || !PropMesh)
	{
		return;
	}

	// Evaluate damage/stun from charge-driven curves (fall back to flat values if curve is null).
	// Sampled before the charge transfer below so the impact reflects the prop's charge at the moment of contact.
	const float AbsChargeAtImpact = FMath::Abs(GetCharge());
	const float ResolvedDamage = WeakImpactDamageByCharge
		? WeakImpactDamageByCharge->GetFloatValue(AbsChargeAtImpact)
		: WeakImpactDamage;
	const float ResolvedStunDuration = WeakImpactStunDurationByCharge
		? WeakImpactStunDurationByCharge->GetFloatValue(AbsChargeAtImpact)
		: WeakImpactStunDuration;

	// 1. Damage
	if (ResolvedDamage > 0.0f)
	{
		FDamageEvent WeakEvent;
		WeakEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
		HitNPC->TakeDamage(ResolvedDamage, WeakEvent, nullptr, this);
	}

	// 2. Stun (short, separate from explosion stun)
	if (ResolvedStunDuration > 0.0f && !HitNPC->IsDead())
	{
		HitNPC->ApplyExplosionStun(ResolvedStunDuration, WeakImpactStunMontage);
	}

	// 3. Charge transfer: WeakImpactChargeShareRatio of prop's charge goes to NPC, the rest stays
	const float MyCharge = GetCharge();
	if (!FMath::IsNearlyZero(MyCharge))
	{
		if (UEMFVelocityModifier* NPCModifier = HitNPC->FindComponentByClass<UEMFVelocityModifier>())
		{
			const float TransferAmount = MyCharge * WeakImpactChargeShareRatio;
			NPCModifier->SetCharge(NPCModifier->GetCharge() + TransferAmount);
			SetCharge(MyCharge - TransferAmount);
		}
	}

	// 4. Velocity reflection — manual, because:
	//    - During reverse flight prop is ECR_Overlap on Pawn (no natural physics block)
	//    - Prop's PhysMaterial.Restitution = 0, so Block path doesn't bounce naturally either
	//    - Skip if velocity already points away from surface (dot >= 0) so we don't fight physics
	const FVector InVel = PropMesh->GetPhysicsLinearVelocity();
	const float VelDotN = FVector::DotProduct(InVel, ImpactNormal);
	if (VelDotN < 0.0f)
	{
		const FVector Reflected = FMath::GetReflectionVector(InVel, ImpactNormal);
		PropMesh->SetPhysicsLinearVelocity(Reflected * WeakImpactBounceRestitution);
	}

	// 5. Exit reverse-flight + restore Block on Pawn so subsequent collisions behave normally
	if (bIsInReverseFlight)
	{
		bIsInReverseFlight = false;
	}
	if (CapturingPlate.IsValid())
	{
		ReleasedFromCapture();
	}

	// 6. Effects (reuse the existing impact assets)
	if (EMFDischargeVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), EMFDischargeVFX, ImpactPoint,
			FRotator::ZeroRotator, FVector(EMFDischargeVFXScale),
			true, true, ENCPoolMethod::None);
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, ImpactPoint);
	}

	LastCollisionDamageTime = GetWorld()->GetTimeSeconds();
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

	// If prop can explode and hasn't yet — explode instead of just dying.
	//
	// CanDetonate is asked here as well as inside Explode, and it has to be: this branch returns on
	// the assumption that the explosion finished the prop off, so a prop whose class refuses to
	// detonate used to come out of Die neither exploded nor dead. Shot to pieces it simply stood
	// there, and a decoy shot to pieces never let its enemies go.
	if (bCanExplode && !bHasExploded && CanDetonate())
	{
		Explode(1.0f, 1.0f, 1.0f);
		return;
	}

	bIsDead = true;
	SetCharge(0.0f);
	SetActorTickEnabled(false);

	// Shot to pieces while it was the bait: the enemies on it are released here rather than left to
	// notice on their own. No-op on a client and on a prop that was never a decoy.
	EndDecoy();

	OnPropDeath.Broadcast(this, Killer);

	// Release from capture if held
	if (CapturingPlate.IsValid())
	{
		ReleasedFromCapture();
	}

	// Spawn GC destruction if assigned. Every machine has to run it: it is what hides the intact
	// mesh as well as what makes the pieces, so skipping it on a client leaves the prop standing.
	if (PropGeometryCollection)
	{
		if (HasAuthority())
		{
			Multicast_PlayDeathVisuals(GetActorLocation());
		}
		else
		{
			SpawnDestructionGC(GetActorLocation());
		}
	}
}

void AEMFPhysicsProp::Multicast_PlayDeathVisuals_Implementation(FVector DestructionOrigin)
{
	SpawnDestructionGC(DestructionOrigin);
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
	ApplyPropPhysicsSimulation(false);
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
	// Gated here rather than at the six call sites: every one of them is a different reason to
	// explode (impact, velocity, damage, scripted), and missing one would leave the mechanic working
	// almost always, which is worse than not working at all.
	if (!CanDetonate())
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s did not explode: spent by %s, whose class uses props another way"),
			*GetName(), *GetNameSafe(GetSpendingCharacter()));
		return;
	}

	if (bHasExploded || bIsDead)
	{
		return;
	}

	// An explosion damages, launches, stuns and destroys — all of it world state, none of it a
	// client's to decide. This used to be true by accident, because only the server ever simulated
	// a prop and so only the server's collisions fired. A client now simulates the prop it carries,
	// so the accident is gone and the rule has to be written down. Everyone still SEES the blast:
	// the authority multicasts the effects at the end of this function.
	if (!HasAuthority())
	{
		return;
	}

	bHasExploded = true;
	bIsInReverseFlight = false;
	bAirMailEligibleFlight = false;

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

		for (const FOverlapResult& Overlap : DamageOverlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || ShouldSkipPlayerForAreaEffect(HitActor) || DamagedActors.Contains(HitActor))
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

	// Credit the character who spent this prop.
	if (ImpactTotalDamage > 0.0f)
	{
		AShooterCharacter* Credited = SpendingCharacter.Get();
		if (!Credited)
		{
			// Nobody ever held this prop (chain explosion, shot in place, NPC threw it). Single
			// player still credits the local player so BP upgrades counting prop impacts keep
			// firing. TODO(COOP): delete this fallback once teammates exist, it credits player 0.
			Credited = Cast<AShooterCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
			UE_LOG(LogTemp, Verbose, TEXT("[COOP_DEBUG] %s exploded with no spending character, credited local player 0"), *GetName());
		}

		if (Credited)
		{
			Credited->OnPropImpact.Broadcast(this, ImpactTotalDamage, ImpactKillCount);
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
				// The boss takes NO physics impulse from prop explosions — it reacts via slowdown +
				// action-interrupt in ApplyExplosionStun (below) instead of being launched.
				if (!Cast<ABossCharacter>(HitCharacter))
				{
					const FVector LaunchVelocity = ImpulseDir * ExplosionImpulseStrength * FalloffAlpha * DamageMultiplier * ChargeScale;
					HitCharacter->LaunchCharacter(LaunchVelocity, false, true);
				}
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

	// Stun nearby NPCs (with LOS).
	// Players cannot be stunned from here at all: the AShooterNPC cast below filters them out
	// before any gate runs. To let a thrown prop stun teammates, add a player branch to this loop
	// and let ShouldSkipPlayerForAreaEffect decide, same as the damage loop above.
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

	// Distribute charge among nearby props and NPCs (instead of chain reaction destruction)
	if (FinalRadius > 0.0f)
	{
		const float MyCharge = GetCharge();
		if (!FMath::IsNearlyZero(MyCharge))
		{
			// Overlap on both WorldDynamic (props) and Pawn (NPCs) channels
			TArray<FOverlapResult> ChargeOverlaps;
			FCollisionShape ChargeSphere = FCollisionShape::MakeSphere(FinalRadius);
			FCollisionQueryParams ChargeQueryParams;
			ChargeQueryParams.AddIgnoredActor(this);

			// Gather props
			TArray<AEMFPhysicsProp*> NearbyProps;
			TArray<AShooterNPC*> NearbyNPCs;

			GetWorld()->OverlapMultiByChannel(
				ChargeOverlaps, ExplosionLocation, FQuat::Identity,
				ECC_WorldDynamic, ChargeSphere, ChargeQueryParams);

			for (const FOverlapResult& Overlap : ChargeOverlaps)
			{
				AEMFPhysicsProp* OtherProp = Cast<AEMFPhysicsProp>(Overlap.GetActor());
				if (OtherProp && OtherProp != this && !OtherProp->IsDead())
				{
					NearbyProps.AddUnique(OtherProp);
				}
			}

			// Gather NPCs
			ChargeOverlaps.Reset();
			GetWorld()->OverlapMultiByChannel(
				ChargeOverlaps, ExplosionLocation, FQuat::Identity,
				ECC_Pawn, ChargeSphere, ChargeQueryParams);

			for (const FOverlapResult& Overlap : ChargeOverlaps)
			{
				AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
				if (NPC && !NPC->IsDead() && NPC->FindComponentByClass<UEMFVelocityModifier>())
				{
					NearbyNPCs.AddUnique(NPC);
				}
			}

			const int32 TotalReceivers = NearbyProps.Num() + NearbyNPCs.Num();
			if (TotalReceivers > 0)
			{
				const float ChargePerReceiver = MyCharge / static_cast<float>(TotalReceivers);

				for (AEMFPhysicsProp* Prop : NearbyProps)
				{
					Prop->SetCharge(Prop->GetCharge() + ChargePerReceiver);
				}

				for (AShooterNPC* NPC : NearbyNPCs)
				{
					UEMFVelocityModifier* NPCModifier = NPC->FindComponentByClass<UEMFVelocityModifier>();
					NPCModifier->SetCharge(NPCModifier->GetCharge() + ChargePerReceiver);
				}

				UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: Distributed charge %.1f among %d receivers (%d props, %d NPCs, %.1f each)"),
					*GetName(), MyCharge, TotalReceivers, NearbyProps.Num(), NearbyNPCs.Num(), ChargePerReceiver);
			}
		}
	}

	// VFX and SFX, for everyone. Explode only runs on the authority, so spawning these directly here
	// showed the blast to the host and left every client watching the prop vanish in silence. The
	// multicast plays them on the server too, so this is still one call.
	Multicast_PlayExplosionEffects(ExplosionLocation, FinalVFXScale);

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

bool AEMFPhysicsProp::CanBeGrabbedBy(const AActor* Grabber) const
{
	if (!bCanBeCaptured || IsCapturedByPlate() || IsDead())
	{
		return false;
	}

	// Only a fully charged prop can be taken. Charging it IS the cost of the ammunition, and a
	// half-charged prop being grabbable turns the verb into "pick things up" rather than "spend a
	// charged object".
	//
	// Unconditional, and specifically NOT gated on the grabber's class item verb any more. That gate
	// came from the Wizard work and it only ever fired for a character with a PlayerClassDefinition
	// whose ItemVerb was Throw -- on a plain BP_ShooterCharacter, GetItemVerb() returns None and the
	// whole rule silently did not exist, which is exactly what "grabbing works at any charge" was.
	return IsAtMaxCharge();
}

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
	bAirMailEligibleFlight = false;
	bAirMailBounceConsumed = false;
	CachedPreCollisionSpeed = 0.0f;
	CachedChargeScale = 1.0f;
	CurrentHP = MaxHP;
	SetActorTickEnabled(true);

	// A decoy that ran out and vanished also gave up its charge bar, because the widget hides itself
	// on IsDead and a client never learns that a prop is dead. Restoring the prop has to restore the
	// bar with it, and RegisterProp ignores a prop it already has.
	EndDecoy();
	if (UWorld* World = GetWorld())
	{
		if (UEMFChargeWidgetSubsystem* WidgetSub = World->GetSubsystem<UEMFChargeWidgetSubsystem>())
		{
			WidgetSub->RegisterProp(this);
		}
	}

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

		// Match BeginPlay logic: uncharged props start with physics off.
		// Static-mode subclasses never enable physics on PropMesh — they manage the mesh state themselves.
		if (FMath::IsNearlyZero(DefaultCharge) || bKeepPropMeshStatic)
		{
			ApplyPropPhysicsSimulation(false);
		}
		else
		{
			ApplyPropPhysicsSimulation(true);
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

void AEMFPhysicsProp::RestoreFromCheckpointState(const FPropCheckpointData& State)
{
	if (State.bWasDead)
	{
		// Prop should be dead at checkpoint — silently set dead state (no GC spawn / explosion)
		if (!bIsDead)
		{
			bIsDead = true;
			bHasExploded = true;
			bIsInReverseFlight = false;
			bAirMailEligibleFlight = false;
			CachedPreCollisionSpeed = 0.0f;
			SetCharge(0.0f);
			SetActorTickEnabled(false);
			SetActorHiddenInGame(true);

			if (CapturingPlate.IsValid())
			{
				ReleasedFromCapture();
			}

			if (PropMesh)
			{
				PropMesh->SetVisibility(false);
				PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				ApplyPropPhysicsSimulation(false);
			}
		}
		// Already dead — leave as is
	}
	else
	{
		// Prop should be alive at checkpoint
		if (bIsDead)
		{
			// Bring back from dead — ResetProp handles GC cleanup, visibility, capture release
			ResetProp();
		}

		// Apply checkpoint state
		SetActorTransform(State.Transform);
		CurrentHP = FMath::Clamp(State.CurrentHP, 0.0f, MaxHP);
		SetCharge(State.Charge);

		if (PropMesh)
		{
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
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

	// Push it out to everyone. Guarded so the OnRep that calls back in here on a client cannot
	// bounce its own value back.
	if (HasAuthority())
	{
		ReplicatedCharge = NewCharge;
	}

	// Enable physics and tick when prop transitions from uncharged to charged.
	// Static-mode subclasses (e.g. ATurretBuilding) opt out — they keep PropMesh kinematic
	// and handle destruction via their own path (e.g. hiding PropMesh + spawning GC).
	if (PropMesh && FMath::IsNearlyZero(OldCharge) && !FMath::IsNearlyZero(NewCharge) && !bKeepPropMeshStatic)
	{
		ApplyPropPhysicsSimulation(true);
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

	// A decoy wears its own overlay, and its charge keeps changing while it is one (enemies shoot
	// it, it discharges). Without this the siren overlay was replaced by the ordinary charge one on
	// the first charge event after the throw.
	if (PresentedPhase == EPropDecoyPhase::Active && DecoyOverlayMaterial)
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
