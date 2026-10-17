// GrenadeComponent.cpp

#include "Variant_Shooter/Grenades/GrenadeComponent.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/Grenades/AbilityHandler_Grenade.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/Health/FieldCareComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/UI/GadgetWheelWidget.h"
#include "EnhancedInputComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundMix.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGrenadeComponent::UGrenadeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UGrenadeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGrenadeComponent, SelectedType, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGrenadeComponent, bThrowing, COND_OwnerOnly);
}

AShooterCharacter* UGrenadeComponent::GetCharacter() const
{
	return Cast<AShooterCharacter>(GetOwner());
}

void UGrenadeComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* const Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (UInventoryComponent* const Bag = Owner->FindComponentByClass<UInventoryComponent>())
	{
		Bag->OnInventoryChanged.AddDynamic(this, &UGrenadeComponent::OnInventoryChanged);
	}

	// Next tick: the bag sizes its grid in its own BeginPlay, and the order of the two is not ours.
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UGrenadeComponent::GiveStartingGrenades);
	}
}

void UGrenadeComponent::GiveStartingGrenades()
{
	UInventoryComponent* const Bag = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Bag)
	{
		return;
	}

	for (const FGrenadeStock& Stock : StartingGrenades)
	{
		if (!Stock.Type || Stock.Count <= 0)
		{
			continue;
		}
		FInventoryItem Item;
		Item.Kind = EInventorySlotKind::Grenade;
		Item.Payload = Stock.Type;
		Item.Count = Stock.Count;
		Item.StackMax = FMath::Max(1, Stock.Type->MaxPerCell);
		const int32 Left = Bag->TryAdd(Item);
		UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s starts with %d %s (%d did not fit)"),
			*GetNameSafe(GetOwner()), Stock.Count - Left, *GetNameSafe(Stock.Type), Left);
	}
	RefreshSelection();
}

void UGrenadeComponent::OnInventoryChanged()
{
	RefreshSelection();
}

void UGrenadeComponent::RefreshSelection()
{
	const UInventoryComponent* const Bag = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Bag || !GetOwner()->HasAuthority())
	{
		return;
	}

	TArray<UGrenadeDefinition*> Types;
	Bag->GetGrenadeTypes(Types);
	const int32 Found = SelectedType ? Types.IndexOfByKey(SelectedType) : INDEX_NONE;
	if (Found != INDEX_NONE)
	{
		SelectedPlace = Found;
		return;
	}

	// The kind is gone. Stay on its place: a kind picked up in exchange for it sits right there.
	SelectedPlace = Types.Num() > 0 ? FMath::Clamp(SelectedPlace, 0, Types.Num() - 1) : 0;
	SelectedType = Types.IsValidIndex(SelectedPlace) ? Types[SelectedPlace] : nullptr;
}

int32 UGrenadeComponent::GetSelectedCount() const
{
	const UInventoryComponent* const Bag = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	return Bag && SelectedType ? Bag->GetGrenadeCount(SelectedType) : 0;
}

// ==================== Input (owning client) ====================

void UGrenadeComponent::BindInput(UEnhancedInputComponent* Input)
{
	if (!Input)
	{
		return;
	}
	if (GrenadeAction)
	{
		Input->BindAction(GrenadeAction, ETriggerEvent::Started, this, &UGrenadeComponent::OnGrenadePressed);
		Input->BindAction(GrenadeAction, ETriggerEvent::Completed, this, &UGrenadeComponent::OnGrenadeReleased);
		Input->BindAction(GrenadeAction, ETriggerEvent::Canceled, this, &UGrenadeComponent::OnGrenadeReleased);
	}
	if (GrenadeCycleAction)
	{
		Input->BindAction(GrenadeCycleAction, ETriggerEvent::Started, this, &UGrenadeComponent::OnCyclePressed);
		Input->BindAction(GrenadeCycleAction, ETriggerEvent::Completed, this, &UGrenadeComponent::OnCycleReleased);
		Input->BindAction(GrenadeCycleAction, ETriggerEvent::Canceled, this, &UGrenadeComponent::OnCycleReleased);
	}
}

void UGrenadeComponent::OnGrenadePressed()
{
	bKeyHeld = true;
	KeyDownTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		BeginThrow();
	}
	else
	{
		Server_BeginThrow();
	}
}

void UGrenadeComponent::OnGrenadeReleased()
{
	bKeyHeld = false;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ReleaseThrow();
	}
	else
	{
		Server_ReleaseThrow();
	}
}

