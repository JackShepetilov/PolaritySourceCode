// TacticalDeviceComponent.cpp

#include "Variant_Shooter/Tactical/TacticalDeviceComponent.h"
#include "Variant_Shooter/Tactical/TacticalDeviceDefinition.h"
#include "Variant_Shooter/Tactical/TacticalDeviceHandlers.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "ApexMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UTacticalDeviceComponent::UTacticalDeviceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the camera and the weapon meshes have moved this frame, so the prototype shapes and the
	// light sit where the gun is drawn rather than a frame behind it.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

void UTacticalDeviceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everybody draws everybody's light, so the device and the switch go to all.
	DOREPLIFETIME(UTacticalDeviceComponent, Device);
	DOREPLIFETIME(UTacticalDeviceComponent, bActive);

	// Only the bar reads the charge, and the bar is the owner's.
	DOREPLIFETIME_CONDITION(UTacticalDeviceComponent, Charge, COND_OwnerOnly);
}

void UTacticalDeviceComponent::BeginPlay()
{
	Super::BeginPlay();
	SyncHandler();
}

void UTacticalDeviceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetRunningHere(false);
	if (Handler)
	{
		Handler->Shutdown();
		Handler = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

AShooterCharacter* UTacticalDeviceComponent::GetCharacter() const
{
	return Cast<AShooterCharacter>(GetOwner());
}

AShooterWeapon* UTacticalDeviceComponent::GetHeldWeapon() const
{
	const AShooterCharacter* const Character = GetCharacter();
	return Character ? Character->GetCurrentWeapon() : nullptr;
}

bool UTacticalDeviceComponent::IsAuthority() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

bool UTacticalDeviceComponent::IsLocallyViewed() const
{
	const AShooterCharacter* const Character = GetCharacter();
	return Character && Character->IsLocallyControlled();
}

bool UTacticalDeviceComponent::IsChargeReady() const
{
	return Device && Charge >= Device->MinChargeToActivate;
}

const UTacticalDeviceDefinition* UTacticalDeviceComponent::ResolveHeldDevice() const
{
	const AShooterWeapon* const Weapon = GetHeldWeapon();
	if (!Weapon)
	{
		return nullptr;
	}
	const UWeaponAttachmentDefinition* const Attachment = Weapon->GetAttachmentOfType(EWeaponAttachmentType::Tactical);
	return Attachment ? Attachment->Device.Get() : nullptr;
}

bool UTacticalDeviceComponent::WantsToRun() const
{
	const AShooterCharacter* const Character = GetCharacter();
	if (!Character || Character->IsDead())
	{
		return false;
	}

	const AShooterWeapon* const Weapon = Character->GetCurrentWeapon();
	if (!Weapon || Weapon->IsMeleeWeapon())
	{
		return false;
	}

	// The owner's own machine knows the swap phase; the server does not track it for a remote client,
	// and ADS drops during a swap anyway.
	if (Character->IsLocallyControlled() && Character->IsWeaponSwitchInProgress())
	{
		return false;
	}

	// The movement component's aim bit is the one copy every relevant machine agrees on: written by
	// the owner, carried in the saved move to the server.
	const UApexMovementComponent* const Apex = Character->GetApexMovement();
	return Apex && Apex->IsAiming();
}

void UTacticalDeviceComponent::GetAim(FVector& OutOrigin, FVector& OutDirection) const
{
	const AShooterCharacter* const Character = GetCharacter();
	if (!Character)
	{
		OutOrigin = FVector::ZeroVector;
		OutDirection = FVector::ForwardVector;
		return;
	}

	// The owner's real camera when we have it (exact on the owner and the listen host's own pawn);
	// the pawn's view point everywhere else.
	if (Character->IsLocallyControlled())
	{
		if (const UCameraComponent* const Camera = Character->GetFirstPersonCameraComponent())
		{
			OutOrigin = Camera->GetComponentLocation();
			OutDirection = Character->GetBaseAimRotation().Vector();
			return;
		}
	}
	OutOrigin = Character->GetPawnViewLocation();
	OutDirection = Character->GetBaseAimRotation().Vector();
}

FVector UTacticalDeviceComponent::GetVisualOrigin() const
{
	const AShooterWeapon* const Weapon = GetHeldWeapon();
	if (Weapon)
	{
		const bool bFirstPerson = IsLocallyViewed();
		if (const UStaticMeshComponent* const Rail = Weapon->GetAttachmentMeshComponent(EWeaponAttachmentType::Tactical, bFirstPerson))
		{
			// A device mesh may name where its light comes out; without one, its middle will do.
			static const FName EmitterSocket(TEXT("SOCKET_Emitter"));
			return Rail->DoesSocketExist(EmitterSocket) ? Rail->GetSocketLocation(EmitterSocket) : Rail->Bounds.Origin;
		}
		return Weapon->GetMuzzleWorldLocation();
	}

	FVector Origin, Direction;
	GetAim(Origin, Direction);
	return Origin;
}

void UTacticalDeviceComponent::SpendCharge(float Fraction)
{
	if (!IsAuthority() || Fraction <= 0.0f)
	{
		return;
	}
	Charge = FMath::Max(0.0f, Charge - Fraction);
	RechargeWait = Device ? Device->RechargeDelay : 0.0f;
}

float UTacticalDeviceComponent::AbsorbIncomingDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!IsAuthority() || !bActive || !Handler || Damage <= 0.0f)
	{
		return Damage;
	}
	return Handler->AbsorbDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}

