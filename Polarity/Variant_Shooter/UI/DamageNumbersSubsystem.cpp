// DamageNumbersSubsystem.cpp
// World subsystem for managing floating damage numbers

#include "DamageNumbersSubsystem.h"
#include "DamageNumberWidget.h"
#include "Variant_Shooter/DamageCategory/PlayerDamageCategory.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void UDamageNumbersSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Pool will be created on first use (when WidgetClass is set)
}

void UDamageNumbersSubsystem::Deinitialize()
{
	CleanupWidgets();
	Super::Deinitialize();
}

bool UDamageNumbersSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Only create for game worlds, not editor preview
	UWorld* World = Cast<UWorld>(Outer);
	if (World)
	{
		return World->IsGameWorld();
	}
	return false;
}

void UDamageNumbersSubsystem::Tick(float DeltaTime)
{
	// FTickableGameObject::Tick - no Super call needed

	// Update batch timers
	TArray<FDamageBatchKey> BatchesToRemove;

	for (auto& Pair : ActiveBatches)
	{
		FDamageBatch& Batch = Pair.Value;
		Batch.TimeRemaining -= DeltaTime;

		if (Batch.TimeRemaining <= 0.0f)
		{
			BatchesToRemove.Add(Pair.Key);
		}
	}

	// Remove expired batches (widgets will finish their animations naturally)
	for (const FDamageBatchKey& Key : BatchesToRemove)
	{
		FinalizeBatch(Key);
	}
}

void UDamageNumbersSubsystem::SpawnDamageNumber(const FVector& WorldLocation, float Damage, EPlayerDamageCategory Category)
{
	if (!bEnabled)
	{
		return;
	}

	// Filter out tiny damage
	if (Damage < Settings.MinDamageToShow)
	{
		return;
	}

	// Check distance
	APlayerController* PC = GetLocalPlayerController();
	if (!PC || !PC->GetPawn())
	{
		return;
	}

	float Distance = FVector::Dist(WorldLocation, PC->GetPawn()->GetActorLocation());
	if (Distance > Settings.MaxDistance)
	{
		return;
	}

	// Note: We don't check IsLocationVisible here anymore
	// The widget handles visibility in its Tick based on camera direction
	// This allows damage numbers to appear even for close-range melee hits

	// Get widget from pool
	UDamageNumberWidget* Widget = GetWidgetFromPool();
	if (!Widget)
	{
		return;
	}

	// Add random spread to world position (vertical offset now handled in ShooterNPC)
	FVector SpreadLocation = WorldLocation + FVector(
		FMath::RandRange(-Settings.RandomSpreadX, Settings.RandomSpreadX),
		FMath::RandRange(-Settings.RandomSpreadY, Settings.RandomSpreadY),
		0.0f
	);

	// Set color based on category
	FLinearColor Color = GetColorForCategory(Category);
	Widget->SetCategoryColor(Color);

	// Calculate and apply scale
	float Scale = CalculateScaleForDamage(Damage);
	Widget->SetRenderScale(FVector2D(Scale, Scale));

	// Show widget (position will be updated in widget's Tick)
	Widget->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Bind callback for when animation finishes
	Widget->OnFinished.BindLambda([this, Widget]()
	{
		ReturnWidgetToPool(Widget);
	});

	// Initialize and play animation - widget will track this world location
	Widget->Initialize(Damage, Category, SpreadLocation);

	// Track as active
	ActiveWidgets.Add(Widget);
}

void UDamageNumbersSubsystem::SpawnDamageNumberFromType(const FVector& WorldLocation, float Damage, TSubclassOf<UDamageType> DamageTypeClass)
{
	EPlayerDamageCategory Category = UDamageCategoryHelper::GetCategoryFromDamageType(DamageTypeClass);
	SpawnDamageNumber(WorldLocation, Damage, Category);
}

void UDamageNumbersSubsystem::RegisterNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	// Check if already registered
	for (const TWeakObjectPtr<AShooterNPC>& RegisteredNPC : RegisteredNPCs)
	{
		if (RegisteredNPC.Get() == NPC)
		{
			return;  // Already registered
		}
	}

	// Bind to the NPC's OnDamageTaken delegate
	NPC->OnDamageTaken.AddDynamic(this, &UDamageNumbersSubsystem::OnNPCDamageTaken);

	// Track the NPC
	RegisteredNPCs.Add(NPC);
}

