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
class UGadgetWheelWidget;

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
 * Docs/Grenades_Plan_2026-10-14.md. The grenades themselves are in the inventory's gadget section
 * (UInventoryComponent::GetGadgets); this is the hand that throws them.
 *
 * IA_Grenade (G): press winds up, holding draws the arc, letting go throws; a quick tap throws at
 * once. The same pipeline the thrown abilities use (UAbilityHandler_Throw), run without a slot or a
 * cooldown. IA_GrenadeCycle (H): a tap is the next kind, held it is the wheel.
 *
 * Like the abilities, the throw is decided and run on the authority; the owning client draws the arc
 * itself. TODO(COOP): as for every ability, the throw montage plays on the server only, so a client
 * does not see his own hand throw (the grenade and the arc he does see).
 */
UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UGrenadeComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class ABenchLoadoutButton;

public:

	UGrenadeComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Setup ====================

	/** Throw the selected type. G. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> GrenadeAction;

	/** H. A tap selects the next kind; held, it opens the wheel [author, 2026-10-16]. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> GrenadeCycleAction;

	/** Held longer than this, H opens the wheel instead of cycling. */
	UPROPERTY(EditAnywhere, Category = "Input|Wheel", meta = (ClampMin = "0.05", Units = "s"))
	float WheelHoldTime = 0.25f;

	/** How far the look has to move, in look input, to pick a kind on the wheel. */
	UPROPERTY(EditAnywhere, Category = "Input|Wheel", meta = (ClampMin = "0.1"))
	float WheelPickDistance = 4.0f;

	/** The wheel. Empty: the plain painted one. */
	UPROPERTY(EditAnywhere, Category = "Input|Wheel")
	TSubclassOf<UGadgetWheelWidget> WheelWidgetClass;

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

	/** Owning client, from the character's look: while the wheel is open the look moves the pick, not
	 *  the camera. True when it took the input. */
	bool HandleLookForWheel(float Yaw, float Pitch);

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsWheelOpen() const { return bWheelOpen; }

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

	UFUNCTION(Server, Reliable)
	void Server_SelectType(UGrenadeDefinition* Type);

	UFUNCTION()
	void OnInventoryChanged();

private:

	void OnGrenadePressed();
	void OnGrenadeReleased();
	void OnCyclePressed();
	void OnCycleReleased();

	void BeginThrow();
	void ReleaseThrow();
	void CycleType();
	void SelectType(UGrenadeDefinition* Type);
	void OnThrowFinished(bool bCompleted);

	/** Owning client: the wheel. */
	void OpenWheel();
	void CloseWheel(bool bApply);
	void RefreshWheel();

	/** Keep the selection on a type there is some of. Authority. */
	void RefreshSelection();
	void GiveStartingGrenades();

	/** Owning client: drive the burn's fade. From TickComponent. */
	void TickBurn();
	void EndDeafness();

	AShooterCharacter* GetCharacter() const;

	UPROPERTY(Replicated)
	TObjectPtr<UGrenadeDefinition> SelectedType;

	/** Authority: the place in the gadget section the selection was at. When that kind is gone (thrown
	 *  out, or dropped for a new one that took its place) the selection stays on the place. */
	int32 SelectedPlace = 0;

	/** Owning client: H. */
	bool bCycleKeyHeld = false;
	bool bWheelOpen = false;
	FTimerHandle WheelHoldTimer;

	/** Owning client, while the wheel is open: the kinds as drawn, the look so far, the pick. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGrenadeDefinition>> WheelTypes;

	FVector2D WheelCursor = FVector2D::ZeroVector;
	int32 WheelHovered = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UGadgetWheelWidget> Wheel;

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
