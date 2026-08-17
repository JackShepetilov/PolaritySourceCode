// AbilityComponent.cpp

#include "AbilityComponent.h"
#include "AbilityDefinition.h"
#include "AbilityHandler.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "EMFVelocityModifier.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

static TAutoConsoleVariable<int32> CVarAbilityBeamDebug(
	TEXT("polarity.ability.beamdebug"),
	0,
	TEXT("Log and draw ability beams (the Tank's damage return today).\n")
	TEXT("Answers one question: does the beam actually run from the player to the enemy.\n")
	TEXT("  0 = off\n")
	TEXT("  1 = log both ends on every machine, and draw the line for 3 seconds"),
	ECVF_Default);

bool UAbilityComponent::IsBeamDebugEnabled()
{
	return CVarAbilityBeamDebug.GetValueOnAnyThread() > 0;
}

UAbilityComponent::UAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Without this the inventory and the cast state never leave the server and a client's ability
	// runs on its own machine only.
	SetIsReplicatedByDefault(true);
}

void UAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Owner-only throughout. What a teammate is carrying, whether they are mid-cast and how long
	// their cooldown has left are things only their own screen has any use for.
	DOREPLIFETIME_CONDITION(UAbilityComponent, ReplicatedSlots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAbilityComponent, ActiveSlotIndex, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAbilityComponent, bIsCasting, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAbilityComponent, CooldownTimeRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAbilityComponent, PassiveDefinition, COND_OwnerOnly);
}

// ==================== Passive channel ====================

void UAbilityComponent::GrantPassive(UAbilityDefinition* Definition)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (PassiveDefinition == Definition)
	{
		return;
	}

	PassiveDefinition = Definition;
	RebuildPassiveHandler();
}

void UAbilityComponent::OnRep_PassiveDefinition()
{
	RebuildPassiveHandler();
}

void UAbilityComponent::RebuildPassiveHandler()
{
	if (PassiveHandler && PassiveHandler->GetDefinition() == PassiveDefinition)
	{
		return;
	}

	if (PassiveHandler)
	{
		// Unequip first: a passive's whole existence is its bindings, and dropping the handler
		// without telling it would leave those bound to a garbage object.
		PassiveHandler->OnUnequip();
		DestroyHandler(PassiveHandler);
		PassiveHandler = nullptr;
	}

	if (!PassiveDefinition)
	{
		return;
	}

	// Level 1 always: there is no way to upgrade a passive yet, and inventing a path for it here
	// would be a system nobody has asked for. When class progression arrives it goes through the
	// same CreateHandler / SetLevel road the slots already use.
	PassiveHandler = CreateHandler(PassiveDefinition, 1);
	if (PassiveHandler)
	{
		// Slots get equipped by being switched to. A passive has no such moment, so this is it.
		PassiveHandler->OnEquip();
	}
}

void UAbilityComponent::NotifyOwnerDamaged(float Damage, AActor* DamageCauser, AController* InstigatedBy, const FHitResult& HitInfo)
{
	if (PassiveHandler)
	{
		PassiveHandler->OnOwnerDamaged(Damage, DamageCauser, InstigatedBy, HitInfo);
	}
}

void UAbilityComponent::Multicast_PlayBeamVFX_Implementation(UNiagaraSystem* System, USoundBase* Sound, FVector Start, FVector End, FVector Scale)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (IsBeamDebugEnabled())
	{
		// Printed on every machine that receives the call, with the role, because the two ends are
		// FVector RPC parameters and "the server had it right" is not evidence that the client did.
		const AActor* Owner = GetOwner();
		UE_LOG(LogTemp, Warning,
			TEXT("[BEAM_DEBUG] ARRIVED role=%d owner=%s start=%s end=%s len=%.0f system=%s sound=%s"),
			Owner ? (int32)Owner->GetLocalRole() : -1,
			*GetNameSafe(Owner),
			*Start.ToCompactString(), *End.ToCompactString(),
			FVector::Dist(Start, End),
			*GetNameSafe(System), *GetNameSafe(Sound));

		// Green ball where the beam starts, red where it ends, line between. If both balls sit inside
		// the enemy, the start was wrong before it ever got here; if they are correct and the effect
		// still looks wrong, the asset is not using BeamEndPoint.
		DrawDebugLine(World, Start, End, FColor::Yellow, false, 3.0f, 0, 2.0f);
		DrawDebugSphere(World, Start, 12.0f, 8, FColor::Green, false, 3.0f, 0, 1.5f);
		DrawDebugSphere(World, End, 12.0f, 8, FColor::Red, false, 3.0f, 0, 1.5f);
	}

	// Dedicated servers draw and hear nothing. Left to the engine rather than guarded here: a listen
	// server IS a machine with a screen and ears, and it has to see and hear this like everybody else.
	if (System)
	{
		UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, System, Start, FRotator::ZeroRotator, Scale, /*bAutoDestroy=*/ true);

		if (Spawned)
		{
			// Same parameter name as the channelling beams. One shot, so unlike those it is never
			// re-pointed: by the time it would matter the enemy has already been hit.
			Spawned->SetVariableVec3(FName("BeamEndPoint"), End);
		}
	}

	if (Sound)
	{
		// At the far end, not the near one: this announces the enemy taking the hit back, and a sound
		// coming from the Tank's own wound would read as him being hurt a second time.
		UGameplayStatics::PlaySoundAtLocation(World, Sound, End);
	}
}

