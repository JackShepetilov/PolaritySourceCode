// TacticalDeviceComponent.h
// The side rail device on whatever gun the player is holding: its charge, when it runs, and the
// runtime object (handler) that does what the device does. Docs/TacticalAttachment_Plan_2026-10-01.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Variant_Shooter/Tactical/TacticalDeviceDefinition.h"
#include "TacticalDeviceComponent.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UTacticalDeviceDefinition;
class UTacticalDeviceHandler;
class UInputAction;
class UEnhancedInputComponent;
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
	friend class ABenchLoadoutButton;

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

	// ==================== Modes [author, 2026-10-15] ====================

	/** Cycles Off -> Normal -> Active -> Off on a device that has modes (the flashlight, the laser).
	 *  Active is skipped while the charge is not ready. Set on the Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical|Input")
	TObjectPtr<UInputAction> ModeCycleAction;

	/** Called from the character's input setup. */
	void BindInput(UEnhancedInputComponent* Input);

	/** The mode of a device that has modes (Off for one that does not). */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	ETacticalDeviceMode GetMode() const { return Mode; }

	// ==================== For the handlers ====================

	AShooterCharacter* GetCharacter() const;
	AShooterWeapon* GetHeldWeapon() const;
	bool IsAuthority() const;
	bool IsLocallyViewed() const;

	/** Where gameplay aims from and to: the owner's eyes and view direction. Same answer on the
	 *  server and the owner, close enough on the others. */
	void GetAim(FVector& OutOrigin, FVector& OutDirection) const;

	/** Where the device's light goes [author, 2026-10-15]: the screen centre while the gun is steady,
	 *  down the barrel in a sprint, a slide, a jump, and as a string of fire goes on, blended by
	 *  AShooterWeapon::GetDeviceBarrelWeight. Worked out on the owner's machine, which is the only one
	 *  that sees its first person gun move; the server and the others get it as an offset from the aim
	 *  (DeviceAimOffset), so the light lands for everybody where it is drawn. */
	void GetDeviceAim(FVector& OutOrigin, FVector& OutDirection) const;

	/** Where the rounds go before their spread [author, 2026-10-15]: the screen centre, or down the
	 *  barrel in a sprint, a jump, a slide, a wall run. Read by AShooterCharacter::GetWeaponTargetLocation.
	 *  The laser's beam is this plus the next round's spread (AShooterWeapon::GetNextShotOffset). */
	void GetShotAim(FVector& OutOrigin, FVector& OutDirection) const;

	/** Where the device's own effects start: the rail mesh (first person for the owner, third person
	 *  for everybody else), else the muzzle, else the eyes. */
	FVector GetVisualOrigin() const;

	/** GetVisualOrigin as the owner SEES it. The first person gun is drawn with a field of view of its
	 *  own (Docs/Gotchas/Camera_FirstPerson.md): its sideways and up offsets from the camera are scaled
	 *  in the picture, so its rail is not where its bones are. A beam, which is world geometry, has to
	 *  start at the point that lands on the same pixel. Elsewhere the same as GetVisualOrigin. */
	FVector GetApparentVisualOrigin() const;

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

	/** The device's direction in the aim's own frame (X ahead). Sent up by the owner, a few times a
	 *  second; everybody else reads it. @see GetDeviceAim */
	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal DeviceAimOffset = FVector(1.0f, 0.0f, 0.0f);

	UFUNCTION(Server, Unreliable)
	void Server_SetDeviceAimOffset(FVector_NetQuantizeNormal Offset);

	/** Owner: where the device points right now (world), the smoothed blend, and "straight ahead" in
	 *  the gun's own frame, learned while the gun is steady. */
	FVector OwnerDeviceDirection = FVector::ForwardVector;
	FVector OwnerShotDirection = FVector::ForwardVector;
	bool bOwnerAimValid = false;

	/** The beam's direction in the aim's own frame, eased toward where it should be. */
	FVector BeamInAimFrame = FVector(1.0f, 0.0f, 0.0f);
	float OwnerBarrelWeight = 0.0f;
	FVector BarrelInGunFrame = FVector::ZeroVector;
	float DeviceAimSendClock = 0.0f;

	/** Owner: work the blend out for this frame. */
	void UpdateOwnerDeviceAim(float DeltaTime);

	/** @see GetMode. Everybody gets it: the strobe and the burner are drawn for all. */
	UPROPERTY(Replicated)
	ETacticalDeviceMode Mode = ETacticalDeviceMode::Off;

	void OnModeCyclePressed();

	UFUNCTION(Server, Reliable)
	void Server_CycleMode();

	/** The next mode from Current, skipping Active without the charge for it. */
	ETacticalDeviceMode NextMode(ETacticalDeviceMode Current) const;

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
