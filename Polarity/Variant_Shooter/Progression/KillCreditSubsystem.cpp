// KillCreditSubsystem.cpp

#include "Variant_Shooter/Progression/KillCreditSubsystem.h"

#include "Variant_Shooter/Progression/RunLevelComponent.h"
#include "Variant_Shooter/Progression/RunLevelDefinition.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"

void UKillCreditSubsystem::NoteDamage(AActor* Victim, APlayerState* By)
{
	UWorld* const World = Victim ? Victim->GetWorld() : nullptr;
	UKillCreditSubsystem* const Self = World ? World->GetSubsystem<UKillCreditSubsystem>() : nullptr;
	if (!Self || !By || !Victim->HasAuthority() || Self->Paid.Contains(Victim))
	{
		return;
	}

	// Enemies that leave without dying (despawned, streamed out) would stay here for good.
	if (Self->Contributors.Num() + Self->Paid.Num() > 256)
	{
		Self->Prune();
	}
	Self->Contributors.FindOrAdd(Victim).AddUnique(By);
}

void UKillCreditSubsystem::NoteKill(AActor* Victim)
{
	UWorld* const World = Victim ? Victim->GetWorld() : nullptr;
	UKillCreditSubsystem* const Self = World ? World->GetSubsystem<UKillCreditSubsystem>() : nullptr;
	if (!Self || !Victim->HasAuthority() || Self->Paid.Contains(Victim))
	{
		return;
	}
	Self->Paid.Add(Victim);

	TArray<TWeakObjectPtr<APlayerState>> Players;
	Self->Contributors.RemoveAndCopyValue(Victim, Players);

	const URunLevelDefinition* const Definition = URunLevelSettings::GetDefinition();
	const int32 XP = Definition ? Definition->XPPerKill : 1;
	for (const TWeakObjectPtr<APlayerState>& Player : Players)
	{
		if (URunLevelComponent* const RunLevel = URunLevelComponent::FindFor(Player.Get()))
		{
			UE_LOG(LogTemp, Log, TEXT("[LEVEL_DEBUG] %s killed, %s gets %d XP for hurting it"),
				*GetNameSafe(Victim), *GetNameSafe(Player.Get()), XP);
			RunLevel->AddXP(XP);
		}
	}
}

bool UKillCreditSubsystem::IsPaid(const AActor* Victim)
{
	const UWorld* const World = Victim ? Victim->GetWorld() : nullptr;
	const UKillCreditSubsystem* const Self = World ? World->GetSubsystem<UKillCreditSubsystem>() : nullptr;
	return Self && Self->Paid.Contains(Victim);
}

void UKillCreditSubsystem::Prune()
{
	for (auto It = Contributors.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Paid.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
