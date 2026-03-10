// DroppedRangedWeapon.cpp

#include "DroppedRangedWeapon.h"
#include "ShooterWeapon.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/UI/EMFChargeWidgetSubsystem.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

ADroppedRangedWeapon::ADroppedRangedWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Weapon mesh — root, physics-simulated
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetSimulatePhysics(true);
	WeaponMesh->SetCollisionProfileName(FName("PhysicsActor"));
	WeaponMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	WeaponMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	WeaponMesh->SetGenerateOverlapEvents(true);
	WeaponMesh->BodyInstance.bUseCCD = true;

	// EMF field component for charge storage
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void ADroppedRangedWeapon::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT("[DroppedRangedWeapon] %s BeginPlay: Charge=%.2f, bCanBeCaptured=%d"),
		*GetName(), GetCharge(), bCanBeCaptured);
}

void ADroppedRangedWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
	}
}

// ==================== Charge API ====================

float ADroppedRangedWeapon::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void ADroppedRangedWeapon::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Register (or re-register) charge widget
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterDroppedRangedWeapon(this);
		WidgetSub->RegisterDroppedRangedWeapon(this);
	}
}

// ==================== Capture Range ====================

float ADroppedRangedWeapon::CalculateCaptureRange() const
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

void ADroppedRangedWeapon::StartPull(AShooterCharacter* InPullingPlayer)
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

void ADroppedRangedWeapon::UpdatePull(float DeltaTime)
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

void ADroppedRangedWeapon::CompletePull()
{
	bPullComplete = true;
	bIsBeingPulled = false;

	// Unregister charge widget
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterDroppedRangedWeapon(this);
	}

	// Hide this actor
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (!PullingCharacter.IsValid() || !WeaponClass)
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

	// Check if player already has a weapon of this class — if so, skip (no stacking for ranged)
	AShooterWeapon* ExistingWeapon = Player->FindWeaponOfType(WeaponClass);
	if (!ExistingWeapon)
	{
		// Grant a new weapon (permanent)
		Player->AddWeaponClass(WeaponClass);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[DroppedRangedWeapon] Player already has %s — skipping grant"),
			*WeaponClass->GetName());
	}

	// Destroy this world actor (weapon is now in player's inventory)
	Destroy();
}
