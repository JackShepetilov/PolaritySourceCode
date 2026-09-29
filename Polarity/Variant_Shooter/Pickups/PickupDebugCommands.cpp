// PickupDebugCommands.cpp
// Console commands that put a pickup on the floor in front of the local player, to test pickups
// without a dispenser or an enemy drop. Debug only.
//
//   polarity.spawn.upgrade <name|path>      an upgrade pickup (grants one level above owned)
//   polarity.spawn.attachment <name|path>   an attachment pickup
//   polarity.spawn.actor <Blueprint path>   any actor Blueprint (a pickup of any other kind)
//   polarity.spawn.list <upgrade|attachment> [filter]   print the asset names that fit
//
// <name> is a data asset name or part of it (DA_Upgrade_ExtraJump, extrajump, scope4x); a full
// /Game/... path works too. Pickups are spawned on the server only: on a listen server that is the
// host's console. A client's console refuses, the same as the other debug commands.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Coop/CoopPlayers.h"
#include "Upgrades/UpgradeDefinition.h"
#include "Upgrades/DispenserUpgradePool.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Pickups/UpgradePickup.h"
#include "Variant_Shooter/Pickups/AttachmentPickup.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"

namespace PickupDebug
{
	/** The player at this console. Local by nature, which is what GetLocalController is for. */
	static APawn* GetLocalPawn(UWorld* World)
	{
		const APlayerController* PC = CoopPlayers::GetLocalController(World);
		return PC ? PC->GetPawn() : nullptr;
	}