void UGrenadeComponent::OnCyclePressed()
{
	// Tap or hold is only known when it is let go or held long enough: wait for one of the two.
	bCycleKeyHeld = true;
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(WheelHoldTimer, this, &UGrenadeComponent::OpenWheel, WheelHoldTime, false);
	}
}

void UGrenadeComponent::OnCycleReleased()
{
	if (!bCycleKeyHeld)
	{
		return;
	}
	bCycleKeyHeld = false;
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WheelHoldTimer);
	}

	if (bWheelOpen)
	{
		CloseWheel(true);
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CycleType();
	}
	else
	{
		Server_CycleType();
	}
}

void UGrenadeComponent::Server_BeginThrow_Implementation() { BeginThrow(); }
void UGrenadeComponent::Server_ReleaseThrow_Implementation() { ReleaseThrow(); }
void UGrenadeComponent::Server_CycleType_Implementation() { CycleType(); }
void UGrenadeComponent::Server_SelectType_Implementation(UGrenadeDefinition* Type) { SelectType(Type); }

// ==================== The throw (authority) ====================

void UGrenadeComponent::BeginThrow()
{
	AShooterCharacter* const Character = GetCharacter();
	if (!Character || bThrowing || Character->IsDead() || Character->IsDowned())
	{
		return;
	}

	// The hands are somebody else's: a swap, an ability, a medkit. The press is dropped, not queued.
	const UAbilityComponent* const Abilities = Character->GetAbilityComponent();
	const UFieldCareComponent* const Care = Character->FindComponentByClass<UFieldCareComponent>();
	if (Character->IsWeaponSwitchInProgress() || (Abilities && Abilities->IsCasting()) || (Care && Care->IsBusy())
		|| Character->IsHandPickupActive())
	{
		UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s: hands busy, no throw"), *GetNameSafe(Character));
		return;
	}

	RefreshSelection();
	if (!SelectedType || GetSelectedCount() <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s: no grenade in the bag"), *GetNameSafe(Character));
		return;
	}

	TSubclassOf<UAbilityHandler> HandlerClass = SelectedType->HandlerClass;
	if (!HandlerClass || !HandlerClass->IsChildOf(UAbilityHandler_Grenade::StaticClass()))
	{
		HandlerClass = UAbilityHandler_Grenade::StaticClass();
	}

	ActiveHandler = NewObject<UAbilityHandler_Grenade>(this, HandlerClass);
	TWeakObjectPtr<UGrenadeComponent> WeakThis(this);
	ActiveHandler->InitializeStandalone(Character, SelectedType, [WeakThis](bool bCompleted)
	{
		if (UGrenadeComponent* const Self = WeakThis.Get())
		{
			Self->OnThrowFinished(bCompleted);
		}
	});

	bThrowing = true;
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s winds up %s (%d in the bag)"),
		*GetNameSafe(Character), *GetNameSafe(SelectedType), GetSelectedCount());
	ActiveHandler->OnActivate();
}

void UGrenadeComponent::ReleaseThrow()
{
	if (bThrowing && ActiveHandler)
	{
		ActiveHandler->OnButtonReleased();
	}
}

void UGrenadeComponent::CancelThrow()
{
	if (ActiveHandler)
	{
		ActiveHandler->OnCancelRequested();
	}
	bThrowing = false;
	ActiveHandler = nullptr;
}

void UGrenadeComponent::OnThrowFinished(bool bCompleted)
{
	bThrowing = false;
	ActiveHandler = nullptr;
	RefreshSelection();
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s: throw %s"), *GetNameSafe(GetOwner()), bCompleted ? TEXT("done") : TEXT("cancelled"));
}

void UGrenadeComponent::CycleType()
{
	if (bThrowing)
	{
		return;
	}

	const UInventoryComponent* const Bag = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Bag)
	{
		return;
	}

	TArray<UGrenadeDefinition*> Types;
	Bag->GetGrenadeTypes(Types);
	if (Types.Num() == 0)
	{
		SelectedType = nullptr;
		return;
	}

	const int32 Current = Types.IndexOfByKey(SelectedType);
	SelectedPlace = (Current + 1) % Types.Num();
	SelectedType = Types[SelectedPlace];
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s selects %s"), *GetNameSafe(GetOwner()), *GetNameSafe(SelectedType));
}

void UGrenadeComponent::SelectType(UGrenadeDefinition* Type)
{
	const UInventoryComponent* const Bag = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (bThrowing || !Bag || !GetOwner()->HasAuthority())
	{
		return;
	}

	TArray<UGrenadeDefinition*> Types;
	Bag->GetGrenadeTypes(Types);
	const int32 Place = Types.IndexOfByKey(Type);
	if (Place == INDEX_NONE)
	{
		return;
	}
	SelectedPlace = Place;
	SelectedType = Type;
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s selects %s on the wheel"), *GetNameSafe(GetOwner()), *GetNameSafe(SelectedType));
}

