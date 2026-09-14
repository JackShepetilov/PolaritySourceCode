// BuilderComponent.cpp

#include "BuilderComponent.h"

#include "BuildableActor.h"
#include "BuildablePreview.h"
#include "TurretBuildable.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Coop/CoopPlayers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

UBuilderComponent::UBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UBuilderComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UBuilderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The pawn is going (death, level change): nothing of the menu or the ghost may outlive it.
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DemolishHoldTimer);
	}
	DestroyPreview();
	PopMapping(PlacementMappingContext);
	PopMapping(MenuMappingContext);
	FeedTarget.Reset();
	Mode = EBuilderMode::Idle;
	Super::EndPlay(EndPlayReason);
}

// ==================== Wiring ====================

AShooterCharacter* UBuilderComponent::GetCharacter() const
{
	return Cast<AShooterCharacter>(GetOwner());
}

bool UBuilderComponent::IsLocallyControlled() const
{
	const AShooterCharacter* const Character = GetCharacter();
	return Character && Character->IsLocallyControlled();
}

AShooterPlayerState* UBuilderComponent::GetOwnerPlayerState() const
{
	const AShooterCharacter* const Character = GetCharacter();
	return Character ? Character->GetPlayerState<AShooterPlayerState>() : nullptr;
}

void UBuilderComponent::SetupInput(UEnhancedInputComponent* Input)
{
	if (!Input)
	{
		return;
	}

	if (BuildMenuAction)
	{
		Input->BindAction(BuildMenuAction, ETriggerEvent::Started, this, &UBuilderComponent::ToggleMenu);
	}

	// The slot keys are bound once and for all; they only ever fire while MenuMappingContext is
	// pushed, because that is the only context that maps them.
	for (int32 SlotIndex = 0; SlotIndex < SlotActions.Num(); ++SlotIndex)
	{
		UInputAction* const SlotAction = SlotActions[SlotIndex];
		if (!SlotAction)
		{
			continue;
		}
		Input->BindActionValueLambda(SlotAction, ETriggerEvent::Started,
			[this, SlotIndex](const FInputActionValue&) { HandleSlotPressed(SlotIndex); });
		Input->BindActionValueLambda(SlotAction, ETriggerEvent::Completed,
			[this, SlotIndex](const FInputActionValue&) { HandleSlotReleased(SlotIndex); });
	}

	if (ConfirmAction)
	{
		Input->BindAction(ConfirmAction, ETriggerEvent::Started, this, &UBuilderComponent::ConfirmPlacement);
	}
	if (RotateAction)
	{
		Input->BindAction(RotateAction, ETriggerEvent::Started, this, &UBuilderComponent::RotatePreview);
	}
	if (CancelAction)
	{
		Input->BindAction(CancelAction, ETriggerEvent::Started, this, &UBuilderComponent::HandleCancelPressed);
	}
	if (FeedAction)
	{
		Input->BindAction(FeedAction, ETriggerEvent::Started, this, &UBuilderComponent::ToggleFeedMenu);
	}
}

// ==================== Feeding a turret ====================

ATurretBuildable* UBuilderComponent::FindTurretUnderAim() const
{
	const AShooterCharacter* const Character = GetCharacter();
	if (!Character || !GetWorld())
	{
		return nullptr;
	}
	// Generous ray, then the turret's own reach decides: every turret may set its own.
	FVector Start, End;
	Character->GetAimRay(1000.0f, Start, End);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurretFeedAim), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Character);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return nullptr;
	}
	ATurretBuildable* const Turret = Cast<ATurretBuildable>(Hit.GetActor());
	if (!Turret || Turret->IsDestroyed() || Hit.Distance > Turret->FeedReachCm)
	{
		return nullptr;
	}
	return Turret;
}

ATurretBuildable* UBuilderComponent::GetFeedTarget() const
{
	return FeedTarget.Get();
}

const ATurretBuildable* UBuilderComponent::GetFeedTurretDefaults() const
{
	if (Mode == EBuilderMode::Feeding)
	{
		return FeedTarget.Get();
	}
	if (Mode == EBuilderMode::PickingWeapon || Mode == EBuilderMode::Placing)
	{
		const UBuildableDefinition* const Def = GetDefinition(PlacingSlot);
		return (Def && Def->ActorClass) ? Cast<ATurretBuildable>(Def->ActorClass->GetDefaultObject()) : nullptr;
	}
	return nullptr;
}

void UBuilderComponent::GetFeedWeapons(TArray<AShooterWeapon*>& OutWeapons) const
{
	OutWeapons.Reset();
	const AShooterCharacter* const Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	// Every ranged gun the player owns, in inventory order (the class weapon first, the looted
	// one after it), which is also the order of the hotkeys.
	for (AShooterWeapon* const Weapon : Character->GetOwnedWeapons())
	{
		if (IsValid(Weapon) && !Weapon->IsMeleeWeapon())
		{
			OutWeapons.Add(Weapon);
		}
	}
}

