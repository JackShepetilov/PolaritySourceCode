// BuilderComponent.cpp

#include "BuilderComponent.h"

#include "BuildableActor.h"
#include "BuildablePreview.h"
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
	SetComponentTickEnabled(Mode == EBuilderMode::Placing);
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
	if (Mode == EBuilderMode::Menu)
	{
		SetMode(EBuilderMode::Idle);
	}
}

void UBuilderComponent::HandleSlotPressed(int32 SlotIndex)
{
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
	// The ghost leaves on the press, not on the answer: a round trip of waiting would read as a
	// stuck button. If the server refuses, nothing appears and the metal never moved.
	CancelPlacement();
	Server_PlaceBuildable(SlotIndex, Transform);
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

void UBuilderComponent::Server_PlaceBuildable_Implementation(int32 SlotIndex, FTransform Transform)
{
	SpawnBuildable(SlotIndex, Transform, true);
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
	return SpawnBuildable(SlotIndex, Transform, false);
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