// ==================== The wheel (owning client) ====================

void UGrenadeComponent::OpenWheel()
{
	const AShooterCharacter* const Character = GetCharacter();
	APlayerController* const PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	const UInventoryComponent* const Bag = Character ? Character->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!bCycleKeyHeld || bWheelOpen || !PC || !Bag || !Character->IsLocallyControlled())
	{
		return;
	}

	TArray<UGrenadeDefinition*> Types;
	Bag->GetGrenadeTypes(Types);
	if (Types.Num() == 0)
	{
		return;
	}

	if (!Wheel)
	{
		Wheel = CreateWidget<UGadgetWheelWidget>(PC, WheelWidgetClass ? WheelWidgetClass.Get() : UGadgetWheelWidget::StaticClass());
	}
	if (!Wheel)
	{
		return;
	}

	WheelTypes.Reset();
	for (UGrenadeDefinition* const Type : Types)
	{
		WheelTypes.Add(Type);
	}
	WheelCursor = FVector2D::ZeroVector;
	WheelHovered = WheelTypes.IndexOfByKey(SelectedType);
	bWheelOpen = true;

	// CloseWheel takes it off the screen every time, so it is never on it here.
	Wheel->AddToPlayerScreen(12);
	Wheel->SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshWheel();
}

void UGrenadeComponent::RefreshWheel()
{
	const UInventoryComponent* const Bag = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Wheel || !Bag)
	{
		return;
	}

	TArray<FGadgetWheelEntry> Entries;
	for (const UGrenadeDefinition* const Type : WheelTypes)
	{
		FGadgetWheelEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Label = !Type ? FText::GetEmpty() : (Type->DisplayName.IsEmpty() ? FText::FromString(Type->GetName()) : Type->DisplayName);
		Entry.Count = Bag->GetGrenadeCount(Type);
	}
	Wheel->SetEntries(Entries, Bag->GetGadgetsPerKind(), WheelTypes.IndexOfByKey(SelectedType));
	Wheel->SetHover(WheelHovered, WheelCursor / FMath::Max(0.1f, WheelPickDistance));
}

bool UGrenadeComponent::HandleLookForWheel(float Yaw, float Pitch)
{
	if (!bWheelOpen)
	{
		return false;
	}

	// The look's pitch is up-positive; the wheel's y is the screen's, down-positive.
	WheelCursor = (WheelCursor + FVector2D(Yaw, -Pitch)).GetClampedToMaxSize(WheelPickDistance);
	if (WheelCursor.Size() >= WheelPickDistance * 0.5f && WheelTypes.Num() > 0)
	{
		const FVector2D Direction = WheelCursor.GetSafeNormal();
		float Best = -2.0f;
		for (int32 Index = 0; Index < WheelTypes.Num(); ++Index)
		{
			const float Dot = FVector2D::DotProduct(Direction, UGadgetWheelWidget::GetEntryDirection(Index, WheelTypes.Num()));
			if (Dot > Best)
			{
				Best = Dot;
				WheelHovered = Index;
			}
		}
	}
	RefreshWheel();
	return true;
}

void UGrenadeComponent::CloseWheel(bool bApply)
{
	if (!bWheelOpen)
	{
		return;
	}
	bWheelOpen = false;

	UGrenadeDefinition* const Picked = WheelTypes.IsValidIndex(WheelHovered) ? WheelTypes[WheelHovered].Get() : nullptr;
	if (bApply && Picked && Picked != SelectedType)
	{
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			SelectType(Picked);
		}
		else
		{
			Server_SelectType(Picked);
		}
	}

	WheelTypes.Reset();
	if (Wheel)
	{
		Wheel->RemoveFromParent();
	}
}

// ==================== The arc (owning client) ====================

void UGrenadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickBurn();

	// The counts can change under an open wheel (a throw, a pickup); the owner's copy of the section
	// arrives by replication, with no event on this side.
	if (bWheelOpen)
	{
		RefreshWheel();
	}

	// bThrowing is the authority's answer, so a press it refused never draws an arc.
	const AShooterCharacter* const Character = GetCharacter();
	if (!bKeyHeld || !bThrowing || !SelectedType || !Character || !Character->IsLocallyControlled())
	{
		return;
	}

	const float HeldFor = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f) - KeyDownTime;
	if (HeldFor >= SelectedType->AimPreviewDelay)
	{
		UAbilityHandler_Throw::DrawAimPreview(Character, SelectedType);
	}
}

