// DispenserSlotMachineComponent.cpp

#include "DispenserSlotMachineComponent.h"

#include "BuildableActor.h"
#include "BuilderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "DispenserCardPickup.h"
#include "DispenserMachineAnimInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Polarity/Upgrades/DispenserUpgradePool.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "TimerManager.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Pickups/AttachmentPickup.h"
#include "Variant_Shooter/Pickups/UpgradePickup.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

static TAutoConsoleVariable<int32> CVarCasinoFair(
	TEXT("polarity.casino.fair"),
	0,
	TEXT("1: an upgrade reel's rarity is the stake value itself (multiplier 1), to compare against the casino."),
	ECVF_Cheat);

namespace SlotMachine
{
	// Cells of the reel symbol strip (Docs/Dispenser_SlotMachine_3D_Handoff_2026-09-25.md, 2.3).
	constexpr uint8 SymbolBlank = 0;
	constexpr uint8 SymbolBuff = 1;
	constexpr uint8 SymbolFirstRarity = 2;

	void SeedLinear(FRuntimeFloatCurve& Target, std::initializer_list<TPair<float, float>> Keys)
	{
		if (FRichCurve* const Curve = Target.GetRichCurve())
		{
			for (const TPair<float, float>& Key : Keys)
			{
				Curve->SetKeyInterpMode(Curve->AddKey(Key.Key, Key.Value), RCIM_Linear);
			}
		}
	}

	float Eval(const FRuntimeFloatCurve& Source, float X, float Fallback)
	{
		const FRichCurve* const Curve = Source.GetRichCurveConst();
		return (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(X) : Fallback;
	}

	/** Weighted draw among Candidates, Weights[i] being Candidates[i]'s weight (all above zero). */
	int32 DrawEntry(const TArray<int32>& Candidates, const TArray<float>& Weights)
	{
		float Total = 0.0f;
		for (const float Weight : Weights)
		{
			Total += Weight;
		}
		float Pick = FMath::FRand() * Total;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			Pick -= Weights[i];
			if (Pick <= 0.0f)
			{
				return Candidates[i];
			}
		}
		return Candidates.Num() > 0 ? Candidates.Last() : INDEX_NONE;
	}
}

UDispenserSlotMachineComponent::UDispenserSlotMachineComponent()
{
	// Ticks only while the model moves (UpdateModel), and only where there is a model.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);

	// The names of the slot machine model (Docs/Handoff_SlotMachine_MeshHookup_2026-09-25.md).
	LidBones = {FName(TEXT("lid_0")), FName(TEXT("lid_1")), FName(TEXT("lid_2"))};
	FlapBone = FName(TEXT("intake_flap"));
	ReelSlots = {FName(TEXT("Reel_0")), FName(TEXT("Reel_1")), FName(TEXT("Reel_2"))};
	ReelStopSeconds = {1.2f, 1.6f, 2.0f};

	UpgradePool = TSoftObjectPtr<UDispenserUpgradePool>(
		FSoftObjectPath(TEXT("/Game/Variant_Shooter/Blueprints/Upgrades/DA_DispenserUpgradePool.DA_DispenserUpgradePool")));
	CardPickupClass = ADispenserCardPickup::StaticClass();
	BoxSockets = {FName(TEXT("Box_0_Item")), FName(TEXT("Box_1_Item")), FName(TEXT("Box_2_Item"))};

	// The author's shape (2026-09-25): two magazines next to nothing, 3-5 decent, 6-9 good, 9+
	// very good, barely growing after that.
	SlotMachine::SeedLinear(StakeCurve, {{0.0f, 0.0f}, {1.0f, 5.0f}, {2.0f, 10.0f}, {3.0f, 40.0f}, {5.0f, 60.0f},
		{6.0f, 90.0f}, {9.0f, 140.0f}, {12.0f, 160.0f}, {20.0f, 180.0f}, {40.0f, 200.0f}});

	// A blank is always possible, at nine magazines too (the rage bait is part of the game). An
	// empty gun (stake 0) is a certain blank.
	SlotMachine::SeedLinear(DudChanceCurve, {{0.0f, 1.0f}, {5.0f, 0.45f}, {10.0f, 0.45f}, {40.0f, 0.30f},
		{60.0f, 0.30f}, {140.0f, 0.10f}, {200.0f, 0.08f}});
	SlotMachine::SeedLinear(BuffChanceCurve, {{0.0f, 0.0f}, {5.0f, 0.45f}, {10.0f, 0.45f}, {40.0f, 0.30f},
		{60.0f, 0.30f}, {140.0f, 0.15f}, {200.0f, 0.12f}});

	// Rarity multiplier: 30% of rolls nothing, the middle around 1, the last 2% up to x10.
	SlotMachine::SeedLinear(PayoutCurve, {{0.0f, 0.0f}, {0.30f, 0.0f}, {0.55f, 0.6f}, {0.75f, 1.3f}, {0.90f, 2.5f},
		{0.98f, 4.5f}, {1.0f, 10.0f}});
}

void UDispenserSlotMachineComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UDispenserSlotMachineComponent, Spin);
}

void UDispenserSlotMachineComponent::BeginPlay()
{
	Super::BeginPlay();
	SetUpModel();
}

void UDispenserSlotMachineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhaseTimer);
	}
	DestroyItems();
	Super::EndPlay(EndPlayReason);
}

FLinearColor UDispenserSlotMachineComponent::RarityColor(EUpgradeRarity Rarity)
{
	switch (Rarity)
	{
	case EUpgradeRarity::Rare:      return FLinearColor(0.15f, 0.45f, 1.0f);
	case EUpgradeRarity::Epic:      return FLinearColor(0.65f, 0.2f, 1.0f);
	case EUpgradeRarity::Legendary: return FLinearColor(1.0f, 0.6f, 0.05f);
	default:                        return FLinearColor(0.85f, 0.85f, 0.85f);
	}
}

FLinearColor UDispenserSlotMachineComponent::CardColor(const FDispenserCard& Card)
{
	switch (Card.Outcome)
	{
	case EDispenserCardOutcome::Dud:  return FLinearColor(0.35f, 0.35f, 0.35f);
	case EDispenserCardOutcome::Buff: return FLinearColor(0.2f, 0.9f, 0.35f);
	default:                          return RarityColor(Card.Rarity);
	}
}

