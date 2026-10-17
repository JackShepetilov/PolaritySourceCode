// ShopComponent.h
// One player's account at the dispenser shop [author, 2026-10-07]: the credit their fed guns earned,
// the shelf dealt to them from the deck, and the shelf for the guns they carry.
//
// Lives on the player state, like the metal, because the character is the thing that dies. The
// dispenser is only the counter: it is where guns are fed and where the shop may be opened, and it
// names the pool (UDispenserUpgradePool) that is the catalogue. Every player has their own deck and
// shelves, so one player buying does not empty another's shop.
//
//   Credit       magazines fed, at a fixed rate. No curve and no luck: that was the slot machine.
//   Shelf        ShopShelfSize offers off the top of a shuffled deck of everything in the pool. No
//                card comes back until the deck has run out, so the whole catalogue passes through.
//                A restock is paid; there is no free one.
//   WeaponShelf  ShopOffersPerWeapon attachments for each carried gun, always there, dearer by
//                ShopWeaponShelfMarkup. Rarity is a ladder per attachment type on that gun: nothing
//                fitted offers common only, and each fitted tier lets the next one show up.
//
// Server decides everything; the three arrays replicate to the owner only.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Upgrades/UpgradeDefinition.h"
#include "ShopComponent.generated.h"

class ABuildableActor;
class AShooterCharacter;
class AShooterWeapon;
class UDispenserUpgradePool;

UENUM(BlueprintType)
enum class EShopOfferKind : uint8
{
	Upgrade,
	Attachment,
	Buff,
};

/** One thing on a shelf. */
USTRUCT(BlueprintType)
struct FShopOffer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	EShopOfferKind Kind = EShopOfferKind::Buff;

	/** UUpgradeDefinition, UWeaponAttachmentDefinition or UDispenserBuffDefinition, by Kind. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UObject> Definition = nullptr;

	/** Upgrade only: the level this offer brings it to. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	EUpgradeRarity Rarity = EUpgradeRarity::Common;

	/** In magazines of credit. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	float Price = 0.0f;

	/** Weapon shelf only: the hotkey slot of the gun this is for. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	int32 WeaponSlot = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bSold = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShopChanged);

UCLASS(ClassGroup = (Polarity))
class POLARITY_API UShopComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class ABenchLoadoutButton;

public:

	UShopComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The account of the player driving Character, or null (no player state yet). */
	static UShopComponent* Get(const AShooterCharacter* Character);

	// ==================== Reading (any machine that owns it) ====================

	UFUNCTION(BlueprintPure, Category = "Shop")
	float GetCredit() const { return Credit; }

	UFUNCTION(BlueprintPure, Category = "Shop")
	const TArray<FShopOffer>& GetShelf() const { return Shelf; }

	UFUNCTION(BlueprintPure, Category = "Shop")
	const TArray<FShopOffer>& GetWeaponShelf() const { return WeaponShelf; }

	/** "[Rare] Kick push 2" and the like, for a row or the log. */
	UFUNCTION(BlueprintPure, Category = "Shop")
	static FText DescribeOffer(const FShopOffer& Offer);

	/** Fires on the server and on the owning client whenever credit or a shelf changes. */
	UPROPERTY(BlueprintAssignable, Category = "Shop")
	FOnShopChanged OnShopChanged;

	// ==================== Server ====================

	/** A gun was fed to the dispenser. */
	void AddCredit(float Magazines);

	/** The customer walked up: deal the first shelf when there has never been one, and bring the
	 *  weapon shelf in line with the guns carried now. */
	void OpenWith(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer);

	/** Pay ShopRestockPrice for a new shelf off the deck and a new weapon shelf. */
	bool Restock(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer);

	bool Buy(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer, bool bWeaponShelf, int32 Index);

	// The owning client's requests. Each names the dispenser it is standing at: the server checks the
	// reach and takes the catalogue from it.

	UFUNCTION(Server, Reliable)
	void Server_Open(ABuildableActor* Dispenser);

	UFUNCTION(Server, Reliable)
	void Server_Buy(ABuildableActor* Dispenser, bool bWeaponShelf, int32 Index);

	UFUNCTION(Server, Reliable)
	void Server_Restock(ABuildableActor* Dispenser);

protected:

	UPROPERTY(ReplicatedUsing = OnRep_Shop)
	float Credit = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Shop)
	TArray<FShopOffer> Shelf;

	UPROPERTY(ReplicatedUsing = OnRep_Shop)
	TArray<FShopOffer> WeaponShelf;

	UFUNCTION()
	void OnRep_Shop();

private:

	/** What is left of the shuffled catalogue, top at the end. Server. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Deck;

	/** What each weapon slot looked like when its offers were picked: the gun and what is fitted.
	 *  A change is what makes that slot's offers be picked again. */
	TArray<uint32> WeaponSignatures;

	bool bEverStocked = false;

	AShooterCharacter* GetCustomer() const;

	/** The catalogue of the dispenser the customer is standing at, or null (too far, not a shop). */
	const UDispenserUpgradePool* ResolvePool(const ABuildableActor* Dispenser, const AShooterCharacter* Customer) const;

	void RebuildDeck(const UDispenserUpgradePool* Pool);
	void DealShelf(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer);
	void RefreshWeaponShelf(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer, bool bForce);

	/** An offer for one catalogue entry as this customer stands now, or false when it has nothing to
	 *  give them (the upgrade is at its top level, the attachment is owned). */
	bool MakeOffer(const UDispenserUpgradePool* Pool, const AShooterCharacter* Customer, UObject* Definition, FShopOffer& OutOffer) const;

	static uint32 SignatureOf(const AShooterWeapon* Weapon);

	void NotifyChanged();
};
