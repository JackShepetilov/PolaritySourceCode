// NPCRiotShieldComponent.cpp

#include "NPCRiotShieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Variant_Shooter/Weapons/RiotShieldPickup.h"
#include "Variant_Shooter/ShooterCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UNPCRiotShieldComponent::UNPCRiotShieldComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Aim must run after the skeletal-mesh animation has placed the socket for this frame,
	// otherwise our SetWorldRotation gets overwritten the next animation pass.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UNPCRiotShieldComponent::BeginPlay()
{
	Super::BeginPlay();
	TryActivate();
}

void UNPCRiotShieldComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	Super::EndPlay(EndPlayReason);
}

void UNPCRiotShieldComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bShieldActive || !bAimAtPlayer || !ShieldMesh) return;

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;

	const FVector ShieldLoc = ShieldMesh->GetComponentLocation();
	FVector ToPlayer = Player->GetActorLocation() - ShieldLoc;
	ToPlayer.Z = 0.0f;
	if (ToPlayer.IsNearlyZero()) return;

	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Y, ToPlayer.X)) + AimYawOffsetDeg;

	const FRotator CurrentFlat(0.0f, ShieldMesh->GetComponentRotation().Yaw, 0.0f);
	const FRotator TargetFlat(0.0f, TargetYaw, 0.0f);
	const FRotator Interp = FMath::RInterpTo(CurrentFlat, TargetFlat, DeltaTime, AimYawInterpSpeed);

	ShieldMesh->SetWorldRotation(FRotator(0.0f, Interp.Yaw, 0.0f));
}

bool UNPCRiotShieldComponent::CanBeYanked() const
{
	return bShieldActive && ShieldMesh != nullptr && PickupClass != nullptr && GetOwner() != nullptr;
}

float UNPCRiotShieldComponent::CalculateYankRange() const
{
	if (!CanBeYanked()) return 0.0f;

	AActor* Owner = GetOwner();
	UEMFVelocityModifier* OwnerMod = Owner ? Owner->FindComponentByClass<UEMFVelocityModifier>() : nullptr;
	if (!OwnerMod) return 0.0f;

	const float NpcAbs = FMath::Abs(OwnerMod->GetCharge());
	if (NpcAbs < KINDA_SMALL_NUMBER) return 0.0f;

	float PlayerAbs = 0.0f;
	if (ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		if (UEMFVelocityModifier* PMod = Player->FindComponentByClass<UEMFVelocityModifier>())
		{
			PlayerAbs = FMath::Abs(PMod->GetCharge());
		}
	}
	if (PlayerAbs < KINDA_SMALL_NUMBER) return 0.0f;

	const float Ratio = (PlayerAbs * NpcAbs) / FMath::Max(YankNormCoeff, 0.01f);
	const float Mult = FMath::Max(1.0f, 1.0f + FMath::Loge(Ratio));
	return YankBaseRange * Mult;
}

bool UNPCRiotShieldComponent::TryYank(AShooterCharacter* Puller)
{
	if (!CanBeYanked() || !Puller) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	const FVector SpawnLoc = ShieldMesh->GetComponentLocation();
	const FRotator SpawnRot = ShieldMesh->GetComponentRotation();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = GetOwner(); // pickup's GetOwner() = NPC, not the puller — puller is free to overlap-pick immediately

	ARiotShieldPickup* Pickup = World->SpawnActor<ARiotShieldPickup>(PickupClass, SpawnLoc, SpawnRot, Params);
	if (Pickup)
	{
		const FVector ToPuller = (Puller->GetActorLocation() - SpawnLoc).GetSafeNormal();
		const FVector LinearImpulse = ToPuller * YankLinearImpulseMagnitude;
		Pickup->SpawnAsThrown(LinearImpulse, YankAngularImpulseDeg);
	}

	Deactivate();

	UE_LOG(LogTemp, Warning, TEXT("[NPC_SHIELD] %s: Yanked, pickup=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Pickup));

	return Pickup != nullptr;
}

void UNPCRiotShieldComponent::ResetForPool()
{
	Deactivate();
	TryActivate();
}

void UNPCRiotShieldComponent::TryActivate()
{
	if (bShieldActive) return;
	if (!ShieldMeshAsset || !PickupClass) return; // designed no-op path — leaves vanilla BP_HumanoidNPC unaffected

	USkeletalMeshComponent* OwnerMesh = GetOwnerSkeletalMesh();
	if (!OwnerMesh) return;

	if (!OwnerMesh->DoesSocketExist(AttachSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPC_SHIELD] %s: socket '%s' not found on '%s' — mesh will attach to skeleton root"),
			*GetNameSafe(GetOwner()),
			*AttachSocketName.ToString(),
			*GetNameSafe(OwnerMesh->GetSkeletalMeshAsset()));
	}

	ShieldMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("NPCShieldMesh"));
	if (!ShieldMesh) return;

	ShieldMesh->SetStaticMesh(ShieldMeshAsset);
	ShieldMesh->SetMobility(EComponentMobility::Movable);
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // visual only — blocking is a future concern, not part of this task
	ShieldMesh->AttachToComponent(OwnerMesh, FAttachmentTransformRules::KeepRelativeTransform, AttachSocketName);
	ShieldMesh->SetRelativeLocationAndRotation(RelativeLocation, RelativeRotation);
	ShieldMesh->SetRelativeScale3D(RelativeScale);
	ShieldMesh->RegisterComponent();
	if (AActor* Owner = GetOwner())
	{
		Owner->AddInstanceComponent(ShieldMesh);
	}

	bShieldActive = true;

	UE_LOG(LogTemp, Warning, TEXT("[NPC_SHIELD] %s: activated on socket '%s'"),
		*GetNameSafe(GetOwner()), *AttachSocketName.ToString());
}

void UNPCRiotShieldComponent::Deactivate()
{
	if (ShieldMesh)
	{
		ShieldMesh->DestroyComponent();
		ShieldMesh = nullptr;
	}
	bShieldActive = false;
}

USkeletalMeshComponent* UNPCRiotShieldComponent::GetOwnerSkeletalMesh() const
{
	const AActor* Owner = GetOwner();
	if (!Owner) return nullptr;
	return Owner->FindComponentByClass<USkeletalMeshComponent>();
}
