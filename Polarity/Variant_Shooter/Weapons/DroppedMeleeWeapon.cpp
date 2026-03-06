// DroppedMeleeWeapon.cpp

#include "DroppedMeleeWeapon.h"
#include "ShooterWeapon_Melee.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"

ADroppedMeleeWeapon::ADroppedMeleeWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Weapon mesh — root, physics-simulated
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetSimulatePhysics(true);
	WeaponMesh->SetCollisionProfileName(FName("PhysicsActor"));
	WeaponMesh->SetGenerateOverlapEvents(true);

	// EMF field component for charge storage
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
	FieldComponent->SetupAttachment(RootComponent);
}

void ADroppedMeleeWeapon::BeginPlay()
{
	Super::BeginPlay();
}

void ADroppedMeleeWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
	}
}

// ==================== Charge API ====================

float ADroppedMeleeWeapon::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void ADroppedMeleeWeapon::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}
}

// ==================== Capture Range ====================

float ADroppedMeleeWeapon::CalculateCaptureRange() const
{
	// Get player charge
	float PlayerCharge = 0.0f;
	if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0))
	{
		if (UEMFVelocityModifier* PlayerMod = PlayerChar->FindComponentByClass<UEMFVelocityModifier>())
		{
			PlayerCharge = PlayerMod->GetCharge();
		}
	}

	const float WeaponChargeAbs = FMath::Abs(GetCharge());
	const float PlayerChargeAbs = FMath::Abs(PlayerCharge);

	// No capture possible without charge on both sides
	if (WeaponChargeAbs < KINDA_SMALL_NUMBER || PlayerChargeAbs < KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float ChargeProduct = PlayerChargeAbs * WeaponChargeAbs;
	const float Ratio = ChargeProduct / FMath::Max(CaptureChargeNormCoeff, 0.01f);
	const float RangeMultiplier = FMath::Max(1.0f, 1.0f + FMath::Loge(Ratio));

	return CaptureBaseRange * RangeMultiplier;
}

// ==================== Pull ====================

void ADroppedMeleeWeapon::StartPull(AShooterCharacter* InPullingPlayer)
{
	if (!InPullingPlayer || bIsBeingPulled || bPullComplete)
	{
		return;
	}

	bIsBeingPulled = true;
	PullElapsed = 0.0f;
	PullingCharacter = InPullingPlayer;
	PullStartLocation = GetActorLocation();
	PullStartRotation = GetActorRotation();

	// Disable physics — we drive position directly
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADroppedMeleeWeapon::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		// Player gone — drop the weapon back
		bIsBeingPulled = false;
		WeaponMesh->SetSimulatePhysics(true);
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		return;
	}

	PullElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(PullElapsed / PullDuration, 0.0f, 1.0f);
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

	// Calculate camera-relative target in world space
	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!CamMgr)
	{
		return;
	}

	const FVector CameraLoc = CamMgr->GetCameraLocation();
	const FRotator CameraRot = CamMgr->GetCameraRotation();

	// Transform offset into world space relative to camera
	const FVector WorldTarget = CameraLoc
		+ CameraRot.RotateVector(PullTargetOffset);
	const FRotator WorldTargetRot = CameraRot + PullTargetRotation;

	// Interpolate
	const FVector NewPos = FMath::Lerp(PullStartLocation, WorldTarget, CurvedAlpha);
	const FRotator NewRot = FMath::Lerp(PullStartRotation, WorldTargetRot, CurvedAlpha);
	SetActorLocation(NewPos);
	SetActorRotation(NewRot);

	if (Alpha >= 1.0f)
	{
		CompletePull();
	}
}

void ADroppedMeleeWeapon::CompletePull()
{
	bPullComplete = true;
	bIsBeingPulled = false;

	// Hide this actor
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (!PullingCharacter.IsValid() || !MeleeWeaponClass)
	{
		Destroy();
		return;
	}

	AShooterCharacter* Player = PullingCharacter.Get();

	// Play pickup sound
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	// Grant the melee weapon
	Player->AddWeaponClass(MeleeWeaponClass);

	// Find the newly created weapon and configure it
	AShooterWeapon* NewWeapon = Player->GetCurrentWeapon();
	if (AShooterWeapon_Melee* MeleeWeapon = Cast<AShooterWeapon_Melee>(NewWeapon))
	{
		if (GrantedHitCount > 0)
		{
			MeleeWeapon->SetRemainingHits(GrantedHitCount);
		}
		MeleeWeapon->SetBreakData(BreakGeometryCollection, BreakImpulse, BreakAngularImpulse, BreakGibLifetime);
	}

	// Destroy this world actor (weapon is now in player's inventory)
	Destroy();
}

// ==================== Geometry Collection Break ====================

void ADroppedMeleeWeapon::SpawnBreakGC(const FTransform& BreakTransform)
{
	if (!BreakGeometryCollection || !GetWorld())
	{
		return;
	}

	const FVector Origin = BreakTransform.GetLocation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		Origin, BreakTransform.GetRotation().Rotator(), SpawnParams);

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

	// Match source scale
	GCActor->SetActorScale3D(BreakTransform.GetScale3D());

	// Collision: gibs should not push pawns or block camera
	GCComp->SetCollisionProfileName(FName("Ragdoll"));
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	// Assign GC asset
	GCComp->SetRestCollection(BreakGeometryCollection);

	// Copy materials from WeaponMesh
	if (WeaponMesh)
	{
		const int32 NumMats = WeaponMesh->GetNumMaterials();
		for (int32 i = 0; i < NumMats; i++)
		{
			if (UMaterialInterface* Mat = WeaponMesh->GetMaterial(i))
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

	// Scatter pieces radially
	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = BreakImpulse;
	RadialVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	// Angular velocity for tumbling
	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = BreakAngularImpulse;
	AngularVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVelocity);

	// Self-destruct gibs after lifetime
	GCActor->SetLifeSpan(BreakGibLifetime);
}