void UDamageNumbersSubsystem::UnregisterNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	// Unbind from the delegate
	NPC->OnDamageTaken.RemoveDynamic(this, &UDamageNumbersSubsystem::OnNPCDamageTaken);

	// Remove from tracking
	RegisteredNPCs.RemoveAll([NPC](const TWeakObjectPtr<AShooterNPC>& Registered)
	{
		return Registered.Get() == NPC || !Registered.IsValid();
	});
}

void UDamageNumbersSubsystem::OnNPCDamageTaken(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser)
{
	if (!DamagedNPC)
	{
		return;
	}

	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		return;
	}

	// Determine damage category to check if it's indirect (environmental) damage
	EPlayerDamageCategory Category = UDamageCategoryHelper::GetCategoryFromDamageType(DamageType);

	// Kinetic (wallslam, momentum, dropkick) and EMF damage are always shown
	// because they're caused indirectly by player actions even when DamageCauser is null
	bool bIsIndirectDamage = (Category == EPlayerDamageCategory::Kinetic || Category == EPlayerDamageCategory::EMF);

	// For direct damage (melee, ranged), verify it came from the player
	bool bFromPlayer = bIsIndirectDamage;  // Indirect damage is always "from player"

	if (!bFromPlayer && DamageCauser)
	{
		// Check if the damage causer is the player's pawn
		if (DamageCauser == PC->GetPawn())
		{
			bFromPlayer = true;
		}
		// Check if the damage causer is owned by the player (e.g., projectile)
		else if (DamageCauser->GetOwner() == PC->GetPawn())
		{
			bFromPlayer = true;
		}
		// Check instigator
		else if (DamageCauser->GetInstigator() == PC->GetPawn())
		{
			bFromPlayer = true;
		}
	}

	if (!bFromPlayer)
	{
		return;
	}

	// Use batching if enabled
	if (Settings.bEnableBatching)
	{
		ProcessDamageWithBatching(DamagedNPC, Damage, Category, HitLocation);
	}
	else
	{
		// No batching - spawn immediately
		SpawnDamageNumber(HitLocation, Damage, Category);
	}
}

FLinearColor UDamageNumbersSubsystem::GetColorForCategory(EPlayerDamageCategory Category) const
{
	switch (Category)
	{
	case EPlayerDamageCategory::Base:
		return Settings.BaseColor;
	case EPlayerDamageCategory::Kinetic:
		return Settings.KineticColor;
	case EPlayerDamageCategory::EMF:
		return Settings.EMFColor;
	default:
		return Settings.BaseColor;
	}
}

float UDamageNumbersSubsystem::CalculateScaleForDamage(float Damage) const
{
	// Linear interpolation based on damage
	float Alpha = FMath::Clamp(Damage / Settings.DamageForMaxScale, 0.0f, 1.0f);
	return FMath::Lerp(Settings.MinScale, Settings.MaxScale, Alpha);
}

UDamageNumberWidget* UDamageNumbersSubsystem::GetWidgetFromPool()
{
	// Ensure pool is created
	if (WidgetPool.Num() == 0 && WidgetClass)
	{
		CreateWidgetPool();
	}

	// Try to get from pool
	if (WidgetPool.Num() > 0)
	{
		UDamageNumberWidget* Widget = WidgetPool.Pop();
		return Widget;
	}

	// Pool exhausted - try to create one more if we have room
	APlayerController* PC = GetLocalPlayerController();
	if (PC && WidgetClass && ActiveWidgets.Num() < Settings.PoolSize * 2)
	{
		UDamageNumberWidget* NewWidget = CreateWidget<UDamageNumberWidget>(PC, WidgetClass);
		if (NewWidget)
		{
			NewWidget->AddToViewport(100);  // High Z-order to be on top
			NewWidget->SetVisibility(ESlateVisibility::Collapsed);
			return NewWidget;
		}
	}

	return nullptr;
}

void UDamageNumbersSubsystem::ReturnWidgetToPool(UDamageNumberWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	// Remove from active
	ActiveWidgets.Remove(Widget);

	// Reset and hide
	Widget->ResetWidget();
	Widget->SetVisibility(ESlateVisibility::Collapsed);

	// Return to pool
	WidgetPool.Add(Widget);
}

void UDamageNumbersSubsystem::CreateWidgetPool()
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC || !WidgetClass)
	{
		return;
	}

	// Pre-create widgets
	for (int32 i = 0; i < Settings.PoolSize; ++i)
	{
		UDamageNumberWidget* Widget = CreateWidget<UDamageNumberWidget>(PC, WidgetClass);
		if (Widget)
		{
			Widget->AddToViewport(100);  // High Z-order
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			WidgetPool.Add(Widget);
		}
	}
}