void UBuilderComponent::ToggleFeedMenu()
{
	if (!IsLocallyControlled())
	{
		return;
	}
	if (Mode == EBuilderMode::Feeding)
	{
		CloseFeedMenu();
		return;
	}
	if (Mode != EBuilderMode::Idle)
	{
		return;
	}
	const AShooterCharacter* const Character = GetCharacter();
	ATurretBuildable* const Turret = FindTurretUnderAim();
	if (!Character || !Turret)
	{
		// Say what the aim did find, so "F does nothing" reads as "you were 4 m away" or "you were
		// looking at the wall next to it" rather than as a dead key.
		FString What = TEXT("nothing");
		if (Character && GetWorld())
		{
			FVector Start, End;
			Character->GetAimRay(1000.0f, Start, End);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TurretFeedAim), false);
			Params.AddIgnoredActor(Character);
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				const ATurretBuildable* const Far = Cast<ATurretBuildable>(Hit.GetActor());
				What = Far
					? FString::Printf(TEXT("%s at %.0f cm, reach is %.0f"), *Far->GetName(), Hit.Distance, Far->FeedReachCm)
					: FString::Printf(TEXT("%s at %.0f cm"), *GetNameSafe(Hit.GetActor()), Hit.Distance);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] feed: no turret under the aim (aim found %s)"), *What);
		return;
	}
	if (!Turret->IsActive())
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] feed: %s is not standing yet"), *Turret->GetName());
		return;
	}
	TArray<AShooterWeapon*> Weapons;
	GetFeedWeapons(Weapons);
	if (Weapons.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] feed: %s owns no gun to give"), *Character->GetName());
		return;
	}
	FeedTarget = Turret;
	PushMapping(MenuMappingContext);
	SetMode(EBuilderMode::Feeding);
}

void UBuilderComponent::CloseFeedMenu()
{
	if (Mode != EBuilderMode::Feeding)
	{
		return;
	}
	PopMapping(MenuMappingContext);
	FeedTarget.Reset();
	SetMode(EBuilderMode::Idle);
}

void UBuilderComponent::FeedWeapon(int32 WeaponIndex)
{
	if (Mode != EBuilderMode::Feeding || !IsLocallyControlled())
	{
		return;
	}
	ATurretBuildable* const Turret = FeedTarget.Get();
	TArray<AShooterWeapon*> Weapons;
	GetFeedWeapons(Weapons);
	if (!Turret || !Weapons.IsValidIndex(WeaponIndex))
	{
		return;
	}
	AShooterWeapon* const Weapon = Weapons[WeaponIndex];
	// The same question the server will ask, asked here first so a refusal is instant and local:
	// the row flashes instead of a round trip ending in silence.
	int32 ViceIndex = INDEX_NONE;
	if (!Turret->FindViceFor(Weapon->GetClass(), ViceIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] feed: %s has no vice for %s"), *Turret->GetName(), *Weapon->GetClass()->GetName());
		OnFeedRefused.Broadcast(WeaponIndex);
		return;
	}
	Server_FeedTurret(Turret, Weapon, Weapon->GetBulletCount());
	CloseFeedMenu();
}

void UBuilderComponent::Server_FeedTurret_Implementation(ATurretBuildable* Turret, AShooterWeapon* Weapon, int32 ReportedLoadedRounds)
{
	AShooterCharacter* const Character = GetCharacter();
	if (!Character || !Turret || Turret->IsDestroyed())
	{
		return;
	}
	// Reach is checked from the pawn, not re-traced: the client aimed at it a round trip ago and
	// may have turned since. What matters is that the turret is next to them.
	const float Reach = Turret->FeedReachCm + 150.0f;
	if (FVector::DistSquared(Character->GetActorLocation(), Turret->GetActorLocation()) > Reach * Reach)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] feed refused: %s is too far from %s"), *Character->GetName(), *Turret->GetName());
		return;
	}
	Turret->AcceptWeaponFrom(Character, ReportedLoadedRounds, Weapon);
}

UEnhancedInputLocalPlayerSubsystem* UBuilderComponent::ResolveInputSubsystem()
{
	// Through the controller while there is one. A dying pawn has already been detached from its
	// controller when EndPlay runs (APawn::Destroyed detaches first), so the local player is
	// remembered from the push: a context left behind on it would keep eating the number keys.
	const AShooterCharacter* const Character = GetCharacter();
	const APlayerController* const PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (PC && PC->GetLocalPlayer())
	{
		MappedLocalPlayer = PC->GetLocalPlayer();
	}
	const ULocalPlayer* const LocalPlayer = MappedLocalPlayer.Get();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

void UBuilderComponent::PushMapping(UInputMappingContext* Context)
{
	UEnhancedInputLocalPlayerSubsystem* const Subsystem = ResolveInputSubsystem();
	if (Subsystem && Context && !Subsystem->HasMappingContext(Context))
	{
		Subsystem->AddMappingContext(Context, MappingPriority);
	}
}

void UBuilderComponent::PopMapping(UInputMappingContext* Context)
{
	UEnhancedInputLocalPlayerSubsystem* const Subsystem = ResolveInputSubsystem();
	if (Subsystem && Context && Subsystem->HasMappingContext(Context))
	{
		Subsystem->RemoveMappingContext(Context);
	}
}

void UBuilderComponent::SetMode(EBuilderMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}
	Mode = NewMode;
	SetComponentTickEnabled(Mode == EBuilderMode::Placing || Mode == EBuilderMode::Feeding);
	OnModeChanged.Broadcast(Mode);
}