void UAbilityComponent::PublishSlotsFromHandlers()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	ReplicatedSlots.Reset(EquippedHandlers.Num());
	for (const TObjectPtr<UAbilityHandler>& Handler : EquippedHandlers)
	{
		FAbilitySlotState Slot;
		if (Handler)
		{
			Slot.Definition = Handler->GetDefinition();
			Slot.Level = Handler->GetCurrentLevel();
		}
		ReplicatedSlots.Add(Slot);
	}
}

void UAbilityComponent::RebuildHandlersFromSlots()
{
	// Handlers that already match are kept rather than recreated: a handler can be holding state
	// (a charge in progress, a bound delegate), and rebuilding the whole list on every inventory
	// change would throw that away for slots that did not change.
	TArray<TObjectPtr<UAbilityHandler>> Rebuilt;
	Rebuilt.Reserve(ReplicatedSlots.Num());

	for (int32 i = 0; i < ReplicatedSlots.Num(); ++i)
	{
		const FAbilitySlotState& Slot = ReplicatedSlots[i];

		UAbilityHandler* Existing = EquippedHandlers.IsValidIndex(i) ? EquippedHandlers[i].Get() : nullptr;
		if (Existing && Existing->GetDefinition() == Slot.Definition && Existing->GetCurrentLevel() == Slot.Level)
		{
			Rebuilt.Add(Existing);
			continue;
		}

		if (Existing)
		{
			DestroyHandler(Existing);
		}
		Rebuilt.Add(Slot.Definition ? CreateHandler(Slot.Definition, Slot.Level) : nullptr);
	}

	// Anything past the new length is gone.
	for (int32 i = ReplicatedSlots.Num(); i < EquippedHandlers.Num(); ++i)
	{
		if (UAbilityHandler* Stale = EquippedHandlers[i].Get())
		{
			DestroyHandler(Stale);
		}
	}

	EquippedHandlers = MoveTemp(Rebuilt);
}

void UAbilityComponent::OnRep_ReplicatedSlots()
{
	RebuildHandlersFromSlots();

	// The inventory changed under the HUD's feet; it listens to these the same way it does on the
	// authority, so a client's ability bar is built by the same path.
	OnAbilityAdded.Broadcast(ActiveSlotIndex);
}

void UAbilityComponent::OnRep_ActiveSlotIndex()
{
	OnAbilitySwitched.Broadcast(ActiveSlotIndex);
}

void UAbilityComponent::Server_TryActivate_Implementation()
{
	TryActivate();
}

void UAbilityComponent::Server_OnButtonReleased_Implementation()
{
	OnButtonReleased();
}

void UAbilityComponent::Server_SwitchToSlot_Implementation(int32 SlotIndex)
{
	SwitchToSlot(SlotIndex);
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

	if (PassiveHandler)
	{
		PassiveHandler->OnUnequip();
		DestroyHandler(PassiveHandler);
		PassiveHandler = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UAbilityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();

	// The cooldown runs down on the authority and is replicated. A client counting its own would
	// drift from the machine that decides whether the next activation is allowed, and the two would
	// disagree at exactly the moment it matters.
	if (bAuthority && CooldownTimeRemaining > 0.0f)
	{
		CooldownTimeRemaining = FMath::Max(0.0f, CooldownTimeRemaining - DeltaTime);
		if (CooldownTimeRemaining <= 0.0f)
		{
			OnCooldownEnded.Broadcast();
		}
	}

	// Republished from the live handlers rather than from each of the five places that can change
	// the inventory. Missing one of those would leave a client holding a stale slot, and the array is
	// at most eight entries; the engine only puts it on the wire when it actually differs.
	if (bAuthority)
	{
		PublishSlotsFromHandlers();
	}

	// The passive gets the tick on every machine. Its own guards decide what it may do there, the
	// same way the handler decides what it may do anywhere else.
	if (PassiveHandler)
	{
		PassiveHandler->OnPassiveTick(DeltaTime);
	}
}

// ==================== Inventory ====================

int32 UAbilityComponent::AddAbility(UAbilityDefinition* Definition, int32 Level)
{
	if (!Definition || !Definition->HandlerClass)
	{
		return INDEX_NONE;
	}

	// Inventory is the authority's. Pickups already live on the server, so this is a guard rather
	// than a change of behaviour — but a client that granted itself an ability would hold a handler
	// nothing else knows about, and the mismatch would surface somewhere else entirely.
	if (AActor* Owner = GetOwner())
	{
		if (!Owner->HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s tried to grant itself '%s' on a client - refused"),
				*GetNameSafe(Owner), *GetNameSafe(Definition));
			return INDEX_NONE;
		}
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

	// Unlike activation, switching decides nothing about the world, so the client changes at once and
	// tells the server, which will agree — the same shape the weapon switch already uses. Waiting a
	// round trip to see your own selection move is the kind of delay players read as input lag.
	if (AActor* Owner = GetOwner())
	{
		if (!Owner->HasAuthority())
		{
			Server_SwitchToSlot(SlotIndex);
		}
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
	// A client asks; it does not decide. An ability changes the world — who is slowed, what takes
	// damage — and running it locally would change nothing anywhere else, which is exactly how a
	// client's melee, its knockback and its ability all used to "work" on one screen only.
	//
	// Nothing is predicted here. The gate below is cheap and the round trip is short, and guessing
	// wrong would mean playing a cast that the server then refuses. Cosmetic prediction, if any turns
	// out to be wanted, belongs in the individual handler where the ability's own timing is known.
	if (AActor* Owner = GetOwner())
	{
		if (!Owner->HasAuthority())
		{
			Server_TryActivate();
			return false;
		}
	}

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
	if (AActor* Owner = GetOwner())
	{
		if (!Owner->HasAuthority())
		{
			Server_OnButtonReleased();
			return;
		}
	}

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
