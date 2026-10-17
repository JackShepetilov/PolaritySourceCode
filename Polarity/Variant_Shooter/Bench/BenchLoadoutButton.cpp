#include "Variant_Shooter/Bench/BenchLoadoutButton.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Coop/CoopPlayers.h"
#include "EMFVelocityModifier.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Upgrades/UpgradeDefinition.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/Grenades/GrenadeComponent.h"
#include "Variant_Shooter/Health/FieldCareComponent.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShootableButtonComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/Tactical/TacticalDeviceComponent.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

ABenchLoadoutButton::ABenchLoadoutButton()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	Button = CreateDefaultSubobject<UShootableButtonComponent>(TEXT("Button"));
	Button->SetupAttachment(SceneRoot);
	Button->bAutoDisableOnPress = false;
	Button->ActivationCooldown = 0.8f;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded() && Button->GetButtonMesh())
	{
		Button->GetButtonMesh()->SetStaticMesh(Cube.Object);
		Button->GetButtonMesh()->SetRelativeScale3D(FVector(0.15f, 0.5f, 0.5f));
	}
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(10.0f, 0.0f, 40.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(17.0f);
	Label->SetTextRenderColor(FColor(180, 255, 180));
}

bool ABenchLoadoutButton::HasHubSpawn(UWorld* World)
{
	if (!World) return false;
	TArray<AActor*> Starts;
	UGameplayStatics::GetAllActorsOfClass(World, APlayerStart::StaticClass(), Starts);
	return Starts.ContainsByPredicate([](const AActor* Start) { return Start->ActorHasTag(TEXT("CombatBenchHubSpawn")); });
}

void ABenchLoadoutButton::UpdateLabel()
{
	const TCHAR* Title = Action == EBenchLoadoutAction::GiveAmmo ? TEXT("+20 MAGAZINES\nCURRENT WEAPON")
		: Action == EBenchLoadoutAction::SavePreset ? TEXT("SAVE PRESET") : TEXT("RESTORE SAVED PRESET");
	Label->SetText(FText::FromString(Title));
}

void ABenchLoadoutButton::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateLabel();
}

void ABenchLoadoutButton::BeginPlay()
{
	Super::BeginPlay();
	UpdateLabel();
	if (HasAuthority())
	{
		Button->OnButtonPressed.AddDynamic(this, &ABenchLoadoutButton::HandleButtonPressed);
	}
}

void ABenchLoadoutButton::HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator)
{
	APawn* Pawn = Cast<APawn>(Activator);
	if (!Pawn && Activator) Pawn = Activator->GetInstigator();
	if (!Pawn && Activator) Pawn = Cast<APawn>(Activator->GetOwner());
	AShooterCharacter* Player = CoopPlayers::IsPlayer(Pawn) ? Cast<AShooterCharacter>(Pawn) : nullptr;
	if (!HasAuthority() || !Player) return;

	// Hitscan is still inside Fire(), and a projectile still inside ApplyWeaponHit(). Never destroy
	// that weapon on its own call stack. The second tick also lets RefundRoundForButton finish.
	const TWeakObjectPtr<AShooterCharacter> WeakPlayer(Player);
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]()
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]()
		{
			if (WeakPlayer.IsValid()) ExecuteForPlayer(WeakPlayer.Get());
		}));
	}));
}

FString ABenchLoadoutButton::PresetSlot(const AShooterCharacter* Player)
{
	const APlayerState* PlayerState = Player->GetPlayerState();
	FString Identity;
	if (const APlayerController* PC = Cast<APlayerController>(Player->GetController()); PC && PC->GetLocalPlayer())
	{
		Identity = FString::Printf(TEXT("local_%d"), PC->GetLocalPlayer()->GetControllerId());
	}
	else if (PlayerState && PlayerState->GetUniqueId().IsValid())
	{
		Identity = PlayerState->GetUniqueId().ToString();
	}
	else
	{
		Identity = FString::Printf(TEXT("peer_%d"), PlayerState ? PlayerState->GetPlayerId() : -1);
	}
	return FString::Printf(TEXT("CombatBenchPreset_%08x"), FCrc::StrCrc32(*Identity));
}

