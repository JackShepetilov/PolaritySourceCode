// WeaponMasteryComponent.cpp

#include "Variant_Shooter/Progression/WeaponMasteryComponent.h"

#include "Variant_Shooter/Progression/WeaponMasteryDefinition.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Coop/CoopPlayers.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UWeaponMasteryComponent::UWeaponMasteryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWeaponMasteryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWeaponMasteryComponent, Progress);
}

UWeaponMasteryComponent* UWeaponMasteryComponent::FindFor(const AActor* Player)
{
	const APlayerState* PlayerState = Cast<APlayerState>(Player);
	if (!PlayerState)
	{
		if (const APawn* const Pawn = Cast<APawn>(Player))
		{
			PlayerState = Pawn->GetPlayerState();
		}
		else if (const AController* const Controller = Cast<AController>(Player))
		{
			PlayerState = Controller->PlayerState;
		}
	}
	return PlayerState ? PlayerState->FindComponentByClass<UWeaponMasteryComponent>() : nullptr;
}

void UWeaponMasteryComponent::AddXP(const UWeaponMasteryDefinition* Definition, int32 Amount)
{
	AActor* const Owner = GetOwner();
	if (!Definition || Amount <= 0 || !Owner || !Owner->HasAuthority())
	{
		return;
	}

	FWeaponMasteryProgress* Entry = Progress.FindByPredicate(
		[Definition](const FWeaponMasteryProgress& P) { return P.Definition == Definition; });
	if (!Entry)
	{
		Entry = &Progress.AddDefaulted_GetRef();
		Entry->Definition = const_cast<UWeaponMasteryDefinition*>(Definition);
	}

	const int32 OldLevel = Entry->Level;
	Entry->XP += Amount;
	Entry->Level = Definition->GetLevelForXP(Entry->XP);

	UE_LOG(LogTemp, Log, TEXT("[MASTERY_DEBUG] %s %s +%d XP = %d, level %d%s (next at %d)"),
		*GetNameSafe(Owner), *GetNameSafe(Definition), Amount, Entry->XP, Entry->Level,
		Entry->Level != OldLevel ? TEXT(" UP") : TEXT(""), Definition->GetXPToReach(Entry->Level + 1));

	Owner->ForceNetUpdate();
}

void UWeaponMasteryComponent::SetLevelForDebug(const UWeaponMasteryDefinition* Definition, int32 NewLevel)
{
	AActor* const Owner = GetOwner();
	if (!Definition || !Owner || !Owner->HasAuthority())
	{
		return;
	}

	FWeaponMasteryProgress* Entry = Progress.FindByPredicate(
		[Definition](const FWeaponMasteryProgress& P) { return P.Definition == Definition; });
	if (!Entry)
	{
		Entry = &Progress.AddDefaulted_GetRef();
		Entry->Definition = const_cast<UWeaponMasteryDefinition*>(Definition);
	}
	Entry->Level = FMath::Clamp(NewLevel, 0, Definition->Steps.Num());
	Entry->XP = Entry->Level > 0 ? Definition->GetXPToReach(Entry->Level) : 0;
	UE_LOG(LogTemp, Log, TEXT("[MASTERY_DEBUG] %s %s set to level %d (%d XP)"),
		*GetNameSafe(Owner), *GetNameSafe(Definition), Entry->Level, Entry->XP);
	Owner->ForceNetUpdate();
}

int32 UWeaponMasteryComponent::GetLevel(const UWeaponMasteryDefinition* Definition) const
{
	const FWeaponMasteryProgress* const Entry = Progress.FindByPredicate(
		[Definition](const FWeaponMasteryProgress& P) { return P.Definition == Definition; });
	return Entry ? Entry->Level : 0;
}

int32 UWeaponMasteryComponent::GetXP(const UWeaponMasteryDefinition* Definition) const
{
	const FWeaponMasteryProgress* const Entry = Progress.FindByPredicate(
		[Definition](const FWeaponMasteryProgress& P) { return P.Definition == Definition; });
	return Entry ? Entry->XP : 0;
}

// ==================== Debug ====================
// Local by nature: the player at this screen and the gun in his hands, on a machine with authority.

namespace WeaponMasteryDebug
{
	static const AShooterCharacter* FindLocalCharacter(UWorld* World)
	{
		const APlayerController* const PC = CoopPlayers::GetLocalController(World);
		return PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
	}

	static void CmdXP(const TArray<FString>& Args, UWorld* World)
	{
		const AShooterCharacter* const Character = FindLocalCharacter(World);
		const AShooterWeapon* const Weapon = Character ? Character->GetCurrentWeapon() : nullptr;
		UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(Character);
		if (!Weapon || !Mastery || !Weapon->MasteryDefinition || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MASTERY_DEBUG] usage: polarity.mastery.xp <amount> (needs a gun in hand with a MasteryDefinition)"));
			return;
		}
		Mastery->AddXP(Weapon->MasteryDefinition, FCString::Atoi(*Args[0]));
	}

	static void CmdShow(UWorld* World)
	{
		const AShooterCharacter* const Character = FindLocalCharacter(World);
		const UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(Character);
		if (!Mastery)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MASTERY_DEBUG] no local player with a mastery component"));
			return;
		}
		for (const FWeaponMasteryProgress& Entry : Mastery->GetProgress())
		{
			UE_LOG(LogTemp, Log, TEXT("[MASTERY_DEBUG] %s: level %d, %d XP, next at %d"), *GetNameSafe(Entry.Definition),
				Entry.Level, Entry.XP, Entry.Definition ? Entry.Definition->GetXPToReach(Entry.Level + 1) : -1);
		}
		if (const AShooterWeapon* const Weapon = Character->GetCurrentWeapon())
		{
			const FWeaponMasteryPerks Perks = Weapon->GetMasteryPerks();
			UE_LOG(LogTemp, Log, TEXT("[MASTERY_DEBUG] in hand %s (%s): ADS in slide %d, in air %d, reload in ADS %d, in slide %d, no ADS slow %d, tac sprint %d, fast swap %d, throw %d"),
				*GetNameSafe(Weapon), *GetNameSafe(Weapon->MasteryDefinition), Perks.bAllowADSWhileSliding, Perks.bAllowADSWhileAirborne,
				Perks.bAllowReloadWhileADS, Perks.bAllowReloadWhileSliding, Perks.bIgnoreADSMoveSpeedPenalty,
				Perks.bAllowTacticalSprint, Perks.bFastSwapAfterKill, Perks.bCanThrowWeapon);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdWeaponMasteryXP(
	TEXT("polarity.mastery.xp"),
	TEXT("Give mastery experience for the gun in the local player's hands. Usage: polarity.mastery.xp <amount>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WeaponMasteryDebug::CmdXP)
);

static FAutoConsoleCommandWithWorld CmdWeaponMasteryShow(
	TEXT("polarity.mastery.show"),
	TEXT("Print the local player's weapon mastery and what the gun in hand opens."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&WeaponMasteryDebug::CmdShow)
);
