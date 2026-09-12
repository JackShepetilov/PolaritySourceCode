// HudRegistry.h
// Builds the HUD from a layout asset and is the only thing that holds its widgets.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "HudRegistry.generated.h"

class APawn;
class APlayerController;
class UHudLayoutAsset;
class UHudRootWidget;
class UUserWidget;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnHudSlotFilled, FGameplayTag /*SlotTag*/, UUserWidget* /*Widget*/);

/**
 * One per local player, so a listen host and each client build their own and the server builds
 * none. The controller calls Build once it is local and on screen; from then on the registry
 * follows the controller's pawn and rebinds every slot on each change (spawn, respawn, the
 * client's late possess), which used to be two hand-written paths with a branch per widget.
 *
 * Gameplay never keeps a pointer to a slot's widget. It asks here, by tag, when it needs one:
 *   Registry->Get<UInventoryBarWidget>(FGameplayTag::RequestGameplayTag("HUD.Slot.Weapon"))
 */
UCLASS()
class POLARITY_API UHudRegistry : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:

	/** Create the root and fill its slots from Layout. Safe to call again: tears the old one down. */
	void Build(APlayerController* Controller, UHudLayoutAsset* Layout);

	/** Remove everything from the screen and forget it. */
	void Teardown();

	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsBuilt() const { return Root != nullptr; }

	UFUNCTION(BlueprintPure, Category = "HUD")
	UHudRootWidget* GetRoot() const { return Root; }

	/** The widget living in a slot, or null when the layout has no such slot. */
	UFUNCTION(BlueprintPure, Category = "HUD", meta = (AutoCreateRefTerm = "SlotTag"))
	UUserWidget* GetSlotWidget(const FGameplayTag& SlotTag) const;

	template <class T>
	T* Get(const FGameplayTag& SlotTag) const { return Cast<T>(GetSlotWidget(SlotTag)); }

	/** Collapse or show one slot's content. The slot keeps its place either way. */
	UFUNCTION(BlueprintCallable, Category = "HUD", meta = (AutoCreateRefTerm = "SlotTag"))
	void SetSlotVisible(const FGameplayTag& SlotTag, bool bVisible);

	/** Fired once per slot as Build fills it, for systems that want a widget they do not own. */
	FOnHudSlotFilled OnSlotFilled;

	virtual void Deinitialize() override;

private:

	/** Unbind every slot from the old character and bind it to the new one (null = just unbind). */
	void BindPawn(APawn* Pawn);

	UFUNCTION()
	void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);

	UPROPERTY(Transient)
	TObjectPtr<UHudRootWidget> Root;

	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UUserWidget>> Widgets;

	TWeakObjectPtr<APlayerController> BoundController;
};
