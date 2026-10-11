// ShopScreenWidget.h
// The dispenser shop on the screen [author, 2026-10-07]: a cursor screen, opened with the feed key at
// a dispenser. Three columns of rows, each row one click:
//
//   the guns carried       a click sells that gun for credit
//   the shelf              a click buys the offer
//   for your guns          the same, the attachments always on offer for each carried gun
//
// and a restock row under them. It draws UShopComponent (the player's account) and asks the server
// for everything; nothing is decided here. The rows are the dispenser menu's own row Blueprint
// (UTurretFeedEntryWidget), so the shop looks like the menu it replaced.
//
// Hosted by the player controller the way the inventory overlay is: built once, hidden, shown on
// demand, input in GameAndUI so the player can still walk away. No pause: this is a coop game.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "ShopScreenWidget.generated.h"

class ABuildableActor;
class AShooterCharacter;
class AShooterWeapon;
class UPanelWidget;
class UShopComponent;
class UTextBlock;
class UTurretFeedEntryWidget;
struct FShopOffer;

UCLASS(Abstract, Blueprintable)
class POLARITY_API UShopScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Follow this character's account and guns. Safe to call again after a respawn. */
	void InitializeFor(AShooterCharacter* InCharacter);
	void Shutdown();

	/** Show the shop of the dispenser the player is standing at. */
	void Open(ABuildableActor* InDispenser);
	void Close();
	bool IsOpen() const { return bIsOpen; }

	/** The row Blueprint (WBP_TurretFeedEntry). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	TSubclassOf<UTurretFeedEntryWidget> EntryClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Colors", meta = (Categories = "Palette"))
	FGameplayTag AvailableColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Colors", meta = (Categories = "Palette"))
	FGameplayTag UnavailableColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Colors", meta = (Categories = "Palette"))
	FGameplayTag PlateColorTag;

	/** A row flashes this when a click could not be paid for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Colors", meta = (Categories = "Palette"))
	FGameplayTag RefusedFlashTag;

	/** How see-through a row is while the cursor is not on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float RestingRowOpacity = 0.8f;

protected:

	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Shop", meta = (DisplayName = "On Opened"))
	void BP_OnOpened();

	UFUNCTION(BlueprintImplementableEvent, Category = "Shop", meta = (DisplayName = "On Closed"))
	void BP_OnClosed();

	/** The guns carried: a click sells. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidget))
	TObjectPtr<UPanelWidget> SellPanel;

	/** The offers dealt off the deck. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidget))
	TObjectPtr<UPanelWidget> ShelfPanel;

	/** The attachments always on offer for the guns carried. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidget))
	TObjectPtr<UPanelWidget> WeaponShelfPanel;

	/** Holds the one restock row. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> RestockPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CreditText;

private:

	enum class ERowAction : uint8 { Sell, Buy, BuyForWeapon, Restock };

	struct FRow
	{
		TWeakObjectPtr<UTurretFeedEntryWidget> Widget;
		ERowAction Action = ERowAction::Buy;
		int32 Index = INDEX_NONE;
		TWeakObjectPtr<AShooterWeapon> Weapon;
		bool bAffordable = true;
	};

	void Rebuild();
	UTurretFeedEntryWidget* AddRow(UPanelWidget* Panel, ERowAction Action, int32 Index);
	void AddOfferRow(UPanelWidget* Panel, ERowAction Action, int32 Index, const FShopOffer& Offer, const FText& Detail);
	void Activate(const FRow& Row);
	int32 FindRowUnder(const FVector2D& ScreenPosition) const;

	UFUNCTION()
	void HandleShopChanged();

	UFUNCTION()
	void HandleWeaponInventoryChanged();

	TArray<FRow> Rows;

	TWeakObjectPtr<AShooterCharacter> BoundCharacter;
	TWeakObjectPtr<UShopComponent> BoundShop;
	TWeakObjectPtr<ABuildableActor> Dispenser;

	bool bIsOpen = false;
};
