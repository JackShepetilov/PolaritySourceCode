// EMFStaticCharge.cpp
// Static point charge actor for ChargeLauncher ability

#include "EMFStaticCharge.h"
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"
#include "Components/SphereComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"

AEMFStaticCharge::AEMFStaticCharge()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root component
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Collision sphere for receiving damage
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->SetupAttachment(SceneRoot);
	CollisionSphere->SetSphereRadius(CollisionRadius);
	CollisionSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	CollisionSphere->SetGenerateOverlapEvents(true);

	// EMF field component - point charge, static, player-owned
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
	FieldComponent->bUseOwnerInterface = false;
	FieldComponent->bAutoRegister = false;
	FieldComponent->bSimulatePhysics = false;
	FieldComponent->SourceParams.SourceType = EEMSourceType::PointCharge;
	FieldComponent->SourceParams.bIsStatic = true;
	FieldComponent->SourceParams.bShowFieldLines = false;
	FieldComponent->SourceParams.OwnerType = EEMSourceOwnerType::Player;
}

void AEMFStaticCharge::BeginPlay()
{
	Super::BeginPlay();

	CurrentHP = MaxHP;

	// Set collision radius (in case it was changed in BP)
	CollisionSphere->SetSphereRadius(CollisionRadius);

	// Initialize charge and mass in field component
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = DefaultCharge;
		Desc.PhysicsParams.Mass = DefaultMass;
		FieldComponent->SetSourceDescription(Desc);

		FieldComponent->RegisterWithRegistry();
	}

	// Spawn VFX based on charge sign
	SpawnChargeVFX();

	// Ambient sound
	if (AmbientLoopSound)
	{
		AmbientAudioComponent = UGameplayStatics::SpawnSoundAttached(
			AmbientLoopSound, SceneRoot, NAME_None,
			FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
			false, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}

	// Auto-destroy timer
	if (MaxLifetime > 0.0f)
	{
		FTimerHandle LifetimeTimer;
		GetWorldTimerManager().SetTimer(LifetimeTimer, [this]()
		{
			Die(nullptr);
		}, MaxLifetime, false);
	}
}

void AEMFStaticCharge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
	}

	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
	}

	Super::EndPlay(EndPlayReason);
}

void AEMFStaticCharge::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Re-spawn VFX with new polarity
	SpawnChargeVFX();
}

float AEMFStaticCharge::GetCharge() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetSourceDescription().PointChargeParams.Charge;
	}
	return 0.0f;
}

float AEMFStaticCharge::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return 0.0f;
	}

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	CurrentHP = FMath::Max(0.0f, CurrentHP - ActualDamage);

	if (CurrentHP <= 0.0f)
	{
		Die(DamageCauser);
	}

	return ActualDamage;
}

void AEMFStaticCharge::Die(AActor* Killer)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;

	// Unregister from EMF registry immediately
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
	}

	// Broadcast death event
	OnStaticChargeDeath.Broadcast(this, Killer);

	// Death VFX
	if (DeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), DeathVFX, GetActorLocation(), GetActorRotation());
	}

	// Death sound
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), DeathSound, GetActorLocation());
	}

	// Stop ambient effects
	if (ActiveVFXComponent)
	{
		ActiveVFXComponent->DestroyComponent();
		ActiveVFXComponent = nullptr;
	}

	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
		AmbientAudioComponent = nullptr;
	}

	Destroy();
}

void AEMFStaticCharge::SpawnChargeVFX()
{
	// Remove old VFX
	if (ActiveVFXComponent)
	{
		ActiveVFXComponent->DestroyComponent();
		ActiveVFXComponent = nullptr;
	}

	float CurrentCharge = GetCharge();
	UNiagaraSystem* VFXToSpawn = (CurrentCharge >= 0.0f) ? PositiveChargeVFX : NegativeChargeVFX;

	if (VFXToSpawn)
	{
		ActiveVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			VFXToSpawn, SceneRoot, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true);
	}
}