// ==================== Queries ====================

UBuildableDefinition* UBuilderComponent::GetDefinition(int32 SlotIndex) const
{
	return IsValidSlot(SlotIndex) ? Buildables[SlotIndex].Get() : nullptr;
}

bool UBuilderComponent::CanAfford(int32 SlotIndex) const
{
	const UBuildableDefinition* const Def = GetDefinition(SlotIndex);
	const AShooterPlayerState* const State = GetOwnerPlayerState();
	return Def && State && State->CanAffordMetal(Def->MetalCost);
}

int32 UBuilderComponent::CountBuilt(int32 SlotIndex) const
{
	const UBuildableDefinition* const Def = GetDefinition(SlotIndex);
	const AShooterPlayerState* const State = GetOwnerPlayerState();
	return (Def && State) ? State->CountOwnedBuildables(Def) : 0;
}

int32 UBuilderComponent::GetMaxCount(int32 SlotIndex) const
{
	const UBuildableDefinition* const Def = GetDefinition(SlotIndex);
	return Def ? Def->MaxCountPerPlayer : 0;
}

// ==================== Menu ====================

void UBuilderComponent::ToggleMenu()
{
	switch (Mode)
	{
	case EBuilderMode::Menu:
	case EBuilderMode::PickingWeapon:
		CloseMenu();
		break;
	case EBuilderMode::Placing:
		// One press goes one step back: the ghost goes away and the menu comes up again.
		CancelPlacement();
		OpenMenu();
		break;
	default:
		OpenMenu();
		break;
	}
}

void UBuilderComponent::OpenMenu()
{
	// One menu at a time: the build key over the feed menu swaps them.
	CloseFeedMenu();
	const AShooterCharacter* const Character = GetCharacter();
	if (!Character || !IsLocallyControlled() || Character->IsDead() || Mode != EBuilderMode::Idle)
	{
		return;
	}
	if (Buildables.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BUILD_DEBUG] %s has nothing to build: Buildables is empty on the builder component"), *Character->GetName());
		return;
	}
	PushMapping(MenuMappingContext);
	SetMode(EBuilderMode::Menu);
}

void UBuilderComponent::CloseMenu()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DemolishHoldTimer);
	}
	DemolishHoldSlot = -1;
	PopMapping(MenuMappingContext);
	if (Mode == EBuilderMode::Menu || Mode == EBuilderMode::PickingWeapon)
	{
		PendingFeedWeapon.Reset();
		SetMode(EBuilderMode::Idle);
	}
}

bool UBuilderComponent::IsTurretSlot(int32 SlotIndex) const
{
	const UBuildableDefinition* const Def = GetDefinition(SlotIndex);
	return Def && Def->ActorClass && Def->ActorClass->IsChildOf(ATurretBuildable::StaticClass());
}

void UBuilderComponent::BeginWeaponPick(int32 SlotIndex)
{
	// The same refusals the ghost would give, first: a full slot or an empty purse should not open
	// a gun list that leads nowhere.
	if (CountBuilt(SlotIndex) >= GetMaxCount(SlotIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] slot %d refused: limit %d reached"), SlotIndex, GetMaxCount(SlotIndex));
		OnSlotRefused.Broadcast(SlotIndex, EBuildablePlacementResult::Blocked);
		return;
	}
	if (!CanAfford(SlotIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] slot %d refused: cannot afford %d metal"), SlotIndex, Buildables[SlotIndex]->MetalCost);
		OnSlotRefused.Broadcast(SlotIndex, EBuildablePlacementResult::Blocked);
		return;
	}
	TArray<AShooterWeapon*> Weapons;
	GetFeedWeapons(Weapons);
	if (Weapons.Num() == 0)
	{
		// A turret is a vice for a gun; with no gun to give there is nothing to place.
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] slot %d refused: no gun to give the turret"), SlotIndex);
		OnSlotRefused.Broadcast(SlotIndex, EBuildablePlacementResult::Blocked);
		return;
	}
	PlacingSlot = SlotIndex;
	PendingFeedWeapon.Reset();
	// The menu context stays: the number keys now pick a gun.
	SetMode(EBuilderMode::PickingWeapon);
}