// ==================== Flash (owning client) ====================

void UGrenadeComponent::Client_Flashbang_Implementation(const FGrenadeFlashHit& Hit)
{
	UWorld* const World = GetWorld();
	const AShooterCharacter* const Character = GetCharacter();
	APlayerController* const PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!World || !PC)
	{
		return;
	}

	// The eyes: a burn where the flash was on the screen. It stays there, as a burn on the eye does,
	// whatever the player does with the camera afterwards.
	if (Hit.Burn > 0.0f && Hit.BurnMaterial && Hit.BurnTime > 0.0f)
	{
		FVector2D Screen;
		int32 Width = 0;
		int32 Height = 0;
		PC->GetViewportSize(Width, Height);
		FVector2D Centre(0.5f, 0.5f);
		if (Width > 0 && Height > 0 && PC->ProjectWorldLocationToScreen(Hit.FlashLocation, Screen, true))
		{
			Centre = FVector2D(FMath::Clamp(Screen.X / Width, 0.0f, 1.0f), FMath::Clamp(Screen.Y / Height, 0.0f, 1.0f));
		}

		if (!BurnVolume)
		{
			FActorSpawnParameters Params;
			Params.Owner = GetOwner();
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			BurnVolume = World->SpawnActor<APostProcessVolume>(GetOwner()->GetActorLocation(), FRotator::ZeroRotator, Params);
			if (BurnVolume)
			{
				BurnVolume->bUnbound = true;
				BurnVolume->Priority = 20.0f;
			}
		}
		if (BurnVolume && (!BurnMID || BurnMID->Parent != Hit.BurnMaterial))
		{
			BurnMID = UMaterialInstanceDynamic::Create(Hit.BurnMaterial, this);
			BurnVolume->Settings.WeightedBlendables.Array.Reset();
			BurnVolume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, BurnMID));
		}
		if (BurnVolume && BurnMID)
		{
			BurnMID->SetVectorParameterValue(TEXT("Center"), FLinearColor(Centre.X, Centre.Y, 0.0f, 0.0f));
			BurnMID->SetScalarParameterValue(TEXT("Size"), Hit.BurnSize);
			BurnMID->SetScalarParameterValue(TEXT("Strength"), Hit.Burn);
			BurnMID->SetScalarParameterValue(TEXT("Age"), 0.0f);
			BurnVolume->bEnabled = true;
			BurnVolume->BlendWeight = 1.0f;
			BurnStartTime = World->GetTimeSeconds();
			BurnDuration = Hit.BurnTime;
		}
	}

	// The ears: the world muffled, the ringing on top. A second bang restarts the clock.
	if (Hit.Loud > 0.0f && Hit.DeafTime > 0.0f)
	{
		if (Hit.DeafMix)
		{
			if (PushedDeafMix)
			{
				UGameplayStatics::PopSoundMixModifier(this, PushedDeafMix);
			}
			UGameplayStatics::PushSoundMixModifier(this, Hit.DeafMix);
			PushedDeafMix = Hit.DeafMix;
		}
		World->GetTimerManager().SetTimer(DeafTimer, this, &UGrenadeComponent::EndDeafness, Hit.DeafTime, false);

		if (Hit.Ring)
		{
			UGameplayStatics::PlaySound2D(this, Hit.Ring, Hit.Loud);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s flashed: burn %.2f size %.2f for %.1f s, loud %.2f for %.1f s"),
		*GetNameSafe(Character), Hit.Burn, Hit.BurnSize, Hit.BurnTime, Hit.Loud, Hit.DeafTime);
}

void UGrenadeComponent::TickBurn()
{
	const UWorld* const World = GetWorld();
	if (!BurnVolume || !BurnMID || BurnStartTime < 0.0f || !World)
	{
		return;
	}

	const float Age = BurnDuration > 0.0f ? (World->GetTimeSeconds() - BurnStartTime) / BurnDuration : 1.0f;
	if (Age >= 1.0f)
	{
		BurnVolume->bEnabled = false;
		BurnVolume->BlendWeight = 0.0f;
		BurnStartTime = -1.0f;
		return;
	}
	BurnMID->SetScalarParameterValue(TEXT("Age"), Age);
}

void UGrenadeComponent::EndDeafness()
{
	if (PushedDeafMix)
	{
		UGameplayStatics::PopSoundMixModifier(this, PushedDeafMix);
		PushedDeafMix = nullptr;
	}
}

void UGrenadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WheelHoldTimer);
	}
	CloseWheel(false);
	EndDeafness();
	if (BurnVolume)
	{
		BurnVolume->Destroy();
		BurnVolume = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
