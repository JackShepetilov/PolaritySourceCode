// FightRecorder.cpp

#include "Variant_Shooter/Bench/FightRecorder.h"

#include "AIController.h"
#include "AI/Components/AIAccuracyComponent.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "Coop/CoopPlayers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericTeamAgentInterface.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Suppression/PlayerSuppressionComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

static TAutoConsoleVariable<int32> CVarFightAutoReport(
	TEXT("polarity.fight.autoreport"),
	1,
	TEXT("1 - when a recorded fight ends, draw its pictures and write its report (analyze_fight.py) without being asked."));

namespace
{
	const float SamplePeriod = 0.5f;
	const float FlushPeriod = 2.0f;

	/** Slower than this is standing (cm/s). */
	const float StandingSpeed = 20.0f;

	/** Faster than this is a step taken on purpose (cm/s). */
	const float MovingSpeed = 50.0f;

	void AppendTo(const FString& Path, FString& Text)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
		Text.Reset();
	}

	const TCHAR* PostureName(ESquadPosture Posture)
	{
		switch (Posture)
		{
		case ESquadPosture::Defense: return TEXT("DEFENSE");
		case ESquadPosture::Assault: return TEXT("ASSAULT");
		default: return TEXT("FIREFIGHT");
		}
	}

	/** Which player a pawn is, by the place of its controller in the list of players. A player that
	 *  dies comes back in a new pawn, and the record has to go on calling it the same name: the first
	 *  DUEL written down had a PLAYER that died and a PLAYER_2 that appeared in its place. */
	TMap<TWeakObjectPtr<const AActor>, int32> PlayerIndexByPawn;

	FString SafeFileName(const FString& In)
	{
		FString Out;
		for (const TCHAR Char : In)
		{
			Out.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('_'));
		}
		return Out;
	}
}

UFightRecorderSubsystem* UFightRecorderSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* const World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UFightRecorderSubsystem>() : nullptr;
}

TStatId UFightRecorderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFightRecorderSubsystem, STATGROUP_Tickables);
}

float UFightRecorderSubsystem::FightTime() const
{
	const UWorld* const World = GetWorld();
	return World ? World->GetTimeSeconds() - StartTime : 0.0f;
}

