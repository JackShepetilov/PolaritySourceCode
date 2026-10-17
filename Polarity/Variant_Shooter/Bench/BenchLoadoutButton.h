#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SaveGame.h"
#include "Variant_Shooter/Inventory/InventoryTypes.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/Shop/ShopComponent.h"
#include "BenchLoadoutButton.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UUpgradeDefinition;
class UWeaponAttachmentDefinition;
class UPlayerClassDefinition;
class UGrenadeDefinition;
class UShootableButtonComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EBenchLoadoutAction : uint8
{
	GiveAmmo,
	SavePreset,
	RestorePreset
};

USTRUCT()
struct FBenchWeaponPreset
{
	GENERATED_BODY()
	UPROPERTY() TSubclassOf<AShooterWeapon> WeaponClass;
	UPROPERTY() TArray<TObjectPtr<UWeaponAttachmentDefinition>> Attachments;
	UPROPERTY() TSubclassOf<class ADroppedRangedWeapon> SourceDropClass;
	UPROPERTY() int32 HotkeySlot = INDEX_NONE;
	UPROPERTY() int32 Bullets = 0;
	UPROPERTY() int32 Reserve = 0;
	UPROPERTY() int32 ReserveMagazines = 3;
	UPROPERTY() bool bReserveCopy = false;
	UPROPERTY() bool bYanked = false;
	UPROPERTY() bool bLimitedAmmo = false;
	UPROPERTY() bool bRegenerating = false;
	UPROPERTY() bool bFinite = false;
	UPROPERTY() float DropCharge = 0.0f;
};

/** Separate from run saves. Asset references only; live weapon references are indices. */
UCLASS()
class POLARITY_API UBenchPlayerPreset : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TArray<FBenchWeaponPreset> Weapons;
	UPROPERTY() int32 CurrentWeapon = INDEX_NONE;
	UPROPERTY() TArray<FInventorySlot> Cells;
	UPROPERTY() TArray<int32> CellWeapons;
	UPROPERTY() int32 FreeAttachmentSlots = 0;
	UPROPERTY() int32 MaxAmmoCells = 0;
	UPROPERTY() int32 MaxMedkits = 3;
	UPROPERTY() TArray<FGadgetStack> Gadgets;
	UPROPERTY() int32 GadgetKinds = 2;
	UPROPERTY() int32 GadgetsPerKind = 3;
	UPROPERTY() TArray<TObjectPtr<UUpgradeDefinition>> Upgrades;
	UPROPERTY() TArray<int32> UpgradeLevels;
	UPROPERTY() int32 StoredHealthPickups = 0;
	UPROPERTY() TArray<FAbilitySlotState> Abilities;
	UPROPERTY() int32 ActiveAbility = INDEX_NONE;
	UPROPERTY() TObjectPtr<UAbilityDefinition> Passive;
	UPROPERTY() float CooldownRemaining = 0.0f;
	UPROPERTY() float CooldownTotal = 0.0f;
	UPROPERTY() TObjectPtr<UPlayerClassDefinition> PlayerClass;
	UPROPERTY() TSubclassOf<AShooterWeapon> StartingWeapon;
	UPROPERTY() TObjectPtr<UGrenadeDefinition> SelectedGrenade;
	UPROPERTY() float Health = 100.0f;
	UPROPERTY() float MaxHealth = 100.0f;
	UPROPERTY() float Armor = 0.0f;
	UPROPERTY() float MaxArmor = 100.0f;
	UPROPERTY() float Charge = 0.0f;
	UPROPERTY() int32 Metal = 0;
	UPROPERTY() int32 MaxMetal = 200;
	UPROPERTY() float TacticalCharge = 1.0f;
	UPROPERTY() float TacticalRechargeWait = 0.0f;
	UPROPERTY() float ShopCredit = 0.0f;
	UPROPERTY() TArray<FShopOffer> ShopShelf;
	UPROPERTY() TArray<FShopOffer> ShopWeaponShelf;
	UPROPERTY() TArray<TObjectPtr<UObject>> ShopDeck;
	UPROPERTY() bool bShopStocked = false;
};

/** Shoot to operate on the player behind the shot. Presets survive death and PIE restarts. */
UCLASS()
class POLARITY_API ABenchLoadoutButton : public AActor
{
	GENERATED_BODY()
public:
	ABenchLoadoutButton();
	static bool HasHubSpawn(UWorld* World);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bench")
	EBenchLoadoutAction Action = EBenchLoadoutAction::GiveAmmo;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Bench")
	bool ExecuteForPlayer(AShooterCharacter* Player);
	bool SaveReportedPreset(AShooterCharacter* Player, const TArray<AShooterWeapon*>& Weapons, const TArray<int32>& Bullets);
	virtual void OnConstruction(const FTransform& Transform) override;
protected:
	virtual void BeginPlay() override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UShootableButtonComponent> Button;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
private:
	UFUNCTION() void HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator);
	bool SavePreset(AShooterCharacter* Player, const TArray<AShooterWeapon*>& ReportedWeapons, const TArray<int32>& ReportedBullets);
	bool RestorePreset(AShooterCharacter* Player);
	static FString PresetSlot(const AShooterCharacter* Player);
	void UpdateLabel();
};