void UBuilderComponent::PickWeaponForPlacement(int32 WeaponIndex)
{
	if (Mode != EBuilderMode::PickingWeapon || !IsLocallyControlled())
	{
		return;
	}
	TArray<AShooterWeapon*> Weapons;
	GetFeedWeapons(Weapons);
	const ATurretBuildable* const Fresh = GetFeedTurretDefaults();
	if (!Weapons.IsValidIndex(WeaponIndex) || !Fresh)
	{
		return;
	}
	AShooterWeapon* const Weapon = Weapons[WeaponIndex];
	int32 ViceIndex = INDEX_NONE;
	if (!Fresh->FindViceFor(Weapon->GetClass(), ViceIndex))
	{
		// A heavy gun into a fresh turret: its vice opens at level 3.
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] pick: a new turret has no vice for %s"), *Weapon->GetClass()->GetName());
		OnFeedRefused.Broadcast(WeaponIndex);
		return;
	}
	const int32 SlotIndex = PlacingSlot;
	PendingFeedWeapon = Weapon;
	// Straight on to the ghost. BeginPlacement closes the menu, which would clear the pick, so the
	// pick is set again after it.
	BeginPlacement(SlotIndex);
	PendingFeedWeapon = Weapon;
}

void UBuilderComponent::HandleSlotPressed(int32 SlotIndex)
{
	if (Mode == EBuilderMode::Feeding)
	{
		FeedWeapon(SlotIndex);
		return;
	}
	if (Mode == EBuilderMode::PickingWeapon)
	{
		PickWeaponForPlacement(SlotIndex);
		return;
	}
	if (Mode != EBuilderMode::Menu || !IsValidSlot(SlotIndex))
	{
		return;
	}

	if (CountBuilt(SlotIndex) >= GetMaxCount(SlotIndex))
	{
		// Full: the key now means "take one down", and it has to be held so a slip does not cost
		// a building. The release before the timer clears it.
		DemolishHoldSlot = SlotIndex;
		GetWorld()->GetTimerManager().SetTimer(DemolishHoldTimer, this, &UBuilderComponent::HandleDemolishHoldFired,
			DemolishHoldSeconds, false);
		return;
	}

	if (IsTurretSlot(SlotIndex))
	{
		BeginWeaponPick(SlotIndex);
		return;
	}
	BeginPlacement(SlotIndex);
}

void UBuilderComponent::HandleSlotReleased(int32 SlotIndex)
{
	if (DemolishHoldSlot == SlotIndex)
	{
		GetWorld()->GetTimerManager().ClearTimer(DemolishHoldTimer);
		DemolishHoldSlot = -1;
	}
}

void UBuilderComponent::HandleDemolishHoldFired()
{
	const int32 SlotIndex = DemolishHoldSlot;
	DemolishHoldSlot = -1;
	if (Mode == EBuilderMode::Menu && IsValidSlot(SlotIndex))
	{
		RequestDemolish(SlotIndex);
		CloseMenu();
	}
}

void UBuilderComponent::HandleCancelPressed()
{
	if (Mode == EBuilderMode::Placing)
	{
		CancelPlacement();
	}
	else if (Mode == EBuilderMode::Menu)
	{
		CloseMenu();
	}
	else if (Mode == EBuilderMode::Feeding)
	{
		CloseFeedMenu();
	}
	else if (Mode == EBuilderMode::PickingWeapon)
	{
		// One step back: the gun list goes, the build menu is up again.
		PendingFeedWeapon.Reset();
		SetMode(EBuilderMode::Menu);
	}
}

void UBuilderComponent::RequestDemolish(int32 SlotIndex)
{
	if (!IsValidSlot(SlotIndex) || !IsLocallyControlled())
	{
		return;
	}
	Server_DemolishBuildable(SlotIndex);
}

// ==================== Placement ====================

void UBuilderComponent::BeginPlacement(int32 SlotIndex)
{
	AShooterCharacter* const Character = GetCharacter();
	if (!Character || !IsLocallyControlled() || Character->IsDead() || !IsValidSlot(SlotIndex))
	{
		return;
	}

	if (CountBuilt(SlotIndex) >= GetMaxCount(SlotIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] slot %d refused: limit %d reached"), SlotIndex, GetMaxCount(SlotIndex));
		OnSlotRefused.Broadcast(SlotIndex, EBuildablePlacementResult::Blocked);
		return;
	}
	if (!CanAfford(SlotIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] slot %d refused: cannot afford %d metal"), SlotIndex, Buildables[SlotIndex]->MetalCost);
		OnSlotRefused.Broadcast(SlotIndex, EBuildablePlacementResult::Blocked);
		return;
	}

	if (IsTurretSlot(SlotIndex) && !PendingFeedWeapon.IsValid())
	{
		// A turret is never placed empty: the gun is chosen first, then the ghost.
		BeginWeaponPick(SlotIndex);
		return;
	}

	if (Mode == EBuilderMode::Placing)
	{
		CancelPlacement();
	}
	CloseMenu();

	PlacingSlot = SlotIndex;
	PreviewYawOffset = 0.0f;
	LastPlacementResult = EBuildablePlacementResult::NoGround;

	UWorld* const World = GetWorld();
	FActorSpawnParameters Params;
	Params.Owner = Character;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const TSubclassOf<ABuildablePreview> ClassToSpawn = PreviewClass ? PreviewClass : TSubclassOf<ABuildablePreview>(ABuildablePreview::StaticClass());
	Preview = World->SpawnActor<ABuildablePreview>(ClassToSpawn, Character->GetActorTransform(), Params);
	if (Preview)
	{
		Preview->SetupFromDefinition(Buildables[SlotIndex]);
	}

	// The toolbox comes out: the character's own holster, so every gate it already closes (fire,
	// reload, abilities) is closed here too. It may refuse (mid-swing, mid-cast); the placement
	// still works, only the gun stays in hand.
	bHolsteredForPlacement = false;
	if (bHolsterWeaponWhilePlacing && !Character->IsWeaponHolsteredByPlayer())
	{
		Character->HolsterWeaponByPlayer();
		bHolsteredForPlacement = Character->IsWeaponHolsteredByPlayer();
	}

	PushMapping(PlacementMappingContext);
	SetMode(EBuilderMode::Placing);
}

