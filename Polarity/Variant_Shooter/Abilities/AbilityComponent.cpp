// AbilityComponent.cpp

#include "AbilityComponent.h"
#include "AbilityDefinition.h"
#include "AbilityHandler.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "EMFVelocityModifier.h"

UAbilityComponent::UAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UAbilityComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAbilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsCasting)
	{
		CancelCast();
	}
	for (UAbilityHandler* Handler : EquippedHandlers)
	{
		DestroyHandler(Handler);
	}
	EquippedHandlers.Empty();
	Super::EndPlay(EndPlayReason);
}

void UAbilityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (CooldownTimeRemaining > 0.0f)
	{
		CooldownTimeRemaining = FMath::Max(0.0f, CooldownTimeRemaining - DeltaTime);
		if (CooldownTimeRemaining <= 0.0f)
		{
			OnCooldownEnded.Broadcast();
		}
	}
}

// ==================== Inventory ====================

int32 UAbilityComponent::AddAbility(UAbilityDefinition* Definition, int32 Level)
{
	if (!Definition || !Definition->HandlerClass)
	{
		return INDEX_NONE;
	}

	const int32 ExistingSlot = FindSlotIndexForDefinition(Definition);
	if (ExistingSlot != INDEX_NONE)
	{
		UAbilityHandler* Handler = EquippedHandlers[ExistingSlot];
		if (Handler && Level > Handler->GetCurrentLevel())
		{
			Handler->SetLevel(Level);
			OnAbilityLevelChanged.Broadcast(ExistingSlot, Handler->GetCurrentLevel());
			return ExistingSlot;
		}
		return INDEX_NONE;
	}

	if (EquippedHandlers.Num() < MaxAbilitySlots)
	{
		UAbilityHandler* Handler = CreateHandler(Definition, Level);
		if (!Handler)
		{
			return INDEX_NONE;
		}
		const int32 NewIndex = EquippedHandlers.Add(Handler);
		Handler->OnEquip();
		if (ActiveSlotIndex == INDEX_NONE)
		{
			ActiveSlotIndex = NewIndex;
			OnAbilitySwitched.Broadcast(NewIndex);
		}
		OnAbilityAdded.Broadcast(NewIndex);
		return NewIndex;
	}

	if (bReplaceActiveWhenFull && ActiveSlotIndex != INDEX_NONE)
	{
		if (ReplaceAbility(ActiveSlotIndex, Definition, Level))
		{
			return ActiveSlotIndex;
		}
	}
	return INDEX_NONE;
}

bool UAbilityComponent::ReplaceAbility(int32 SlotIndex, UAbilityDefinition* Definition, int32 Level)
{
	if (!Definition || !Definition->HandlerClass || !EquippedHandlers.IsValidIndex(SlotIndex))
	{
		return false;
	}
	const int32 ExistingSlot = FindSlotIndexForDefinition(Definition);
	if (ExistingSlot != INDEX_NONE && ExistingSlot != SlotIndex)
	{
		return false;
	}

	if (SlotIndex == ActiveSlotIndex && bIsCasting)
	{
		CancelCast();
	}

	UAbilityHandler* OldHandler = EquippedHandlers[SlotIndex];
	if (OldHandler)
	{
		OldHandler->OnUnequip();
		DestroyHandler(OldHandler);
	}

	UAbilityHandler* NewHandler = CreateHandler(Definition, Level);
	if (!NewHandler)
	{
		EquippedHandlers.RemoveAt(SlotIndex);
		if (ActiveSlotIndex >= EquippedHandlers.Num())
		{
			ActiveSlotIndex = EquippedHandlers.Num() > 0 ? 0 : INDEX_NONE;
		}
		OnAbilityRemoved.Broadcast(SlotIndex);
		return false;
	}

	EquippedHandlers[SlotIndex] = NewHandler;
	NewHandler->OnEquip();
	OnAbilityAdded.Broadcast(SlotIndex);
	return true;
}

