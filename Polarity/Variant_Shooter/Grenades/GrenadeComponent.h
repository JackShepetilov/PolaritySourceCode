// GrenadeComponent.h
// The player's grenades: the key, which type is up, the throw, and what a flash does to his screen.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrenadeComponent.generated.h"

class UGrenadeDefinition;
class UAbilityHandler_Grenade;
class UEnhancedInputComponent;
class UInputAction;
class USoundBase;
class USoundMix;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class APostProcessVolume;
class AShooterCharacter;

/** What a flashbang did to one player, worked out by the authority and sent to his machine. */
USTRUCT(BlueprintType)
struct FGrenadeFlashHit
{
	GENERATED_BODY()

	/** Where it went off. The burn sits where this was on his screen at that moment. */
	UPROPERTY()
	FVector FlashLocation = FVector::ZeroVector;

	/** 0..1, 0 = he did not look at it. */
	UPROPERTY()
	float Burn = 0.0f;

	UPROPERTY()
	float BurnTime = 0.0f;

	/** In screen heights. */
	UPROPERTY()
	float BurnSize = 0.0f;

	/** 0..1, 0 = too far to be deafened. */
	UPROPERTY()
	float Loud = 0.0f;

	UPROPERTY()
	float DeafTime = 0.0f;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BurnMaterial;

	UPROPERTY()
	TObjectPtr<USoundMix> DeafMix;

	UPROPERTY()
	TObjectPtr<USoundBase> Ring;
};

/** A starting stock: so many of this type in the bag on spawn. */
USTRUCT(BlueprintType)
struct FGrenadeStock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade")
	TObjectPtr<UGrenadeDefinition> Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade", meta = (ClampMin = "0"))
	int32 Count = 0;
};

/**
 * Docs/Grenades_Plan_2026-10-14.md. The grenades themselves are cells of the bag
 * (EInventorySlotKind::Grenade); this is the hand that throws them.
 *
 * IA_Grenade (G): press winds up, holding draws the arc, letting go throws; a quick tap throws at
 * once. The same pipeline the thrown abilities use (UAbilityHandler_Throw), run without a slot or a
 * cooldown. IA_GrenadeCycle: the next type in the bag.
 *
 * Like the abilities, the throw is decided and run on the authority; the owning client draws the arc
 * itself. TODO(COOP): as for every ability, the throw montage plays on the server only, so a client
 * does not see his own hand throw (the grenade and the arc he does see).
 */
UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UGrenadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UGrenadeComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Setup ====================

	/** Throw the selected type. G. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> GrenadeAction;

	/** Select the next type in the bag. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> GrenadeCycleAction;

	/** Put in the bag when the player spawns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade")
	TArray<FGrenadeStock> StartingGrenades;

	/** Called by the character from SetupPlayerInputComponent. */
	void BindInput(UEnhancedInputComponent* Input);

	// ==================== State ====================

	/** The type G throws. Null with no grenade in the bag. */
	UFUNCTION(BlueprintPure, Category = "Grenade")
	UGrenadeDefinition* GetSelectedType() const { return SelectedType; }

	/** How many of the selected type are in the bag. */
	UFUNCTION(BlueprintPure, Category = "Grenade")
	int32 GetSelectedCount() const;

	/** From the press to the end of the throw, as the authority sees it (the owner gets it replicated).
	 *  Firing, the weapon swap and the abilities wait while it is true. */
	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsThrowing() const { return bThrowing; }

	/** Authority: drop whatever throw is in the hands (downed, respawned). A grenade that already left
	 *  stays thrown. */
	void CancelThrow();

	// ==================== Flash ====================

	/** Owning client: a burn on the eye where the flash was on the screen (fades over BurnTime), and
	 *  for a loud one the world muffled for DeafTime with the ears ringing. Local to the one flashed. */
	UFUNCTION(Client, Reliable)
	void Client_Flashbang(const FGrenadeFlashHit& Hit);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void Server_BeginThrow();

	UFUNCTION(Server, Reliable)
	void Server_ReleaseThrow();

	UFUNCTION(Server, Reliable)
	void Server_CycleType();

	UFUNCTION()
	void OnInventoryChanged();

private:

	void OnGrenadePressed();
	void OnGrenadeReleased();
	void OnCyclePressed();

	void BeginThrow();
	void ReleaseThrow();
	void CycleType();
	void OnThrowFinished(bool bCompleted);

	/** Keep the selection on a type there is some of. Authority. */
	void RefreshSelection();
	void GiveStartingGrenades();

	/** Owning client: drive the burn's fade. From TickComponent. */
	void TickBurn();
	void EndDeafness();

	AShooterCharacter* GetCharacter() const;

	UPROPERTY(Replicated)
	TObjectPtr<UGrenadeDefinition> SelectedType;

	UPROPERTY(Replicated)
	bool bThrowing = false;

	/** The throw in progress, authority. */
	UPROPERTY()
	TObjectPtr<UAbilityHandler_Grenade> ActiveHandler;

	/** Owning client: the key is down, and since when. For the arc. */
	bool bKeyHeld = false;
	float KeyDownTime = 0.0f;

	/** Owning client: the burn on the screen. A volume of our own, unbound: a blendable on the camera
	 *  is not seen while the weapon's ADS camera draws the frame (Docs/Gotchas/Camera_FirstPerson.md). */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> BurnVolume;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BurnMID;

	float BurnStartTime = -1.0f;
	float BurnDuration = 0.0f;

	/** Owning client: the deaf mix while it is pushed. */
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> PushedDeafMix;

	FTimerHandle DeafTimer;
};
