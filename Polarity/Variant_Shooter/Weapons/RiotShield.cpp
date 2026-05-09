// RiotShield.cpp

#include "RiotShield.h"
#include "RiotShieldPickup.h"
#include "ShooterCharacter.h"
#include "WeaponRecoilComponent.h"
#include "ChargeAnimationComponent.h"
#include "EMF_FieldComponent.h"
#include "DamageTypes/DamageType_Melee.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveVector.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"

// GC includes (mirrors ShooterWeapon_Melee break path)
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemTypes.h"
#include "Field/FieldSystemObjects.h"

ARiotShield::ARiotShield()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	ShieldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldMesh"));
	ShieldMesh->SetupAttachment(RootComponent);
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ShieldMesh->SetCollisionResponseToAllChannels(ECR_Block);
	ShieldMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	ShieldMesh->SetGenerateOverlapEvents(false);
	ShieldMesh->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	// FP rendering: render only in owner view, no shadows from shield mesh in cameraproxies
	ShieldMesh->SetOnlyOwnerSee(true);
	ShieldMesh->bCastDynamicShadow = false;
	ShieldMesh->CastShadow = false;
	ShieldMesh->bSelfShadowOnly = false;

	// EMF field: holds the shield's charge while equipped (no widget on player-held shield).
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));

	// Default damage types blocked: ranged is the obvious one; designer can extend in BP.
	HealthDrainingDamageTypes.Reset();
}

float ARiotShield::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void ARiotShield::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}
}

void ARiotShield::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = MaxHealth;
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void ARiotShield::EquipToCharacter(AShooterCharacter* Character)
{
	if (!Character)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SHIELD] EquipToCharacter: null character"));
		return;
	}

	OwnerCharacter = Character;
	SetOwner(Character);

	UCameraComponent* Camera = Character->GetFirstPersonCameraComponent();
	if (!Camera)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SHIELD] EquipToCharacter: character has no first-person camera"));
		return;
	}

	AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(FVector::ZeroVector);
	SetActorRelativeRotation(FRotator::ZeroRotator);

	// Start in raised pose immediately (per design: pickup → equipped + raised).
	State = ERiotShieldState::Raised;
	PostTransitionState = ERiotShieldState::Raised;
	TransitionElapsed = StateTransitionTime;
	ShieldMesh->SetRelativeLocation(RaisedRelativeLocation);
	ShieldMesh->SetRelativeRotation(RaisedRelativeRotation);

	UpdateMeshCollision();
	ApplyWalkSpeedPenalty();

	// Play catch → hold-loop on the FP mesh, reusing the prop-capture montages.
	if (UChargeAnimationComponent* ChargeAnim = Character->FindComponentByClass<UChargeAnimationComponent>())
	{
		ChargeAnim->PlayShieldCatchAndHold();
	}

	OnStateChanged.Broadcast(State);
}

void ARiotShield::Toggle()
{
	if (State == ERiotShieldState::Bashing)
	{
		return;
	}
	if (IsRaised())
	{
		Lower();
	}
	else
	{
		Raise();
	}
}

void ARiotShield::Raise()
{
	if (State == ERiotShieldState::Raised || State == ERiotShieldState::Bashing)
	{
		return;
	}

	TransitionStartLocation = ShieldMesh->GetRelativeLocation();
	TransitionStartRotation = ShieldMesh->GetRelativeRotation();
	TransitionElapsed = 0.0f;
	PostTransitionState = ERiotShieldState::Raised;
	State = ERiotShieldState::Transitioning;

	ApplyWalkSpeedPenalty();
	UpdateMeshCollision();

	if (RaiseSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, RaiseSound, GetActorLocation());
	}
	OnStateChanged.Broadcast(State);
}

void ARiotShield::Lower()
{
	if (State == ERiotShieldState::Lowered || State == ERiotShieldState::Bashing)
	{
		return;
	}

	TransitionStartLocation = ShieldMesh->GetRelativeLocation();
	TransitionStartRotation = ShieldMesh->GetRelativeRotation();
	TransitionElapsed = 0.0f;
	PostTransitionState = ERiotShieldState::Lowered;
	State = ERiotShieldState::Transitioning;

	RestoreWalkSpeedPenalty();
	UpdateMeshCollision();

	if (LowerSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, LowerSound, GetActorLocation());
	}
	OnStateChanged.Broadcast(State);
}

void ARiotShield::StartBash()
{
	if (BashCooldownRemaining > 0.0f)
	{
		return;
	}
	if (State != ERiotShieldState::Raised)
	{
		return;
	}

	State = ERiotShieldState::Bashing;
	BashElapsed = 0.0f;
	BashHitActorsThisSwing.Reset();

	if (BashSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BashSound, GetActorLocation());
	}
	OnStateChanged.Broadcast(State);
}

