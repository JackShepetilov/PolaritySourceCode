// LootOutline.cpp

#include "LootOutline.h"
#include "LootCardWidget.h"
#include "Variant_Shooter/Abilities/GrappleFetchable.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/PostProcessVolume.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace LootOutline
{
	static void ApplyStencil(AActor* Actor, int32 Stencil)
	{
		TArray<UPrimitiveComponent*> Primitives;
		Actor->GetComponents(Primitives);
		for (UPrimitiveComponent* const Primitive : Primitives)
		{
			// Only what is drawn: collision shapes, the hologram and text have no outline to give.
			if (!Primitive || Primitive->IsA<UShapeComponent>() || Primitive->IsA<UWidgetComponent>()
				|| Primitive->IsA<UTextRenderComponent>())
			{
				continue;
			}
			const bool bWant = Stencil > 0;
			if (Primitive->bRenderCustomDepth != bWant)
			{
				Primitive->SetRenderCustomDepth(bWant);
			}
			if (bWant && Primitive->CustomDepthStencilValue != Stencil)
			{
				Primitive->SetCustomDepthStencilValue(Stencil);
			}
		}
	}

	static const FName VolumeTag(TEXT("LootOutlineVolume"));

	UMaterialInstanceDynamic* InstallPostProcess(AShooterCharacter* Viewer, UMaterialInterface* Material)
	{
		UWorld* const World = Viewer ? Viewer->GetWorld() : nullptr;
		if (!World || !Material)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.Owner = Viewer;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APostProcessVolume* const Volume = World->SpawnActor<APostProcessVolume>(Viewer->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->Tags.Add(VolumeTag);
		Volume->bUnbound = true;
		Volume->bEnabled = true;
		Volume->BlendWeight = 1.0f;
		UMaterialInstanceDynamic* const Instance = UMaterialInstanceDynamic::Create(Material, Volume);
		Instance->SetVectorParameterValue(TEXT("CommonColor"), ULootCardWidget::GetRarityColor(EUpgradeRarity::Common));
		Instance->SetVectorParameterValue(TEXT("RareColor"), ULootCardWidget::GetRarityColor(EUpgradeRarity::Rare));
		Instance->SetVectorParameterValue(TEXT("EpicColor"), ULootCardWidget::GetRarityColor(EUpgradeRarity::Epic));
		Instance->SetVectorParameterValue(TEXT("LegendaryColor"), ULootCardWidget::GetRarityColor(EUpgradeRarity::Legendary));
		Volume->Settings.AddBlendable(Instance, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[LOOT_DEBUG] Outline volume %s for %s"), *Volume->GetName(), *Viewer->GetName());
		return Instance;
	}

	void UninstallPostProcess(AShooterCharacter* Viewer)
	{
		if (!Viewer)
		{
			return;
		}
		// The volumes this player spawned are its children by ownership.
		const TArray<TObjectPtr<AActor>> Owned = Viewer->Children;
		for (AActor* const Child : Owned)
		{
			if (Child && Child->ActorHasTag(VolumeTag))
			{
				Child->Destroy();
			}
		}
	}

	void Refresh(const AShooterCharacter* Viewer)
	{
		const UWorld* const World = Viewer ? Viewer->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}
		TArray<AActor*> Fetchables;
		GrappleFetch::GetAll(World, Fetchables);
		for (AActor* const Actor : Fetchables)
		{
			if (!Actor)
			{
				continue;
			}
			// The card's own reading of the item, for this player. Nothing to say (an empty box): no line.
			FLootCardData Data;
			int32 Stencil = 0;
			if (ULootCardWidget::BuildData(Actor, Viewer, FText::GetEmpty(), Data))
			{
				Stencil = StencilBase + static_cast<int32>(Data.bHasRarity ? Data.Rarity : EUpgradeRarity::Common);
			}
			ApplyStencil(Actor, Stencil);

			// A dispenser box shows its item as a local copy attached to it.
			TArray<AActor*> Attached;
			Actor->GetAttachedActors(Attached);
			for (AActor* const Child : Attached)
			{
				if (Child && !Child->IsA<APawn>())
				{
					ApplyStencil(Child, Stencil);
				}
			}
		}
	}
}
