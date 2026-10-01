// TacticalDeviceComponent.h
// The side rail device on whatever gun the player is holding: its charge, when it runs, and the
// runtime object (handler) that does what the device does. Docs/TacticalAttachment_Plan_2026-10-01.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "TacticalDeviceComponent.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UTacticalDeviceDefinition;
class UTacticalDeviceHandler;
struct FDamageEvent;

/**
 * Lives on AShooterCharacter. Reads the Tactical attachment of the gun in hand every tick.
 *
 * WHEN IT RUNS. By itself while the owner aims down sights (author's call, 2026-10-01), with the gun
 * out, as long as there is charge. Below MinChargeToActivate it will not switch on; at zero it
 * switches off and has to refill past that line again.
 *
 * WHO DECIDES. The server: the charge, on/off, and every gameplay effect (handlers check
 * IsAuthority). The owner gets the charge (owner only) for the bar; everybody gets the device and
 * the on/off flag for the visuals. The owner does not wait for the round trip to SEE its own device:
 * it predicts on/off from its own aim and its copy of the charge, so the light comes on with the
 * aim rather than a ping later. The server's word still decides everything that matters.
 *
 * Aim on the server comes from UApexMovementComponent::IsAiming, which rides the saved move; the
 * character's bWantsToAim only exists on the aiming player's own machine.
 */
UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UTacticalDeviceComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UTacticalDeviceComponent();

	// ==================== State ====================

	/** 0..1. Exact on the server and the owner; other machines do not get it. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	float GetCharge() const { return Charge; }

	/** The device on the gun in hand, or null. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	const UTacticalDeviceDefinition* GetDevice() const { return Device; }

	/** Running on this machine right now (predicted on the owner, replicated elsewhere). */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	bool IsDeviceRunning() const { return bRunningHere; }

	/** Charge can switch the device on (for the bar: drawn dimmed below this line). */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	bool IsChargeReady() const;

	// ==================== For the handlers ====================

	AShooterCharacter* GetCharacter() const;
	AShooterWeapon* GetHeldWeapon() const;
	bool IsAuthority() const;
	bool IsLocallyViewed() const;

	/** Where gameplay aims from and to: the owner's eyes and view direction. Same answer on the
	 *  server and the owner, close enough on the others. */
	void GetAim(FVector& OutOrigin, FVector& OutDirection) const;

	/** Where the device's own effects start: the rail mesh (first person for the owner, third person
	 *  for everybody else), else the muzzle, else the eyes. */
	FVector GetVisualOrigin() const;

	/** Server: take charge off (the shield pays with it). Switches the device off at zero. */
	void SpendCharge(float Fraction);

	/** Server: called by AShooterCharacter::TakeDamage before armour. Returns what gets through. */
	float AbsorbIncomingDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser);

	/** Server: play a one-off device moment (a shield hit, a shield break) on every machine. The
	 *  handler on each machine decides what that looks and sounds like. */
	void BroadcastDeviceEvent(uint8 EventId, const FVector& Location);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:

	UPROPERTY(ReplicatedUsing = OnRep_Device)
	TObjectPtr<UTacticalDeviceDefinition> Device;

	UPROPERTY(ReplicatedUsing = OnRep_Active)
	bool bActive = false;

	UPROPERTY(Replicated)
	float Charge = 1.0f;

	/** Seconds left before the charge starts coming back. Server. */
	float RechargeWait = 0.0f;

	/** What runs on THIS machine, the predicted-or-replicated version of bActive. */
	bool bRunningHere = false;

	UPROPERTY(Transient)
	TObjectPtr<UTacticalDeviceHandler> Handler;

	UFUNCTION()
	void OnRep_Device();

	UFUNCTION()
	void OnRep_Active();

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_DeviceEvent(uint8 EventId, FVector_NetQuantize Location);

	/** The device on the gun in hand, from its attachments. */
	const UTacticalDeviceDefinition* ResolveHeldDevice() const;

	/** The owner wants the device on: aiming, gun out, not dead. */
	bool WantsToRun() const;

	/** Build or drop the handler to match Device. Every machine. */
	void SyncHandler();

	/** Switch this machine's running state, telling the handler. */
	void SetRunningHere(bool bRunning);

	void ServerTick(float DeltaTime);
};