void ARiotShield::ThrowAway()
{
	if (bPendingDestroy)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !PickupClass || !OwnerCharacter)
	{
		TearDownAndScheduleDestroy();
		return;
	}

	UCameraComponent* Camera = OwnerCharacter->GetFirstPersonCameraComponent();
	const FTransform RefTransform = Camera ? Camera->GetComponentTransform() : OwnerCharacter->GetActorTransform();

	const FVector SpawnLoc = RefTransform.TransformPosition(ThrowSpawnOffset);
	const FRotator SpawnRot = RefTransform.Rotator();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = OwnerCharacter;

	ARiotShieldPickup* Pickup = World->SpawnActor<ARiotShieldPickup>(PickupClass, SpawnLoc, SpawnRot, Params);
	if (Pickup)
	{
		// Transfer the shield's stored charge onto the pickup so it survives the throw and can drive
		// the floating charge widget over the world pickup. This is the value originally yanked from
		// the NPC (or set by designer for hand-placed pickups).
		Pickup->SetCharge(GetCharge());

		const FVector WorldLinearImpulse = RefTransform.TransformVector(ThrowLinearImpulse);
		Pickup->SpawnAsThrown(WorldLinearImpulse, ThrowAngularImpulse);
		// Active player throw → arm impact-stun. Passively-placed pickups (no SpawnAsThrown call) keep this false.
		Pickup->SetCanStunOnImpact(true);
	}

	// Throw FP montage interrupts catch/hold and clears the loop timer.
	if (OwnerCharacter)
	{
		if (UChargeAnimationComponent* ChargeAnim = OwnerCharacter->FindComponentByClass<UChargeAnimationComponent>())
		{
			ChargeAnim->PlayShieldThrow();
		}
	}

	TearDownAndScheduleDestroy();
}

void ARiotShield::BreakShield()
{
	if (bPendingDestroy)
	{
		return;
	}

	if (BreakSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BreakSound, GetActorLocation());
	}
	OnBroken.Broadcast();

	// Stop catch/hold without playing the throw montage — shield was destroyed, not thrown.
	if (OwnerCharacter)
	{
		if (UChargeAnimationComponent* ChargeAnim = OwnerCharacter->FindComponentByClass<UChargeAnimationComponent>())
		{
			ChargeAnim->StopShieldFPMontages();
		}
	}

	SpawnBreakDestructionGC();
	TearDownAndScheduleDestroy();
}

void ARiotShield::TearDownAndScheduleDestroy()
{
	if (bPendingDestroy)
	{
		return;
	}
	bPendingDestroy = true;

	// Stop intercepting hits and become invisible immediately so the rest of this frame's
	// hitscan callers can finish referencing us without surprises (we're still alive
	// as a UObject — destruction happens on the next tick via SetLifeSpan).
	if (ShieldMesh)
	{
		ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShieldMesh->SetVisibility(false, true);
	}

	RestoreWalkSpeedPenalty();
	SetActorTickEnabled(false);

	// One-tick deferral keeps us alive long enough for the calling stack frame to unwind
	// safely (e.g. ApplyHitscanDamage continues to dereference WallHit.GetActor() afterwards).
	SetLifeSpan(0.01f);
}

float ARiotShield::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageAmount <= 0.0f || State == ERiotShieldState::Lowered || bPendingDestroy)
	{
		// When lowered the mesh shouldn't be intercepting damage at all (collision is off);
		// guard anyway in case some other path calls TakeDamage directly.
		return 0.0f;
	}

	bool bDrainsHealth = false;
	if (HealthDrainingDamageTypes.Num() == 0)
	{
		// Empty list = drain on any damage that hit the shield.
		bDrainsHealth = true;
	}
	else if (DamageEvent.DamageTypeClass)
	{
		for (const TSubclassOf<UDamageType>& Type : HealthDrainingDamageTypes)
		{
			if (Type && DamageEvent.DamageTypeClass->IsChildOf(*Type))
			{
				bDrainsHealth = true;
				break;
			}
		}
	}

	if (bDrainsHealth)
	{
		CurrentHealth = FMath::Max(0.0f, CurrentHealth - DamageAmount);
		OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	}

	if (AbsorbHitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, AbsorbHitSound, GetActorLocation());
	}
	if (AbsorbHitVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, AbsorbHitVFX, GetActorLocation(), GetActorRotation());
	}

	if (CurrentHealth <= 0.0f)
	{
		BreakShield();
	}

	// Always swallow the full damage — player never sees it.
	return DamageAmount;
}