bool UDispenserSlotMachineComponent::IsAllBlanks() const
{
	for (const FDispenserCard& Card : Spin.Cards)
	{
		if (Card.Outcome != EDispenserCardOutcome::Dud)
		{
			return false;
		}
	}
	return true;
}

const UDispenserUpgradePool* UDispenserSlotMachineComponent::GetPool() const
{
	return UpgradePool.LoadSynchronous();
}

// ==================== Maths ====================

float UDispenserSlotMachineComponent::EvaluateStakeValue(float Magazines) const
{
	return FMath::Max(0.0f, SlotMachine::Eval(StakeCurve, FMath::Max(0.0f, Magazines), Magazines * 15.0f));
}

float UDispenserSlotMachineComponent::EvaluatePayoutMultiplier(float Roll) const
{
	return FMath::Max(0.0f, SlotMachine::Eval(PayoutCurve, FMath::Clamp(Roll, 0.0f, 1.0f), 1.0f));
}

EUpgradeRarity UDispenserSlotMachineComponent::RarityForValue(float Value) const
{
	if (Value >= LegendaryValue)
	{
		return EUpgradeRarity::Legendary;
	}
	if (Value >= EpicValue)
	{
		return EUpgradeRarity::Epic;
	}
	if (Value >= RareValue)
	{
		return EUpgradeRarity::Rare;
	}
	return EUpgradeRarity::Common;
}

int32 UDispenserSlotMachineComponent::RankOf(const FDispenserCard& Card)
{
	switch (Card.Outcome)
	{
	case EDispenserCardOutcome::Buff:       return 1;
	case EDispenserCardOutcome::Upgrade:
	case EDispenserCardOutcome::Attachment: return 2 + static_cast<int32>(Card.Rarity);
	default:                             return 0;
	}
}

void UDispenserSlotMachineComponent::RollCard(float StakeValue, const UDispenserUpgradePool* Pool,
	const AShooterCharacter* Player, TArray<const UUpgradeDefinition*>& UsedUpgrades, TArray<int32>& UsedAttachments,
	FDispenserCard& OutCard) const
{
	const UUpgradeManagerComponent* const Upgrades = Player ? Player->GetUpgradeManager() : nullptr;
	OutCard = FDispenserCard();

	const float DudChance = FMath::Clamp(SlotMachine::Eval(DudChanceCurve, StakeValue, 0.3f), 0.0f, 1.0f);
	const float BuffChance = FMath::Clamp(SlotMachine::Eval(BuffChanceCurve, StakeValue, 0.3f), 0.0f, 1.0f - DudChance);
	const float Outcome = FMath::FRand();
	if (Outcome < DudChance)
	{
		OutCard.Symbol = SlotMachine::SymbolBlank;
		return;
	}

	// A prize reel: its rarity from the stake and the payout curve, then the best thing the pool
	// has for this player at no more than that rarity, an upgrade level or an attachment, whichever
	// is rarer (a coin decides a tie). Nothing left for them turns it into a buff, not a blank:
	// running out of prizes is not the gamble.
	if (Outcome >= DudChance + BuffChance && Pool)
	{
		const bool bFair = CVarCasinoFair.GetValueOnAnyThread() != 0;
		const EUpgradeRarity Rolled = RarityForValue(StakeValue * (bFair ? 1.0f : EvaluatePayoutMultiplier(FMath::FRand())));

		FUpgradeOfferCard Up;
		const bool bUpgrade = Upgrades && Upgrades->BuildUpgradeCard(Pool, Rolled, UsedUpgrades, Up);

		// The rarest attachments that fit under the roll, drawn by weight. Any attachment, not only
		// ones for the guns in hand. A family (Mag.Light) only offers a tier above the best of that
		// family the player has; a thing already owned may carry its own, lower weight.
		int32 Attachment = INDEX_NONE;
		{
			TArray<const UWeaponAttachmentDefinition*> Owned;
			GatherOwnedAttachments(Player, Owned);

			EUpgradeRarity Top = EUpgradeRarity::Common;
			TArray<int32> Candidates;
			TArray<float> Weights;
			for (int32 Index = 0; Index < Pool->Attachments.Num(); ++Index)
			{
				const FDispenserAttachmentEntry& Entry = Pool->Attachments[Index];
				const UWeaponAttachmentDefinition* const Definition = Entry.Attachment;
				if (!Definition || Definition->Rarity > Rolled || UsedAttachments.Contains(Index))
				{
					continue;
				}
				if (Definition->Family != NAME_None)
				{
					bool bHasFamily = false;
					EUpgradeRarity BestOwned = EUpgradeRarity::Common;
					for (const UWeaponAttachmentDefinition* const Have : Owned)
					{
						if (Have && Have->Family == Definition->Family && (!bHasFamily || Have->Rarity > BestOwned))
						{
							BestOwned = Have->Rarity;
							bHasFamily = true;
						}
					}
					if (bHasFamily && Definition->Rarity <= BestOwned)
					{
						continue;
					}
				}
				const float Weight = Entry.bOverrideChanceWhenAlreadyEquipped && Owned.Contains(Definition)
					? Entry.WeightWhenEquipped : Entry.Weight;
				if (Weight <= 0.0f)
				{
					continue;
				}
				if (Candidates.Num() == 0 || Definition->Rarity > Top)
				{
					Candidates.Reset();
					Weights.Reset();
					Top = Definition->Rarity;
				}
				if (Definition->Rarity == Top)
				{
					Candidates.Add(Index);
					Weights.Add(Weight);
				}
			}
			Attachment = SlotMachine::DrawEntry(Candidates, Weights);
		}

		bool bTakeAttachment = Attachment != INDEX_NONE;
		const EUpgradeRarity AttachmentRarity = bTakeAttachment ? Pool->Attachments[Attachment].Attachment->Rarity : EUpgradeRarity::Common;
		if (bUpgrade && bTakeAttachment)
		{
			bTakeAttachment = AttachmentRarity > Up.Rarity || (AttachmentRarity == Up.Rarity && FMath::RandBool());
		}

		if (bTakeAttachment)
		{
			OutCard.Outcome = EDispenserCardOutcome::Attachment;
			OutCard.PickupIndex = Attachment;
			OutCard.Rarity = AttachmentRarity;
			OutCard.Symbol = SlotMachine::SymbolFirstRarity + static_cast<uint8>(OutCard.Rarity);
			UsedAttachments.Add(Attachment);
			return;
		}
		if (bUpgrade)
		{
			OutCard.Outcome = EDispenserCardOutcome::Upgrade;
			OutCard.Upgrade = Up;
			OutCard.Rarity = Up.Rarity;
			OutCard.Symbol = SlotMachine::SymbolFirstRarity + static_cast<uint8>(OutCard.Rarity);
			UsedUpgrades.Add(Up.Definition);
			return;
		}
	}

	// A buff, drawn by weight among the pool's buffs. None authored: the reel stays blank.
	if (Pool)
	{
		TArray<int32> Candidates;
		TArray<float> Weights;
		for (int32 Index = 0; Index < Pool->Buffs.Num(); ++Index)
		{
			if (Pool->Buffs[Index].Buff && Pool->Buffs[Index].Weight > 0.0f)
			{
				Candidates.Add(Index);
				Weights.Add(Pool->Buffs[Index].Weight);
			}
		}
		const int32 Buff = SlotMachine::DrawEntry(Candidates, Weights);
		if (Buff != INDEX_NONE)
		{
			OutCard.Outcome = EDispenserCardOutcome::Buff;
			OutCard.PickupIndex = Buff;
			OutCard.Symbol = SlotMachine::SymbolBuff;
			return;
		}
	}
	OutCard.Symbol = SlotMachine::SymbolBlank;
}

