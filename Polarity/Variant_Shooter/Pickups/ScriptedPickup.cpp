// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "ScriptedPickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "ShooterCharacter.h"
#include "UpgradeTooltipWidget.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

AScriptedPickup::AScriptedPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pickup collision (root) — used for capture scan overlap detection
	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetSphereRadius(100.0f);
	PickupCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupCollision->SetGenerateOverlapEvents(true);

	// Visual mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupCollision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Tooltip trigger sphere (larger radius)
	TooltipTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("TooltipTrigger"));
	TooltipTrigger->SetupAttachment(PickupCollision);
	TooltipTrigger->SetSphereRadius(400.0f);
	TooltipTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TooltipTrigger->SetGenerateOverlapEvents(true);
	TooltipTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	TooltipTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Tooltip widget (screen-space, hidden by default)
	TooltipWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("TooltipWidget"));
	TooltipWidgetComponent->SetupAttachment(PickupCollision);
	TooltipWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	TooltipWidgetComponent->SetDrawAtDesiredSize(true);
	TooltipWidgetComponent->SetVisibility(false);

	// EMF field component for charge storage
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void AScriptedPickup::BeginPlay()
{
	Super::BeginPlay();

	// Sync collision radii
	PickupCollision->SetSphereRadius(PickupRadius);
	TooltipTrigger->SetSphereRadius(TooltipRadius);

	// Position tooltip above mesh
	TooltipWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, TooltipHeight));

	// Bind tooltip overlaps (no pickup overlap — capture only via channeling)
	TooltipTrigger->OnComponentBeginOverlap.AddDynamic(this, &AScriptedPickup::OnTooltipBeginOverlap);
	TooltipTrigger->OnComponentEndOverlap.AddDynamic(this, &AScriptedPickup::OnTooltipEndOverlap);

	// Store initial mesh Z for bob
	if (Mesh)
	{
		InitialMeshZ = Mesh->GetRelativeLocation().Z;
	}

	// Spawn idle VFX
	if (IdleVFX)
	{
		IdleVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			IdleVFX, PickupCollision, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
			true);
	}

	// Initialize tooltip widget with direct data
	if (TooltipWidgetClass)
	{
		TooltipWidgetComponent->SetWidgetClass(TooltipWidgetClass);
		TooltipWidgetComponent->InitWidget();

		if (UUpgradeTooltipWidget* Tooltip = Cast<UUpgradeTooltipWidget>(TooltipWidgetComponent->GetWidget()))
		{
			Tooltip->UpgradeName = DisplayName;
			Tooltip->UpgradeDescription = Description;
			Tooltip->UpgradeIcon = Icon;
			Tooltip->UpgradeTier = Tier;
			Tooltip->BP_OnTooltipInitialized();
		}
	}

	// If tooltip disabled from start, deactivate trigger
	if (!bTooltipEnabled)
	{
		TooltipTrigger->SetGenerateOverlapEvents(false);
	}
}

void AScriptedPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Pull interpolation takes priority
	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
		return;
	}

	if (!Mesh)
	{
		return;
	}

	// Rotate mesh
	if (RotationSpeed > 0.0f)
	{
		Mesh->AddLocalRotation(FRotator(0.0f, RotationSpeed * DeltaTime, 0.0f));
	}

	// Bob mesh
	if (BobAmplitude > 0.0f)
	{
		BobTime += DeltaTime;
		const float BobOffset = FMath::Sin(BobTime * BobFrequency * 2.0f * UE_PI) * BobAmplitude;
		FVector MeshLocation = Mesh->GetRelativeLocation();
		MeshLocation.Z = InitialMeshZ + BobOffset;
		Mesh->SetRelativeLocation(MeshLocation);
	}
}

// ==================== Charge API ====================

float AScriptedPickup::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void AScriptedPickup::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}
}

// ==================== Capture Range ====================

