// NPCDebugCommands.cpp
// Console commands for watching an enemy's animations without fighting it: it stays where it is and
// keeps doing everything it normally does (aiming, firing, reloading, hit reactions) while the
// player walks around it. Host side, like the other polarity.* debug commands.
//
//   polarity.npc.freeze 0 [radius]    back to normal: speeds and positions restored
//   polarity.npc.freeze 1 [radius]    hold still: movement stopped, speed pinned to zero
//   polarity.npc.freeze 2 [radius]    run in place: velocity stays, so locomotion animations keep
//                                     playing, but the actor is put back where it was every tick
//   polarity.npc.damage <scale>       outgoing shot damage of every enemy weapon (0 = harmless)
//
// radius is centimetres from the local player's pawn; omitted or 0 means every NPC in the world.
// The freeze is re-applied on a timer because an AI state that sets MaxWalkSpeed back would undo a
// one-shot.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace NPCDebug
{
	/** 0 off, 1 hold still, 2 run in place. Remembered so the repeating applier can decide. */
	static int32 GFrozenMode = 0;
	static float GFrozenRadius = 0.0f;

	/** Speed to put back when the freeze is lifted. */
	static TMap<TWeakObjectPtr<AShooterNPC>, float> GSavedWalkSpeeds;
	/** Where each actor stood when mode 2 pinned it. */
	static TMap<TWeakObjectPtr<AShooterNPC>, FVector> GPinnedLocations;
	static FTimerHandle GFreezeTimer;

	/** Every NPC in the world, or only those within Radius centimetres of the local player. */
	static void CollectNPCs(UWorld* World, float Radius, TArray<AShooterNPC*>& Out)
	{
		const APlayerController* PC = CoopPlayers::GetLocalController(World);
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const FVector Origin = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

		for (TActorIterator<AShooterNPC> It(World); It; ++It)
		{
			AShooterNPC* NPC = *It;
			if (!IsValid(NPC))
			{
				continue;
			}
			if (Radius > 0.0f && (!Pawn || FVector::Dist(NPC->GetActorLocation(), Origin) > Radius))
			{
				continue;
			}
			Out.Add(NPC);
		}
	}

	static void ApplyFreeze(UWorld* World)
	{
		TArray<AShooterNPC*> NPCs;
		CollectNPCs(World, GFrozenRadius, NPCs);

		for (AShooterNPC* NPC : NPCs)
		{
			UCharacterMovementComponent* Movement = NPC->GetCharacterMovement();
			if (!Movement)
			{
				continue;
			}

			if (!GSavedWalkSpeeds.Contains(NPC))
			{
				GSavedWalkSpeeds.Add(NPC, Movement->MaxWalkSpeed);
			}

			Movement->MaxWalkSpeed = 0.0f;
			Movement->MaxFlySpeed = 0.0f;

			if (GFrozenMode == 1)
			{
				Movement->StopMovementImmediately();
				Movement->Velocity = FVector::ZeroVector;
			}
			else if (const FVector* Pinned = GPinnedLocations.Find(NPC))
			{
				NPC->SetActorLocation(*Pinned, /*bSweep*/ false, nullptr, ETeleportType::None);
			}
		}
	}

	/** Puts every speed the freeze touched back. Positions are not restored on purpose: an NPC that
	 *  ran in place was never displaced, and one that was held never left its spot. */
	static void ReleaseFreeze()
	{
		for (const TPair<TWeakObjectPtr<AShooterNPC>, float>& Pair : GSavedWalkSpeeds)
		{
			if (AShooterNPC* NPC = Pair.Key.Get())
			{
				if (UCharacterMovementComponent* Movement = NPC->GetCharacterMovement())
				{
					Movement->MaxWalkSpeed = Pair.Value;
				}
			}
		}
		GSavedWalkSpeeds.Reset();
		GPinnedLocations.Reset();
	}

	static void CmdFreeze(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPC_DEBUG] freeze runs on the server (host console) only"));
			return;
		}

		const int32 Mode = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const float Radius = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;

		World->GetTimerManager().ClearTimer(GFreezeTimer);
		ReleaseFreeze();
		GFrozenMode = FMath::Clamp(Mode, 0, 2);
		GFrozenRadius = FMath::Max(0.0f, Radius);

		if (GFrozenMode == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[NPC_DEBUG] freeze OFF, speeds restored"));
			return;
		}

		TArray<AShooterNPC*> NPCs;
		CollectNPCs(World, GFrozenRadius, NPCs);

		if (GFrozenMode == 2)
		{
			for (AShooterNPC* NPC : NPCs)
			{
				GPinnedLocations.Add(NPC, NPC->GetActorLocation());
			}
		}

		ApplyFreeze(World);
		const FTimerDelegate Delegate = FTimerDelegate::CreateStatic(&NPCDebug::ApplyFreeze, World);
		World->GetTimerManager().SetTimer(GFreezeTimer, Delegate, 0.05f, /*bLoop*/ true);

		UE_LOG(LogTemp, Log, TEXT("[NPC_DEBUG] freeze mode %d on %d NPC(s), radius %.0f"),
			GFrozenMode, NPCs.Num(), GFrozenRadius);
	}

	static void CmdDamage(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPC_DEBUG] damage runs on the server (host console) only"));
			return;
		}

		const float Scale = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.0f;

		int32 Count = 0;
		for (TActorIterator<AShooterWeapon> It(World); It; ++It)
		{
			AShooterWeapon* Weapon = *It;
			if (!Weapon)
			{
				continue;
			}

			// Only guns an enemy is holding: this is for watching an enemy, not for testing what
			// the player's own weapons do.
			const AActor* Holder = Weapon->GetAttachParentActor();
			if (!Holder)
			{
				Holder = Weapon->GetOwner();
			}
			if (!Cast<AShooterNPC>(Holder))
			{
				continue;
			}

			Weapon->SetDebugDamageScale(Scale);
			++Count;
		}

		UE_LOG(LogTemp, Log, TEXT("[NPC_DEBUG] enemy weapon damage x%.2f on %d weapon(s)"), Scale, Count);
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdNPCFreeze(
	TEXT("polarity.npc.freeze"),
	TEXT("Freeze enemies for animation checks: 0 off, 1 hold still, 2 run in place. Usage: polarity.npc.freeze <0|1|2> [radius]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NPCDebug::CmdFreeze)
);

static FAutoConsoleCommandWithWorldAndArgs CmdNPCDamage(
	TEXT("polarity.npc.damage"),
	TEXT("Scale the outgoing damage of every enemy weapon (0 = harmless). Usage: polarity.npc.damage <scale>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NPCDebug::CmdDamage)
);
