// BuilderComponent.h
// The engineer's half of building: the menu, the blueprint ghost, and the request to the server.
//
// Sits on AShooterCharacter. Everything before the server call is local to the owning player (a
// menu and a hologram are that player's business alone); everything after it is the server's
// (price, room, spawn, ownership). The building itself is ABuildableActor and belongs to the
// AShooterPlayerState, so a dead character does not take its buildings with it.
//
// Input is two mapping contexts pushed on top of the weapons: one while the menu is open (the
// number keys become building slots and swallow the key, so the weapon hotkeys on the same keys
// stay quiet) and one while placing (fire places, aim rotates). Which physical keys is the
// contexts' business, set in the character Blueprint like every other action here.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuildableDefinition.h"
#include "BuilderComponent.generated.h"

class ABuildableActor;
class ABuildablePreview;
class AShooterCharacter;
class AShooterPlayerState;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;

UENUM(BlueprintType)
enum class EBuilderMode : uint8
{
	/** Nothing open. */
	Idle,
	/** The menu is up, waiting for a slot. */
	Menu,
	/** A ghost is out, waiting for a spot. */
	Placing
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBuilderModeChangedDelegate, EBuilderMode, Mode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBuilderPlacementDelegate, int32, SlotIndex, EBuildablePlacementResult, Result);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class POLARITY_API UBuilderComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UBuilderComponent();

	// ==================== The catalogue ====================

	/** What this player can build, in menu order: slot 0 is the first key. Edited on the character
	 *  Blueprint; every class gets the same list until a class definition wants its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Builder")
	TArray<TObjectPtr<UBuildableDefinition>> Buildables;

	// ==================== Input ====================

	/** Opens and closes the menu. Mapped in the ordinary weapons context. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TObjectPtr<UInputAction> BuildMenuAction;

	/** Pushed while the menu is open. Maps SlotActions onto the number keys and consumes them. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TObjectPtr<UInputMappingContext> MenuMappingContext;

	/** One per slot, in slot order. Press = build that kind; hold, when the kind is fully built =
	 *  demolish one. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TArray<TObjectPtr<UInputAction>> SlotActions;

	/** Pushed while the ghost is out. Maps the three actions below over fire, aim and the like. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TObjectPtr<UInputMappingContext> PlacementMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TObjectPtr<UInputAction> ConfirmAction;

	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TObjectPtr<UInputAction> RotateAction;

	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	TObjectPtr<UInputAction> CancelAction;

	/** Above the weapons context (0), so a slot key is ours first. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Input")
	int32 MappingPriority = 10;

	// ==================== Placement ====================

	/** The hologram. Unset = the plain C++ ghost with the building's own materials. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Placement")
	TSubclassOf<ABuildablePreview> PreviewClass;

	/** Degrees per press of the rotate key. TF2 turns a quarter at a time. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Placement", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float RotateStepDegrees = 90.0f;

	/** How far below the aim point the floor is looked for. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Placement", meta = (ClampMin = "10.0", Units = "cm"))
	float GroundSearchDepth = 300.0f;

	/** Put the weapon away while the ghost is out, the way the engineer holds a toolbox instead of
	 *  a gun. Uses the character's own holster, so its gates (firing, abilities) come for free. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Placement")
	bool bHolsterWeaponWhilePlacing = true;

	/** Seconds a slot key has to be held, when that kind is fully built, to take one down. */
	UPROPERTY(EditDefaultsOnly, Category = "Builder|Menu", meta = (ClampMin = "0.1", Units = "s"))
	float DemolishHoldSeconds = 0.5f;

	// ==================== Wiring ====================

	/** Called by AShooterCharacter::SetupPlayerInputComponent. */
	void SetupInput(UEnhancedInputComponent* Input);

	// ==================== Menu ====================

	UFUNCTION(BlueprintCallable, Category = "Builder")
	void ToggleMenu();

	UFUNCTION(BlueprintCallable, Category = "Builder")
	void OpenMenu();

	UFUNCTION(BlueprintCallable, Category = "Builder")
	void CloseMenu();

	UFUNCTION(BlueprintPure, Category = "Builder")
	EBuilderMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Builder")
	bool IsMenuOpen() const { return Mode == EBuilderMode::Menu; }

	UFUNCTION(BlueprintPure, Category = "Builder")
	bool IsPlacing() const { return Mode == EBuilderMode::Placing; }

	// ==================== Placement ====================

	/** Close the menu and bring out the ghost for a slot. Refused when the slot is full or the
	 *  player cannot pay; the refusal is logged and OnSlotRefused fires for the HUD. */
	UFUNCTION(BlueprintCallable, Category = "Builder")
	void BeginPlacement(int32 SlotIndex);

	/** Ask the server to put it where the ghost is. Does nothing while the spot is refused. */
	UFUNCTION(BlueprintCallable, Category = "Builder")
	void ConfirmPlacement();

	UFUNCTION(BlueprintCallable, Category = "Builder")
	void CancelPlacement();

	UFUNCTION(BlueprintCallable, Category = "Builder")
	void RotatePreview();

	/** Take down the oldest standing building of a slot's kind. */
	UFUNCTION(BlueprintCallable, Category = "Builder")
	void RequestDemolish(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "Builder")
	int32 GetPlacingSlot() const { return PlacingSlot; }

	UFUNCTION(BlueprintPure, Category = "Builder")
	EBuildablePlacementResult GetPlacementResult() const { return LastPlacementResult; }

	// ==================== Queries the HUD asks ====================

	UFUNCTION(BlueprintPure, Category = "Builder")
	UBuildableDefinition* GetDefinition(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Builder")
	bool CanAfford(int32 SlotIndex) const;

	/** Standing (or going up) buildings of this slot's kind, against the kind's limit. */
	UFUNCTION(BlueprintPure, Category = "Builder")
	int32 CountBuilt(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Builder")
	int32 GetMaxCount(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Builder")
	AShooterPlayerState* GetOwnerPlayerState() const;

	// ==================== Events for the HUD ====================

	UPROPERTY(BlueprintAssignable, Category = "Builder")
	FBuilderModeChangedDelegate OnModeChanged;

	/** The ghost's verdict changed (valid, blocked, too steep...). */
	UPROPERTY(BlueprintAssignable, Category = "Builder")
	FBuilderPlacementDelegate OnPlacementUpdated;

	/** A slot key was pressed and nothing could be done with it: full, or too poor. */
	UPROPERTY(BlueprintAssignable, Category = "Builder")
	FBuilderPlacementDelegate OnSlotRefused;

	// ==================== Debug (authority) ====================

	/** Put a slot's building at the aim point for free, skipping the menu and the ghost. What the
	 *  polarity.build.place command does. Null when refused (no room, no ground). */
	ABuildableActor* DebugPlace(int32 SlotIndex);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The request. The server does not trust the transform, it checks it: room, ground, reach,
	 *  limit, price, in that order, and refuses silently (the ghost is already gone on the client,
	 *  the metal never moved, the weapon is back). */
	UFUNCTION(Server, Reliable)
	void Server_PlaceBuildable(int32 SlotIndex, FTransform Transform);

	UFUNCTION(Server, Reliable)
	void Server_DemolishBuildable(int32 SlotIndex);

private:

	// ==================== Server side ====================

	/** Spawn, register, charge. Null when any check fails. */
	ABuildableActor* SpawnBuildable(int32 SlotIndex, const FTransform& Transform, bool bCharge);

	// ==================== Owner side ====================

	/** Where the ghost goes this frame: on the floor under the aim point, within reach, facing the
	 *  player plus the rotation offset. False when there is no floor. */
	bool ComputePlacementTransform(FTransform& OutTransform) const;

	void SetMode(EBuilderMode NewMode);
	UEnhancedInputLocalPlayerSubsystem* ResolveInputSubsystem();
	void PushMapping(UInputMappingContext* Context);
	void PopMapping(UInputMappingContext* Context);
	void DestroyPreview();

	void HandleSlotPressed(int32 SlotIndex);
	void HandleSlotReleased(int32 SlotIndex);
	void HandleDemolishHoldFired();
	void HandleCancelPressed();

	AShooterCharacter* GetCharacter() const;
	bool IsLocallyControlled() const;
	bool IsValidSlot(int32 SlotIndex) const { return Buildables.IsValidIndex(SlotIndex) && Buildables[SlotIndex] != nullptr; }

	EBuilderMode Mode = EBuilderMode::Idle;
	int32 PlacingSlot = -1;
	float PreviewYawOffset = 0.0f;
	EBuildablePlacementResult LastPlacementResult = EBuildablePlacementResult::NoGround;
	FTransform LastPlacementTransform = FTransform::Identity;
	bool bHolsteredForPlacement = false;

	UPROPERTY(Transient)
	TObjectPtr<ABuildablePreview> Preview;

	/** Where the mapping contexts were pushed, so they can be popped after the controller is gone. */
	TWeakObjectPtr<ULocalPlayer> MappedLocalPlayer;

	int32 DemolishHoldSlot = -1;
	FTimerHandle DemolishHoldTimer;
};