// ==================== Queries ====================

bool UDispenserSlotMachineComponent::IsSpinner(const AShooterCharacter* Character) const
{
	return Character && Spin.Spinner && Character->GetPlayerState() == Spin.Spinner;
}

FText UDispenserSlotMachineComponent::DescribeCard(int32 Index) const
{
	if (!Spin.Cards.IsValidIndex(Index))
	{
		return FText::GetEmpty();
	}
	const FDispenserCard& Card = Spin.Cards[Index];
	switch (Card.Outcome)
	{
	case EDispenserCardOutcome::Upgrade:
	{
		const FUpgradeOfferCard& Up = Card.Upgrade;
		const FText Name = Up.Definition ? Up.Definition->DisplayName : FText::GetEmpty();
		const FText Rarity = UEnum::GetDisplayValueAsText(Up.Rarity);
		if (Up.Kind == EUpgradeOfferKind::LevelUp)
		{
			return FText::Format(NSLOCTEXT("SlotMachine", "CardUp", "[{0}] {1} Lv {2} -> {3}"), Rarity, Name,
				FText::AsNumber(Up.FromLevel), FText::AsNumber(Up.ToLevel));
		}
		if (Up.Kind == EUpgradeOfferKind::Replace && Up.Replaces)
		{
			return FText::Format(NSLOCTEXT("SlotMachine", "CardReplace", "[{0}] {1} Lv {2}, replaces {3}"), Rarity, Name,
				FText::AsNumber(Up.ToLevel), Up.Replaces->DisplayName);
		}
		return FText::Format(NSLOCTEXT("SlotMachine", "CardNew", "[{0}] {1} Lv {2}"), Rarity, Name, FText::AsNumber(Up.ToLevel));
	}
	case EDispenserCardOutcome::Attachment:
	{
		const UWeaponAttachmentDefinition* const Attachment = GetCardAttachment(Index);
		const FText Name = !Attachment ? NSLOCTEXT("SlotMachine", "AttachmentUnknown", "attachment")
			: (Attachment->DisplayName.IsEmpty() ? FText::FromString(Attachment->GetName()) : Attachment->DisplayName);
		return FText::Format(NSLOCTEXT("SlotMachine", "CardAttachment", "[{0}] {1}"), UEnum::GetDisplayValueAsText(Card.Rarity), Name);
	}
	case EDispenserCardOutcome::Buff:
	{
		const UDispenserBuffDefinition* const Buff = GetCardBuff(Index);
		if (!Buff)
		{
			return NSLOCTEXT("SlotMachine", "CardBuffUnknown", "buff");
		}
		const FText Name = Buff->DisplayName.IsEmpty() ? FText::FromString(Buff->GetName()) : Buff->DisplayName;
		return FText::Format(NSLOCTEXT("SlotMachine", "CardBuff", "{0} +{1}"), Name, FText::AsNumber(FMath::RoundToInt(Buff->Amount)));
	}
	default:
		return NSLOCTEXT("SlotMachine", "CardBlank", "blank");
	}
}

TSubclassOf<AActor> UDispenserSlotMachineComponent::GetCardVisualClass(int32 Index) const
{
	const UDispenserUpgradePool* const Pool = GetPool();
	if (!Spin.Cards.IsValidIndex(Index))
	{
		return nullptr;
	}
	switch (Spin.Cards[Index].Outcome)
	{
	case EDispenserCardOutcome::Upgrade:
		return Pool && Pool->UpgradePickupClass ? Pool->UpgradePickupClass : TSubclassOf<AActor>(AUpgradePickup::StaticClass());
	case EDispenserCardOutcome::Attachment:
		return GetCardAttachment(Index) ? TSubclassOf<AActor>(AAttachmentPickup::StaticClass()) : TSubclassOf<AActor>();
	default:
		// A blank shows nothing; a buff has no pickup, the box item wears the buff's mesh itself.
		return nullptr;
	}
}

UWeaponAttachmentDefinition* UDispenserSlotMachineComponent::GetCardAttachment(int32 Index) const
{
	const UDispenserUpgradePool* const Pool = GetPool();
	if (!Pool || !Spin.Cards.IsValidIndex(Index) || Spin.Cards[Index].Outcome != EDispenserCardOutcome::Attachment
		|| !Pool->Attachments.IsValidIndex(Spin.Cards[Index].PickupIndex))
	{
		return nullptr;
	}
	return Pool->Attachments[Spin.Cards[Index].PickupIndex].Attachment;
}

const UDispenserBuffDefinition* UDispenserSlotMachineComponent::GetCardBuff(int32 Index) const
{
	const UDispenserUpgradePool* const Pool = GetPool();
	if (!Pool || !Spin.Cards.IsValidIndex(Index) || Spin.Cards[Index].Outcome != EDispenserCardOutcome::Buff
		|| !Pool->Buffs.IsValidIndex(Spin.Cards[Index].PickupIndex))
	{
		return nullptr;
	}
	return Pool->Buffs[Spin.Cards[Index].PickupIndex].Buff;
}

