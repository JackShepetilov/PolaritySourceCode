// ShootableButtonComponent.cpp

#include "ShootableButtonComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Controller.h"

UShootableButtonComponent::UShootableButtonComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// ButtonMesh is a child subobject — visible and editable in BP viewport
	ButtonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ButtonMesh"));
	ButtonMesh->SetupAttachment(this);

	// Collision: respond to projectile traces (Visibility) and melee sweeps (Pawn)
	ButtonMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ButtonMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ButtonMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ButtonMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// A round that flies is a sphere of type WorldDynamic moved by a sweep, and a sweep stops only
	// where BOTH sides block. The button answered traces alone, so every flying round went through
	// it [author, 2026-10-11]. Two halves:
	//   the button blocks WorldDynamic (the round's type);
	//   the button IS a PhysicsBody, because the pistol and SMG rounds are set in their blueprints
	//   to ignore WorldDynamic (measured: BP_Bullet_Pistol, BP_Bullet_SMG) and every round blocks
	//   PhysicsBody. The melee sweep looks for PhysicsBody too, so a punch still finds the button.
	ButtonMesh->SetCollisionObjectType(ECC_PhysicsBody);
	ButtonMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	ButtonMesh->SetGenerateOverlapEvents(false);
}

void UShootableButtonComponent::BeginPlay()
{
	Super::BeginPlay();

	// Again here: a button saved in a blueprint or on a level keeps the responses it was saved with.
	if (ButtonMesh)
	{
		ButtonMesh->SetCollisionObjectType(ECC_PhysicsBody);
		ButtonMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		// Auto-bind: component listens to owner's damage — no TakeDamage override needed
		Owner->OnTakeAnyDamage.AddDynamic(this, &UShootableButtonComponent::OnOwnerTakeAnyDamage);

		// Add tag so melee weapon's IsValidMeleeTarget() accepts this actor
		if (!Owner->ActorHasTag(TEXT("MeleeDestructible")))
		{
			Owner->Tags.Add(TEXT("MeleeDestructible"));
		}
	}

	bIsToggledOn = bStartToggledOn;
	UpdateMaterial();
}

bool UShootableButtonComponent::HandleOwnerTakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	return Press(DamageCauser);
}

bool UShootableButtonComponent::Press(AActor* Activator)
{
	if (!bIsEnabled)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[BUTTON_DEBUG] Press ignored — button disabled"));
		return false;
	}

	if (!IsCooldownReady())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[BUTTON_DEBUG] Press ignored — cooldown active (%.2fs remaining)"),
			ActivationCooldown - (GetWorld()->GetTimeSeconds() - LastActivationTime));
		return false;
	}

	LastActivationTime = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Warning, TEXT("[BUTTON_DEBUG] Button pressed on %s by %s"),
		*GetOwner()->GetName(),
		Activator ? *Activator->GetName() : TEXT("null"));

	if (bAutoDisableOnPress)
	{
		// One-shot mode: disable until Reset()
		bIsEnabled = false;
	}
	else
	{
		// Toggle mode: flip internal state
		bIsToggledOn = !bIsToggledOn;
	}

	UpdateMaterial();

	// SFX
	if (PressSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PressSound, ButtonMesh->GetComponentLocation());
	}

	OnButtonPressed.Broadcast(this, Activator);
	return true;
}

void UShootableButtonComponent::Reset()
{
	SetEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[BUTTON_DEBUG] Button reset on %s"), *GetOwner()->GetName());

	// SFX
	if (ResetSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ResetSound, ButtonMesh->GetComponentLocation());
	}

	OnButtonReset.Broadcast(this);
}

void UShootableButtonComponent::SetEnabled(bool bNewEnabled)
{
	bIsEnabled = bNewEnabled;
	UpdateMaterial();
}

void UShootableButtonComponent::OnOwnerTakeAnyDamage(AActor* DamagedActor, float Damage,
	const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	// An enemy's stray round is not a press [author, 2026-10-11]: on the bench they shot the consoles
	// in the middle of a fight, which cleared the fight they were in. Damage with nobody behind it
	// (a script, a trap) still presses.
	if (InstigatedBy && !InstigatedBy->IsPlayerController())
	{
		return;
	}

	Press(DamageCauser);
}

void UShootableButtonComponent::UpdateMaterial()
{
	if (!ButtonMesh)
	{
		return;
	}

	// One-shot mode: material follows bIsEnabled
	// Toggle mode: material follows bIsToggledOn
	const bool bShowOnMaterial = bAutoDisableOnPress ? bIsEnabled : bIsToggledOn;
	UMaterialInterface* Mat = bShowOnMaterial ? OnMaterial : OffMaterial;

	if (Mat)
	{
		ButtonMesh->SetMaterial(MaterialSlotIndex, Mat);
	}
}

bool UShootableButtonComponent::IsCooldownReady() const
{
	if (!GetWorld())
	{
		return true;
	}

	return (GetWorld()->GetTimeSeconds() - LastActivationTime) >= ActivationCooldown;
}