void UTacticalDeviceComponent::BroadcastDeviceEvent(uint8 EventId, const FVector& Location)
{
	if (IsAuthority())
	{
		Multicast_DeviceEvent(EventId, Location);
	}
}

void UTacticalDeviceComponent::Multicast_DeviceEvent_Implementation(uint8 EventId, FVector_NetQuantize Location)
{
	if (Handler)
	{
		Handler->PlayEvent(EventId, Location);
	}
}

void UTacticalDeviceComponent::SyncHandler()
{
	// A device that arrived before BeginPlay (a late joiner's first replication) had nobody to build
	// its handler yet.
	if (!Handler && Device)
	{
		Handler = Device->CreateHandler(this);
	}
}

void UTacticalDeviceComponent::SetRunningHere(bool bRunning)
{
	if (bRunning == bRunningHere)
	{
		return;
	}
	bRunningHere = bRunning;

	if (Handler)
	{
		if (bRunning)
		{
			Handler->OnActivated();
		}
		else
		{
			Handler->OnDeactivated();
		}
	}
}

void UTacticalDeviceComponent::OnRep_Device()
{
	// Drop whatever ran for the old device on this machine before the new one takes over.
	SetRunningHere(false);
	if (Handler)
	{
		Handler->Shutdown();
		Handler = nullptr;
	}
	if (Device)
	{
		Handler = Device->CreateHandler(this);
	}
}

void UTacticalDeviceComponent::OnRep_Active()
{
	// The owner predicts its own state in TickComponent; this is for everybody else.
	if (!IsLocallyViewed())
	{
		SetRunningHere(bActive);
	}
}

void UTacticalDeviceComponent::ServerTick(float DeltaTime)
{
	// The device follows the gun in hand: a swap, a pickup, a removed attachment all land here.
	UTacticalDeviceDefinition* const Held = const_cast<UTacticalDeviceDefinition*>(ResolveHeldDevice());
	if (Held != Device)
	{
		if (bActive)
		{
			bActive = false;
		}
		Device = Held;
		OnRep_Device();
		UE_LOG(LogTemp, Log, TEXT("[TACTICAL_DEBUG] %s device now %s"), *GetNameSafe(GetOwner()), *GetNameSafe(Device));
	}

	if (!Device)
	{
		// Nothing on the rail: the battery still fills, so a device mounted later is not born empty.
		Charge = FMath::Min(1.0f, Charge + DeltaTime / 4.0f);
		return;
	}

	const bool bWants = WantsToRun();
	if (bActive)
	{
		if (!bWants || Charge <= 0.0f)
		{
			bActive = false;
			RechargeWait = Device->RechargeDelay;
			if (Charge <= 0.0f && Device->DepletedSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, Device->DepletedSound, GetOwner()->GetActorLocation());
			}
			UE_LOG(LogTemp, Log, TEXT("[TACTICAL_DEBUG] %s %s off (charge %.2f)"),
				*GetNameSafe(GetOwner()), *GetNameSafe(Device), Charge);
		}
	}
	else if (bWants && Charge >= Device->MinChargeToActivate)
	{
		bActive = true;
		UE_LOG(LogTemp, Log, TEXT("[TACTICAL_DEBUG] %s %s on (charge %.2f)"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Device), Charge);
	}

	if (bActive)
	{
		const float Drain = Handler ? Handler->GetDrainPerSecond() : 0.0f;
		Charge = FMath::Max(0.0f, Charge - Drain * DeltaTime);
		if (Drain > 0.0f)
		{
			RechargeWait = Device->RechargeDelay;
		}
	}
	else if (RechargeWait > 0.0f)
	{
		RechargeWait -= DeltaTime;
	}
	else
	{
		Charge = FMath::Min(1.0f, Charge + DeltaTime / FMath::Max(Device->RechargeDuration, 0.1f));
	}
}

void UTacticalDeviceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsAuthority())
	{
		ServerTick(DeltaTime);
	}

	// What runs on this machine. The server runs what it decided; the owner predicts from its own
	// aim and its copy of the charge, so its light comes on with the aim and not a ping later;
	// everybody else follows the replicated switch (OnRep_Active).
	bool bRunNow = bRunningHere;
	if (IsAuthority())
	{
		bRunNow = bActive;
	}
	else if (IsLocallyViewed())
	{
		const bool bWants = Device && WantsToRun();
		bRunNow = bRunningHere ? (bWants && Charge > 0.0f) : (bWants && IsChargeReady());
	}
	SetRunningHere(bRunNow);

	if (bRunningHere && Handler)
	{
		Handler->TickActive(DeltaTime);
	}
}
