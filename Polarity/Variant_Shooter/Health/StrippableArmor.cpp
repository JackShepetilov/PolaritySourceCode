// StrippableArmor.cpp

#include "Variant_Shooter/Health/StrippableArmor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Shield/ShieldFieldComponent.h"

// ==================== AStrippableArmor ====================

AStrippableArmor::AStrippableArmor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(18.0f);
	Label->SetTextRenderColor(FColor(90, 170, 255));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AStrippableArmor::BeginPlay()
{
	Super::BeginPlay();

	RefreshLabel();
	RefreshBodyGlow();
	if (HasAuthority() && Lifetime > 0.0f)
	{
		SetLifeSpan(Lifetime);
	}
}

void AStrippableArmor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Gone, taken or timed out: the body stops glowing either way.
	bTaken = true;
	RefreshBodyGlow();

	Super::EndPlay(EndPlayReason);
}

void AStrippableArmor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStrippableArmor, ArmorAmount);
	DOREPLIFETIME(AStrippableArmor, bTaken);
	DOREPLIFETIME(AStrippableArmor, Body);
	DOREPLIFETIME(AStrippableArmor, BodyGlowMaterial);
	DOREPLIFETIME(AStrippableArmor, ArmorFraction);
}

void AStrippableArmor::OnRep_ArmorAmount()
{
	RefreshLabel();
}

void AStrippableArmor::OnRep_Glow()
{
	// Body, material and fraction arrive in no promised order: each one tries again.
	RefreshBodyGlow();
}

void AStrippableArmor::OnRep_Taken()
{
	RefreshBodyGlow();
}

UMeshComponent* AStrippableArmor::GetBodyMesh() const
{
	const ACharacter* const Character = Cast<ACharacter>(Body);
	return Character ? Character->GetMesh() : nullptr;
}

void AStrippableArmor::RefreshBodyGlow()
{
	UMeshComponent* const Mesh = GetBodyMesh();
	if (!Mesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (bTaken || !BodyGlowMaterial)
	{
		// Only ever its own: something else may have dressed the body since.
		if (GlowInstance && Mesh->GetOverlayMaterial() == GlowInstance)
		{
			Mesh->SetOverlayMaterial(nullptr);
		}
		GlowInstance = nullptr;
		return;
	}

	if (!GlowInstance || GlowInstance->Parent != BodyGlowMaterial)
	{
		GlowInstance = UMaterialInstanceDynamic::Create(BodyGlowMaterial, this);
	}
	if (GlowInstance)
	{
		GlowInstance->SetScalarParameterValue(TEXT("Amount"), FMath::Clamp(ArmorFraction, 0.0f, 1.0f));
		Mesh->SetOverlayMaterial(GlowInstance);
	}
}

void AStrippableArmor::RefreshLabel()
{
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("ARMOR %.0f"), ArmorAmount)));
	}
}

float AStrippableArmor::TakeArmor()
{
	if (!HasAuthority() || bTaken)
	{
		return 0.0f;
	}

	bTaken = true;
	const float Taken = ArmorAmount;
	ArmorAmount = 0.0f;
	RefreshBodyGlow();

	// A moment, so the last update reaches the clients before the actor goes.
	SetLifeSpan(0.2f);
	return Taken;
}

// ==================== UStrippableArmorComponent ====================

UStrippableArmorComponent::UStrippableArmorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStrippableArmorComponent::BeginPlay()
{
	Super::BeginPlay();

	AShooterNPC* const NPC = Cast<AShooterNPC>(GetOwner());
	if (NPC && NPC->HasAuthority())
	{
		NPC->OnNPCDeath.AddDynamic(this, &UStrippableArmorComponent::HandleNPCDeath);
	}
}

void UStrippableArmorComponent::HandleNPCDeath(AShooterNPC* DeadNPC)
{
	LeaveArmorHere();
}

void UStrippableArmorComponent::LeaveArmorHere()
{
	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (bLeft || !Owner || !World || !Owner->HasAuthority() || ArmorAmount <= 0.0f)
	{
		return;
	}
	bLeft = true;

	// On the floor under the body, not at the middle of the capsule.
	FVector Where = Owner->GetActorLocation();
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(StrippableArmorFloor), false, Owner);
	if (World->LineTraceSingleByChannel(Floor, Where, Where - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, Params))
	{
		Where = Floor.ImpactPoint + FVector(0.0f, 0.0f, 20.0f);
	}

	// Armour that was shot up is worth less: between BrokenArmorFraction for a pool at zero and all
	// of it for a pool never touched [author, 2026-10-12].
	float Fraction = 1.0f;
	if (const UShieldFieldComponent* const Worn = UShieldFieldStatics::GetShieldField(Owner))
	{
		Fraction = FMath::Lerp(BrokenArmorFraction, 1.0f, Worn->GetShieldFraction());
	}
	const float Amount = ArmorAmount * Fraction;
	if (Amount <= 0.0f)
	{
		return;
	}

	// An enemy that leaves a body keeps the armour on it.
	const AShooterNPC* const NPC = Cast<AShooterNPC>(Owner);
	const bool bOnBody = NPC && NPC->CorpseSeconds > 0.0f && NPC->GetMesh();

	const TSubclassOf<AStrippableArmor> Class = ArmorClass ? ArmorClass : TSubclassOf<AStrippableArmor>(AStrippableArmor::StaticClass());
	const FTransform Transform(FRotator::ZeroRotator, Where);
	AStrippableArmor* const Left = World->SpawnActorDeferred<AStrippableArmor>(Class, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Left)
	{
		Left->ArmorAmount = Amount;
		Left->ArmorFraction = Fraction;
		if (bOnBody)
		{
			Left->Body = Owner;
			Left->BodyGlowMaterial = BodyGlowMaterial;
			Left->Lifetime = NPC->CorpseSeconds;
		}
		Left->FinishSpawning(Transform);
		if (bOnBody)
		{
			Left->AttachToComponent(NPC->GetMesh(), FAttachmentTransformRules::KeepWorldTransform, BodyBone);
		}
		UE_LOG(LogTemp, Log, TEXT("[CARE] %s left %.0f armour (%.0f%% of %.0f) at %s, on the body %d"),
			*Owner->GetName(), Amount, Fraction * 100.0f, ArmorAmount, *Where.ToCompactString(), bOnBody ? 1 : 0);
	}
}
