// StrippableArmor.cpp

#include "Variant_Shooter/Health/StrippableArmor.h"

#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

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
	if (HasAuthority() && Lifetime > 0.0f)
	{
		SetLifeSpan(Lifetime);
	}
}

void AStrippableArmor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStrippableArmor, ArmorAmount);
	DOREPLIFETIME(AStrippableArmor, bTaken);
}

void AStrippableArmor::OnRep_ArmorAmount()
{
	RefreshLabel();
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

	const TSubclassOf<AStrippableArmor> Class = ArmorClass ? ArmorClass : TSubclassOf<AStrippableArmor>(AStrippableArmor::StaticClass());
	const FTransform Transform(FRotator::ZeroRotator, Where);
	AStrippableArmor* const Left = World->SpawnActorDeferred<AStrippableArmor>(Class, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Left)
	{
		Left->ArmorAmount = ArmorAmount;
		Left->FinishSpawning(Transform);
		UE_LOG(LogTemp, Log, TEXT("[CARE] %s left %.0f armour at %s"), *Owner->GetName(), ArmorAmount, *Where.ToCompactString());
	}
}