void UDispenserSlotMachineComponent::GatherOwnedAttachments(const AShooterCharacter* Character, TArray<const UWeaponAttachmentDefinition*>& OutOwned)
{
	OutOwned.Reset();
	if (!Character)
	{
		return;
	}
	if (const UInventoryComponent* const Inventory = Character->GetInventoryComponent())
	{
		for (const FInventorySlot& Cell : Inventory->GetSlots())
		{
			if (Cell.Kind == EInventorySlotKind::Attachment)
			{
				if (const UWeaponAttachmentDefinition* const Have = Cast<UWeaponAttachmentDefinition>(Cell.Payload))
				{
					OutOwned.AddUnique(Have);
				}
			}
		}
	}
	for (const AShooterWeapon* const Weapon : Character->GetOwnedWeapons())
	{
		if (!Weapon)
		{
			continue;
		}
		for (const UWeaponAttachmentDefinition* const Have : Weapon->GetInstalledAttachments())
		{
			if (Have)
			{
				OutOwned.AddUnique(Have);
			}
		}
	}
}

FString UDispenserSlotMachineComponent::GetSpinRefusal(const AShooterCharacter* Donor) const
{
	if (Spin.Phase == EDispenserSpinPhase::Idle)
	{
		return FString();
	}
	if (IsSpinner(Donor))
	{
		return TEXT("Dispenser: take one of your cards first");
	}
	return FString::Printf(TEXT("Dispenser: busy with %s's spin"), Spin.Spinner ? *Spin.Spinner->GetPlayerName() : TEXT("someone"));
}

// ==================== The spin (server) ====================

void UDispenserSlotMachineComponent::StartSpin(AShooterCharacter* Donor, float Magazines, const FString& WeaponName)
{
	UWorld* const World = GetWorld();
	if (!World || GetOwnerRole() != ROLE_Authority || !Donor || Spin.Phase != EDispenserSpinPhase::Idle)
	{
		return;
	}

	const UDispenserUpgradePool* const Pool = UpgradePool.LoadSynchronous();
	const float StakeValue = EvaluateStakeValue(Magazines);

	FDispenserSpin Next;
	Next.Phase = EDispenserSpinPhase::Spinning;
	Next.Spinner = Donor->GetPlayerState();
	Next.Magazines = Magazines;
	Next.StakeValue = StakeValue;
	Next.OpenServerTime = World->GetTimeSeconds() + SpinSeconds;
	Next.Serial = Spin.Serial + 1;

	TArray<const UUpgradeDefinition*> UsedUpgrades;
	TArray<int32> UsedAttachments;
	for (int32 Reel = 0; Reel < CardCount; ++Reel)
	{
		FDispenserCard Card;
		RollCard(StakeValue, Pool, Donor, UsedUpgrades, UsedAttachments, Card);
		Next.Cards.Add(Card);
	}
	Spin = Next;

	FString Text = FString::Printf(TEXT("%s: %.1f magazines -> stake %.0f"), *WeaponName, Magazines, StakeValue);
	for (int32 Index = 0; Index < Spin.Cards.Num(); ++Index)
	{
		Text += FString::Printf(TEXT("\n  %d) %s"), Index + 1, *DescribeCard(Index).ToString());
	}
	if (!Pool)
	{
		Text += TEXT("\n  (no upgrade pool asset: create DA_DispenserUpgradePool)");
	}
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s spun %s: %s"), *Donor->GetName(), *GetNameSafe(GetOwner()), *Text);
	Receipt(Donor, Text);
	// Replicate the loaded rewards while the shutters are still closed.
	if (bCassetteModel)
	{
		SpawnItems();
	}
	GetOwner()->ForceNetUpdate();
	FireSpinEvents();
	World->GetTimerManager().SetTimer(PhaseTimer, this, &UDispenserSlotMachineComponent::OpenBoxes, FMath::Max(0.01f, SpinSeconds), false);
}

void UDispenserSlotMachineComponent::OpenBoxes()
{
	UWorld* const World = GetWorld();
	if (!World || Spin.Phase != EDispenserSpinPhase::Spinning)
	{
		return;
	}

	bool bAnything = false;
	for (const FDispenserCard& Card : Spin.Cards)
	{
		if (Card.Outcome != EDispenserCardOutcome::Dud)
		{
			bAnything = true;
		}
	}

	if (!bAnything)
	{
		// Three blanks: every box open and empty, the finger up, then the machine is free again.
		Spin.Phase = EDispenserSpinPhase::Closing;
		UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s: all blanks for %s"), *GetNameSafe(GetOwner()),
			Spin.Spinner ? *Spin.Spinner->GetPlayerName() : TEXT("?"));
		FireSpinEvents();
		World->GetTimerManager().SetTimer(PhaseTimer, this, &UDispenserSlotMachineComponent::FinishClose, FMath::Max(0.01f, AllDudsSeconds), false);
		return;
	}

	Spin.Phase = EDispenserSpinPhase::Open;
	const float OfferDuration = OfferTimeoutSeconds + (bCassetteModel ? CassetteMoveSeconds : 0.0f);
	Spin.ExpireServerTime = World->GetTimeSeconds() + OfferDuration;
	if (!bCassetteModel)
	{
		SpawnItems();
	}
	GetOwner()->ForceNetUpdate();
	FireSpinEvents();
	World->GetTimerManager().SetTimer(PhaseTimer, this, &UDispenserSlotMachineComponent::ExpireOffer, FMath::Max(1.0f, OfferDuration), false);
}