float AScriptedPickup::CalculateCaptureRange() const
{
	float PlayerCharge = 0.0f;
	if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0))
	{
		if (UEMFVelocityModifier* PlayerMod = PlayerChar->FindComponentByClass<UEMFVelocityModifier>())
		{
			PlayerCharge = PlayerMod->GetCharge();
		}
	}

	const float PickupChargeAbs = FMath::Abs(GetCharge());
	const float PlayerChargeAbs = FMath::Abs(PlayerCharge);

	if (PickupChargeAbs < KINDA_SMALL_NUMBER || PlayerChargeAbs < KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float ChargeProduct = PlayerChargeAbs * PickupChargeAbs;
	const float Ratio = ChargeProduct / FMath::Max(CaptureChargeNormCoeff, 0.01f);
	const float RangeMultiplier = FMath::Max(1.0f, 1.0f + FMath::Loge(Ratio));

	return CaptureBaseRange * RangeMultiplier;
}

// ==================== Pull ====================

void AScriptedPickup::StartPull(AShooterCharacter* InPullingPlayer)
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

	// Disable collision during pull
	PickupCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TooltipTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Hide tooltip during pull
	TooltipWidgetComponent->SetVisibility(false);

	// Stop idle VFX
	if (IdleVFXComponent)
	{
		IdleVFXComponent->Deactivate();
	}
}

void AScriptedPickup::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		// Player gone — re-enable collision and abort pull
		bIsBeingPulled = false;
		PickupCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TooltipTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		return;
	}

	PullElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(PullElapsed / PullDuration, 0.0f, 1.0f);
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!CamMgr)
	{
		return;
	}

	const FVector CameraLoc = CamMgr->GetCameraLocation();
	const FRotator CameraRot = CamMgr->GetCameraRotation();

	const FVector WorldTarget = CameraLoc + CameraRot.RotateVector(PullTargetOffset);
	const FRotator WorldTargetRot = CameraRot + PullTargetRotation;

	const FVector NewPos = FMath::Lerp(PullStartLocation, WorldTarget, CurvedAlpha);
	const FRotator NewRot = FMath::Lerp(PullStartRotation, WorldTargetRot, CurvedAlpha);
	SetActorLocation(NewPos);
	SetActorRotation(NewRot);

	if (Alpha >= 1.0f)
	{
		CompletePull();
	}
}

void AScriptedPickup::CompletePull()
{
	bPullComplete = true;
	bIsBeingPulled = false;

	// Hide this actor
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (!PullingCharacter.IsValid())
	{
		if (bDestroyOnPickup)
		{
			Destroy();
		}
		return;
	}

	AShooterCharacter* Player = PullingCharacter.Get();

	// Effects
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	if (PickupVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), PickupVFX, GetActorLocation(),
			FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}

	// Fire events
	OnPickedUp.Broadcast(Player);
	BP_OnPickedUp(Player);

	if (bDestroyOnPickup)
	{
		Destroy();
	}
}

// ==================== Tooltip ====================

void AScriptedPickup::SetTooltipEnabled(bool bEnabled)
{
	bTooltipEnabled = bEnabled;
	TooltipTrigger->SetGenerateOverlapEvents(bEnabled);

	if (!bEnabled)
	{
		TooltipWidgetComponent->SetVisibility(false);
	}
}

void AScriptedPickup::OnTooltipBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bTooltipEnabled)
	{
		return;
	}

	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player || Player->IsDead())
	{
		return;
	}

	TooltipWidgetComponent->SetVisibility(true);

	if (UUpgradeTooltipWidget* Tooltip = Cast<UUpgradeTooltipWidget>(TooltipWidgetComponent->GetWidget()))
	{
		Tooltip->BP_OnTooltipShow();
	}
}

void AScriptedPickup::OnTooltipEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	if (UUpgradeTooltipWidget* Tooltip = Cast<UUpgradeTooltipWidget>(TooltipWidgetComponent->GetWidget()))
	{
		Tooltip->BP_OnTooltipHide();
	}
}