void ARiotShield::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (BashCooldownRemaining > 0.0f)
	{
		BashCooldownRemaining = FMath::Max(0.0f, BashCooldownRemaining - DeltaTime);
	}

	// Step 1: write the base/state pose to the mesh.
	switch (State)
	{
	case ERiotShieldState::Transitioning:
		TickPose(DeltaTime);
		break;
	case ERiotShieldState::Bashing:
		TickBash(DeltaTime);
		break;
	case ERiotShieldState::Raised:
		ShieldMesh->SetRelativeLocation(RaisedRelativeLocation);
		ShieldMesh->SetRelativeRotation(RaisedRelativeRotation);
		break;
	case ERiotShieldState::Lowered:
		ShieldMesh->SetRelativeLocation(LoweredRelativeLocation);
		ShieldMesh->SetRelativeRotation(LoweredRelativeRotation);
		break;
	}

	// Step 2: apply external sway (run sway from movement + recoil/mouse sway from weapon) on top.
	ApplyExternalSway();
}

void ARiotShield::ApplyExternalSway()
{
	if (!OwnerCharacter || !ShieldMesh)
	{
		return;
	}

	FVector LocOffset = FVector::ZeroVector;
	FRotator RotOffset = FRotator::ZeroRotator;

	if (RunSwayInfluence > 0.0f)
	{
		LocOffset += OwnerCharacter->GetCurrentRunSwayPosition() * RunSwayInfluence;
		RotOffset += OwnerCharacter->GetCurrentRunSwayRotation() * RunSwayInfluence;
	}

	if (RecoilSwayInfluence > 0.0f)
	{
		if (UWeaponRecoilComponent* Recoil = OwnerCharacter->GetRecoilComponent())
		{
			LocOffset += Recoil->GetWeaponOffset() * RecoilSwayInfluence;
			RotOffset += Recoil->GetWeaponRotationOffset() * RecoilSwayInfluence;
		}
	}

	if (!LocOffset.IsNearlyZero())
	{
		ShieldMesh->SetRelativeLocation(ShieldMesh->GetRelativeLocation() + LocOffset);
	}
	if (!RotOffset.IsNearlyZero())
	{
		ShieldMesh->SetRelativeRotation(ShieldMesh->GetRelativeRotation() + RotOffset);
	}
}

void ARiotShield::TickPose(float DeltaTime)
{
	TransitionElapsed += DeltaTime;
	const float Alpha = StateTransitionTime > KINDA_SMALL_NUMBER
		? FMath::Clamp(TransitionElapsed / StateTransitionTime, 0.0f, 1.0f)
		: 1.0f;

	const FVector TargetLoc = PostTransitionState == ERiotShieldState::Raised ? RaisedRelativeLocation : LoweredRelativeLocation;
	const FRotator TargetRot = PostTransitionState == ERiotShieldState::Raised ? RaisedRelativeRotation : LoweredRelativeRotation;

	const float SmoothAlpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);
	ShieldMesh->SetRelativeLocation(FMath::Lerp(TransitionStartLocation, TargetLoc, SmoothAlpha));
	ShieldMesh->SetRelativeRotation(FMath::Lerp(TransitionStartRotation, TargetRot, SmoothAlpha));

	if (Alpha >= 1.0f)
	{
		State = PostTransitionState;
		UpdateMeshCollision();
		OnStateChanged.Broadcast(State);
	}
}

void ARiotShield::TickBash(float DeltaTime)
{
	BashElapsed += DeltaTime;
	const float NormTime = BashDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(BashElapsed / BashDuration, 0.0f, 1.0f)
		: 1.0f;

	// ----- Location offset (curve OR sin-based fallback) -----
	FVector LocalLocOffset;
	if (BashLocationCurve)
	{
		// Curve outputs an absolute local-space offset in cm (X=forward, Y=right, Z=up).
		LocalLocOffset = BashLocationCurve->GetVectorValue(NormTime);
	}
	else
	{
		const FVector LocalForward = RaisedRelativeRotation.RotateVector(FVector::ForwardVector);
		LocalLocOffset = LocalForward * FMath::Sin(NormTime * PI) * BashDistance;
	}

	// ----- Rotation offset (curve only, no fallback — null = no rotation animation) -----
	FRotator LocalRotOffset = FRotator::ZeroRotator;
	if (BashRotationCurve)
	{
		// FVector → FRotator: X=Roll, Y=Pitch, Z=Yaw (degrees).
		const FVector RotVec = BashRotationCurve->GetVectorValue(NormTime);
		LocalRotOffset = FRotator(RotVec.Y, RotVec.Z, RotVec.X);
	}

	ShieldMesh->SetRelativeLocation(RaisedRelativeLocation + LocalLocOffset);
	ShieldMesh->SetRelativeRotation(RaisedRelativeRotation + LocalRotOffset);

	if (NormTime >= BashDamageWindowOpen && NormTime <= BashDamageWindowClose)
	{
		PerformBashTrace();
	}

	if (NormTime >= 1.0f)
	{
		State = ERiotShieldState::Raised;
		BashCooldownRemaining = BashCooldown;
		OnStateChanged.Broadcast(State);
	}
}

