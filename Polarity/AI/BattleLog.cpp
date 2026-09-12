// BattleLog.cpp

#include "AI/BattleLog.h"

#include "AI/PolarityTeams.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

UBattleLogSubsystem* UBattleLogSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UBattleLogSubsystem>() : nullptr;
}

float UBattleLogSubsystem::NowRelative() const
{
	const UWorld* const World = GetWorld();
	return World ? World->GetTimeSeconds() - StartTime : 0.0f;
}

void UBattleLogSubsystem::BeginFight(const FString& ScenarioName)
{
	Events.Reset();
	Sides.Reset();

	Scenario = ScenarioName;
	StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	bFightRunning = true;

	UE_LOG(LogTemp, Warning, TEXT("[BATTLE] === %s begins ==="), *Scenario);
}

void UBattleLogSubsystem::RecordSpawn(APawn* Pawn, uint8 TeamId)
{
	if (!Pawn)
	{
		return;
	}

	Sides.FindOrAdd(TeamId).Spawned++;

	// Deaths arrive by delegate rather than by polling: the detailed one carries the damage type and
	// the causer, which is the whole reason a kill line can say what killed whom instead of just
	// noting that somebody stopped existing.
	if (AShooterNPC* const NPC = Cast<AShooterNPC>(Pawn))
	{
		NPC->OnNPCDeathDetailed.AddDynamic(this, &UBattleLogSubsystem::OnMemberDeath);
	}

	FBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Time = NowRelative();
	Event.Kind = TEXT("spawn");
	Event.Subject = Pawn->GetName();
	Event.SubjectTeam = TeamId;
}

void UBattleLogSubsystem::OnMemberDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingDamageCauser)
{
	if (!DeadNPC || !bFightRunning)
	{
		return;
	}

	const uint8 VictimTeam = PolarityTeams::GetTeam(DeadNPC);

	// The causer can be a projectile, a weapon or a pawn. Same resolution the damage code uses, so
	// the log credits the shooter rather than the bullet.
	AActor* const Killer = PolarityTeams::ResolveDamageSource(KillingDamageCauser, nullptr);
	const uint8 KillerTeam = Killer ? PolarityTeams::GetTeam(Killer) : static_cast<uint8>(255);

	const float Distance = Killer ? FVector::Dist(Killer->GetActorLocation(), DeadNPC->GetActorLocation()) : 0.0f;

	FBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Time = NowRelative();
	Event.Kind = TEXT("kill");
	Event.Subject = DeadNPC->GetName();
	Event.SubjectTeam = VictimTeam;
	Event.Other = Killer ? Killer->GetName() : TEXT("world");
	Event.OtherTeam = KillerTeam;
	Event.Distance = Distance;
	Event.Detail = KillingDamageType ? KillingDamageType->GetName() : TEXT("unknown");

	Sides.FindOrAdd(VictimTeam).Lost++;

	// Friendly fire and world kills are recorded but credited to nobody, so a side cannot farm its
	// own casualties into a kill count.
	if (Killer && KillerTeam != VictimTeam && KillerTeam != 255)
	{
		FBattleSideStats& KillerStats = Sides.FindOrAdd(KillerTeam);
		KillerStats.Kills++;
		KillerStats.KillDistanceSum += Distance;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BATTLE] %6.1fs  kill  %s (team %d) <- %s (team %d)  %.0fcm  %s"),
		Event.Time, *Event.Subject, VictimTeam, *Event.Other, KillerTeam, Distance, *Event.Detail);
}

void UBattleLogSubsystem::RecordNote(const FString& Kind, const FString& Subject, uint8 TeamId, const FString& Detail)
{
	if (!bFightRunning)
	{
		return;
	}

	FBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Time = NowRelative();
	Event.Kind = Kind;
	Event.Subject = Subject;
	Event.SubjectTeam = TeamId;
	Event.Detail = Detail;

	UE_LOG(LogTemp, Warning, TEXT("[BATTLE] %6.1fs  %s  %s (team %d)  %s"),
		Event.Time, *Kind, *Subject, TeamId, *Detail);
}

void UBattleLogSubsystem::FinishFight(const FString& Reason)
{
	if (!bFightRunning)
	{
		return;
	}

	bFightRunning = false;

	UE_LOG(LogTemp, Warning, TEXT("[BATTLE] === fight over: %s ==="), *Reason);
	Dump();
}

void UBattleLogSubsystem::Dump() const
{
	const float Duration = NowRelative();

	UE_LOG(LogTemp, Warning, TEXT("[BATTLE] ---- %s, %.1f s, %d events ----"),
		*Scenario, Duration, Events.Num());

	// Sides in team order so two runs can be read side by side without hunting.
	TArray<uint8> Teams;
	Sides.GetKeys(Teams);
	Teams.Sort();

	for (const uint8 Team : Teams)
	{
		const FBattleSideStats& Stats = Sides[Team];
		const int32 Alive = Stats.Spawned - Stats.Lost;
		const float AverageKillDistance = Stats.Kills > 0 ? Stats.KillDistanceSum / Stats.Kills : 0.0f;

		UE_LOG(LogTemp, Warning, TEXT("[BATTLE] team %d: spawned %d, lost %d, alive %d, kills %d, avg kill range %.0f cm"),
			Team, Stats.Spawned, Stats.Lost, Alive, Stats.Kills, AverageKillDistance);
	}

	// First blood says a lot about who found whom: a side that scores at ten seconds saw the other
	// one coming, a fight that opens at ninety was two squads wandering.
	for (const FBattleEvent& Event : Events)
	{
		if (Event.Kind == TEXT("kill"))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BATTLE] first blood at %.1f s: team %d killed %s"),
				Event.Time, Event.OtherTeam, *Event.Subject);
			break;
		}
	}
}
