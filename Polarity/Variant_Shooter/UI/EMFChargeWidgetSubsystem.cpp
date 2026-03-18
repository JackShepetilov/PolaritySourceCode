// EMFChargeWidgetSubsystem.cpp
// World subsystem for managing EMF charge indicator widgets above NPCs and Props

#include "EMFChargeWidgetSubsystem.h"
#include "EMFChargeWidget.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "EMFPhysicsProp.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void UEMFChargeWidgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UEMFChargeWidgetSubsystem::Deinitialize()
{
	CleanupWidgets();
	Super::Deinitialize();
}

bool UEMFChargeWidgetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	if (World)
	{
		return World->IsGameWorld();
	}
	return false;
}

void UEMFChargeWidgetSubsystem::Tick(float DeltaTime)
{
	// FTickableGameObject::Tick — called every frame independently of Slate
	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		return;
	}

	// Process deferred registrations (actors that registered before WidgetClass was set)
	if (WidgetClass && (PendingNPCs.Num() > 0 || PendingProps.Num() > 0))
	{
		ProcessPendingRegistrations();
	}

	// === Clutter reduction: count active widgets per category ===
	int32 CategoryCounts[3] = { 0, 0, 0 }; // NPC, Prop, Weapon
	for (const auto& Pair : ActiveWidgets)
	{
		if (Pair.Value)
		{
			uint8 CatIndex = static_cast<uint8>(Pair.Value->GetCategory());
			CategoryCounts[CatIndex]++;
		}
	}

	// Compute effective distances per category (base + clutter reduction)
	float EffectiveDistances[3];
	EffectiveDistances[0] = Settings.NPCClutter.ComputeEffectiveDistance(CategoryCounts[0]);
	EffectiveDistances[1] = Settings.PropClutter.ComputeEffectiveDistance(CategoryCounts[1]);
	EffectiveDistances[2] = Settings.WeaponClutter.ComputeEffectiveDistance(CategoryCounts[2]);

	// Update screen positions for all active widgets with clutter-adjusted distance
	for (auto& Pair : ActiveWidgets)
	{
		if (Pair.Value)
		{
			uint8 CatIndex = static_cast<uint8>(Pair.Value->GetCategory());
			Pair.Value->EffectiveMinScaleDistance = EffectiveDistances[CatIndex];
			Pair.Value->UpdateScreenPosition(PC);
		}
	}
}

void UEMFChargeWidgetSubsystem::RegisterNPC(AShooterNPC* NPC)
{
	if (!NPC || !bEnabled)
	{
		return;
	}

	// Check if already registered
	if (ActiveWidgets.Contains(NPC))
	{
		return;
	}

	// Defer if WidgetClass not yet set (level-placed actors may BeginPlay before BP setup)
	if (!WidgetClass)
	{
		PendingNPCs.AddUnique(NPC);
		return;
	}

	// Get a widget from pool
	UEMFChargeWidget* Widget = GetWidgetFromPool();
	if (!Widget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EMFChargeWidget] GetWidgetFromPool returned nullptr! WidgetClass=%s"),
			WidgetClass ? *WidgetClass->GetName() : TEXT("NULL"));
		return;
	}

	// Bind widget to NPC
	Widget->BindToNPC(NPC, Settings.NPCVerticalOffset);

	// Track
	ActiveWidgets.Add(NPC, Widget);

	// Listen for death to auto-unregister
	NPC->OnNPCDeath.AddDynamic(this, &UEMFChargeWidgetSubsystem::OnNPCDied);
}

void UEMFChargeWidgetSubsystem::UnregisterNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	PendingNPCs.Remove(NPC);

	// Unbind death delegate
	NPC->OnNPCDeath.RemoveDynamic(this, &UEMFChargeWidgetSubsystem::OnNPCDied);

	// Find and reclaim widget
	TObjectPtr<UEMFChargeWidget>* FoundWidget = ActiveWidgets.Find(NPC);
	if (FoundWidget && *FoundWidget)
	{
		ReturnWidgetToPool(*FoundWidget);
	}

	ActiveWidgets.Remove(NPC);
}

void UEMFChargeWidgetSubsystem::OnNPCDied(AShooterNPC* DeadNPC)
{
	UnregisterNPC(DeadNPC);
}

void UEMFChargeWidgetSubsystem::RegisterProp(AEMFPhysicsProp* Prop)
{
	if (!Prop || !bEnabled)
	{
		return;
	}

	if (ActiveWidgets.Contains(Prop))
	{
		return;
	}

	// Defer if WidgetClass not yet set (level-placed actors may BeginPlay before BP setup)
	if (!WidgetClass)
	{
		PendingProps.AddUnique(Prop);
		UE_LOG(LogTemp, Log, TEXT("[EMFChargeWidget] RegisterProp: %s deferred (WidgetClass not set yet)"), *Prop->GetName());
		return;
	}

	UEMFChargeWidget* Widget = GetWidgetFromPool();
	if (!Widget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EMFChargeWidget] RegisterProp: GetWidgetFromPool returned nullptr for %s!"), *Prop->GetName());
		return;
	}

	Widget->BindToProp(Prop, Settings.PropVerticalOffset);
	ActiveWidgets.Add(Prop, Widget);

	Prop->OnPropDeath.AddDynamic(this, &UEMFChargeWidgetSubsystem::OnPropDied);

	UE_LOG(LogTemp, Log, TEXT("[EMFChargeWidget] RegisterProp: %s registered OK. ActiveWidgets=%d"), *Prop->GetName(), ActiveWidgets.Num());
}

