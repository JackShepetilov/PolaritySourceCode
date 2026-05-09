// NPCRiotShieldComponent.cpp

#include "NPCRiotShieldComponent.h"
#include "EMFVelocityModifier.h"
#include "HumanoidNPC.h"
#include "Variant_Shooter/Weapons/RiotShieldPickup.h"
#include "Variant_Shooter/ShooterCharacter.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

const FName UNPCRiotShieldComponent::ShieldComponentTag = TEXT("NPCShield");

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
	Params.Owner = GetOwner();

	ARiotShieldPickup* Pickup = World->SpawnActor<ARiotShieldPickup>(PickupClass, SpawnLoc, SpawnRot, Params);
	if (Pickup)
	{
		// Scripted pull (canonical yank flow) — pickup ignores physics/collision and lerps to the player,
		// guaranteed to arrive and equip. NEVER use SpawnAsThrown here: that's for FP-throw flow.
		Pickup->StartPull(Puller);
	}

	Deactivate();

	return Pickup != nullptr;
}

void UNPCRiotShieldComponent::ResetForPool()
{
	Deactivate();
	TryActivate();
}

void UNPCRiotShieldComponent::TryActivate()
{
	if (bShieldActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPC_SHIELD] %s: TryActivate skipped — already active"), *GetNameSafe(GetOwner()));
		return;
	}
	if (!ShieldMeshAsset)
	{
		// Verbose: every vanilla BP_HumanoidNPC without shield asset hits this — silent by design
		UE_LOG(LogTemp, Verbose, TEXT("[NPC_SHIELD] %s: no-op (ShieldMeshAsset not set)"), *GetNameSafe(GetOwner()));
		return;
	}
	if (!PickupClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPC_SHIELD] %s: TryActivate skipped — PickupClass NOT SET (mesh present but no pickup class — config error)"), *GetNameSafe(GetOwner()));
		return;
	}

	USkeletalMeshComponent* OwnerMesh = GetOwnerSkeletalMesh();
	if (!OwnerMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPC_SHIELD] %s: TryActivate skipped — owner has no skeletal mesh"), *GetNameSafe(GetOwner()));
		return;
	}

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

	// Query-only profile.
	//  - ObjectType MUST be ECC_Pawn so weapon `LineTraceByObjectType(ECC_Pawn)` actually finds
	//    the shield (object-type query filters by CollisionObjectType, not response). With WorldDynamic
	//    the pawn-trace passed straight through and HitComponent was always the body capsule.
	//  - Block ALL channels so Visibility traces still stop at the shield (walls / projectile traces).
	//  - QueryOnly so the shield doesn't physically push the carrying NPC capsule or the player.
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ShieldMesh->SetCollisionObjectType(ECC_Pawn);
	ShieldMesh->SetCollisionResponseToAllChannels(ECR_Block);
	ShieldMesh->SetGenerateOverlapEvents(false);

	// Tag the mesh so ionization paths (ShouldBlockBodyIonization) can identify "hit shield" vs "hit body"
	ShieldMesh->ComponentTags.AddUnique(ShieldComponentTag);

	UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] %s: ShieldMesh tagged '%s' (tags now: %d entries)"),
		*GetNameSafe(GetOwner()), *ShieldComponentTag.ToString(), ShieldMesh->ComponentTags.Num());

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

bool UNPCRiotShieldComponent::ShouldBlockBodyIonization(AActor* HitTarget, UPrimitiveComponent* HitComp)
{
	if (!HitTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ShouldBlock: HitTarget=null → false"));
		return false;
	}

	const AHumanoidNPC* Humanoid = Cast<AHumanoidNPC>(HitTarget);
	if (!Humanoid)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ShouldBlock: target=%s is not HumanoidNPC → false (allow ionize)"),
			*HitTarget->GetName());
		return false;
	}

	const UNPCRiotShieldComponent* Shield = Humanoid->GetShieldComponent();
	if (!Shield || !Shield->HasActiveShield())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ShouldBlock: %s has no active shield (comp=%d active=%d) → false (allow ionize)"),
			*Humanoid->GetName(), Shield != nullptr, Shield ? Shield->HasActiveShield() : false);
		return false;
	}

	// Hit lands on the shield iff the hit component carries our tag — anything else (capsule,
	// skeletal mesh, hitboxes) is "body" and ionization must NOT pass through.
	const bool bHitShield = HitComp && HitComp->ComponentTags.Contains(ShieldComponentTag);

	UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ShouldBlock: target=%s hitComp=%s (class=%s, tags=%d) → bHitShield=%d → block=%d"),
		*Humanoid->GetName(),
		HitComp ? *HitComp->GetName() : TEXT("null"),
		HitComp ? *HitComp->GetClass()->GetName() : TEXT("null"),
		HitComp ? HitComp->ComponentTags.Num() : -1,
		bHitShield,
		!bHitShield);

	return !bHitShield;
}