void UBuilderComponent::CancelPlacement()
{
	if (Mode != EBuilderMode::Placing)
	{
		return;
	}
	DestroyPreview();
	PopMapping(PlacementMappingContext);

	if (bHolsteredForPlacement)
	{
		if (AShooterCharacter* const Character = GetCharacter())
		{
			Character->DrawHolsteredWeapon();
		}
		bHolsteredForPlacement = false;
	}

	PlacingSlot = -1;
	PendingFeedWeapon.Reset();
	SetMode(EBuilderMode::Idle);
}

void UBuilderComponent::ConfirmPlacement()
{
	if (Mode != EBuilderMode::Placing || !IsLocallyControlled())
	{
		return;
	}
	if (LastPlacementResult != EBuildablePlacementResult::Valid)
	{
		OnSlotRefused.Broadcast(PlacingSlot, LastPlacementResult);
		return;
	}

	const int32 SlotIndex = PlacingSlot;
	const FTransform Transform = LastPlacementTransform;
	AShooterWeapon* const Weapon = PendingFeedWeapon.Get();
	if (IsTurretSlot(SlotIndex) && !Weapon)
	{
		// The gun went away while the ghost was out (dropped, taken by a pickup): no turret.
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] place refused: the chosen gun is gone"));
		OnSlotRefused.Broadcast(SlotIndex, EBuildablePlacementResult::Blocked);
		CancelPlacement();
		return;
	}
	// The request goes first, while the gun is still holstered: on the host it runs right here,
	// and the turret takes the gun before the hands reach for it. Then the ghost leaves on the
	// press, not on the answer: a round trip of waiting would read as a stuck button. If the
	// server refuses, nothing appears and the metal never moved.
	Server_PlaceBuildable(SlotIndex, Transform, Weapon, Weapon ? Weapon->GetBulletCount() : -1);
	CancelPlacement();
}

void UBuilderComponent::RotatePreview()
{
	if (Mode == EBuilderMode::Placing)
	{
		PreviewYawOffset = FMath::Fmod(PreviewYawOffset + RotateStepDegrees, 360.0f);
	}
}

void UBuilderComponent::DestroyPreview()
{
	if (Preview)
	{
		Preview->Destroy();
		Preview = nullptr;
	}
}

bool UBuilderComponent::ComputePlacementTransform(FTransform& OutTransform) const
{
	const AShooterCharacter* const Character = GetCharacter();
	const UBuildableDefinition* const Def = GetDefinition(PlacingSlot);
	UWorld* const World = GetWorld();
	if (!Character || !Def || !World)
	{
		return false;
	}

	// Along the view, as far as the reach allows or until something is in the way; then straight
	// down to the floor. What the player looks at is where it goes, at their feet if they look up.
	const UCameraComponent* const Camera = Character->GetFirstPersonCameraComponent();
	const FVector Start = Camera ? Camera->GetComponentLocation() : Character->GetPawnViewLocation();
	const FVector Direction = Camera ? Camera->GetForwardVector() : Character->GetControlRotation().Vector();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildableAim), false);
	Params.AddIgnoredActor(Character);
	if (Preview)
	{
		Params.AddIgnoredActor(Preview);
	}

	FVector AimPoint = Start + Direction * Def->MaxPlaceDistance;
	FHitResult ViewHit;
	if (World->LineTraceSingleByChannel(ViewHit, Start, AimPoint, ECC_Visibility, Params))
	{
		AimPoint = ViewHit.ImpactPoint + ViewHit.ImpactNormal * 5.0f;
	}

	// Never further from the feet than the reach, whatever the view ray did.
	const FVector Feet = Character->GetActorLocation();
	FVector Flat = AimPoint - Feet;
	Flat.Z = 0.0f;
	if (Flat.SizeSquared() > FMath::Square(Def->MaxPlaceDistance))
	{
		AimPoint = Feet + Flat.GetSafeNormal() * Def->MaxPlaceDistance + FVector(0.0f, 0.0f, AimPoint.Z - Feet.Z);
	}

	FHitResult Floor;
	const FVector Above = AimPoint + FVector(0.0f, 0.0f, 50.0f);
	const FVector Below = AimPoint - FVector(0.0f, 0.0f, GroundSearchDepth);
	if (!World->LineTraceSingleByChannel(Floor, Above, Below, ECC_Visibility, Params))
	{
		return false;
	}

	const float Yaw = Character->GetActorRotation().Yaw + PreviewYawOffset;
	OutTransform = FTransform(FRotator(0.0f, Yaw, 0.0f), Floor.ImpactPoint, FVector::OneVector);
	return true;
}

void UBuilderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Mode == EBuilderMode::Feeding)
	{
		// The menu is a conversation with one turret: it ends when the turret goes, or the player
		// walks off, or dies.
		const AShooterCharacter* const Character = GetCharacter();
		const ATurretBuildable* const Turret = FeedTarget.Get();
		const float Reach = Turret ? Turret->FeedReachCm + 100.0f : 0.0f;
		if (!Character || Character->IsDead() || !Turret || Turret->IsDestroyed()
			|| FVector::DistSquared(Character->GetActorLocation(), Turret->GetActorLocation()) > Reach * Reach)
		{
			CloseFeedMenu();
		}
		return;
	}

	if (Mode != EBuilderMode::Placing)
	{
		return;
	}
	AShooterCharacter* const Character = GetCharacter();
	if (!Character || !IsLocallyControlled() || Character->IsDead())
	{
		CancelPlacement();
		return;
	}

	const UBuildableDefinition* const Def = GetDefinition(PlacingSlot);
	EBuildablePlacementResult Result = EBuildablePlacementResult::NoGround;
	FTransform Transform;
	if (Def && ComputePlacementTransform(Transform))
	{
		TArray<AActor*> Ignore;
		Ignore.Add(Character);
		if (Preview)
		{
			Ignore.Add(Preview);
		}
		Result = Def->ValidatePlacement(GetWorld(), Transform, Ignore);
		LastPlacementTransform = Transform;
		if (Preview)
		{
			Preview->SetActorTransform(Transform);
			Preview->SetActorHiddenInGame(false);
		}
	}
	else if (Preview)
	{
		// Nowhere to stand: no ghost at all rather than one floating in the air.
		Preview->SetActorHiddenInGame(true);
	}

	if (Preview)
	{
		Preview->SetValid(Result == EBuildablePlacementResult::Valid);
	}
	if (Result != LastPlacementResult)
	{
		LastPlacementResult = Result;
		OnPlacementUpdated.Broadcast(PlacingSlot, Result);
	}
}

// ==================== Server ====================

void UBuilderComponent::Server_PlaceBuildable_Implementation(int32 SlotIndex, FTransform Transform, AShooterWeapon* Weapon, int32 ReportedLoadedRounds)
{
	AShooterCharacter* const Character = GetCharacter();
	if (IsTurretSlot(SlotIndex))
	{
		// No gun, no turret: checked before anything is spent or spawned, and checked against the
		// donor's own list, because the client names an actor and an actor is not a proof.
		if (!Character || !IsValid(Weapon) || Weapon->IsMeleeWeapon() || !Character->GetOwnedWeapons().Contains(Weapon))
		{
			UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] place refused: %s did not name one of their guns (%s)"),
				*GetNameSafe(Character), *GetNameSafe(Weapon));
			return;
		}
	}
	ABuildableActor* const Placed = SpawnBuildable(SlotIndex, Transform, true);
	ATurretBuildable* const Turret = Cast<ATurretBuildable>(Placed);
	if (Turret && !Turret->AcceptWeaponFrom(Character, ReportedLoadedRounds, Weapon))
	{
		// It refused after all (the gun changed hands in the same tick): a turret with nothing in
		// it is not a turret. Take it down and give the metal back.
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s would not take %s at placement; demolished, metal returned"),
			*Turret->GetName(), *GetNameSafe(Weapon));
		if (AShooterPlayerState* const State = GetOwnerPlayerState())
		{
			if (const UBuildableDefinition* const Def = GetDefinition(SlotIndex))
			{
				State->AddMetal(Def->MetalCost);
			}
		}
		Turret->Demolish();
	}
}

void UBuilderComponent::Server_DemolishBuildable_Implementation(int32 SlotIndex)
{
	const UBuildableDefinition* const Def = GetDefinition(SlotIndex);
	AShooterPlayerState* const State = GetOwnerPlayerState();
	if (!Def || !State)
	{
		return;
	}
	TArray<ABuildableActor*> Standing;
	State->GetOwnedBuildablesOfKind(Def, Standing);
	if (Standing.Num() > 0 && Standing[0])
	{
		// The oldest goes first: the order of the list is the order they were placed.
		Standing[0]->Demolish();
	}
}

