// RiotShieldPickup.cpp

#include "RiotShieldPickup.h"
#include "RiotShield.h"
#include "ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/UI/EMFChargeWidgetSubsystem.h"
#include "EMF_FieldComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

ARiotShieldPickup::ARiotShieldPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->SetupAttachment(RootComponent);
	SphereCollision->SetSphereRadius(60.0f);
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereCollision->SetCollisionObjectType(ECC_WorldDynamic);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	// NOTE: no OnComponentBeginOverlap binding — pickup is acquired only via the channel/grab key
	// (StartPull → UpdatePull → TryEquipOnPullingCharacter), never by simply walking into it.

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(FName("PhysicsActor"));
	Mesh->SetSimulatePhysics(false); // toggled to true in SpawnAsThrown
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// Bind stun-on-hit. Callback gates on bCanStunOnImpact at runtime, so always bind (cheap).
	Mesh->SetNotifyRigidBodyCollision(true);
	Mesh->OnComponentHit.AddDynamic(this, &ARiotShieldPickup::OnMeshHit);

	// EMF field — pickup carries the shield's charge while on the ground (drives the floating widget).
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

float ARiotShieldPickup::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void ARiotShieldPickup::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	// (Re)register charge widget. Mirrors ADroppedRangedWeapon::SetCharge — re-register triggers
	// a refresh in the widget subsystem so visuals follow the new charge.
	if (UWorld* World = GetWorld())
	{
		if (UEMFChargeWidgetSubsystem* WidgetSub = World->GetSubsystem<UEMFChargeWidgetSubsystem>())
		{
			WidgetSub->UnregisterRiotShieldPickup(this);
			WidgetSub->RegisterRiotShieldPickup(this);
		}
	}
}

void ARiotShieldPickup::BeginPlay()
{
	Super::BeginPlay();
}

void ARiotShieldPickup::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bCanStunOnImpact || !OtherActor || !Mesh) return;

	// Cooldown — single bounce shouldn't fire multiple stuns
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastStunTime < StunCooldown) return;

	// Velocity gate — settling/rolling shield shouldn't stun on incidental brush
	const float ImpactSpeed = Mesh->GetPhysicsLinearVelocity().Size();
	if (ImpactSpeed < StunImpactVelocityThreshold) return;

	// Only NPCs (not props, not the player). HumanoidNPC's ApplyExplosionStun is a no-op (immune by spec) — passes through.
	AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor);
	if (!NPC || NPC->IsDead()) return;

	NPC->ApplyExplosionStun(StunDuration, StunMontage);
	LastStunTime = Now;

	UE_LOG(LogTemp, Warning, TEXT("[SHIELD_THROW] %s stunned %s for %.1fs (impact speed=%.0f)"),
		*GetName(), *NPC->GetName(), StunDuration, ImpactSpeed);
}

void ARiotShieldPickup::SpawnAsThrown(const FVector& WorldLinearImpulse, const FVector& AngularImpulseDeg)
{
	bThrownMode = true;
	TimeUntilReacquireEnabled = ReacquireDelay;
	// GetOwner() was set in SpawnActor params (= the thrower) — used in OnOverlap to block re-pickup.

	if (Mesh)
	{
		Mesh->SetSimulatePhysics(true);
		// Allow the thrown shield to bonk NPCs on impact.
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Mesh->AddImpulse(WorldLinearImpulse, NAME_None, true);
		Mesh->AddAngularImpulseInDegrees(AngularImpulseDeg, NAME_None, true);
	}
}

void ARiotShieldPickup::StartPull(AShooterCharacter* InPuller)
{
	if (!InPuller || bIsBeingPulled)
	{
		return;
	}

	bIsBeingPulled = true;
	bThrownMode = false;
	TimeUntilReacquireEnabled = 0.0f;

	PullElapsed = 0.0f;
	PullingCharacter = InPuller;
	PullStartLocation = GetActorLocation();
	PullStartRotation = GetActorRotation();

	// Disable physics + collision so the pickup flies straight to the player ignoring obstacles
	if (Mesh)
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (SphereCollision)
	{
		// Prevent OnOverlap firing mid-flight against random pawns; we drive equip from UpdatePull arrival
		SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ARiotShieldPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (TimeUntilReacquireEnabled > 0.0f)
	{
		TimeUntilReacquireEnabled = FMath::Max(0.0f, TimeUntilReacquireEnabled - DeltaTime);
	}

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
	}
}

void ARiotShieldPickup::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		// Puller gone (died/destroyed). Restore physics + collision so the pickup behaves like a normal thrown shield.
		bIsBeingPulled = false;
		if (Mesh)
		{
			Mesh->SetSimulatePhysics(true);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		}
		if (SphereCollision)
		{
			SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		return;
	}

	PullElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(PullElapsed / FMath::Max(PullDuration, 0.05f), 0.0f, 1.0f);
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);

	FVector WorldTarget;
	FRotator WorldTargetRot;
	if (CamMgr)
	{
		const FVector CameraLoc = CamMgr->GetCameraLocation();
		const FRotator CameraRot = CamMgr->GetCameraRotation();
		WorldTarget = CameraLoc + CameraRot.RotateVector(PullTargetOffset);
		WorldTargetRot = CameraRot;
	}
	else
	{
		WorldTarget = PullingCharacter->GetActorLocation();
		WorldTargetRot = PullingCharacter->GetActorRotation();
	}

	SetActorLocation(FMath::Lerp(PullStartLocation, WorldTarget, CurvedAlpha));
	SetActorRotation(FMath::Lerp(PullStartRotation, WorldTargetRot, CurvedAlpha));

	if (Alpha >= 1.0f)
	{
		TryEquipOnPullingCharacter();
	}
}

void ARiotShieldPickup::TryEquipOnPullingCharacter()
{
	bIsBeingPulled = false;

	AShooterCharacter* Char = PullingCharacter.Get();
	if (!Char || !ShieldClass)
	{
		Destroy();
		return;
	}
	if (Char->HasShield())
	{
		// Player picked up another shield mid-flight (or had one already): silently drop this one
		Destroy();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		Destroy();
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Char;

	ARiotShield* NewShield = World->SpawnActor<ARiotShield>(ShieldClass, Char->GetActorLocation(), Char->GetActorRotation(), Params);
	if (NewShield)
	{
		// Carry the pickup's charge onto the equipped shield (preserves NPC's original charge through the cycle).
		NewShield->SetCharge(GetCharge());
		Char->EquipShield(NewShield);
	}

	// Make sure the floating widget is gone before we Destroy.
	if (UEMFChargeWidgetSubsystem* WidgetSub = World->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterRiotShieldPickup(this);
	}

	Destroy();
}

void ARiotShieldPickup::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Intentionally empty — pickup acquisition is exclusively driven by the channel/grab key
	// (StartPull → UpdatePull → TryEquipOnPullingCharacter). Walking over the pickup never picks it up.
}