void UDispenserSlotMachineComponent::ExpireOffer()
{
	if (Spin.Phase != EDispenserSpinPhase::Open)
	{
		return;
	}

	// Nobody took anything in time: the machine takes the WORST card for the spinner (the author's
	// call), so no one loses the spin outright and the machine is free for the others.
	int32 Worst = INDEX_NONE;
	for (int32 Index = 0; Index < Spin.Cards.Num(); ++Index)
	{
		if (Spin.Cards[Index].Outcome == EDispenserCardOutcome::Dud)
		{
			continue;
		}
		if (Worst == INDEX_NONE || RankOf(Spin.Cards[Index]) < RankOf(Spin.Cards[Worst]))
		{
			Worst = Index;
		}
	}

	AShooterCharacter* const Spinner = Spin.Spinner ? Cast<AShooterCharacter>(Spin.Spinner->GetPawn()) : nullptr;
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s: time is up, taking the worst card %d for %s"),
		*GetNameSafe(GetOwner()), Worst + 1, *GetNameSafe(Spinner));
	if (Worst == INDEX_NONE || !Spinner || !TakeCard(Worst, Spinner))
	{
		// The spinner is gone (dead, left): nothing to give, just free the machine.
		BeginClosing();
	}
}

bool UDispenserSlotMachineComponent::TakeCard(int32 Index, AShooterCharacter* Taker)
{
	UWorld* const World = GetWorld();
	if (!World || GetOwnerRole() != ROLE_Authority || Spin.Phase != EDispenserSpinPhase::Open
		|| !IsSpinner(Taker) || !Spin.Cards.IsValidIndex(Index) || !IsCardPresented(Index))
	{
		return false;
	}
	const FDispenserCard Card = Spin.Cards[Index];
	if (Card.Outcome == EDispenserCardOutcome::Dud)
	{
		return false;
	}

	if (Card.Outcome == EDispenserCardOutcome::Upgrade)
	{
		if (UUpgradeManagerComponent* const Upgrades = Taker->GetUpgradeManager())
		{
			Upgrades->GrantOfferCard(Card.Upgrade);
		}
	}
	else if (Card.Outcome == EDispenserCardOutcome::Attachment)
	{
		DeliverAttachment(GetCardAttachment(Index), Taker);
	}
	else
	{
		ApplyBuff(GetCardBuff(Index), Taker);
	}

	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s took card %d: %s"), *Taker->GetName(), Index + 1, *DescribeCard(Index).ToString());

	Spin.TakenIndex = Index;
	if (Items.IsValidIndex(Index))
	{
		if (ADispenserCardPickup* const Taken = Items[Index].Get())
		{
			Taken->Destroy();
		}
		Items[Index].Reset();
	}
	// Other rewards stay on their trays until the shutters have closed around them.
	BeginClosing();
	return true;
}

float UDispenserSlotMachineComponent::GetCloseDuration() const
{
	return FMath::Max(0.01f, bCassetteModel ? FMath::Max(CloseSeconds, CassetteMoveSeconds + 0.12f) : CloseSeconds);
}

void UDispenserSlotMachineComponent::BeginClosing()
{
	if (!bCassetteModel)
	{
		DestroyItems();
	}
	Spin.Phase = EDispenserSpinPhase::Closing;
	const float Duration = GetCloseDuration();
	// During Closing this replicated deadline is also the animation's shared clock.
	Spin.ExpireServerTime = GetWorld()->GetTimeSeconds() + Duration;
	GetOwner()->ForceNetUpdate();
	FireSpinEvents();
	GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &UDispenserSlotMachineComponent::FinishClose, Duration, false);
}

void UDispenserSlotMachineComponent::FinishClose()
{
	DestroyItems();
	Spin.Phase = EDispenserSpinPhase::Idle;
	Spin.Spinner = nullptr;
	FireSpinEvents();
}

void UDispenserSlotMachineComponent::DeliverAttachment(UWeaponAttachmentDefinition* Attachment, AShooterCharacter* Taker) const
{
	UWorld* const World = GetWorld();
	if (!World || !Attachment || !Taker)
	{
		return;
	}

	// A fresh attachment pickup right at the taker, giving itself the way it always does: it flies
	// into the bag at once, and whatever does not fit is put back on the floor by the pickup itself.
	// Its Item is filled from Attachment in its own BeginPlay (server), so setting Attachment before
	// FinishSpawning is enough.
	const FTransform Where(Taker->GetActorRotation(), Taker->GetActorLocation() + Taker->GetActorForwardVector() * 40.0f);
	AAttachmentPickup* const Pickup = World->SpawnActorDeferred<AAttachmentPickup>(AAttachmentPickup::StaticClass(), Where,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		return;
	}
	Pickup->Attachment = Attachment;
	Pickup->bStartSimulatingPhysics = false;
	Pickup->FinishSpawning(Where);
	Pickup->ApplyAttachmentLook();
	const bool bPulled = Pickup->TryStartPullForClient(Taker);
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] delivered %s to %s (pull %s)"), *Attachment->GetName(), *Taker->GetName(),
		bPulled ? TEXT("started") : TEXT("refused, left on the floor"));
}

void UDispenserSlotMachineComponent::ApplyBuff(const UDispenserBuffDefinition* Buff, AShooterCharacter* Taker) const
{
	if (!Buff || !Taker)
	{
		return;
	}

	switch (Buff->Kind)
	{
	case EDispenserBuffKind::Heal:
		Taker->RestoreHealth(Buff->Amount);
		break;
	case EDispenserBuffKind::Armor:
		Taker->RestoreArmor(Buff->Amount);
		break;
	case EDispenserBuffKind::Ammo:
	{
		// Amount magazines of the gun in hand: into its energy reserve, or into the bag's ammo cells
		// for a looted gun, the same two routes the dispenser's own ammo service takes.
		AShooterWeapon* const Weapon = Taker->GetCurrentWeapon();
		const int32 Rounds = Weapon ? FMath::RoundToInt(Buff->Amount * FMath::Max(1, Weapon->GetMagazineSize())) : 0;
		int32 Added = 0;
		if (Weapon && Rounds > 0 && Weapon->UsesEnergyReserve())
		{
			Added = FMath::Min(Rounds, FMath::Max(0, Weapon->GetEnergyReserveCapacity() - Weapon->GetEnergyReserve()));
			Weapon->SetEnergyReserve(Weapon->GetEnergyReserve() + Added);
		}
		else if (Weapon && Rounds > 0 && Weapon->OwnsAmmoCells())
		{
			if (UInventoryComponent* const Inventory = Taker->GetInventoryComponent())
			{
				FInventoryItem Item;
				Item.Kind = EInventorySlotKind::Ammo;
				Item.Count = Rounds;
				Item.StackMax = Inventory->GetRoundsPerAmmoCell();
				Added = Rounds - Inventory->TryAdd(Item);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] ammo buff for %s: %d of %d rounds into %s"), *Taker->GetName(), Added, Rounds,
			*GetNameSafe(Weapon));
		break;
	}
	default:
		break;
	}
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] buff %s (%.0f) applied to %s"), *Buff->GetName(), Buff->Amount, *Taker->GetName());
}