bool UAbilityComponent::RemoveAbility(int32 SlotIndex)
{
	if (!EquippedHandlers.IsValidIndex(SlotIndex))
	{
		return false;
	}
	if (SlotIndex == ActiveSlotIndex && bIsCasting)
	{
		CancelCast();
	}
	UAbilityHandler* Handler = EquippedHandlers[SlotIndex];
	if (Handler)
	{
		Handler->OnUnequip();
		DestroyHandler(Handler);
	}
	EquippedHandlers.RemoveAt(SlotIndex);
	OnAbilityRemoved.Broadcast(SlotIndex);

	if (EquippedHandlers.Num() == 0)
	{
		ActiveSlotIndex = INDEX_NONE;
		OnAbilitySwitched.Broadcast(INDEX_NONE);
	}
	else if (SlotIndex == ActiveSlotIndex || ActiveSlotIndex >= EquippedHandlers.Num())
	{
		ActiveSlotIndex = FMath::Clamp(ActiveSlotIndex, 0, EquippedHandlers.Num() - 1);
		OnAbilitySwitched.Broadcast(ActiveSlotIndex);
	}
	return true;
}

bool UAbilityComponent::SetAbilityLevel(int32 SlotIndex, int32 NewLevel)
{
	if (!EquippedHandlers.IsValidIndex(SlotIndex))
	{
		return false;
	}
	UAbilityHandler* Handler = EquippedHandlers[SlotIndex];
	if (!Handler)
	{
		return false;
	}
	const int32 OldLevel = Handler->GetCurrentLevel();
	Handler->SetLevel(NewLevel);
	if (Handler->GetCurrentLevel() != OldLevel)
	{
		OnAbilityLevelChanged.Broadcast(SlotIndex, Handler->GetCurrentLevel());
		return true;
	}
	return false;
}

bool UAbilityComponent::LevelUpAbility(int32 SlotIndex)
{
	if (!EquippedHandlers.IsValidIndex(SlotIndex) || !EquippedHandlers[SlotIndex])
	{
		return false;
	}
	UAbilityHandler* Handler = EquippedHandlers[SlotIndex];
	return SetAbilityLevel(SlotIndex, Handler->GetCurrentLevel() + 1);
}

int32 UAbilityComponent::GetAbilityLevel(int32 SlotIndex) const
{
	if (!EquippedHandlers.IsValidIndex(SlotIndex) || !EquippedHandlers[SlotIndex])
	{
		return 0;
	}
	return EquippedHandlers[SlotIndex]->GetCurrentLevel();
}

bool UAbilityComponent::SwitchToSlot(int32 SlotIndex)
{
	if (!EquippedHandlers.IsValidIndex(SlotIndex) || SlotIndex == ActiveSlotIndex || bIsCasting)
	{
		return false;
	}
	ActiveSlotIndex = SlotIndex;
	OnAbilitySwitched.Broadcast(ActiveSlotIndex);
	return true;
}

bool UAbilityComponent::SwitchToNext()
{
	if (EquippedHandlers.Num() <= 1)
	{
		return false;
	}
	const int32 Next = (ActiveSlotIndex + 1) % EquippedHandlers.Num();
	return SwitchToSlot(Next);
}

bool UAbilityComponent::SwitchToPrevious()
{
	if (EquippedHandlers.Num() <= 1)
	{
		return false;
	}
	const int32 Prev = (ActiveSlotIndex - 1 + EquippedHandlers.Num()) % EquippedHandlers.Num();
	return SwitchToSlot(Prev);
}

bool UAbilityComponent::HasAbility(UAbilityDefinition* Definition) const
{
	return FindSlotIndexForDefinition(Definition) != INDEX_NONE;
}

UAbilityDefinition* UAbilityComponent::GetActiveAbility() const
{
	UAbilityHandler* Handler = GetActiveHandler();
	return Handler ? Handler->GetDefinition() : nullptr;
}

UAbilityHandler* UAbilityComponent::GetActiveHandler() const
{
	return EquippedHandlers.IsValidIndex(ActiveSlotIndex) ? EquippedHandlers[ActiveSlotIndex] : nullptr;
}

UAbilityDefinition* UAbilityComponent::GetAbilityAtSlot(int32 SlotIndex) const
{
	if (!EquippedHandlers.IsValidIndex(SlotIndex) || !EquippedHandlers[SlotIndex])
	{
		return nullptr;
	}
	return EquippedHandlers[SlotIndex]->GetDefinition();
}

