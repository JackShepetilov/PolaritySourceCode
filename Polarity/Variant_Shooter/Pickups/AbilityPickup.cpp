// AbilityPickup.cpp

#include "AbilityPickup.h"
#include "ChargeAnimationComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/Abilities/AbilityDefinition.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

AAbilityPickup::AAbilityPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetSphereRadius(100.0f);
	PickupCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupCollision->SetGenerateOverlapEvents(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupCollision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void AAbilityPickup::BeginPlay()
{
	Super::BeginPlay();

	PickupCollision->SetSphereRadius(PickupRadius);

	if (Mesh)
	{
		InitialMeshZ = Mesh->GetRelativeLocation().Z;
	}

	if (IdleVFX)
	{
		IdleVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			IdleVFX, PickupCollision, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
			true);
	}
}

void AAbilityPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
		return;
	}

	if (!Mesh)
	{
		return;
	}

	if (RotationSpeed > 0.0f)
	{
		Mesh->AddLocalRotation(FRotator(0.0f, RotationSpeed * DeltaTime, 0.0f));
	}

	if (BobAmplitude > 0.0f)
	{
		BobTime += DeltaTime;
		const float BobOffset = FMath::Sin(BobTime * BobFrequency * 2.0f * UE_PI) * BobAmplitude;
		FVector MeshLocation = Mesh->GetRelativeLocation();
		MeshLocation.Z = InitialMeshZ + BobOffset;
		Mesh->SetRelativeLocation(MeshLocation);
	}
}

float AAbilityPickup::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void AAbilityPickup::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}
}

float AAbilityPickup::CalculateCaptureRange() const
{
	return UChargeAnimationComponent::GetCaptureRangeFor(this, FMath::Abs(GetCharge()));
}

void AAbilityPickup::StartPull(AShooterCharacter* InPullingPlayer)
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

	PickupCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (IdleVFXComponent)
	{
		IdleVFXComponent->Deactivate();
	}
}

void AAbilityPickup::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		bIsBeingPulled = false;
		PickupCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
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

void AAbilityPickup::CompletePull()
{
	bPullComplete = true;
	bIsBeingPulled = false;

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (!PullingCharacter.IsValid())
	{
		Destroy();
		return;
	}

	AShooterCharacter* Player = PullingCharacter.Get();

	if (!AbilityDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("AbilityPickup: No AbilityDefinition set on '%s'"), *GetName());
		Destroy();
		return;
	}

	UAbilityComponent* AbilityComp = Player->FindComponentByClass<UAbilityComponent>();
	if (!AbilityComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("AbilityPickup: Player has no UAbilityComponent"));
		Destroy();
		return;
	}

	const int32 GrantedSlot = AbilityComp->AddAbility(AbilityDefinition, GrantedLevel);

	// AddAbility returns INDEX_NONE both for "already owned at >= level" AND for "inventory full
	// + replace disabled". Distinguish by querying HasAbility.
	if (GrantedSlot == INDEX_NONE && AbilityComp->HasAbility(AbilityDefinition))
	{
		BP_OnAbilityAlreadyOwned(Player);
		Destroy();
		return;
	}

	if (GrantedSlot != INDEX_NONE)
	{
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
		BP_OnAbilityPickedUp(Player);
		OnPickedUp.Broadcast(Player);
	}

	Destroy();
}