void UDamageNumbersSubsystem::CleanupWidgets()
{
	// Clean up active widgets
	for (UDamageNumberWidget* Widget : ActiveWidgets)
	{
		if (Widget)
		{
			Widget->RemoveFromParent();
		}
	}
	ActiveWidgets.Empty();

	// Clean up pool
	for (UDamageNumberWidget* Widget : WidgetPool)
	{
		if (Widget)
		{
			Widget->RemoveFromParent();
		}
	}
	WidgetPool.Empty();
}

APlayerController* UDamageNumbersSubsystem::GetLocalPlayerController() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	return UGameplayStatics::GetPlayerController(World, 0);
}

bool UDamageNumbersSubsystem::WorldToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		return false;
	}

	return PC->ProjectWorldLocationToScreen(WorldLocation, OutScreenPosition, false);
}

bool UDamageNumbersSubsystem::IsLocationVisible(const FVector& WorldLocation) const
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		return false;
	}

	FVector2D ScreenPosition;
	if (!PC->ProjectWorldLocationToScreen(WorldLocation, ScreenPosition, false))
	{
		return false;
	}

	// Check if on screen
	int32 ViewportSizeX, ViewportSizeY;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);

	// Add some margin
	const float Margin = 50.0f;
	return ScreenPosition.X >= -Margin &&
	       ScreenPosition.X <= ViewportSizeX + Margin &&
	       ScreenPosition.Y >= -Margin &&
	       ScreenPosition.Y <= ViewportSizeY + Margin;
}

void UDamageNumbersSubsystem::ProcessDamageWithBatching(AActor* TargetNPC, float Damage, EPlayerDamageCategory Category, const FVector& WorldLocation)
{
	if (!TargetNPC)
	{
		return;
	}

	// Create batch key (NPC + Category - different damage types don't combine)
	FDamageBatchKey Key;
	Key.TargetNPC = TargetNPC;
	Key.Category = Category;

	// Check if we have an existing batch for this NPC+Category
	FDamageBatch* ExistingBatch = ActiveBatches.Find(Key);

	if (ExistingBatch && ExistingBatch->ActiveWidget && ExistingBatch->ActiveWidget->IsActive())
	{
		// Add damage to existing batch
		ExistingBatch->AccumulatedDamage += Damage;
		ExistingBatch->TimeRemaining = Settings.BatchingWindow;  // Reset timer

		// Update the widget to show new total
		ExistingBatch->ActiveWidget->UpdateDamage(Damage);

		// Update scale based on new total damage
		float Scale = CalculateScaleForDamage(ExistingBatch->AccumulatedDamage);
		ExistingBatch->ActiveWidget->SetRenderScale(FVector2D(Scale, Scale));
	}
	else
	{
		// Create new batch
		FDamageBatch NewBatch;
		NewBatch.AccumulatedDamage = Damage;
		NewBatch.TimeRemaining = Settings.BatchingWindow;
		NewBatch.WorldLocation = WorldLocation;

		// Get widget from pool
		UDamageNumberWidget* Widget = GetWidgetFromPool();
		if (!Widget)
		{
			return;
		}

		// Set color based on category
		FLinearColor Color = GetColorForCategory(Category);
		Widget->SetCategoryColor(Color);

		// Calculate and apply scale
		float Scale = CalculateScaleForDamage(Damage);
		Widget->SetRenderScale(FVector2D(Scale, Scale));

		// Show widget
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);

		// Bind callback for when animation finishes
		// Capture Key by value for the lambda
		FDamageBatchKey CapturedKey = Key;
		Widget->OnFinished.BindLambda([this, Widget, CapturedKey]()
		{
			// Remove from active batches when animation finishes
			ActiveBatches.Remove(CapturedKey);
			ReturnWidgetToPool(Widget);
		});

		// Initialize and play animation
		Widget->Initialize(Damage, Category, WorldLocation);

		// Track as active
		ActiveWidgets.Add(Widget);

		// Store in batch
		NewBatch.ActiveWidget = Widget;
		ActiveBatches.Add(Key, NewBatch);
	}
}

void UDamageNumbersSubsystem::FinalizeBatch(const FDamageBatchKey& Key)
{
	// Just remove from map - the widget will finish its animation naturally
	// and return to pool via the OnFinished delegate
	ActiveBatches.Remove(Key);
}