int32 UAbilityComponent::FindSlotIndexForDefinition(UAbilityDefinition* Definition) const
{
	if (!Definition)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < EquippedHandlers.Num(); ++i)
	{
		if (EquippedHandlers[i] && EquippedHandlers[i]->GetDefinition() == Definition)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// ==================== Activation ====================

static float GetOwnerChargeModule(AActor* Owner)
{
	if (!Owner)
	{
		return 0.0f;
	}
	if (UEMFVelocityModifier* Mod = Owner->FindComponentByClass<UEMFVelocityModifier>())
	{
		return FMath::Abs(Mod->GetCharge());
	}
	return 0.0f;
}

bool UAbilityComponent::CanActivate() const
{
	if (bIsCasting || CooldownTimeRemaining > 0.0f)
	{
		return false;
	}
	UAbilityHandler* Handler = GetActiveHandler();
	if (!Handler)
	{
		return false;
	}
	return GetOwnerChargeModule(GetOwner()) >= Handler->GetCommonStats().MinimumChargeToActivate;
}

bool UAbilityComponent::TryActivate()
{
	UAbilityHandler* Handler = GetActiveHandler();
	if (!Handler)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TryActivate BLOCKED: no active handler"));
		return false;
	}
	if (bIsCasting)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TryActivate BLOCKED: already casting"));
		return false;
	}
	if (CooldownTimeRemaining > 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TryActivate BLOCKED: cooldown %.2fs remaining"), CooldownTimeRemaining);
		return false;
	}
	const float Charge = GetOwnerChargeModule(GetOwner());
	const float MinCharge = Handler->GetCommonStats().MinimumChargeToActivate;
	if (Charge < MinCharge)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TryActivate BLOCKED: charge %.2f < min %.2f"), Charge, MinCharge);
		return false;
	}

	UAbilityDefinition* Def = Handler->GetDefinition();
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TryActivate OK — ability='%s' level=%d"),
		Def ? *Def->GetName() : TEXT("null"), Handler->GetCurrentLevel());

	bIsCasting = true;
	OnAbilityActivated.Broadcast(Def);
	Handler->OnActivate();
	return true;
}

void UAbilityComponent::OnButtonReleased()
{
	if (UAbilityHandler* Handler = GetActiveHandler())
	{
		Handler->OnButtonReleased();
	}
}

void UAbilityComponent::CancelCast()
{
	if (!bIsCasting)
	{
		return;
	}
	if (UAbilityHandler* Handler = GetActiveHandler())
	{
		Handler->OnCancelRequested();
	}
	// Handler should call NotifyAbilityCancelledFromHandler. As a safety net, force-clear here
	// in case the handler doesn't respond.
	if (bIsCasting)
	{
		bIsCasting = false;
		OnAbilityCancelled.Broadcast(GetActiveAbility());
	}
}

// ==================== Handler-side notifications ====================

void UAbilityComponent::NotifyAbilityCompletedFromHandler(UAbilityHandler* Handler)
{
	if (!Handler || Handler != GetActiveHandler() || !bIsCasting)
	{
		return;
	}
	bIsCasting = false;
	UAbilityDefinition* Def = Handler->GetDefinition();
	OnAbilityCompleted.Broadcast(Def);
	StartCooldown(Handler->GetCommonStats().Cooldown);
}

void UAbilityComponent::NotifyAbilityCancelledFromHandler(UAbilityHandler* Handler)
{
	if (!Handler || Handler != GetActiveHandler() || !bIsCasting)
	{
		return;
	}
	bIsCasting = false;
	OnAbilityCancelled.Broadcast(Handler->GetDefinition());
}

void UAbilityComponent::StartCooldown(float Duration)
{
	if (Duration <= 0.0f)
	{
		return;
	}
	CooldownTimeRemaining = Duration;
	OnCooldownStarted.Broadcast(Duration);
}

// ==================== Handler factory ====================

UAbilityHandler* UAbilityComponent::CreateHandler(UAbilityDefinition* Definition, int32 Level)
{
	if (!Definition || !Definition->HandlerClass)
	{
		return nullptr;
	}
	UAbilityHandler* Handler = NewObject<UAbilityHandler>(this, Definition->HandlerClass);
	if (Handler)
	{
		Handler->Initialize(this, Definition, Level);
	}
	return Handler;
}

void UAbilityComponent::DestroyHandler(UAbilityHandler* Handler)
{
	if (!Handler)
	{
		return;
	}
	Handler->MarkAsGarbage();
}
