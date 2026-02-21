// EMFChargeWidgetSubsystem.cpp
// World subsystem for managing EMF charge indicator widgets above NPCs and Props

#include "EMFChargeWidgetSubsystem.h"
#include "EMFChargeWidget.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
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

	// Update screen positions for all active widgets
	for (auto& Pair : ActiveWidgets)
	{
		if (Pair.Value)
		{
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
	// Ensure pool exists
	if (WidgetPool.Num() == 0 && WidgetClass)
	{
		CreateWidgetPool();
	}

	if (WidgetPool.Num() > 0)
	{
		return WidgetPool.Pop();
	}

	// Pool exhausted - create one more if reasonable
	APlayerController* PC = GetLocalPlayerController();
	if (PC && WidgetClass && ActiveWidgets.Num() < Settings.PoolSize * 2)
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

void UEMFChargeWidgetSubsystem::CreateWidgetPool()
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC || !WidgetClass)
	{
		return;
	}

	for (int32 i = 0; i < Settings.PoolSize; ++i)
	{
		UEMFChargeWidget* Widget = CreateWidget<UEMFChargeWidget>(PC, WidgetClass);
		if (Widget)
		{
			Widget->AddToViewport(90);
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			WidgetPool.Add(Widget);
		}
	}
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