void UEMFChargeWidgetSubsystem::UnregisterProp(AEMFPhysicsProp* Prop)
{
	if (!Prop)
	{
		return;
	}

	PendingProps.Remove(Prop);

	Prop->OnPropDeath.RemoveDynamic(this, &UEMFChargeWidgetSubsystem::OnPropDied);

	TObjectPtr<UEMFChargeWidget>* FoundWidget = ActiveWidgets.Find(Prop);
	if (FoundWidget && *FoundWidget)
	{
		ReturnWidgetToPool(*FoundWidget);
	}

	ActiveWidgets.Remove(Prop);
}

void UEMFChargeWidgetSubsystem::OnPropDied(AEMFPhysicsProp* Prop, AActor* Killer)
{
	UnregisterProp(Prop);
}

void UEMFChargeWidgetSubsystem::RegisterDroppedWeapon(ADroppedMeleeWeapon* Weapon)
{
	if (!Weapon || !bEnabled)
	{
		return;
	}

	if (ActiveWidgets.Contains(Weapon))
	{
		return;
	}

	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Log, TEXT("[EMFChargeWidget] RegisterDroppedWeapon: %s deferred (WidgetClass not set yet)"), *Weapon->GetName());
		return;
	}

	UEMFChargeWidget* Widget = GetWidgetFromPool();
	if (!Widget)
	{
		return;
	}

	Widget->BindToDroppedWeapon(Weapon, Settings.PropVerticalOffset);
	ActiveWidgets.Add(Weapon, Widget);

	UE_LOG(LogTemp, Log, TEXT("[EMFChargeWidget] RegisterDroppedWeapon: %s registered OK"), *Weapon->GetName());
}

void UEMFChargeWidgetSubsystem::UnregisterDroppedWeapon(ADroppedMeleeWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	TObjectPtr<UEMFChargeWidget>* FoundWidget = ActiveWidgets.Find(Weapon);
	if (FoundWidget && *FoundWidget)
	{
		ReturnWidgetToPool(*FoundWidget);
	}

	ActiveWidgets.Remove(Weapon);
}

void UEMFChargeWidgetSubsystem::RegisterDroppedRangedWeapon(ADroppedRangedWeapon* Weapon)
{
	if (!Weapon || !bEnabled)
	{
		return;
	}

	if (ActiveWidgets.Contains(Weapon))
	{
		return;
	}

	if (!WidgetClass)
	{
		return;
	}

	UEMFChargeWidget* Widget = GetWidgetFromPool();
	if (!Widget)
	{
		return;
	}

	Widget->BindToDroppedRangedWeapon(Weapon, Settings.PropVerticalOffset);
	ActiveWidgets.Add(Weapon, Widget);
}

void UEMFChargeWidgetSubsystem::UnregisterDroppedRangedWeapon(ADroppedRangedWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	TObjectPtr<UEMFChargeWidget>* FoundWidget = ActiveWidgets.Find(Weapon);
	if (FoundWidget && *FoundWidget)
	{
		ReturnWidgetToPool(*FoundWidget);
	}

	ActiveWidgets.Remove(Weapon);
}

void UEMFChargeWidgetSubsystem::ProcessPendingRegistrations()
{
	// Process pending NPCs
	TArray<TWeakObjectPtr<AShooterNPC>> NPCsCopy = PendingNPCs;
	PendingNPCs.Empty();
	for (const auto& WeakNPC : NPCsCopy)
	{
		if (AShooterNPC* NPC = WeakNPC.Get())
		{
			RegisterNPC(NPC);
		}
	}

	// Process pending Props
	TArray<TWeakObjectPtr<AEMFPhysicsProp>> PropsCopy = PendingProps;
	PendingProps.Empty();
	for (const auto& WeakProp : PropsCopy)
	{
		if (AEMFPhysicsProp* Prop = WeakProp.Get())
		{
			RegisterProp(Prop);
		}
	}
}

UEMFChargeWidget* UEMFChargeWidgetSubsystem::GetWidgetFromPool()
{
	if (WidgetPool.Num() > 0)
	{
		return WidgetPool.Pop();
	}

	// Pool empty — create a new widget on demand
	APlayerController* PC = GetLocalPlayerController();
	if (PC && WidgetClass)
	{
		UEMFChargeWidget* NewWidget = CreateWidget<UEMFChargeWidget>(PC, WidgetClass);
		if (NewWidget)
		{
			NewWidget->AddToViewport(90);
			NewWidget->SetVisibility(ESlateVisibility::Collapsed);
			return NewWidget;
		}
	}

	return nullptr;
}

void UEMFChargeWidgetSubsystem::ReturnWidgetToPool(UEMFChargeWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	Widget->ResetWidget();
	Widget->SetVisibility(ESlateVisibility::Collapsed);
	WidgetPool.Add(Widget);
}

void UEMFChargeWidgetSubsystem::CleanupWidgets()
{
	// Cleanup active widgets
	for (auto& Pair : ActiveWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->ResetWidget();
			Pair.Value->RemoveFromParent();
		}
	}
	ActiveWidgets.Empty();

	// Cleanup pool
	for (UEMFChargeWidget* Widget : WidgetPool)
	{
		if (Widget)
		{
			Widget->RemoveFromParent();
		}
	}
	WidgetPool.Empty();
}

APlayerController* UEMFChargeWidgetSubsystem::GetLocalPlayerController() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	return UGameplayStatics::GetPlayerController(World, 0);
}

const FWidgetClutterSettings& UEMFChargeWidgetSubsystem::GetClutterSettings(EChargeWidgetCategory Category) const
{
	switch (Category)
	{
	case EChargeWidgetCategory::NPC:
		return Settings.NPCClutter;
	case EChargeWidgetCategory::Prop:
		return Settings.PropClutter;
	case EChargeWidgetCategory::Weapon:
		return Settings.WeaponClutter;
	default:
		return Settings.NPCClutter;
	}
}