bool ABenchLoadoutButton::ExecuteForPlayer(AShooterCharacter* Player)
{
	if (!HasAuthority() || !IsValid(Player) || !Player->HasAuthority() || !CoopPlayers::IsPlayer(Player)
		|| Player->IsDead() || Player->IsDowned()) return false;
	bool bSuccess = false;
	if (Action == EBenchLoadoutAction::SavePreset)
	{
		if (Player->IsLocallyControlled()) bSuccess = SavePreset(Player, {}, {});
		else
		{
			// Remote magazines are predicted on the owner. The server's copy is stale after firing.
			Player->Client_SaveBenchPreset(this);
			return true;
		}
	}
	else if (Action == EBenchLoadoutAction::RestorePreset) bSuccess = RestorePreset(Player);
	else if (AShooterWeapon* Weapon = Player->GetCurrentWeapon(); Weapon && !Weapon->IsMeleeWeapon())
	{
		const int32 Rounds = 20 * FMath::Max(1, Weapon->GetMagazineSize());
		if (Weapon->UsesEnergyReserve())
		{
			// A regenerating reserve otherwise clamps the requested twenty magazines to three.
			Weapon->EnergyReserveMagazines = FMath::Max(Weapon->EnergyReserveMagazines,
				FMath::DivideAndRoundUp(Weapon->GetEnergyReserve() + Rounds, FMath::Max(1, Weapon->GetMagazineSize())));
			Weapon->SetEnergyReserve(Weapon->GetEnergyReserve() + Rounds);
			bSuccess = true;
		}
		else if (Weapon->OwnsAmmoCells())
		{
			if (UInventoryComponent* Inv = Player->GetInventoryComponent())
			{
				Inv->MaxAmmoCells = FMath::Max(Inv->MaxAmmoCells,
					FMath::DivideAndRoundUp(Inv->GetAmmo() + Rounds, Inv->GetRoundsPerAmmoCell()));
				FInventoryItem Ammo;
				Ammo.Kind = EInventorySlotKind::Ammo;
				Ammo.Count = Rounds;
				Ammo.StackMax = Inv->GetRoundsPerAmmoCell();
				bSuccess = Inv->TryAdd(Ammo) == 0;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[BENCH] +20 magazines for %s: weapon=%s rounds=%d reserve=%d"),
			*Player->GetName(), *Weapon->GetName(), Rounds, Weapon->GetEnergyReserve());
	}
	UE_LOG(LogTemp, Log, TEXT("[BENCH] loadout action=%d player=%s success=%d"), int32(Action), *Player->GetName(), bSuccess);
	return bSuccess;
}

bool ABenchLoadoutButton::SaveReportedPreset(AShooterCharacter* Player, const TArray<AShooterWeapon*>& Weapons, const TArray<int32>& Bullets)
{
	return HasAuthority() && Action == EBenchLoadoutAction::SavePreset && IsValid(Player)
		&& Player->HasAuthority() && !Player->IsDead() && CoopPlayers::IsPlayer(Player)
		&& Weapons.Num() == Bullets.Num() && Weapons.Num() <= 16 && SavePreset(Player, Weapons, Bullets);
}

bool ABenchLoadoutButton::SavePreset(AShooterCharacter* Player, const TArray<AShooterWeapon*>& ReportedWeapons, const TArray<int32>& ReportedBullets)
{
	UBenchPlayerPreset* Preset = Cast<UBenchPlayerPreset>(UGameplayStatics::CreateSaveGameObject(UBenchPlayerPreset::StaticClass()));
	if (!Preset) return false;
	TArray<AShooterWeapon*> AllWeapons = Player->OwnedWeapons;
	AllWeapons.Append(Player->ReserveWeapons);
	for (AShooterWeapon* Weapon : AllWeapons)
	{
		if (!IsValid(Weapon)) return false;
		FBenchWeaponPreset& Saved = Preset->Weapons.AddDefaulted_GetRef();
		Saved.WeaponClass = Weapon->GetClass();
		Saved.Attachments = Weapon->GetInstalledAttachments();
		Saved.SourceDropClass = Weapon->SourceYankDropClass;
		Saved.HotkeySlot = Weapon->GetHotkeySlot();
		Saved.Bullets = Weapon->GetBulletCount();
		const int32 ReportedIndex = ReportedWeapons.IndexOfByKey(Weapon);
		if (ReportedBullets.IsValidIndex(ReportedIndex)) Saved.Bullets = FMath::Clamp(ReportedBullets[ReportedIndex], 0, Weapon->GetMagazineSize());
		Saved.Reserve = Weapon->GetEnergyReserve();
		Saved.ReserveMagazines = Weapon->EnergyReserveMagazines;
		Saved.bReserveCopy = Player->ReserveWeapons.Contains(Weapon);
		Saved.bYanked = Weapon->bWasYanked;
		Saved.bLimitedAmmo = Weapon->bHasLimitedAmmo;
		Saved.bRegenerating = Weapon->bRegeneratingReserve;
		Saved.bFinite = Weapon->bFiniteEnergyReserve;
		Saved.DropCharge = Weapon->SourceDropCharge;
	}
	Preset->CurrentWeapon = AllWeapons.IndexOfByKey(Player->CurrentWeapon.Get());
	if (UInventoryComponent* Inv = Player->GetInventoryComponent())
	{
		Preset->Cells = Inv->Slots;
		for (FInventorySlot& Cell : Preset->Cells)
		{
			Preset->CellWeapons.Add(AllWeapons.IndexOfByKey(Cell.InstalledOnWeapon.Get()));
			Cell.InstalledOnWeapon = nullptr;
		}
		Preset->FreeAttachmentSlots = Inv->FreeAttachmentSlots;
		Preset->MaxAmmoCells = Inv->MaxAmmoCells;
		Preset->MaxMedkits = Inv->MaxMedkits;
		Preset->Gadgets = Inv->Gadgets;
		Preset->GadgetKinds = Inv->GadgetKinds;
		Preset->GadgetsPerKind = Inv->GadgetsPerKind;
	}
	if (UUpgradeManagerComponent* Upgrades = Player->GetUpgradeManager())
	{
		for (UUpgradeDefinition* Def : Upgrades->GetAcquiredUpgrades())
		{
			Preset->Upgrades.Add(Def);
			Preset->UpgradeLevels.Add(Upgrades->GetUpgradeLevel(Def->UpgradeTag));
		}
		Preset->StoredHealthPickups = Upgrades->StoredHealthPickups;
	}
	if (UAbilityComponent* Abilities = Player->GetAbilityComponent())
	{
		Preset->Abilities = Abilities->ReplicatedSlots;
		Preset->ActiveAbility = Abilities->ActiveSlotIndex;
		Preset->Passive = Abilities->GetPassiveAbility();
		Preset->CooldownRemaining = Abilities->GetCooldownRemaining();
		Preset->CooldownTotal = Abilities->GetCooldownTotal();
	}
	if (UGrenadeComponent* Grenades = Player->FindComponentByClass<UGrenadeComponent>()) Preset->SelectedGrenade = Grenades->GetSelectedType();
	Preset->PlayerClass = Player->ClassDefinition;
	Preset->StartingWeapon = Player->StartingWeaponClass;
	Preset->Health = Player->GetCurrentHP();
	Preset->MaxHealth = Player->GetMaxHP();
	Preset->Armor = Player->GetCurrentArmor();
	Preset->MaxArmor = Player->GetMaxArmor();
	if (UEMFVelocityModifier* EMF = Player->FindComponentByClass<UEMFVelocityModifier>()) Preset->Charge = EMF->GetCharge();
	if (AShooterPlayerState* PS = Player->GetPlayerState<AShooterPlayerState>())
	{
		Preset->Metal = PS->GetMetal();
		Preset->MaxMetal = PS->GetMaxMetal();
	}
	if (UTacticalDeviceComponent* Tactical = Player->FindComponentByClass<UTacticalDeviceComponent>())
	{
		Preset->TacticalCharge = Tactical->Charge;
		Preset->TacticalRechargeWait = Tactical->RechargeWait;
	}
	if (UShopComponent* Shop = UShopComponent::Get(Player))
	{
		Preset->ShopCredit = Shop->Credit;
		Preset->ShopShelf = Shop->Shelf;
		Preset->ShopWeaponShelf = Shop->WeaponShelf;
		Preset->ShopDeck = Shop->Deck;
		Preset->bShopStocked = Shop->bEverStocked;
	}
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Preset, PresetSlot(Player), 0);
	UE_LOG(LogTemp, Log, TEXT("[BENCH] preset saved: %s weapons=%d cells=%d upgrades=%d hp=%.1f armor=%.1f disk=%d"),
		*Player->GetName(), Preset->Weapons.Num(), Preset->Cells.Num(), Preset->Upgrades.Num(), Preset->Health, Preset->Armor, bSaved);
	return bSaved;
}