void UFightRecorderSubsystem::BeginFight(const FString& Scenario)
{
	const UWorld* const World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	++OpenFights;
	if (bRecording)
	{
		// A second console shot while the first fight runs: one record, both names.
		Scenarios += TEXT(" + ") + Scenario;
		PendingEvents += FString::Printf(TEXT("%.2f,start,%s,,0,\n"), FightTime(), *Scenario);
		return;
	}

	bRecording = true;
	Scenarios = Scenario;
	StartTime = World->GetTimeSeconds();
	LastSampleTime = -1.0f;
	LastFlushTime = StartTime;
	Players.Reset();
	PlayerIndexByPawn.Reset();
	Tallies.Reset();
	Extent = FBox(ForceInit);

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BenchTrack"));
	Folder = Root / (FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT("_") + SafeFileName(Scenario));
	IFileManager::Get().MakeDirectory(*Folder, true);

	// Where the newest record is, for the tool that draws it.
	FFileHelper::SaveStringToFile(Folder, *(Root / TEXT("last.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	PendingTrack = TEXT("wall,t,name,team,x,y,hp,dead,shooting,focus,los,crouch,speed,spread,posture,ratio,suppressed,element,commander,z,off,note\n");
	PendingEvents = TEXT("t,event,who,other,value,extra\n");
	PendingEvents += FString::Printf(TEXT("0.00,start,%s,,0,\n"), *Scenario);

	UE_LOG(LogTemp, Log, TEXT("[FIGHT] recording '%s' to %s"), *Scenario, *Folder);
}

void UFightRecorderSubsystem::EndFight(const FString& Scenario, const FString& Outcome)
{
	if (!bRecording)
	{
		return;
	}

	PendingEvents += FString::Printf(TEXT("%.2f,end,%s,,0,%s\n"), FightTime(), *Scenario, *Outcome.Replace(TEXT(","), TEXT(";")));

	OpenFights = FMath::Max(0, OpenFights - 1);
	if (OpenFights == 0)
	{
		Close(Outcome);
	}
}

void UFightRecorderSubsystem::Deinitialize()
{
	if (bRecording)
	{
		Close(TEXT("play ended"));
	}
	Super::Deinitialize();
}

void UFightRecorderSubsystem::Close(const FString& Outcome)
{
	Sample();
	Flush();
	WriteGeometry();
	WriteSummary(Outcome);

	UE_LOG(LogTemp, Log, TEXT("[FIGHT] '%s' saved, %.1f s, %s: %s"), *Scenarios, FightTime(), *Outcome, *Folder);

	// The pictures and the report, without anybody asking for them after every fight [author,
	// 2026-10-11]. A separate process, not waited for: it takes a few seconds and the game goes on.
	if (CVarFightAutoReport.GetValueOnGameThread() != 0)
	{
		const FString Script = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source/Tools/CombatBench/analyze_fight.py"));
		const FString Arguments = FString::Printf(TEXT("\"%s\" \"%s\""), *Script, *Folder);
		FProcHandle Handle = FPlatformProcess::CreateProc(TEXT("python"), *Arguments, /*bLaunchDetached*/ true,
			/*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true, nullptr, 0, nullptr, nullptr);
		if (Handle.IsValid())
		{
			FPlatformProcess::CloseProc(Handle);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FIGHT] could not start python for the report: %s"), *Script);
		}
	}

	bRecording = false;
	OpenFights = 0;
}

void UFightRecorderSubsystem::Tick(float DeltaTime)
{
	if (!bRecording)
	{
		return;
	}

	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (LastSampleTime < 0.0f || Now - LastSampleTime >= SamplePeriod)
	{
		Sample();
	}
	if (Now - LastFlushTime >= FlushPeriod)
	{
		Flush();
	}
}

void UFightRecorderSubsystem::Flush()
{
	AppendTo(Folder / TEXT("track.csv"), PendingTrack);
	AppendTo(Folder / TEXT("events.csv"), PendingEvents);
	if (const UWorld* const World = GetWorld())
	{
		LastFlushTime = World->GetTimeSeconds();
	}
}

FString UFightRecorderSubsystem::NameOf(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}

	const int32* const Known = PlayerIndexByPawn.Find(Actor);
	if (Known || CoopPlayers::IsPlayer(Actor))
	{
		int32 Index = Known ? *Known : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			const APawn* const Pawn = Cast<APawn>(Actor);
			const AActor* const Key = Pawn && Pawn->GetController() ? static_cast<const AActor*>(Pawn->GetController()) : Actor;
			Index = Players.IndexOfByKey(Key);
			if (Index == INDEX_NONE)
			{
				Index = Players.Add(Key);
			}
			PlayerIndexByPawn.Add(Actor, Index);
		}
		return Index == 0 ? FString(TEXT("PLAYER")) : FString::Printf(TEXT("PLAYER_%d"), Index + 1);
	}

	return Actor->GetName();
}

UFightRecorderSubsystem::FTally& UFightRecorderSubsystem::TallyOf(const AActor* Actor)
{
	const FString Name = NameOf(Actor);
	FTally* Tally = Tallies.Find(Name);
	if (!Tally)
	{
		Tally = &Tallies.Add(Name);
		Tally->Name = Name;
		Tally->bPlayer = Name.StartsWith(TEXT("PLAYER"));
		Tally->FirstSeenTime = FightTime();
		if (const IGenericTeamAgentInterface* const Agent = Cast<IGenericTeamAgentInterface>(Actor))
		{
			Tally->Team = Agent->GetGenericTeamId().GetId();
		}
		else if (Tally->bPlayer)
		{
			Tally->Team = 0;
		}
	}
	return *Tally;
}

const AActor* UFightRecorderSubsystem::ResolveAttacker(const AController* InstigatedBy, const AActor* Causer)
{
	if (InstigatedBy && InstigatedBy->GetPawn())
	{
		return InstigatedBy->GetPawn();
	}

	// A weapon or a round: walk up to the pawn behind it.
	const AActor* Step = Causer;
	for (int32 Depth = 0; Step && Depth < 3; ++Depth)
	{
		if (Step->IsA<APawn>())
		{
			return Step;
		}
		if (const APawn* const Instigator = Step->GetInstigator())
		{
			return Instigator;
		}
		Step = Step->GetOwner();
	}
	return Causer;
}

void UFightRecorderSubsystem::Sample()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	LastSampleTime = Now;

	const float Time = Now - StartTime;
	const double Wall = (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
	AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(World);

	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* const NPC = *It;
		if (!IsValid(NPC))
		{
			continue;
		}

		const AAIController* const Controller = Cast<AAIController>(NPC->GetController());
		AActor* const Focus = Controller ? Controller->GetFocusActor() : nullptr;
		const UAIAccuracyComponent* const Accuracy = NPC->FindComponentByClass<UAIAccuracyComponent>();
		const UCharacterMovementComponent* const Move = NPC->GetCharacterMovement();

		const FVector Location = NPC->GetActorLocation();
		const bool bDead = NPC->IsDead();
		const bool bShooting = NPC->IsCurrentlyShooting();
		const bool bSight = !bDead && Focus && NPC->HasLineOfSightTo(Focus);
		const float Speed = NPC->GetVelocity().Size();

		// How far its target really is from where it thinks the target is, cm. Zero while the target
		// is in view of its side, below zero with no target. @see AShooterAIController::GetTargetLocationError
		const AShooterAIController* const ShooterController = Cast<AShooterAIController>(Controller);
		const float TargetOff = (ShooterController && !bDead) ? ShooterController->GetTargetLocationError() : -1.0f;

		FTally& Tally = TallyOf(NPC);
		if (!bDead)
		{
			Extent += Location;
			++Tally.Samples;
			Tally.ShootingSamples += bShooting ? 1 : 0;
			Tally.SightSamples += bSight ? 1 : 0;
			Tally.IdleSamples += (Speed < StandingSpeed && !bShooting) ? 1 : 0;
			Tally.LastAliveTime = Time;

			if (bSight && Tally.FirstSightTime < 0.0f)
			{
				Tally.FirstSightTime = Time;
			}
			if (Tally.FirstSightTime >= 0.0f && Tally.FirstMoveTime < 0.0f && Speed > MovingSpeed)
			{
				Tally.FirstMoveTime = Time;
			}
			if (Tally.bHasLastLocation)
			{
				Tally.PathLength += FVector::Dist2D(Location, Tally.LastLocation);
			}
			Tally.LastLocation = Location;
			Tally.bHasLastLocation = true;
		}

		PendingTrack += FString::Printf(
			TEXT("%.2f,%.2f,%s,%d,%.0f,%.0f,%.0f,%d,%d,%s,%d,%d,%.0f,%.1f,%s,%.2f,%d,%d,%d,%.0f,%.0f,%s\n"),
			Wall, Time, *Tally.Name, static_cast<int32>(Tally.Team), Location.X, Location.Y, NPC->CurrentHP,
			bDead ? 1 : 0, bShooting ? 1 : 0, *NameOf(Focus), bSight ? 1 : 0,
			Move && Move->IsCrouching() ? 1 : 0, Speed, Accuracy ? Accuracy->LastCalculatedSpread : 0.0f,
			Coordinator ? PostureName(Coordinator->GetPostureFor(NPC)) : TEXT(""),
			Coordinator ? Coordinator->GetForceRatioFor(NPC) : 1.0f,
			Accuracy && Accuracy->IsSuppressed() ? 1 : 0,
			Coordinator && Coordinator->IsInAssaultElement(NPC) ? 1 : 0,
			Coordinator && Coordinator->IsSquadCommander(NPC) ? 1 : 0,
			Location.Z, TargetOff, *NPC->GetTacticNote().ToString().Replace(TEXT(","), TEXT(";")));
	}

	TArray<APawn*> Pawns;
	CoopPlayers::GetAll(World, Pawns);
	for (const APawn* const Pawn : Pawns)
	{
		if (!IsValid(Pawn))
		{
			continue;
		}

		const FVector Location = Pawn->GetActorLocation();
		const float Speed = Pawn->GetVelocity().Size();
		const AShooterCharacter* const Shooter = Cast<AShooterCharacter>(Pawn);
		const UCharacterMovementComponent* const Move = Shooter ? Shooter->GetCharacterMovement() : nullptr;
		const float Health = Shooter ? Shooter->GetCurrentHP() : 0.0f;
		const UPlayerSuppressionComponent* const Suppression = Pawn->FindComponentByClass<UPlayerSuppressionComponent>();
		const float Suppressed = Suppression ? Suppression->GetLevel() : 0.0f;

		FTally& Tally = TallyOf(Pawn);
		Extent += Location;
		++Tally.Samples;
		Tally.LastAliveTime = Time;
		if (Tally.bHasLastLocation)
		{
			Tally.PathLength += FVector::Dist2D(Location, Tally.LastLocation);
		}
		Tally.LastLocation = Location;
		Tally.bHasLastLocation = true;

		// The suppressed column carries the level in hundredths for a player (0..100).
		PendingTrack += FString::Printf(TEXT("%.2f,%.2f,%s,%d,%.0f,%.0f,%.0f,%d,0,,0,%d,%.0f,0.0,,1.00,%d,0,0,%.0f,,\n"),
			Wall, Time, *Tally.Name, static_cast<int32>(Tally.Team), Location.X, Location.Y, Health,
			Health <= 0.0f ? 1 : 0, Move && Move->IsCrouching() ? 1 : 0, Speed, FMath::RoundToInt(Suppressed * 100.0f), Location.Z);
	}
}

void UFightRecorderSubsystem::NoteShot(const AActor* Shooter, const AActor* Target, bool bPinFire, bool bMeantToMiss)
{
	UFightRecorderSubsystem* const Recorder = Get(Shooter);
	if (!Recorder || !Recorder->bRecording)
	{
		return;
	}

	const float Time = Recorder->FightTime();
	FTally& Tally = Recorder->TallyOf(Shooter);
	++Tally.Shots;
	Tally.PinShots += (bPinFire || bMeantToMiss) ? 1 : 0;
	if (Tally.FirstShotTime < 0.0f)
	{
		Tally.FirstShotTime = Time;

		// What it is shooting with, once, at its first round: enemies roll their gun at spawn
		// (AShooterNPC::WeaponOptions), and "who killed the player" means little without "with what".
		TArray<AActor*> Carried;
		Shooter->GetAttachedActors(Carried);
		for (const AActor* const Item : Carried)
		{
			if (Item && Item->IsA<AShooterWeapon>())
			{
				Recorder->PendingEvents += FString::Printf(TEXT("%.2f,gun,%s,,0,%s\n"), Time, *Tally.Name,
					*Item->GetClass()->GetName().Replace(TEXT(","), TEXT(";")));
				break;
			}
		}
	}

	Recorder->PendingEvents += FString::Printf(TEXT("%.2f,shot,%s,%s,%d,%.0f\n"), Time, *Tally.Name,
		*Recorder->NameOf(Target), bPinFire ? 1 : (bMeantToMiss ? 2 : 0),
		Target ? FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation()) : -1.0f);
}

void UFightRecorderSubsystem::NoteDamage(const AActor* Victim, const AController* InstigatedBy, const AActor* Causer,
	float Damage, float HealthAfter)
{
	UFightRecorderSubsystem* const Recorder = Get(Victim);
	if (!Recorder || !Recorder->bRecording || Damage <= 0.0f)
	{
		return;
	}

	const AActor* const Attacker = ResolveAttacker(InstigatedBy, Causer);
	const FString AttackerName = Recorder->NameOf(Attacker);
	if (Attacker)
	{
		FTally& From = Recorder->TallyOf(Attacker);
		++From.HitsLanded;
		From.DamageDealt += Damage;
	}

	FTally& To = Recorder->TallyOf(Victim);
	++To.HitsTaken;
	To.DamageTaken += Damage;
	if (!AttackerName.IsEmpty())
	{
		To.LastHurtBy = AttackerName;
	}

	Recorder->PendingEvents += FString::Printf(TEXT("%.2f,hit,%s,%s,%.1f,%.0f\n"), Recorder->FightTime(),
		*AttackerName, *To.Name, Damage, HealthAfter);
}

void UFightRecorderSubsystem::NoteDeath(const AActor* Victim, const AActor* Killer)
{
	UFightRecorderSubsystem* const Recorder = Get(Victim);
	if (!Recorder || !Recorder->bRecording)
	{
		return;
	}

	const AActor* const Attacker = ResolveAttacker(nullptr, Killer);
	FString AttackerName = Recorder->NameOf(Attacker);

	// No killer named (a player's death comes that way): whoever hurt it last.
	if (AttackerName.IsEmpty())
	{
		AttackerName = Recorder->TallyOf(Victim).LastHurtBy;
	}
	if (FTally* const KillerTally = AttackerName.IsEmpty() ? nullptr : Recorder->Tallies.Find(AttackerName))
	{
		++KillerTally->Kills;
	}

	FTally& Dead = Recorder->TallyOf(Victim);
	Dead.DeathTime = Recorder->FightTime();
	Dead.KilledBy = AttackerName;

	Recorder->PendingEvents += FString::Printf(TEXT("%.2f,death,%s,%s,0,\n"), Dead.DeathTime, *Dead.Name, *AttackerName);
}

void UFightRecorderSubsystem::NoteTactic(const AActor* Who, FName Note)
{
	UFightRecorderSubsystem* const Recorder = Get(Who);
	if (!Recorder || !Recorder->bRecording)
	{
		return;
	}

	Recorder->PendingEvents += FString::Printf(TEXT("%.2f,tactic,%s,,0,%s\n"), Recorder->FightTime(),
		*Recorder->NameOf(Who), *Note.ToString().Replace(TEXT(","), TEXT(";")));
}

void UFightRecorderSubsystem::NoteMark(const UObject* WorldContext, const FString& What)
{
	UFightRecorderSubsystem* const Recorder = Get(WorldContext);
	if (!Recorder || !Recorder->bRecording)
	{
		return;
	}

	Recorder->PendingEvents += FString::Printf(TEXT("%.2f,mark,,,0,%s\n"), Recorder->FightTime(), *What.Replace(TEXT(","), TEXT(";")));
}

void UFightRecorderSubsystem::WriteGeometry() const
{
	const UWorld* const World = GetWorld();
	if (!World || !Extent.IsValid)
	{
		return;
	}

	// The boxes of the level around where anybody stood: enough to draw the walls on the picture.
	const FBox Around(Extent.Min - FVector(2500.0f, 2500.0f, 300.0f), Extent.Max + FVector(2500.0f, 2500.0f, 600.0f));

	FString Out = FString::Printf(TEXT("cx,cy,sx,sy,zmin,zmax\n#floor,%.0f\n"), Extent.Min.Z);
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox();
		if (!Bounds.IsValid || !Bounds.Intersect(Around))
		{
			continue;
		}
		const FVector Centre = Bounds.GetCenter();
		const FVector Size = Bounds.GetSize();
		Out += FString::Printf(TEXT("%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n"), Centre.X, Centre.Y, Size.X, Size.Y, Bounds.Min.Z, Bounds.Max.Z);
	}
	FFileHelper::SaveStringToFile(Out, *(Folder / TEXT("geometry.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UFightRecorderSubsystem::WriteSummary(const FString& Outcome) const
{
	const float Length = FightTime();

	FString Out = FString::Printf(TEXT("fight: %s\nlength: %.1f s\noutcome: %s\n\n"), *Scenarios, Length, *Outcome);
	Out += TEXT("times are seconds from the start of the fight; saw/moved/fired: first line to the target, first step after it, first round\n\n");
	Out += FString::Printf(TEXT("%-26s%5s%7s%7s%6s%6s%8s%8s%8s%6s%7s%7s%7s%7s%7s%7s%8s  %s\n"),
		TEXT("name"), TEXT("team"), TEXT("lived"), TEXT("shots"), TEXT("pin"), TEXT("hits"), TEXT("dmg_out"),
		TEXT("hits_in"), TEXT("dmg_in"), TEXT("kills"), TEXT("shoot%"), TEXT("sight%"), TEXT("idle%"),
		TEXT("saw"), TEXT("moved"), TEXT("fired"), TEXT("path_m"), TEXT("killed_by"));

	TArray<const FTally*> Sorted;
	for (const TPair<FString, FTally>& Pair : Tallies)
	{
		Sorted.Add(&Pair.Value);
	}
	Sorted.Sort([](const FTally& A, const FTally& B)
	{
		return A.Team != B.Team ? A.Team < B.Team : A.Name < B.Name;
	});

	for (const FTally* const Tally : Sorted)
	{
		const float End = Tally->DeathTime >= 0.0f ? Tally->DeathTime : Length;
		const float Lived = FMath::Max(0.0f, End - FMath::Max(0.0f, Tally->FirstSeenTime));
		const float Samples = static_cast<float>(FMath::Max(1, Tally->Samples));

		Out += FString::Printf(TEXT("%-26s%5d%7.1f%7d%6d%6d%8.0f%8d%8.0f%6d%7.0f%7.0f%7.0f%7.1f%7.1f%7.1f%8.1f  %s\n"),
			*Tally->Name, static_cast<int32>(Tally->Team), Lived, Tally->Shots, Tally->PinShots, Tally->HitsLanded,
			Tally->DamageDealt, Tally->HitsTaken, Tally->DamageTaken, Tally->Kills,
			100.0f * Tally->ShootingSamples / Samples, 100.0f * Tally->SightSamples / Samples,
			100.0f * Tally->IdleSamples / Samples, Tally->FirstSightTime, Tally->FirstMoveTime, Tally->FirstShotTime,
			Tally->PathLength / 100.0f, Tally->DeathTime >= 0.0f ? *Tally->KilledBy : TEXT("-"));
	}

	FFileHelper::SaveStringToFile(Out, *(Folder / TEXT("summary.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