ABuildableActor* UBuilderComponent::SpawnBuildable(int32 SlotIndex, const FTransform& Transform, bool bCharge)
{
	AShooterCharacter* const Character = GetCharacter();
	UWorld* const World = GetWorld();
	if (!Character || !World || !Character->HasAuthority())
	{
		return nullptr;
	}

	UBuildableDefinition* const Def = GetDefinition(SlotIndex);
	AShooterPlayerState* const State = GetOwnerPlayerState();
	if (!Def || !Def->ActorClass || !State)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BUILD_DEBUG] %s: slot %d has no definition, no actor class or no player state"), *Character->GetName(), SlotIndex);
		return nullptr;
	}

	if (State->CountOwnedBuildables(Def) >= Def->MaxCountPerPlayer)
	{
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s: %s refused, limit %d reached"), *State->GetPlayerName(), *Def->GetName(), Def->MaxCountPerPlayer);
		return nullptr;
	}

	TArray<AActor*> Ignore;
	Ignore.Add(Character);
	const EBuildablePlacementResult Verdict = Def->ValidatePlacement(World, Transform, Ignore, Character);
	if (Verdict != EBuildablePlacementResult::Valid)
	{
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s: %s refused at %s, verdict %d"), *State->GetPlayerName(), *Def->GetName(),
			*Transform.GetLocation().ToCompactString(), static_cast<int32>(Verdict));
		return nullptr;
	}

	if (bCharge && !State->TrySpendMetal(Def->MetalCost))
	{
		return nullptr;
	}

	// Deferred, so the building knows what it is and whose it is before its BeginPlay runs.
	ABuildableActor* const Buildable = World->SpawnActorDeferred<ABuildableActor>(Def->ActorClass, Transform, nullptr, Character,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Buildable)
	{
		UE_LOG(LogTemp, Error, TEXT("[BUILD_DEBUG] %s: SpawnActorDeferred failed for %s"), *State->GetPlayerName(), *Def->GetName());
		// The metal was taken for nothing; give it back rather than swallow it.
		if (bCharge)
		{
			State->AddMetal(Def->MetalCost);
		}
		return nullptr;
	}
	Buildable->InitializeBuildable(Def, State);
	Buildable->FinishSpawning(Transform);
	State->RegisterBuildable(Buildable);

	UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s placed %s at %s%s"), *State->GetPlayerName(), *Def->GetName(),
		*Transform.GetLocation().ToCompactString(), bCharge ? TEXT("") : TEXT(" (free)"));
	return Buildable;
}

ABuildableActor* UBuilderComponent::DebugPlace(int32 SlotIndex)
{
	const AShooterCharacter* const Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !IsValidSlot(SlotIndex))
	{
		return nullptr;
	}
	// The ghost's own aim, borrowed for a moment: PlacingSlot is what ComputePlacementTransform reads.
	const int32 SavedSlot = PlacingSlot;
	PlacingSlot = SlotIndex;
	FTransform Transform;
	const bool bFound = ComputePlacementTransform(Transform);
	PlacingSlot = SavedSlot;
	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BUILD_DEBUG] no floor under the aim point"));
		return nullptr;
	}
	ABuildableActor* const Placed = SpawnBuildable(SlotIndex, Transform, false);
	// The debug command is allowed an empty turret (it can be fed with polarity.turret.feed), but
	// takes the held gun when there is one, so the usual test needs one command less.
	if (ATurretBuildable* const Turret = Cast<ATurretBuildable>(Placed))
	{
		if (AShooterCharacter* const Owner = GetCharacter(); Owner && Owner->GetCurrentWeapon())
		{
			Turret->AcceptWeaponFrom(Owner, -1, nullptr);
		}
	}
	return Placed;
}

// ==================== Console ====================