bool ABenchLoadoutButton::RestorePreset(AShooterCharacter* Player)
{
	UBenchPlayerPreset* Preset = Cast<UBenchPlayerPreset>(UGameplayStatics::LoadGameFromSlot(PresetSlot(Player), 0));
	if (!Preset || Preset->Version != 1 || Preset->Cells.Num() != Preset->CellWeapons.Num()
		|| Preset->Upgrades.Num() != Preset->UpgradeLevels.Num()) return false;

	// Stage every actor before touching the current inventory. A missing class leaves it intact.
	TArray<AShooterWeapon*> Restored;
	FActorSpawnParameters Params;
	Params.Owner = Player;
	Params.Instigator = Player;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;
	for (const FBenchWeaponPreset& Saved : Preset->Weapons)
	{
		AShooterWeapon* Weapon = Saved.WeaponClass ? GetWorld()->SpawnActor<AShooterWeapon>(Saved.WeaponClass, Player->GetActorTransform(), Params) : nullptr;
		if (!Weapon)
		{
			for (AShooterWeapon* Staged : Restored) Staged->Destroy();
			return false;
		}
		Weapon->DeactivateWeapon();
		Restored.Add(Weapon);
	}
	Player->Client_ResetBenchLoadoutPresentation();
	if (UFieldCareComponent* Care = Player->FindComponentByClass<UFieldCareComponent>()) Care->Cancel();
	if (UGrenadeComponent* Grenades = Player->FindComponentByClass<UGrenadeComponent>()) Grenades->CancelThrow();
	if (UAbilityComponent* Abilities = Player->GetAbilityComponent()) Abilities->CancelCast();
	GetWorldTimerManager().ClearTimer(Player->WeaponSwitchTimer);
	GetWorldTimerManager().ClearTimer(Player->ReloadResumeTimer);
	Player->PendingWeapon = nullptr;
	Player->WeaponSwitchPhase = EWeaponSwitchPhase::None;
	TArray<AShooterWeapon*> OldWeapons = Player->OwnedWeapons;
	OldWeapons.Append(Player->ReserveWeapons);
	if (Player->CurrentWeapon) Player->CurrentWeapon->DeactivateWeapon();
	if (UUpgradeManagerComponent* Upgrades = Player->GetUpgradeManager()) Upgrades->NotifyWeaponChanged(Player->CurrentWeapon, nullptr);
	Player->CurrentWeapon = nullptr;
	Player->OwnedWeapons.Reset();
	Player->ReserveWeapons.Reset();
	for (AShooterWeapon* Weapon : OldWeapons) if (IsValid(Weapon)) Weapon->Destroy();
	if (UInventoryComponent* Inv = Player->GetInventoryComponent()) { Inv->Slots.Reset(); Inv->Gadgets.Reset(); }
	Player->ClassDefinition = Preset->PlayerClass;
	Player->ApplyClassDefinition();
	Player->StartingWeaponClass = Preset->StartingWeapon;
	if (UUpgradeManagerComponent* Upgrades = Player->GetUpgradeManager())
	{
		for (UUpgradeDefinition* Def : Upgrades->GetAcquiredUpgrades()) Upgrades->SetUpgradeLevelEverywhere(Def, 0);
		for (int32 i = 0; i < Preset->Upgrades.Num(); ++i) Upgrades->SetUpgradeLevelEverywhere(Preset->Upgrades[i], Preset->UpgradeLevels[i]);
		Upgrades->StoredHealthPickups = Preset->StoredHealthPickups;
		Upgrades->OnStoredHealthPickupsChanged.Broadcast(Upgrades->StoredHealthPickups, Upgrades->MaxStoredHealthPickups);
	}
	for (int32 i = 0; i < Restored.Num(); ++i)
	{
		AShooterWeapon* Weapon = Restored[i];
		const FBenchWeaponPreset& Saved = Preset->Weapons[i];
		Player->OnWeaponJoinedInventory(Weapon);
		Weapon->bRegeneratingReserve = Saved.bRegenerating;
		Weapon->bFiniteEnergyReserve = Saved.bFinite;
		Weapon->EnergyReserveMagazines = Saved.ReserveMagazines;
		Weapon->bWasYanked = Saved.bYanked;
		Weapon->bHasLimitedAmmo = Saved.bLimitedAmmo;
		Weapon->SourceYankDropClass = Saved.SourceDropClass;
		Weapon->SourceDropCharge = Saved.DropCharge;
		Weapon->SetHotkeySlot(Saved.HotkeySlot);
		Weapon->InstalledAttachments = Saved.Attachments;
		Weapon->OnRep_InstalledAttachments();
		Weapon->SetBulletCount(Saved.Bullets);
		Weapon->SetEnergyReserve(Saved.Reserve);
		if (Saved.bReserveCopy)
		{
			Player->ReserveWeapons.Add(Weapon);
			Weapon->SetActorHiddenInGame(true);
		}
		else Player->OwnedWeapons.Add(Weapon);
		Weapon->ForceNetUpdate();
	}
	if (UInventoryComponent* Inv = Player->GetInventoryComponent())
	{
		Inv->Slots = Preset->Cells;
		Inv->FreeAttachmentSlots = Preset->FreeAttachmentSlots;
		Inv->MaxAmmoCells = Preset->MaxAmmoCells;
		Inv->MaxMedkits = Preset->MaxMedkits;
		Inv->GadgetKinds = Preset->GadgetKinds;
		Inv->GadgetsPerKind = Preset->GadgetsPerKind;
		Inv->Gadgets = Preset->Gadgets;
		for (int32 i = 0; i < Inv->Slots.Num(); ++i)
		{
			const int32 WeaponIndex = Preset->CellWeapons[i];
			Inv->Slots[i].InstalledOnWeapon = Restored.IsValidIndex(WeaponIndex) ? Restored[WeaponIndex] : nullptr;
		}
		// A preset saved before 2026-10-16 has its grenades in cells: move them to the gadget section.
		for (FInventorySlot& Cell : Inv->Slots)
		{
			if (Cell.Kind == EInventorySlotKind::Grenade)
			{
				FInventoryItem Old;
				Old.Kind = EInventorySlotKind::Grenade;
				Old.Payload = Cell.Payload;
				Old.Count = FMath::Max(1, Cell.Count);
				Old.PickupClass = Cell.PickupClass;
				Inv->TryAdd(Old);
				Cell = FInventorySlot();
			}
		}
		Inv->OnRep_Slots();
	}
	if (UAbilityComponent* Abilities = Player->GetAbilityComponent())
	{
		Abilities->GrantPassive(Preset->Passive);
		Abilities->ReplicatedSlots = Preset->Abilities;
		Abilities->ActiveSlotIndex = Preset->ActiveAbility;
		Abilities->OnRep_ReplicatedSlots();
		Abilities->OnRep_ActiveSlotIndex();
		Abilities->CooldownTimeRemaining = Preset->CooldownRemaining;
		Abilities->CooldownTotal = Preset->CooldownTotal;
		Abilities->OnCooldownAdjusted.Broadcast(Preset->CooldownRemaining, Preset->CooldownTotal);
	}
	if (UGrenadeComponent* Grenades = Player->FindComponentByClass<UGrenadeComponent>())
	{
		Grenades->SelectedType = Preset->SelectedGrenade;
		Grenades->RefreshSelection();
	}
	Player->MaxHP = Preset->MaxHealth;
	Player->CurrentHP = Preset->Health;
	Player->MaxArmor = Preset->MaxArmor;
	Player->CurrentArmor = Preset->Armor;
	Player->BroadcastHealthChanged();
	if (UEMFVelocityModifier* EMF = Player->FindComponentByClass<UEMFVelocityModifier>()) EMF->SetCharge(Preset->Charge);
	if (AShooterPlayerState* PS = Player->GetPlayerState<AShooterPlayerState>())
	{
		PS->MaxMetal = Preset->MaxMetal;
		const int32 Difference = Preset->Metal - PS->GetMetal();
		if (Difference > 0) PS->AddMetal(Difference);
		else if (Difference < 0) PS->TrySpendMetal(-Difference);
	}
	if (Restored.IsValidIndex(Preset->CurrentWeapon)) Player->EquipWeaponImmediate(Restored[Preset->CurrentWeapon]);
	else Player->OnActiveWeaponChanged.Broadcast(nullptr);
	if (UTacticalDeviceComponent* Tactical = Player->FindComponentByClass<UTacticalDeviceComponent>())
	{
		Tactical->Charge = Preset->TacticalCharge;
		Tactical->RechargeWait = Preset->TacticalRechargeWait;
	}
	if (UShopComponent* Shop = UShopComponent::Get(Player))
	{
		Shop->Credit = Preset->ShopCredit;
		Shop->Shelf = Preset->ShopShelf;
		Shop->WeaponShelf = Preset->ShopWeaponShelf;
		Shop->Deck = Preset->ShopDeck;
		Shop->bEverStocked = Preset->bShopStocked;
		Shop->WeaponSignatures.Reset();
		for (int32 Slot = 0; Slot < 2; ++Slot) Shop->WeaponSignatures.Add(UShopComponent::SignatureOf(Player->FindOwnedWeaponInHotkeySlot(Slot)));
		Shop->NotifyChanged();
	}
	Player->OnWeaponInventoryChanged.Broadcast();
	Player->UpdateFirstPersonMeshVisibility();
	Player->ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("[BENCH] preset restored: %s weapons=%d cells=%d upgrades=%d hp=%.1f armor=%.1f"),
		*Player->GetName(), Restored.Num(), Preset->Cells.Num(), Preset->Upgrades.Num(), Preset->Health, Preset->Armor);
	return true;
}