// ==================== Items in the boxes (server) ====================

FTransform UDispenserSlotMachineComponent::BoxTransform(int32 Index) const
{
	const AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return FTransform::Identity;
	}

	// The model's socket when there is one (the 3D handoff's Box_N_Item)...
	if (BoxSockets.IsValidIndex(Index))
	{
		TArray<USkeletalMeshComponent*> Meshes;
		Owner->GetComponents<USkeletalMeshComponent>(Meshes);
		for (const USkeletalMeshComponent* const Mesh : Meshes)
		{
			if (Mesh && Mesh->DoesSocketExist(BoxSockets[Index]))
			{
				return Mesh->GetSocketTransform(BoxSockets[Index]);
			}
		}
	}

	// ...else a row in front of the building, waist high, so the machine works before the model.
	const float Offset = (Index - (CardCount - 1) * 0.5f) * 70.0f;
	const FVector Where = Owner->GetActorLocation() + Owner->GetActorForwardVector() * 90.0f
		+ Owner->GetActorRightVector() * Offset + FVector(0.0f, 0.0f, 110.0f);
	return FTransform(Owner->GetActorRotation(), Where);
}

void UDispenserSlotMachineComponent::SpawnItems()
{
	DestroyItems();
	UWorld* const World = GetWorld();
	if (!World || !CardPickupClass)
	{
		return;
	}
	for (int32 Index = 0; Index < Spin.Cards.Num(); ++Index)
	{
		const FDispenserCard& Card = Spin.Cards[Index];
		if (Card.Outcome == EDispenserCardOutcome::Dud)
		{
			Items.Add(nullptr);
			continue;
		}
		const FTransform Where = BoxTransform(Index);
		ADispenserCardPickup* const Item = World->SpawnActorDeferred<ADispenserCardPickup>(CardPickupClass, Where,
			GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Item)
		{
			Items.Add(nullptr);
			continue;
		}
		Item->InitCard(this, Index, DescribeCard(Index), CardColor(Card));
		Item->FinishSpawning(Where);
		if (bCassetteModel && MachineMesh && BoxSockets.IsValidIndex(Index))
		{
			Item->AttachToComponent(MachineMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, BoxSockets[Index]);
		}
		Items.Add(Item);
	}
}

void UDispenserSlotMachineComponent::DestroyItems()
{
	for (const TWeakObjectPtr<ADispenserCardPickup>& Weak : Items)
	{
		if (ADispenserCardPickup* const Item = Weak.Get())
		{
			Item->Destroy();
		}
	}
	Items.Reset();
}

// ==================== Events ====================

void UDispenserSlotMachineComponent::OnRep_Spin()
{
	FireSpinEvents();
}

void UDispenserSlotMachineComponent::FireSpinEvents()
{
	const bool bNewSpin = Spin.Serial != SeenSerial;
	const bool bNewPhase = Spin.Phase != SeenPhase;
	const bool bNewTake = Spin.TakenIndex != SeenTaken && Spin.TakenIndex != INDEX_NONE;
	SeenSerial = Spin.Serial;
	SeenPhase = Spin.Phase;
	SeenTaken = Spin.TakenIndex;

	if (bNewSpin && Spin.Phase != EDispenserSpinPhase::Idle)
	{
		OnSpinStarted.Broadcast(this);
	}
	if (bNewPhase || bNewSpin)
	{
		switch (Spin.Phase)
		{
		case EDispenserSpinPhase::Open:
			OnBoxesOpened.Broadcast(this);
			break;
		case EDispenserSpinPhase::Closing:
			if (Spin.TakenIndex == INDEX_NONE)
			{
				bool bAnything = false;
				for (const FDispenserCard& Card : Spin.Cards)
				{
					if (Card.Outcome != EDispenserCardOutcome::Dud)
					{
						bAnything = true;
					}
				}
				if (!bAnything)
				{
					OnAllBlanks.Broadcast(this);
				}
			}
			break;
		case EDispenserSpinPhase::Idle:
			OnSpinClosed.Broadcast(this);
			break;
		default:
			break;
		}
	}
	if (bNewTake)
	{
		OnCardTaken.Broadcast(this, Spin.TakenIndex);
	}
	OnSpinChanged.Broadcast(this);

	// The building's own change event too: the feed menu and the status widget listen to it.
	if (ABuildableActor* const Building = Cast<ABuildableActor>(GetOwner()))
	{
		Building->OnBuildableChanged.Broadcast(Building);
	}

	// Wake the model: it follows the new phase on its own and goes back to sleep when still.
	if (MachineMesh)
	{
		SetComponentTickEnabled(true);
	}
}

// ==================== Model (every machine) ====================

void UDispenserSlotMachineComponent::SetUpModel()
{
	AActor* const Owner = GetOwner();
	if (!Owner || LidBones.Num() == 0)
	{
		return;
	}

	// The machine is the skeletal mesh with the first lid bone (a turret's head has none).
	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* const Candidate : Meshes)
	{
		if (Candidate && Candidate->GetBoneIndex(LidBones[0]) != INDEX_NONE)
		{
			MachineMesh = Candidate;
			break;
		}
	}
	if (!MachineMesh)
	{
		return;
	}

	bCassetteModel = MachineMesh->GetBoneIndex(TEXT("tray_0")) != INDEX_NONE;
	if (bCassetteModel)
	{
		// Attached rewards need current bone transforms on the server, even off screen.
		MachineMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	if (MachineMesh->GetAnimClass() != UDispenserMachineAnimInstance::StaticClass())
	{
		MachineMesh->SetAnimInstanceClass(UDispenserMachineAnimInstance::StaticClass());
	}
	// The anim instance reads the angles this component moves: this ticks first.
	MachineMesh->AddTickPrerequisiteComponent(this);

	ReelMaterials.Reset();
	int32 ReelsFound = 0;
	for (const FName& SlotName : ReelSlots)
	{
		const int32 MaterialIndex = MachineMesh->GetMaterialIndex(SlotName);
		UMaterialInstanceDynamic* const Material = GetNetMode() != NM_DedicatedServer && MaterialIndex != INDEX_NONE ? MachineMesh->CreateDynamicMaterialInstance(MaterialIndex) : nullptr;
		ReelMaterials.Add(Material);
		ReelsFound += Material ? 1 : 0;
	}
	LidOpenness.Init(0.0f, LidBones.Num());

	int32 LidsFound = 0;
	for (const FName& Bone : LidBones)
	{
		LidsFound += MachineMesh->GetBoneIndex(Bone) != INDEX_NONE ? 1 : 0;
	}
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s: model %s, lids %d/%d, flap %s, reels %d/%d"), *GetNameSafe(Owner),
		*GetNameSafe(MachineMesh->GetSkeletalMeshAsset()), LidsFound, LidBones.Num(),
		MachineMesh->GetBoneIndex(FlapBone) != INDEX_NONE ? TEXT("yes") : TEXT("no"), ReelsFound, ReelSlots.Num());

	if (bCassetteModel)
	{
		UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s: cassette rig, shutter then 20 cm tray, %.2f s, rewards retained through return"),
			*GetNameSafe(Owner), CassetteMoveSeconds);
	}
	// One pass puts whatever spin is already running on it (a machine that arrives mid-spin).
	SetComponentTickEnabled(true);
}

void UDispenserSlotMachineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!UpdateModel(DeltaTime))
	{
		SetComponentTickEnabled(false);
	}
}

float UDispenserSlotMachineComponent::GetLidAngle(int32 Index) const
{
	return LidOpenness.IsValidIndex(Index) ? LidOpenDegrees * FMath::SmoothStep(0.0f, 1.0f, LidOpenness[Index]) : 0.0f;
}

float UDispenserSlotMachineComponent::GetCassetteAlpha(int32 Index) const
{
	return LidOpenness.IsValidIndex(Index) ? LidOpenness[Index] : 0.0f;
}

bool UDispenserSlotMachineComponent::IsCardPresented(int32 Index) const
{
	return Spin.Phase == EDispenserSpinPhase::Open && Spin.Cards.IsValidIndex(Index)
		&& Spin.Cards[Index].Outcome != EDispenserCardOutcome::Dud
		&& (!bCassetteModel || GetCassetteAlpha(Index) >= 0.999f);
}

bool UDispenserSlotMachineComponent::UpdateModel(float DeltaSeconds)
{
	const UWorld* const World = GetWorld();
	if (!World || !MachineMesh)
	{
		return false;
	}
	bool bMoving = false;

	// Seconds into this spin, on the server's clock, so every machine shows the same moment. The
	// spin started SpinSeconds before its boxes open.
	const AGameStateBase* const GameState = World->GetGameState();
	const float Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	const bool bHasSpin = Spin.Serial > 0 && Spin.Phase != EDispenserSpinPhase::Idle;
	const float Elapsed = FMath::Max(0.0f, Now - (Spin.OpenServerTime - SpinSeconds));

	// Lids: open while the cards wait, and on three blanks (empty boxes, the finger up) until the
	// machine is free again; shut otherwise.
	const bool bLidsOpen = Spin.Phase == EDispenserSpinPhase::Open
		|| (Spin.Phase == EDispenserSpinPhase::Closing && Spin.TakenIndex == INDEX_NONE && IsAllBlanks());
	const float LidTarget = bLidsOpen ? 1.0f : 0.0f;
	for (int32 Index = 0; Index < LidOpenness.Num(); ++Index)
	{
		float& Openness = LidOpenness[Index];
		if (bCassetteModel)
		{
			const bool bReward = Spin.Cards.IsValidIndex(Index) && Spin.Cards[Index].Outcome != EDispenserCardOutcome::Dud;
			const float MoveSeconds = FMath::Max(0.6f, CassetteMoveSeconds);
			if (bReward && Spin.Phase == EDispenserSpinPhase::Open)
			{
				Openness = FMath::Clamp((Now - Spin.OpenServerTime) / MoveSeconds, 0.0f, 1.0f);
				bMoving |= Openness < 1.0f;
			}
			else if (bReward && Spin.Phase == EDispenserSpinPhase::Closing)
			{
				const float StartedClosing = Spin.ExpireServerTime - GetCloseDuration();
				Openness = 1.0f - FMath::Clamp((Now - StartedClosing) / MoveSeconds, 0.0f, 1.0f);
				bMoving |= Openness > 0.0f;
			}
			else
			{
				Openness = 0.0f;
			}
		}
		else
		{
			Openness = FMath::FInterpConstantTo(Openness, LidTarget, DeltaSeconds, 1.0f / FMath::Max(0.01f, LidMoveSeconds));
			bMoving |= Openness != LidTarget;
		}
	}

	// Intake flap: once open and shut as the gun goes in.
	FlapAngle = 0.0f;
	if (bHasSpin && Elapsed < FlapSeconds)
	{
		FlapAngle = FlapOpenDegrees * FMath::Sin(PI * Elapsed / FlapSeconds);
		bMoving = true;
	}

	// Reels: full speed, then each slows onto its card's cell at its own stop time; a stopped reel
	// glows in its card's colour until the machine is free again.
	const float Speed = ReelCellsPerSecond * FMath::Abs(ReelOffsetPerCell);
	for (int32 Reel = 0; Reel < ReelMaterials.Num(); ++Reel)
	{
		UMaterialInstanceDynamic* const Material = ReelMaterials[Reel];
		if (!Material || Spin.Serial == 0 || !Spin.Cards.IsValidIndex(Reel))
		{
			continue;
		}
		const FDispenserCard& Card = Spin.Cards[Reel];
		const float Target = FMath::Frac(ReelOffsetForCell0 + Card.Symbol * ReelOffsetPerCell);
		const float StopAt = FMath::Min(ReelStopSeconds.IsValidIndex(Reel) ? ReelStopSeconds[Reel] : SpinSeconds, SpinSeconds);
		const float Left = bHasSpin ? StopAt - Elapsed : 0.0f;

		float Offset = Target;
		float Blur = 0.0f;
		float Glow = 0.0f;
		if (Left > 0.0f)
		{
			// Constant speed, then a straight-line slowdown over the last ReelEaseSeconds: the
			// distance still to go is the area under that speed.
			const float Distance = Left < ReelEaseSeconds
				? Speed * Left * Left / (2.0f * ReelEaseSeconds)
				: Speed * (Left - 0.5f * ReelEaseSeconds);
			Offset = FMath::Frac(Target - Distance);
			Blur = ReelSpinBlur * FMath::Min(1.0f, Left / ReelEaseSeconds);
			bMoving = true;
		}
		else if (bHasSpin)
		{
			Glow = Card.Outcome == EDispenserCardOutcome::Dud ? ReelBlankGlow : 1.0f;
		}
		Material->SetScalarParameterValue(TEXT("ReelOffset"), Offset);
		Material->SetScalarParameterValue(TEXT("ReelBlur"), Blur);
		Material->SetScalarParameterValue(TEXT("ResultGlow"), Glow);
		Material->SetVectorParameterValue(TEXT("ResultColor"), CardColor(Card));
	}

	return bMoving;
}