namespace BuildDebug
{
	/** The builder of the player at THIS screen, on the authority. Same shape as MetalDebug. */
	static UBuilderComponent* FindLocalAuthoritativeBuilder(UWorld* World)
	{
		APlayerController* const PC = CoopPlayers::GetLocalController(World);
		AShooterCharacter* const Character = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
		UBuilderComponent* const Builder = Character ? Character->GetBuilderComponent() : nullptr;
		if (!Builder)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BUILD_DEBUG] no builder component on the local pawn"));
			return nullptr;
		}
		if (!Character->HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("[BUILD_DEBUG] build commands only work on the host or in standalone"));
			return nullptr;
		}
		return Builder;
	}

	static int32 ParseInt(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	/** Closest of the local player's standing buildings, or null. */
	static ABuildableActor* FindNearestOwned(UBuilderComponent* Builder)
	{
		AShooterPlayerState* const State = Builder->GetOwnerPlayerState();
		const AActor* const Pawn = Builder->GetOwner();
		if (!State || !Pawn)
		{
			return nullptr;
		}
		ABuildableActor* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (ABuildableActor* Buildable : State->GetOwnedBuildables())
		{
			if (!Buildable || Buildable->IsDestroyed())
			{
				continue;
			}
			const float Dist = FVector::DistSquared(Buildable->GetActorLocation(), Pawn->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = Buildable;
			}
		}
		return Best;
	}

	static void CmdPlace(const TArray<FString>& Args, UWorld* World)
	{
		if (UBuilderComponent* const Builder = FindLocalAuthoritativeBuilder(World))
		{
			Builder->DebugPlace(ParseInt(Args, 0, 0));
		}
	}

	static void CmdDemolish(const TArray<FString>& Args, UWorld* World)
	{
		UBuilderComponent* const Builder = FindLocalAuthoritativeBuilder(World);
		if (!Builder)
		{
			return;
		}
		if (ABuildableActor* const Target = FindNearestOwned(Builder))
		{
			Target->Demolish();
		}
	}

	static void CmdDamage(const TArray<FString>& Args, UWorld* World)
	{
		UBuilderComponent* const Builder = FindLocalAuthoritativeBuilder(World);
		if (!Builder)
		{
			return;
		}
		if (ABuildableActor* const Target = FindNearestOwned(Builder))
		{
			const float Amount = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 50.0f;
			Target->ApplyBuildableDamage(Amount);
			UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s hit for %.0f -> %.0f/%.0f"), *Target->GetName(), Amount, Target->GetHealth(), Target->GetMaxHealth());
		}
	}

	static void CmdWrench(const TArray<FString>& Args, UWorld* World)
	{
		UBuilderComponent* const Builder = FindLocalAuthoritativeBuilder(World);
		if (!Builder)
		{
			return;
		}
		if (ABuildableActor* const Target = FindNearestOwned(Builder))
		{
			const int32 Hits = FMath::Clamp(ParseInt(Args, 0, 1), 1, 50);
			for (int32 Index = 0; Index < Hits; ++Index)
			{
				const EBuildableWrenchResult Result = Target->ReceiveWrenchHit(Builder->GetOwnerPlayerState());
				UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] wrench on %s: result %d, hp %.0f/%.0f, level %d, upgrade %d/%d"),
					*Target->GetName(), static_cast<int32>(Result), Target->GetHealth(), Target->GetMaxHealth(),
					Target->GetBuildLevel(), Target->GetUpgradeMetal(), Target->GetUpgradeCost());
			}
		}
	}

	static void CmdList(const TArray<FString>& Args, UWorld* World)
	{
		UBuilderComponent* const Builder = FindLocalAuthoritativeBuilder(World);
		AShooterPlayerState* const State = Builder ? Builder->GetOwnerPlayerState() : nullptr;
		if (!State)
		{
			return;
		}
		const TArray<ABuildableActor*> Owned = State->GetOwnedBuildables();
		UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s owns %d building(s), metal %d/%d"), *State->GetPlayerName(), Owned.Num(), State->GetMetal(), State->GetMaxMetal());
		for (const ABuildableActor* Buildable : Owned)
		{
			if (!Buildable)
			{
				UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG]   (unresolved)"));
				continue;
			}
			UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG]   %s state %d hp %.0f/%.0f level %d progress %.0f%%"),
				*Buildable->GetName(), static_cast<int32>(Buildable->GetBuildableState()), Buildable->GetHealth(),
				Buildable->GetMaxHealth(), Buildable->GetBuildLevel(), Buildable->GetConstructionProgress() * 100.0f);
		}
	}

	static void CmdMenu(const TArray<FString>& Args, UWorld* World)
	{
		if (UBuilderComponent* const Builder = FindLocalAuthoritativeBuilder(World))
		{
			Builder->ToggleMenu();
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdBuildPlace(
	TEXT("polarity.build.place"),
	TEXT("Place the local player's building at the aim point for free. Host or standalone only. Usage: polarity.build.place [slot=0]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildDebug::CmdPlace)
);

static FAutoConsoleCommandWithWorldAndArgs CmdBuildDemolish(
	TEXT("polarity.build.demolish"),
	TEXT("Demolish the local player's nearest building. Usage: polarity.build.demolish"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildDebug::CmdDemolish)
);

static FAutoConsoleCommandWithWorldAndArgs CmdBuildDamage(
	TEXT("polarity.build.damage"),
	TEXT("Damage the local player's nearest building, side check skipped. Usage: polarity.build.damage [amount=50]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildDebug::CmdDamage)
);

static FAutoConsoleCommandWithWorldAndArgs CmdBuildWrench(
	TEXT("polarity.build.wrench"),
	TEXT("Hit the local player's nearest building with the wrench, paid with their metal. Usage: polarity.build.wrench [hits=1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildDebug::CmdWrench)
);

static FAutoConsoleCommandWithWorldAndArgs CmdBuildList(
	TEXT("polarity.build.list"),
	TEXT("Log the local player's buildings. Usage: polarity.build.list"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildDebug::CmdList)
);

static FAutoConsoleCommandWithWorldAndArgs CmdBuildMenu(
	TEXT("polarity.build.menu"),
	TEXT("Toggle the local player's build menu, as the key would. Usage: polarity.build.menu"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildDebug::CmdMenu)
);