void ARiotShield::PerformBashTrace()
{
	if (!OwnerCharacter)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UCameraComponent* Camera = OwnerCharacter->GetFirstPersonCameraComponent();
	if (!Camera)
	{
		return;
	}

	const FVector Forward = Camera->GetForwardVector();
	const FVector Start = Camera->GetComponentLocation() + Forward * 30.0f;
	const FVector End = Start + Forward * BashRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(RiotShieldBash), false, this);
	Params.AddIgnoredActor(OwnerCharacter);

	TArray<FHitResult> Hits;
	const bool bHit = World->SweepMultiByChannel(
		Hits, Start, End, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(BashRadius), Params);

	if (!bHit)
	{
		return;
	}

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == OwnerCharacter || BashHitActorsThisSwing.Contains(HitActor))
		{
			continue;
		}

		BashHitActorsThisSwing.Add(HitActor);

		// Damage
		TSubclassOf<UDamageType> DType = BashDamageType ? BashDamageType : TSubclassOf<UDamageType>(UDamageType_Melee::StaticClass());
		FPointDamageEvent DamageEvent(BashDamage, Hit, Forward, DType);
		HitActor->TakeDamage(BashDamage, DamageEvent, OwnerCharacter->GetController(), this);

		// Impulse
		if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
		{
			FVector LaunchVel = Forward * BashImpulse;
			LaunchVel.Z = FMath::Max(LaunchVel.Z, 100.0f); // small upward kick so the launch is visible on flat ground
			HitCharacter->LaunchCharacter(LaunchVel, true, true);
		}
		else if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
		{
			if (RootPrim->IsSimulatingPhysics())
			{
				RootPrim->AddImpulse(Forward * BashImpulse, NAME_None, true);
			}
		}

		// VFX/SFX
		if (BashImpactVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BashImpactVFX, Hit.ImpactPoint, Forward.Rotation());
		}
		if (BashHitSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, BashHitSound, Hit.ImpactPoint);
		}
	}
}

void ARiotShield::UpdateMeshCollision()
{
	if (!ShieldMesh)
	{
		return;
	}

	const bool bShouldBlock = (State != ERiotShieldState::Lowered);
	if (bShouldBlock)
	{
		ShieldMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ShieldMesh->SetCollisionResponseToAllChannels(ECR_Block);
		ShieldMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	else
	{
		ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ARiotShield::ApplyWalkSpeedPenalty()
{
	if (bWalkSpeedModified || !OwnerCharacter)
	{
		return;
	}
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}
	CachedMaxWalkSpeed = Movement->MaxWalkSpeed;
	Movement->MaxWalkSpeed = CachedMaxWalkSpeed * SpeedMultiplierWhenRaised;
	bWalkSpeedModified = true;
}

void ARiotShield::RestoreWalkSpeedPenalty()
{
	if (!bWalkSpeedModified || !OwnerCharacter)
	{
		bWalkSpeedModified = false;
		return;
	}
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (Movement)
	{
		Movement->MaxWalkSpeed = CachedMaxWalkSpeed;
	}
	bWalkSpeedModified = false;
}

void ARiotShield::SpawnBreakDestructionGC()
{
	if (!BreakGeometryCollection || !ShieldMesh)
	{
		return;
	}

	const FTransform MeshTransform = ShieldMesh->GetComponentTransform();
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

	GCActor->SetActorScale3D(MeshTransform.GetScale3D());

	GCComp->SetCollisionProfileName(FName("Ragdoll"));
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GCComp->SetRestCollection(BreakGeometryCollection);

	const int32 NumMats = ShieldMesh->GetNumMaterials();
	for (int32 i = 0; i < NumMats; i++)
	{
		if (UMaterialInterface* Mat = ShieldMesh->GetMaterial(i))
		{
			GCComp->SetMaterial(i, Mat);
		}
	}

	GCComp->SetSimulatePhysics(true);
	GCComp->RecreatePhysicsState();

	UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = BreakImpulse;
	RadialVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = BreakAngularImpulse;
	AngularVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVelocity);

	GCActor->SetLifeSpan(BreakGibLifetime);
}