	/** Two and a half metres ahead on the floor, facing the player. */
	static FTransform GetSpawnTransform(UWorld* World, const APawn* Pawn)
	{
		FVector EyeLoc;
		FRotator EyeRot;
		Pawn->GetActorEyesViewPoint(EyeLoc, EyeRot);

		const FVector Ahead = FRotator(0.0f, EyeRot.Yaw, 0.0f).Vector();
		FVector Location = Pawn->GetActorLocation() + Ahead * 250.0f;

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PickupDebugSpawn), false, Pawn);
		if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.0f, 0.0f, 100.0f),
			Location - FVector(0.0f, 0.0f, 500.0f), ECC_Visibility, Params))
		{
			Location = Hit.ImpactPoint + FVector(0.0f, 0.0f, 50.0f);
		}
		return FTransform(FRotator(0.0f, EyeRot.Yaw + 180.0f, 0.0f), Location);
	}

	/** Server only, and a player to spawn in front of. Logs why not. */
	static APawn* GetSpawnerOrWarn(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] spawn commands run on the server (host console) only"));
			return nullptr;
		}
		APawn* Pawn = GetLocalPawn(World);
		if (!Pawn)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] no local pawn"));
		}
		return Pawn;
	}

	/** Every asset of class T, from the asset registry (nothing is loaded to list them). */
	template <typename T>
	static void GetAssetsOf(TArray<FAssetData>& Out)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.GetAssetsByClass(T::StaticClass()->GetClassPathName(), Out, /*bSearchSubClasses=*/true);
	}

	/** A full path loads directly. A name matches exactly first, then as a part of the asset name,
	 *  ignoring case; more than one partial match is refused and listed. */
	template <typename T>
	static T* FindAsset(const FString& Query)
	{
		if (Query.StartsWith(TEXT("/")))
		{
			return LoadObject<T>(nullptr, *Query);
		}

		TArray<FAssetData> Assets;
		GetAssetsOf<T>(Assets);

		TArray<FAssetData> Partial;
		for (const FAssetData& Asset : Assets)
		{
			const FString Name = Asset.AssetName.ToString();
			if (Name.Equals(Query, ESearchCase::IgnoreCase))
			{
				return Cast<T>(Asset.GetAsset());
			}
			if (Name.Contains(Query, ESearchCase::IgnoreCase))
			{
				Partial.Add(Asset);
			}
		}

		if (Partial.Num() == 1)
		{
			return Cast<T>(Partial[0].GetAsset());
		}
		if (Partial.Num() > 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] '%s' matches %d assets, be more exact:"), *Query, Partial.Num());
			for (const FAssetData& Asset : Partial)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG]     %s"), *Asset.AssetName.ToString());
			}
			return nullptr;
		}
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] nothing called '%s' (polarity.spawn.list shows the names)"), *Query);
		return nullptr;
	}

	static void CmdUpgrade(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] usage: polarity.spawn.upgrade <name|path>"));
			return;
		}
		APawn* Pawn = GetSpawnerOrWarn(World);
		UUpgradeDefinition* Definition = Pawn ? FindAsset<UUpgradeDefinition>(Args[0]) : nullptr;
		if (!Definition)
		{
			return;
		}

		// The same Blueprint the dispenser shows (it carries the hologram); the bare C++ class when
		// the pool names none.
		TSubclassOf<AActor> PickupClass = AUpgradePickup::StaticClass();
		if (const AShooterCharacter* Character = Cast<AShooterCharacter>(Pawn))
		{
			const UUpgradeManagerComponent* Upgrades = Character->GetUpgradeManager();
			const UDispenserUpgradePool* Pool = Upgrades ? Upgrades->GetSlotLayout() : nullptr;
			if (Pool && Pool->UpgradePickupClass && Pool->UpgradePickupClass->IsChildOf(AUpgradePickup::StaticClass()))
			{
				PickupClass = Pool->UpgradePickupClass;
			}
		}

		const FTransform Where = GetSpawnTransform(World, Pawn);
		AUpgradePickup* Pickup = World->SpawnActorDeferred<AUpgradePickup>(PickupClass, Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Pickup)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] spawn failed"));
			return;
		}
		// Before BeginPlay: the hologram is built from it there.
		Pickup->UpgradeDefinition = Definition;
		Pickup->FinishSpawning(Where);
		UE_LOG(LogTemp, Log, TEXT("[PICKUP_DEBUG] spawned upgrade %s (%s)"), *Definition->GetName(), *PickupClass->GetName());
	}

	static void CmdAttachment(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] usage: polarity.spawn.attachment <name|path>"));
			return;
		}
		APawn* Pawn = GetSpawnerOrWarn(World);
		UWeaponAttachmentDefinition* Definition = Pawn ? FindAsset<UWeaponAttachmentDefinition>(Args[0]) : nullptr;
		if (!Definition)
		{
			return;
		}

		// Same order as the dispenser's DeliverAttachment: the attachment before BeginPlay (the item
		// is built from it there), the look after.
		const FTransform Where = GetSpawnTransform(World, Pawn);
		AAttachmentPickup* Pickup = World->SpawnActorDeferred<AAttachmentPickup>(AAttachmentPickup::StaticClass(), Where,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Pickup)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] spawn failed"));
			return;
		}
		Pickup->Attachment = Definition;
		Pickup->FinishSpawning(Where);
		Pickup->ApplyAttachmentLook();
		UE_LOG(LogTemp, Log, TEXT("[PICKUP_DEBUG] spawned attachment %s"), *Definition->GetName());
	}

	static void CmdActor(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] usage: polarity.spawn.actor /Game/Path/BP_Thing"));
			return;
		}
		APawn* Pawn = GetSpawnerOrWarn(World);
		if (!Pawn)
		{
			return;
		}

		// Accept the path the content browser copies (/Game/X/BP_Y.BP_Y), the bare package path
		// (/Game/X/BP_Y), or the class path (.BP_Y_C).
		FString Path = Args[0];
		if (!Path.EndsWith(TEXT("_C")))
		{
			if (!Path.Contains(TEXT(".")))
			{
				Path += TEXT(".") + FPaths::GetBaseFilename(Path);
			}
			Path += TEXT("_C");
		}
		UClass* Class = LoadClass<AActor>(nullptr, *Path);
		if (!Class)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] no actor class at '%s'"), *Path);
			return;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const AActor* Spawned = World->SpawnActor<AActor>(Class, GetSpawnTransform(World, Pawn), Params);
		UE_LOG(LogTemp, Log, TEXT("[PICKUP_DEBUG] spawned %s"), *GetNameSafe(Spawned));
	}

	static void CmdList(const TArray<FString>& Args, UWorld* World)
	{
		const FString Kind = Args.Num() > 0 ? Args[0] : FString();
		const FString Filter = Args.Num() > 1 ? Args[1] : FString();

		TArray<FAssetData> Assets;
		if (Kind.Equals(TEXT("upgrade"), ESearchCase::IgnoreCase))
		{
			GetAssetsOf<UUpgradeDefinition>(Assets);
		}
		else if (Kind.Equals(TEXT("attachment"), ESearchCase::IgnoreCase))
		{
			GetAssetsOf<UWeaponAttachmentDefinition>(Assets);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] usage: polarity.spawn.list <upgrade|attachment> [filter]"));
			return;
		}

		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		for (const FAssetData& Asset : Assets)
		{
			if (Filter.IsEmpty() || Asset.AssetName.ToString().Contains(Filter, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTemp, Log, TEXT("[PICKUP_DEBUG]   %s"), *Asset.AssetName.ToString());
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdSpawnUpgrade(
	TEXT("polarity.spawn.upgrade"),
	TEXT("Spawn an upgrade pickup in front of you. Usage: polarity.spawn.upgrade <name|path> (e.g. extrajump)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PickupDebug::CmdUpgrade)
);

static FAutoConsoleCommandWithWorldAndArgs CmdSpawnAttachment(
	TEXT("polarity.spawn.attachment"),
	TEXT("Spawn an attachment pickup in front of you. Usage: polarity.spawn.attachment <name|path> (e.g. scope4x)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PickupDebug::CmdAttachment)
);

static FAutoConsoleCommandWithWorldAndArgs CmdSpawnActor(
	TEXT("polarity.spawn.actor"),
	TEXT("Spawn any actor Blueprint in front of you. Usage: polarity.spawn.actor /Game/Path/BP_Thing"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PickupDebug::CmdActor)
);

static FAutoConsoleCommandWithWorldAndArgs CmdSpawnList(
	TEXT("polarity.spawn.list"),
	TEXT("List upgrade or attachment asset names. Usage: polarity.spawn.list <upgrade|attachment> [filter]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PickupDebug::CmdList)
);