void UDispenserSlotMachineComponent::Receipt(AShooterCharacter* Donor, const FString& Text) const
{
	if (UBuilderComponent* const Builder = Donor ? Donor->FindComponentByClass<UBuilderComponent>() : nullptr)
	{
		Builder->Client_ShowCasinoReceipt(Text);
	}
}

// ==================== Console ====================

namespace SlotMachineDebug
{
	static UDispenserSlotMachineComponent* FindMachine(UWorld* World, const AActor* Near)
	{
		UDispenserSlotMachineComponent* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ABuildableActor> It(World); It; ++It)
		{
			if (!It->IsDispenser() || !It->GetSlotMachine())
			{
				continue;
			}
			const float Dist = Near ? FVector::DistSquared(Near->GetActorLocation(), It->GetActorLocation()) : 0.0f;
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = It->GetSlotMachine();
			}
		}
		return Best;
	}

	static AShooterCharacter* LocalPlayer(UWorld* World)
	{
		const APlayerController* const PC = CoopPlayers::GetLocalController(World);
		return PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
	}

	/** polarity.casino.sim [reels=1000] [magazines=5]: what reels at that stake come out as, pool aside. */
	static void CmdSim(const TArray<FString>& Args, UWorld* World)
	{
		const int32 Reels = FMath::Max(1, Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1000);
		const float Magazines = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 5.0f;
		const UDispenserSlotMachineComponent* Machine = FindMachine(World, LocalPlayer(World));
		if (!Machine)
		{
			Machine = GetDefault<UDispenserSlotMachineComponent>();
		}
		const float Value = Machine->EvaluateStakeValue(Magazines);
		const float DudChance = FMath::Clamp(SlotMachine::Eval(Machine->DudChanceCurve, Value, 0.3f), 0.0f, 1.0f);
		const float BuffChance = FMath::Clamp(SlotMachine::Eval(Machine->BuffChanceCurve, Value, 0.3f), 0.0f, 1.0f - DudChance);
		int32 ByRarity[4] = {0, 0, 0, 0};
		for (int32 i = 0; i < Reels; ++i)
		{
			++ByRarity[static_cast<int32>(Machine->RarityForValue(Value * Machine->EvaluatePayoutMultiplier(FMath::FRand())))];
		}
		const float Upgrade = 1.0f - DudChance - BuffChance;
		UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] sim %.1f magazines -> stake %.0f | per reel: blank %.0f%%, buff %.0f%%, upgrade %.0f%% | an upgrade reel rolls: common %.0f%%, rare %.0f%%, epic %.0f%%, legendary %.0f%% | three blanks %.2f%%"),
			Magazines, Value, DudChance * 100.0f, BuffChance * 100.0f, Upgrade * 100.0f,
			ByRarity[0] * 100.0f / Reels, ByRarity[1] * 100.0f / Reels, ByRarity[2] * 100.0f / Reels, ByRarity[3] * 100.0f / Reels,
			FMath::Pow(DudChance, 3.0f) * 100.0f);
	}

	/** polarity.casino.offer [magazines=5]: a spin at the nearest dispenser as if a gun of that many
	 *  magazines was fed in, no gun taken. Host or standalone. */
	static void CmdOffer(const TArray<FString>& Args, UWorld* World)
	{
		AShooterCharacter* const Player = LocalPlayer(World);
		UDispenserSlotMachineComponent* const Machine = FindMachine(World, Player);
		if (!Player || !Machine || !Player->HasAuthority())
		{
			UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] offer: needs a dispenser in the world, host or standalone"));
			return;
		}
		const FString Refusal = Machine->GetSpinRefusal(Player);
		if (!Refusal.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] offer: %s"), *Refusal);
			return;
		}
		Machine->StartSpin(Player, Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.0f, TEXT("console"));
	}

	/** polarity.casino.take N: take card N (1-based) of your open spin, without the grapple. */
	static void CmdTake(const TArray<FString>& Args, UWorld* World)
	{
		AShooterCharacter* const Player = LocalPlayer(World);
		UDispenserSlotMachineComponent* const Machine = FindMachine(World, Player);
		const int32 Index = FMath::Max(1, Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1) - 1;
		if (!Player || !Machine)
		{
			return;
		}
		if (Player->HasAuthority())
		{
			Machine->TakeCard(Index, Player);
		}
		else if (UBuilderComponent* const Builder = Player->FindComponentByClass<UBuilderComponent>())
		{
			Builder->Server_TakeDispenserCard(Cast<ABuildableActor>(Machine->GetOwner()), Index);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdCasinoSim(
	TEXT("polarity.casino.sim"),
	TEXT("What reels at a stake come out as: blank/buff/upgrade shares, upgrade rarities, three blanks. Usage: polarity.casino.sim [reels=1000] [magazines=5]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SlotMachineDebug::CmdSim));

static FAutoConsoleCommandWithWorldAndArgs CmdCasinoOffer(
	TEXT("polarity.casino.offer"),
	TEXT("Spin the nearest dispenser as if a gun of N magazines was fed in, no gun taken. Host only. Usage: polarity.casino.offer [magazines=5]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SlotMachineDebug::CmdOffer));

static FAutoConsoleCommandWithWorldAndArgs CmdCasinoTake(
	TEXT("polarity.casino.take"),
	TEXT("Take card N (1-based) of your open dispenser spin, without the grapple. Usage: polarity.casino.take [N=1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SlotMachineDebug::CmdTake));
