// RunDirectorSubsystem.cpp

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"

#include "Variant_Shooter/Map/PoiActor.h"
#include "Variant_Shooter/Map/Banner.h"
#include "Variant_Shooter/Map/FactionHq.h"
#include "Variant_Shooter/Map/ExtractionRoute.h"
#include "Variant_Shooter/Run/RunLaunchPoint.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "AI/FactionContactMemory.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationSystem.h"

namespace
{
	const TCHAR* PhaseName(ERunPhase Phase)
	{
		switch (Phase)
		{
		case ERunPhase::NotStarted:   return TEXT("NotStarted");
		case ERunPhase::Open:         return TEXT("Open");
		case ERunPhase::FinalOpen:    return TEXT("FinalOpen");
		case ERunPhase::HoldingFinal: return TEXT("HoldingFinal");
		case ERunPhase::Extraction:   return TEXT("Extraction");
		case ERunPhase::Ended:        return TEXT("Ended");
		}
		return TEXT("?");
	}

	/** Build the navmesh once when a run starts. See the call site for why this is not optional. */
	TAutoConsoleVariable<int32> CVarBuildNavOnStart(
		TEXT("polarity.map.buildnav"),
		1,
		TEXT("Build navigation once when the run director arms. Off only on maps with a baked navmesh."),
		ECVF_Default);

	const TCHAR* RoleName(EPoiRole Role)
	{
		switch (Role)
		{
		case EPoiRole::Plain:        return TEXT("plain");
		case EPoiRole::Mission:      return TEXT("mission");
		case EPoiRole::Headquarters: return TEXT("hq");
		case EPoiRole::Final:        return TEXT("final");
		}
		return TEXT("?");
	}

	/** On-screen readout of the whole layer, so a run can be judged while it is being played instead
	 *  of afterwards in a log file. Off by default; `polarity.map.hud 1` turns it on. */
	TAutoConsoleVariable<int32> CVarMapHud(
		TEXT("polarity.map.hud"),
		0,
		TEXT("Draw the run director state on screen: phase, points, mission windows, money."),
		ECVF_Cheat);

	/** The same state, but IN THE WORLD and through walls.
	 *
	 *  The text HUD answers "what is the state of the war" and cannot answer "which of those places
	 *  am I looking at", which is the question anybody actually has while standing on the map. A
	 *  label floating over the point answers it in one glance and costs nothing to read.
	 *
	 *  Deliberately a cheat cvar and deliberately off: this is a wallhack, and it is for working on
	 *  the game, not for playing it. */
	/** Run the war faster than real time.
	 *
	 *  Watching whether the factions actually take ground is a question about minutes, not seconds,
	 *  and sitting through those minutes at 1x to find out is most of the cost of testing the map.
	 *
	 *  Applied through world settings rather than the engine's slomo so it survives the map screen
	 *  and does not need cheats switched on. It clamps itself to the world's own limits (20x by
	 *  default), and physics gets unhappy well before that, so treat anything past ~8 as a toy. */
	TAutoConsoleVariable<float> CVarTimeScale(
		TEXT("polarity.debug.timescale"),
		1.0f,
		TEXT("Global time dilation for debugging: 1 is normal, 4 runs the war four times faster."),
		ECVF_Cheat);

	TAutoConsoleVariable<int32> CVarMapDraw(
		TEXT("polarity.map.draw"),
		0,
		TEXT("Label every point of interest in the world: who holds it, who is on it, what it wants."),
		ECVF_Cheat);

	const TCHAR* TeamName(uint8 Team)
	{
		switch (Team)
		{
		case PolarityTeams::Players:  return TEXT("players");
		case PolarityTeams::FactionA: return TEXT("A");
		case PolarityTeams::FactionB: return TEXT("B");
		case PolarityTeams::Neutral:  return TEXT("nobody");
		}
		return TEXT("?");
	}

	/** Everything the director knows, in the corner of the screen.
	 *
	 *  Uses only the public API on purpose: this is the same view a real HUD would build, so if it
	 *  can be drawn from here it can be drawn from a widget later. One message key per line, so the
	 *  block redraws in place instead of scrolling. */
	FColor TeamColour(uint8 Team)
	{
		switch (Team)
		{
		case PolarityTeams::Players:  return FColor::Cyan;
		case PolarityTeams::FactionA: return FColor(80, 140, 255);    // blue, as on the bench discs
		case PolarityTeams::FactionB: return FColor(255, 90, 80);     // red
		default:                      return FColor(210, 210, 210);   // nobody
		}
	}

	/** A label over every place, in the world, readable through everything.
	 *
	 *  Answers the question the text HUD cannot: WHICH of these is the one in front of me. Also
	 *  prints what the holding faction believes about the place, because "nine men are standing
	 *  here doing nothing" and "nine men are standing here because they think four enemies are
	 *  still in front of them" look identical from outside and are completely different bugs. */
	void DrawWorldLabels(const URunDirectorSubsystem& Director, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		// Just over one tick of the director, so the label is redrawn before it can flicker.
		const float Life = 1.1f;

		for (const FPoiWarState& State : Director.GetAllPoiStates())
		{
			const FColor Colour = State.bContested ? FColor::Orange : TeamColour(State.ControllingTeam);
			const FVector Base = State.Location;

			// A beam, so the place is findable from across the map and not only when read.
			DrawDebugLine(World, Base, Base + FVector(0.0f, 0.0f, 2500.0f), Colour, false, Life,
				SDPG_Foreground, 12.0f);

			const int32 DemandA = Director.GetDemandAt(PolarityTeams::FactionA, State.PoiTag);
			const int32 DemandB = Director.GetDemandAt(PolarityTeams::FactionB, State.PoiTag);

			FString Text = FString::Printf(TEXT("%s  [%s]"), *State.PoiTag.ToString(),
				TeamName(State.ControllingTeam));
			Text += FString::Printf(TEXT("\nA %d here, wants %d (believes %d)"),
				State.PresentA, DemandA, State.KnownEnemyForA);
			Text += FString::Printf(TEXT("\nB %d here, wants %d (believes %d)"),
				State.PresentB, DemandB, State.KnownEnemyForB);
			if (State.CaptureProgress > 0.0f)
			{
				Text += FString::Printf(TEXT("\ncapture %.0f%% by %s"),
					State.CaptureProgress * 100.0f, TeamName(State.CapturingTeam));
			}
			if (State.bContested)
			{
				Text += TEXT("\nCONTESTED");
			}

			DrawDebugString(World, Base + FVector(0.0f, 0.0f, 2700.0f), Text, nullptr, Colour, Life, true);
		}
	}

	void DrawOverlay(const URunDirectorSubsystem& Director)
	{
		if (!GEngine)
		{
			return;
		}

		const float Life = 1.1f;
		int32 Key = 71000;

		auto Line = [&Key, Life](const FColor& Colour, const FString& Text)
		{
			GEngine->AddOnScreenDebugMessage(Key++, Life, Colour, Text);
		};

		Line(FColor::White, FString::Printf(TEXT("RUN  %s   t=%.0fs"),
			PhaseName(Director.GetPhase()), Director.GetRunSeconds()));

		const FFinalConditions Earned = Director.GetEarnedFinalConditions();
		Line(FColor::Silver, FString::Printf(TEXT("final: waves %+d, arrival %+.0fs, entry %d   money %d"),
			Earned.WaveDelta, Earned.ArrivalDelaySeconds, Earned.EntryQuality,
			Director.GetMoneyStacksPlaced()));

		if (Director.GetPhase() == ERunPhase::HoldingFinal)
		{
			Line(FColor::Orange, FString::Printf(TEXT("HOLDING  %.0f%%"), Director.GetHoldProgress() * 100.0f));
		}

		if (const AExtractionRoute* Route = Director.GetAnnouncedRoute())
		{
			Line(FColor::Green, FString::Printf(TEXT("RUN FOR IT: %s"), *Route->RouteTag.ToString()));

			if (const AExtractionPoint* Exit = Route->Exit)
			{
				const float Board = Exit->GetBoardProgress();
				if (Board > 0.0f)
				{
					Line(FColor::Green, FString::Printf(TEXT("BOARDING  %.0f%%"), Board * 100.0f));
				}
			}
		}

		for (const FPoiWarState& State : Director.GetAllPoiStates())
		{
			FColor Colour = FColor::Silver;
			if (State.bMissionWindowOpen)
			{
				Colour = FColor::Yellow;
			}
			else if (State.bContested)
			{
				Colour = FColor::Orange;
			}
			else if (State.ControllingTeam == PolarityTeams::Players)
			{
				Colour = FColor::Cyan;
			}

			Line(Colour, FString::Printf(TEXT("  %-16s %-8s %-7s %3.0f%%%s%s%s%s"),
				*State.PoiTag.ToString(),
				RoleName(State.Role),
				TeamName(State.ControllingTeam),
				State.CaptureProgress * 100.0f,
				State.bBannerBroken ? TEXT("  banner down") : TEXT("  BANNER"),
				State.bContested ? TEXT("  FIGHT") : TEXT(""),
				State.bMissionWindowOpen ? TEXT("  MISSION OPEN")
					: (State.bMissionCompleted ? TEXT("  mission done")
						: (State.bMissionExpired ? TEXT("  mission gone") : TEXT(""))),
				State.bLoaded ? TEXT("") : TEXT("  (unloaded)")));
		}
	}
}

// ==================== Lifecycle ====================

URunDirectorSubsystem* URunDirectorSubsystem::GetRunDirector(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
}

void URunDirectorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.IsNetMode(NM_Client))
	{
		return;
	}

	// The launch point is what already tags a level as a run map, so it answers "is this a run" and
	// "where does the team start" in one go. No second actor for the player spawn: two markers that
	// can disagree about where the run begins is a bug waiting for a designer to move one of them.
	for (TActorIterator<ARunLaunchPoint> It(&InWorld); It; ++It)
	{
		PlayerInsertion = It->GetActorLocation();
		SetPhase(ERunPhase::Open);
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Run director armed. Insertion at %s"), *PlayerInsertion.ToCompactString());

		// One navigation build, at the top of the run.
		//
		// A run map comes up with a navmesh that covers part of itself and a navigation system that
		// reports it has finished: measured on this bench at three landmarks out of eleven, sixty
		// seconds in, with every squad honestly logging "MoveTo FAILED (no path?)". One Build() and
		// it is eleven out of eleven within a dozen seconds and the war starts.
		//
		// It belongs here because this is the layer that says a run has begun, and a run whose
		// armies cannot walk is not a run. On a hand-authored map with a baked navmesh this costs a
		// moment at level start and changes nothing; turn it off with polarity.map.buildnav 0 once
		// such a map exists.
		if (CVarBuildNavOnStart.GetValueOnGameThread() != 0)
		{
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
			{
				Nav->Build();
				UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Navigation build kicked at run start."));
			}
		}
		break;
	}
}

void URunDirectorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_Client))
	{
		return;
	}

	// Only written when it actually moves, and compared against what the world really holds rather
	// than against a remembered value: a remembered one goes stale the moment PIE restarts, and we
	// would then never re-apply. Pre-clamped to the world's own limits so an out-of-range request
	// does not make this rewrite the same clamped number every frame.
	if (AWorldSettings* const Settings = const_cast<UWorld*>(World)->GetWorldSettings())
	{
		const float Wanted = FMath::Clamp(CVarTimeScale.GetValueOnGameThread(),
			Settings->MinGlobalTimeDilation, Settings->MaxGlobalTimeDilation);
		if (!FMath::IsNearlyEqual(Settings->TimeDilation, Wanted, 0.001f))
		{
			Settings->SetTimeDilation(Wanted);
		}
	}

	// Drawn before the phase check: "nothing is happening" is exactly when somebody wants to look.
	if (CVarMapHud.GetValueOnGameThread() != 0)
	{
		DrawOverlay(*this);
	}

	if (CVarMapDraw.GetValueOnGameThread() != 0)
	{
		DrawWorldLabels(*this, GetWorld());
	}

	if (Phase == ERunPhase::NotStarted || Phase == ERunPhase::Ended)
	{
		return;
	}

	RunSeconds += DeltaTime;

	// Squads on the road stop counting as strength once their clock runs out: either they arrived
	// and the live count sees them, or they died on the way, and a faction that keeps counting the
	// dead reinforces a place nobody ever reached.
	PruneCommitments();

	// The final opens on the clock, softly: the number of missions done changes what the final is
	// like, never whether it happens. A hard gate would throw away every run that went wrong, and
	// that is half of a roguelite.
	if (Phase == ERunPhase::Open && RunSeconds >= FinalOpensAfterSeconds)
	{
		SetPhase(ERunPhase::FinalOpen);
	}

	TickAccumulator += DeltaTime;
	if (TickAccumulator < 1.0f)
	{
		return;
	}
	TickAccumulator = 0.0f;

	// What the soldiers saw becomes what the staff knows. Slow on purpose: this is the same clock
	// the rest of the war thinks on.
	UpdateIntelFromContacts();

	// Nothing else needs a heartbeat yet: points report themselves, headquarters run their own
	// timers. This is the seam where the faction director will simulate points nobody has loaded.
}

// ==================== Registration ====================

void URunDirectorSubsystem::RegisterPoi(APoiActor* Poi)
{
	if (!Poi || Poi->PoiTag.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] POI %s has no tag and was not registered."),
			Poi ? *Poi->GetName() : TEXT("null"));
		return;
	}

	LoadedPois.AddUnique(Poi);

	FPoiWarState& State = FindOrAddState(Poi);
	State.bLoaded = true;
}

void URunDirectorSubsystem::UnregisterPoi(APoiActor* Poi)
{
	if (!Poi)
	{
		return;
	}

	LoadedPois.Remove(Poi);

	// The state stays. A sublevel unloading means nobody is standing there to be counted, not that
	// the point stopped belonging to anybody.
	if (FPoiWarState* State = FindState(Poi->PoiTag))
	{
		State->bLoaded = false;
		State->bContested = false;
	}
}

void URunDirectorSubsystem::RegisterHq(AFactionHq* Hq)
{
	if (Hq)
	{
		Headquarters.AddUnique(Hq);
	}
}

void URunDirectorSubsystem::UnregisterHq(AFactionHq* Hq)
{
	Headquarters.Remove(Hq);
}

void URunDirectorSubsystem::RegisterExtractionRoute(AExtractionRoute* Route)
{
	if (Route)
	{
		Routes.AddUnique(Route);
	}
}

void URunDirectorSubsystem::UnregisterExtractionRoute(AExtractionRoute* Route)
{
	Routes.Remove(Route);
}

// ==================== State lookup ====================

FPoiWarState* URunDirectorSubsystem::FindState(FName PoiTag)
{
	return PoiStates.FindByPredicate([PoiTag](const FPoiWarState& S) { return S.PoiTag == PoiTag; });
}

const FPoiWarState* URunDirectorSubsystem::FindState(FName PoiTag) const
{
	return PoiStates.FindByPredicate([PoiTag](const FPoiWarState& S) { return S.PoiTag == PoiTag; });
}

FPoiWarState& URunDirectorSubsystem::FindOrAddState(const APoiActor* Poi)
{
	check(Poi);

	if (FPoiWarState* Existing = FindState(Poi->PoiTag))
	{
		Existing->Location = Poi->GetActorLocation();
		return *Existing;
	}

	FPoiWarState New;
	New.PoiTag = Poi->PoiTag;
	New.Role = Poi->PoiRole;
	New.Location = Poi->GetActorLocation();
	New.ControllingTeam = Poi->StartingTeam;

	const int32 Index = PoiStates.Add(New);
	return PoiStates[Index];
}

bool URunDirectorSubsystem::GetPoiState(FName PoiTag, FPoiWarState& OutState) const
{
	if (const FPoiWarState* State = FindState(PoiTag))
	{
		OutState = *State;
		return true;
	}
	return false;
}

uint8 URunDirectorSubsystem::GetPoiController(FName PoiTag) const
{
	const FPoiWarState* State = FindState(PoiTag);
	return State ? State->ControllingTeam : PolarityTeams::Neutral;
}

bool URunDirectorSubsystem::TryClaimOnce(FName Key)
{
	if (Key.IsNone() || ClaimedOnce.Contains(Key))
	{
		return false;
	}

	ClaimedOnce.Add(Key);
	return true;
}

bool URunDirectorSubsystem::TryClaimGarrisonSpawn(FName PoiTag)
{
	FPoiWarState* State = FindState(PoiTag);
	if (!State || State->bGarrisonSpawned)
	{
		return false;
	}

	State->bGarrisonSpawned = true;
	return true;
}

// ==================== Points ====================

void URunDirectorSubsystem::ReportPoiPresence(const APoiActor* Poi, int32 PlayersPresent,
	int32 FactionAPresent, int32 FactionBPresent, float DeltaSeconds)
{
	if (!Poi || Phase == ERunPhase::NotStarted || Phase == ERunPhase::Ended)
	{
		return;
	}

	FPoiWarState& State = FindOrAddState(Poi);
	State.bLoaded = true;

	// Which sides are here at all. Players count as a side: a team standing on a point is taking it,
	// which is the only way the final is ever taken.
	uint8 OnlySide = PolarityTeams::Neutral;
	int32 SidesPresent = 0;
	if (PlayersPresent > 0)  { ++SidesPresent; OnlySide = PolarityTeams::Players; }
	if (FactionAPresent > 0) { ++SidesPresent; OnlySide = PolarityTeams::FactionA; }
	if (FactionBPresent > 0) { ++SidesPresent; OnlySide = PolarityTeams::FactionB; }

	const bool bWasContested = State.bContested;
	State.bContested = SidesPresent >= 2;

	// Presence is ground truth and lives here. What each side BELIEVES about the other is no longer
	// written in this function: standing on a point is only one of the two ways to learn something,
	// and it was the only one being counted. UpdateIntelFromContacts now folds both together, so
	// that a side which was told about an enemy by a scout is not overwritten a moment later by
	// this function insisting it can only know what it is standing on.
	State.PresentA = FactionAPresent;
	State.PresentB = FactionBPresent;

	// Standing here is its own fact, on its own clock. Only boots on the ground write it.
	const float NowHere = Poi->GetWorld() ? Poi->GetWorld()->GetTimeSeconds() : 0.0f;
	if (FactionAPresent > 0)
	{
		State.VisitedByATime = NowHere;
	}
	if (FactionBPresent > 0)
	{
		State.VisitedByBTime = NowHere;
	}

	// A headquarters is broken, not taken. It is a point in every other respect - it has a garrison,
	// loot on the floor and a fight in it - but no amount of standing in one hands it over. Four
	// players who could own a base would become a fourth faction, and both armies would spend the
	// rest of the run walking at them.
	const bool bCapturable = State.Role != EPoiRole::Headquarters;

	if (bCapturable && SidesPresent == 1 && OnlySide != State.ControllingTeam)
	{
		// One side, unopposed, taking the point.
		if (State.CapturingTeam != OnlySide)
		{
			State.CapturingTeam = OnlySide;
			State.CaptureProgress = 0.0f;
		}

		State.CaptureProgress += DeltaSeconds / FMath::Max(CaptureSeconds, 1.0f);

		if (State.CaptureProgress >= 1.0f)
		{
			SetPoiController(State, OnlySide);
		}
	}
	else if (SidesPresent == 0)
	{
		// Nobody is pushing. Progress bleeds back rather than being thrown away, so a squad that
		// died halfway through does not hand the next one a fresh start.
		State.CaptureProgress = FMath::Max(0.0f, State.CaptureProgress - DeltaSeconds / FMath::Max(CaptureSeconds, 1.0f));
		if (State.CaptureProgress <= 0.0f)
		{
			State.CapturingTeam = PolarityTeams::Neutral;
		}
	}
	// Contested: progress is frozen. That freeze IS the window, and helping the losing side is how a
	// player holds it open.

	if (bWasContested != State.bContested)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MAP_DEBUG] POI %s contested = %d"), *State.PoiTag.ToString(), State.bContested ? 1 : 0);
	}

	UpdateMissionWindow(State);

	if (State.Role == EPoiRole::Final)
	{
		TickFinal(State, PlayersPresent, DeltaSeconds);
	}
}

bool URunDirectorSubsystem::FindScoutPost(uint8 FactionTeamId, const FVector& From,
	FVector& OutLocation, FName& OutWatchTag) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Home. Without it there is no "far side" to speak of, so the post degenerates to standing on
	// the point itself, which is the one place an observer must not be.
	FVector Home = FVector::ZeroVector;
	bool bHaveHome = false;
	for (const TWeakObjectPtr<AFactionHq>& WeakHq : Headquarters)
	{
		if (const AFactionHq* const Hq = WeakHq.Get())
		{
			if (Hq->FactionTeamId == FactionTeamId)
			{
				Home = Hq->GetActorLocation();
				bHaveHome = true;
				break;
			}
		}
	}

	const bool bIsA = FactionTeamId == PolarityTeams::FactionA;
	const float Now = World->GetTimeSeconds();

	const FPoiWarState* Best = nullptr;
	float BestScore = -1.0f;

	for (const FPoiWarState& State : PoiStates)
	{
		// A headquarters is not worth watching: it is never taken, only broken, and what is
		// standing in it does not change what the war does next.
		if (State.Role == EPoiRole::Headquarters || State.ControllingTeam == FactionTeamId)
		{
			continue;
		}

		// Staleness first: the whole job is to look at what nobody has looked at. Distance second
		// and only as a tie-break, because sending the one pair of eyes you have to the nearest
		// place would put it where the garrisons already are.
		const float SeenAt = bIsA ? State.KnownEnemyForATime : State.KnownEnemyForBTime;
		const float Stale = FMath::Min(Now - SeenAt, 600.0f);
		const float Reach = FVector::Dist(From, State.Location);
		const float Score = Stale - Reach / 10000.0f;

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &State;
		}
	}

	if (!Best)
	{
		return false;
	}

	// The far side, measured from home. Standing behind a place is how you see what is coming out
	// of it, and it is also the approach nobody watches: their own side arrives from the other way.
	FVector Outward = bHaveHome ? (Best->Location - Home) : (Best->Location - From);
	Outward.Z = 0.0f;
	if (!Outward.Normalize())
	{
		Outward = FVector::ForwardVector;
	}

	OutLocation = Best->Location + Outward * ScoutPostRadius;
	OutWatchTag = Best->PoiTag;
	return true;
}

float URunDirectorSubsystem::BloodAt(uint8 FactionTeamId, const FPoiWarState& State, float Now) const
{
	const bool bIsA = FactionTeamId == PolarityTeams::FactionA;
	const float Value = bIsA ? State.BloodForA : State.BloodForB;
	const float At = bIsA ? State.BloodForATime : State.BloodForBTime;
	if (Value <= 0.0f || PlanBloodHalfLife <= 0.0f)
	{
		return 0.0f;
	}

	// Экспоненциальное выцветание, а не порог: место должно дешеветь постепенно, иначе фракция
	// стоит в стороне ровно N секунд и потом кидается туда всей массой, как по будильнику.
	return Value * FMath::Pow(0.5f, (Now - At) / PlanBloodHalfLife);
}

void URunDirectorSubsystem::NotifyLosses(uint8 FactionTeamId, FName PoiTag, int32 Count)
{
	if (Count <= 0 || PoiTag.IsNone())
	{
		return;
	}

	FPoiWarState* const State = FindState(PoiTag);
	if (!State)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const bool bIsA = FactionTeamId == PolarityTeams::FactionA;

	// Сначала выцветаем к текущему моменту, потом добавляем свежее. Иначе старая кровь оживала бы
	// каждым новым трупом на полную величину.
	const float Aged = BloodAt(FactionTeamId, *State, Now);
	if (bIsA)
	{
		State->BloodForA = Aged + static_cast<float>(Count);
		State->BloodForATime = Now;
	}
	else
	{
		State->BloodForB = Aged + static_cast<float>(Count);
		State->BloodForBTime = Now;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[WAR_DEBUG] faction %d lost %d at %s, blood now %.1f"),
		FactionTeamId, Count, *PoiTag.ToString(), Aged + static_cast<float>(Count));
}

void URunDirectorSubsystem::UpdateIntelFromContacts()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFactionContactMemory* const Memory = UFactionContactMemory::Get(this);
	const USquadSpawnSubsystem* const Squads = World->GetSubsystem<USquadSpawnSubsystem>();
	const float Now = World->GetTimeSeconds();

	// A contact nobody can pin to a point is still worth something if it is standing next to one.
	// Deliberately nearest-and-inside-the-radius rather than nearest outright: a man in the middle
	// of the map belongs to no point, and pretending otherwise would put phantom armies on whichever
	// point happened to be least far away.
	auto NearestTag = [this](const FVector& Where) -> FName
	{
		FName Best = NAME_None;
		float BestSq = IntelContactRadius * IntelContactRadius;
		for (const FPoiWarState& State : PoiStates)
		{
			const float DistSq = FVector::DistSquared(Where, State.Location);
			if (DistSq <= BestSq)
			{
				BestSq = DistSq;
				Best = State.PoiTag;
			}
		}
		return Best;
	};

	const uint8 Sides[2] = { PolarityTeams::FactionA, PolarityTeams::FactionB };
	for (const uint8 Side : Sides)
	{
		// Where this side thinks the enemy is, counted by point.
		TMap<FName, int32> Counted;
		if (Memory)
		{
			TArray<FFactionContact> Contacts;
			Memory->GetContacts(Side, Contacts);
			for (const FFactionContact& Contact : Contacts)
			{
				AActor* const Enemy = Contact.Enemy.Get();
				if (!Enemy)
				{
					continue;
				}

				FName Tag = NAME_None;

				// The cheat, and it is a deliberate one: instead of guessing a destination from a
				// heading, ask the enemy squad where it was sent. A guess that is wrong sends a
				// garrison to the wrong point, and that does not read as fog of war, it reads as
				// the AI being stupid.
				//
				// Players are left out of it by the author's call: a player has no objective tag
				// and no predictable destination, and a faction that tried to predict one would
				// spend the whole run reinforcing wherever he happened to be pointing.
				if (Squads && !CoopPlayers::IsPlayer(Enemy))
				{
					if (const APawn* const Pawn = Cast<APawn>(Enemy))
					{
						Tag = Squads->GetObjectiveTagOfPawn(Pawn);
					}
				}

				if (Tag.IsNone())
				{
					Tag = NearestTag(Contact.LastKnownLocation);
				}

				if (!Tag.IsNone())
				{
					++Counted.FindOrAdd(Tag);
				}
			}
		}

		const bool bIsA = Side == PolarityTeams::FactionA;
		for (FPoiWarState& State : PoiStates)
		{
			const bool bStandingHere = bIsA ? State.PresentA > 0 : State.PresentB > 0;
			const int32 Counting = bStandingHere ? (bIsA ? State.PresentB : State.PresentA) : 0;
			const int32 Reported = Counted.FindRef(State.PoiTag);

			// Both channels, and the larger wins. Counting the bodies in front of you cannot see
			// the column that has not arrived; a scout's report cannot see the man behind the wall.
			const int32 Believed = FMath::Max(Counting, Reported);

			// Silence is not the same as "nobody there", and writing a fresh zero would claim it
			// was. A side that has learned nothing this second keeps its old number and lets it
			// age out on PlanIntelSeconds, which is exactly what not knowing is supposed to feel
			// like. The one exception is standing on the point: there, zero is a fact.
			if (Believed <= 0 && !bStandingHere)
			{
				continue;
			}

			if (bIsA)
			{
				State.KnownEnemyForA = Believed;
				State.KnownEnemyForATime = Now;
			}
			else
			{
				State.KnownEnemyForB = Believed;
				State.KnownEnemyForBTime = Now;
			}
		}
	}
}

void URunDirectorSubsystem::SetPoiController(FPoiWarState& State, uint8 NewTeam)
{
	const uint8 OldTeam = State.ControllingTeam;

	State.ControllingTeam = NewTeam;
	State.CaptureProgress = 0.0f;
	State.CapturingTeam = PolarityTeams::Neutral;

	// This is the line the whole mission window hangs off: a faction finishing a capture is what
	// shuts the window, not a timer. "The fight is still on" is something a player can see, and a
	// countdown is not.
	if (State.Role == EPoiRole::Mission && !State.bMissionCompleted && !State.bMissionExpired
		&& NewTeam != PolarityTeams::Players)
	{
		State.bMissionExpired = true;
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Mission window on %s expired: %s finished the capture."),
			*State.PoiTag.ToString(), TeamName(NewTeam));
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] POI %s changed hands: %s -> %s"),
		*State.PoiTag.ToString(), TeamName(OldTeam), TeamName(NewTeam));

	OnPoiControlChanged.Broadcast(State.PoiTag, NewTeam);
	UpdateMissionWindow(State);
}

void URunDirectorSubsystem::UpdateMissionWindow(FPoiWarState& State)
{
	if (State.Role != EPoiRole::Mission)
	{
		return;
	}

	const bool bShouldBeOpen = State.bContested && !State.bMissionCompleted && !State.bMissionExpired;
	if (bShouldBeOpen == State.bMissionWindowOpen)
	{
		return;
	}

	State.bMissionWindowOpen = bShouldBeOpen;
	OnMissionWindowChanged.Broadcast(State.PoiTag, bShouldBeOpen);

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Mission window on %s is now %s"),
		*State.PoiTag.ToString(), bShouldBeOpen ? TEXT("OPEN") : TEXT("shut"));
}

// ==================== Missions ====================

bool URunDirectorSubsystem::CompleteMission(FName PoiTag)
{
	FPoiWarState* State = FindState(PoiTag);
	if (!State || State->Role != EPoiRole::Mission || State->bMissionCompleted || State->bMissionExpired)
	{
		return false;
	}

	State->bMissionCompleted = true;
	State->bMissionWindowOpen = false;

	// The reward lives on the actor, which may be streamed out by now; the state does not carry it,
	// so read it from whichever point actor is loaded and fall back to nothing.
	for (const TWeakObjectPtr<APoiActor>& WeakPoi : LoadedPois)
	{
		if (const APoiActor* Poi = WeakPoi.Get())
		{
			if (Poi->PoiTag == PoiTag)
			{
				EarnedConditions.Add(Poi->MissionReward);
				break;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Mission %s complete. Final now: waves %+d, arrival %+.0fs, entry %d"),
		*PoiTag.ToString(), EarnedConditions.WaveDelta, EarnedConditions.ArrivalDelaySeconds, EarnedConditions.EntryQuality);

	OnMissionWindowChanged.Broadcast(PoiTag, false);
	OnMissionCompleted.Broadcast(PoiTag);
	return true;
}

// ==================== Headquarters ====================

void URunDirectorSubsystem::NotifyBannerBroken(FName PoiTag, AActor* Breaker)
{
	FPoiWarState* State = FindState(PoiTag);
	if (!State || State->bBannerBroken)
	{
		return;
	}

	State->bBannerBroken = true;

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Banner down on %s (%s), broken by %s"),
		*PoiTag.ToString(), RoleName(State->Role),
		Breaker ? *Breaker->GetName() : TEXT("nobody"));
}

bool URunDirectorSubsystem::IsBannerBroken(FName PoiTag) const
{
	const FPoiWarState* State = FindState(PoiTag);
	return State && State->bBannerBroken;
}

int32 URunDirectorSubsystem::GetBannersBrokenCount() const
{
	int32 Count = 0;
	for (const FPoiWarState& State : PoiStates)
	{
		Count += State.bBannerBroken ? 1 : 0;
	}
	return Count;
}

void URunDirectorSubsystem::NotifySortieSent(uint8 FactionTeamId, FName PoiTag, int32 Members)
{
	if (Members <= 0 || PoiTag.IsNone())
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Commitments.Add({FactionTeamId, PoiTag, Members, Now});

	if (FactionTeamId < UE_ARRAY_COUNT(LastCommitTime))
	{
		LastCommitTime[FactionTeamId] = Now;
	}
}

int32 URunDirectorSubsystem::GetCommittedForce(uint8 FactionTeamId, FName PoiTag) const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	int32 Total = 0;
	for (const FFactionCommitment& C : Commitments)
	{
		if (C.TeamId == FactionTeamId && C.PoiTag == PoiTag
			&& Now - C.SentAt <= PlanCommitmentSeconds)
		{
			Total += C.Members;
		}
	}
	return Total;
}

void URunDirectorSubsystem::PruneCommitments()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Commitments.RemoveAll([this, Now](const FFactionCommitment& C)
	{
		return Now - C.SentAt > PlanCommitmentSeconds;
	});
}

int32 URunDirectorSubsystem::GetDemandAt(uint8 FactionTeamId, FName PoiTag) const
{
	const FPoiWarState* const State = FindState(PoiTag);
	if (!State)
	{
		return PlanTokenGarrison;
	}

	// What we believe is facing us here. Same fog rule as the plan: strength is known only while
	// somebody of ours is standing here to count it, and the memory goes stale.
	const bool bIsA = FactionTeamId == PolarityTeams::FactionA;
	const float SeenAt = bIsA ? State->KnownEnemyForATime : State->KnownEnemyForBTime;
	const int32 SeenCount = bIsA ? State->KnownEnemyForA : State->KnownEnemyForB;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	const bool bFresh = (Now - SeenAt) <= PlanIntelSeconds;
	const int32 Threat = bFresh ? SeenCount : 0;

	// Nobody there and nobody expected: a watch, not a garrison. This is what makes a quiet point
	// give its surplus up instead of hoarding twenty men who have nothing to shoot at.
	if (Threat <= 0)
	{
		return PlanTokenGarrison;
	}

	const int32 Wanted = FMath::CeilToInt(Threat * PlanSuperiorityWanted);
	return FMath::Max(Wanted, PlanTokenGarrison);
}

float URunDirectorSubsystem::CurrentWorthThreshold(uint8 FactionTeamId) const
{
	if (FactionTeamId >= UE_ARRAY_COUNT(LastCommitTime))
	{
		return PlanWorthThreshold;
	}

	// Impatience. A side that has done nothing for a while lowers its own bar until something
	// clears it. Without this both factions can decide that nothing anywhere is worth it and the
	// map goes quiet, which is exactly the failure the threshold was added to prevent, inverted.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Idle = Now - LastCommitTime[FactionTeamId];
	const float Patience = FMath::Clamp(1.0f - Idle / FMath::Max(PlanImpatienceSeconds, 1.0f), 0.0f, 1.0f);
	return PlanWorthThreshold * Patience;
}

void URunDirectorSubsystem::BuildFactionPlan(uint8 FactionTeamId, const FVector& From,
	int32 ForceAvailable, TArray<FFactionOrder>& OutPlan) const
{
	OutPlan.Reset();

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Threshold = CurrentWorthThreshold(FactionTeamId);

	// The scan for the enemy headquarters lived here, to place points on "their side of the map"
	// for the deep-strike term. That term is gone (see the cost block below), and with it the only
	// reason this plan ever needed to know where anybody lives.

	for (const FPoiWarState& State : PoiStates)
	{
		// A headquarters is broken, not taken (see ReportPoiPresence), so marching at one is asking
		// for a siege that cannot end. It stays out of the plan on both sides.
		if (State.Role == EPoiRole::Headquarters)
		{
			continue;
		}

		const bool bOurs = State.ControllingTeam == FactionTeamId;
		const bool bLosingIt = bOurs && State.CaptureProgress > 0.0f
			&& State.CapturingTeam != FactionTeamId && State.CapturingTeam != PolarityTeams::Neutral;

		// Ours and safe: nothing to want. Ours and being taken: worth sending help.
		if (bOurs && !bLosingIt && !State.bContested)
		{
			continue;
		}

		FFactionOrder Order;
		Order.Action = bOurs ? EFactionAction::Reinforce : EFactionAction::Attack;
		Order.PoiTag = State.PoiTag;
		Order.Location = State.Location;
		Order.HeldBy = State.ControllingTeam;

		// When this faction last had somebody standing here. It is the same clock the odds are
		// judged on, hoisted above PRIZE because staleness is now worth something in its own right
		// and not only as a reason to distrust a headcount. Defaults to -100000, so a place nobody
		// has ever visited reads as maximally stale from the first second of the run.
		const bool bIsA = FactionTeamId == PolarityTeams::FactionA;
		const float SeenAt = bIsA ? State.KnownEnemyForATime : State.KnownEnemyForBTime;
		const int32 SeenCount = bIsA ? State.KnownEnemyForA : State.KnownEnemyForB;
		const float Stale = Now - SeenAt;
		const bool bFresh = Stale <= PlanIntelSeconds;

		// ---- PRIZE: what the place is worth holding ----
		float Prize = 1.0f;
		FString Why;
		if (State.Role == EPoiRole::Mission || State.Role == EPoiRole::Final)
		{
			Prize *= PlanObjectiveBonus;
			Why += TEXT("objective ");
		}

		// Финал должен что-то значить и для фракций. Раньше открытие фазы меняло правила только для
		// игрока: армии продолжали делить окраины, и финальной схватки не случалось вообще.
		if (State.Role == EPoiRole::Final
			&& (Phase == ERunPhase::FinalOpen || Phase == ERunPhase::HoldingFinal))
		{
			Prize *= PlanFinalBonus;
			Why += TEXT("FINAL ");
		}
		if (!bOurs && State.ControllingTeam == PolarityTeams::Neutral)
		{
			Prize *= PlanNeutralBonus;
			Why += TEXT("neutral ");
		}

		// A place this faction has not set foot on in a long time is worth more for exactly that
		// reason. Not because the loot grew: because a corner of the map nobody has walked into is
		// where the run stops being a war and becomes two garrisons trading the same three points.
		//
		// The bench 2026-09-06 is the case this exists for. The north point sat at score 0.42
		// against a bar of 0.55, every second of a whole run, and the number never moved, because
		// nothing in the plan changed when nothing happened. A cost that is fixed and a prize that
		// is fixed produce a verdict that is fixed, and "never" is a perfectly stable answer.
		//
		// Deliberately per faction and read off the intel clock rather than a new field: the clock
		// already means "when did WE last stand here", the two sides forget separately, and walking
		// in resets it for free. A point being fought over cannot go stale for either side.
		if (!bOurs && PlanStagnationSeconds > 0.0f)
		{
			// Neglect is about not having GONE, not about not having looked. A scout's report is
			// exactly the thing that must not count here.
			const float Unvisited = Now - (bIsA ? State.VisitedByATime : State.VisitedByBTime);
			const float Neglect = FMath::Clamp(Unvisited / PlanStagnationSeconds, 0.0f, 1.0f);
			if (Neglect > 0.0f)
			{
				Prize *= 1.0f + PlanStagnationBonus * Neglect;
				Why += FString::Printf(TEXT("neglect%.0f%% "), Neglect * 100.0f);
			}
		}
		if (bOurs)
		{
			// Defence is worth more the closer the place is to being lost, but only up to the point
			// where CHANCE decides it is already gone. Wanting it badly and being able to save it
			// are different questions and they are answered in different terms on purpose.
			const float Urgency = FMath::Max(State.CaptureProgress, State.bContested ? 0.25f : 0.0f);
			Prize *= 1.0f + PlanDefenceUrgency * Urgency;
			Why += FString::Printf(TEXT("defend%.0f%% "), Urgency * 100.0f);
		}

		// ---- CHANCE: the odds this faction gives itself, on what it BELIEVES ----
		const int32 MineHere = bIsA ? State.PresentA : State.PresentB;
		const int32 Committed = GetCommittedForce(FactionTeamId, State.PoiTag);

		// Stale or never looked: assume a garrison. A faction that assumes empty walks into every
		// wall on the map; one that assumes an army never leaves home.
		const float TheirForce = bFresh ? float(SeenCount) : float(PlanAssumedGarrison);

		// Men there, men on the road, and men the asker is about to put on the road. All three, or
		// an attack is judged on a force of zero and can never be worth making.
		const float MyForce = float(MineHere + Committed + FMath::Max(ForceAvailable, 0));
		const float Wanted = TheirForce * PlanSuperiorityWanted;
		const float Chance = (MyForce + Wanted) > 0.0f
			? FMath::Clamp(MyForce / (MyForce + Wanted), 0.0f, 1.0f)
			: 1.0f;

		Why += FString::Printf(TEXT("| mine %.0f(+%d sent,+%d ready) theirs %.0f%s "),
			float(MineHere), Committed, FMath::Max(ForceAvailable, 0), TheirForce,
			bFresh ? TEXT("") : TEXT("?"));

		// ---- COST: the walk, plus a flat premium for ground somebody else holds ----
		//
		// This used to carry a second, geometric term: how deep into their country the place sits,
		// as a fraction of the span between the two headquarters. It was measured from the distance
		// to the asker, and then used to divide a cost that is ALREADY that distance. Far points
		// were charged for being far twice over, and no amount of tuning the threshold could undo
		// it, because the double charge grows with the same number the first charge does.
		//
		// The bench showed the result on 2026-09-06: the north point scored prize 1.35 against a
		// cost of 3.19 and could never clear the bar, so nobody attacked it for a whole run.
		//
		// What survives is the part that is not distance: taking a place off somebody costs more
		// than walking onto an empty one, whoever holds it and wherever it is.
		const float Dist = FVector::Dist(From, State.Location);
		float Cost = 1.0f + Dist / FMath::Max(PlanDistanceHalfLife, 1.0f);
		if (!bOurs && State.ControllingTeam != PolarityTeams::Neutral)
		{
			Cost *= FMath::Max(PlanHeldGroundCost, 0.01f);
			Why += TEXT("held ");
		}

		// Место, где эта сторона только что положила людей, дорожает. Именно цена, а не приз: точка
		// не перестала быть нужной оттого, что там убивают - она перестала быть дешёвой.
		const float Blood = BloodAt(FactionTeamId, State, Now);
		if (Blood > 0.05f)
		{
			Cost *= 1.0f + PlanBloodCost * Blood;
			Why += FString::Printf(TEXT("blood%.1f "), Blood);
		}

		// Somebody is already fighting there. Finishing a fight beats starting one, and this is
		// what turns two armies into one war instead of two parallel garrison swaps.
		float Worth = Prize * Chance / FMath::Max(Cost, 0.01f);
		if (State.bContested)
		{
			Worth *= PlanContestedBonus;
			Why += TEXT("contested ");
		}

		Order.Score = Worth;
		Order.Chance = Chance;
		Order.bWorthIt = Worth >= Threshold;
		Order.Reason = Why + FString::Printf(TEXT("| prize %.2f chance %.2f cost %.2f"),
			Prize, Chance, Cost);
		OutPlan.Add(Order);
	}

	OutPlan.Sort([](const FFactionOrder& A, const FFactionOrder& B) { return A.Score > B.Score; });
}

bool URunDirectorSubsystem::GetFactionOrder(uint8 FactionTeamId, const FVector& From,
	int32 ForceAvailable, FFactionOrder& OutOrder) const
{
	TArray<FFactionOrder> Plan;
	BuildFactionPlan(FactionTeamId, From, ForceAvailable, Plan);
	if (Plan.IsEmpty())
	{
		return false;
	}

	// The candle. The best line on the map can still be a bad idea, and saying so is the whole
	// point: a faction that always acts on its favourite option feeds itself into one fight one
	// squad at a time. Refusing is a decision, and the caller waits and tries again later, by
	// which time impatience has lowered the bar or somebody has died and changed the odds.
	if (!Plan[0].bWorthIt)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WAR_DEBUG] faction %d holds: best is %s at %.3f, threshold %.3f"),
			FactionTeamId, *Plan[0].PoiTag.ToString(), Plan[0].Score,
			CurrentWorthThreshold(FactionTeamId));
		return false;
	}

	OutOrder = Plan[0];
	return true;
}

void URunDirectorSubsystem::DumpFactionPlan(uint8 FactionTeamId) const
{
	TArray<FFactionOrder> Plan;

	// From the faction's own headquarters, because that is who will act on it.
	FVector From = FVector::ZeroVector;
	for (const TWeakObjectPtr<AFactionHq>& Weak : Headquarters)
	{
		if (const AFactionHq* Hq = Weak.Get())
		{
			if (Hq->FactionTeamId == FactionTeamId)
			{
				From = Hq->GetActorLocation();
				break;
			}
		}
	}

	// A dump is a question about the map, not a plan to act on, so it asks with a typical squad's
	// worth of force rather than pretending nobody is available.
	BuildFactionPlan(FactionTeamId, From, PlanTokenGarrison, Plan);
	UE_LOG(LogTemp, Log, TEXT("[WAR_DEBUG] plan for faction %d, %d lines, threshold %.3f:"),
		FactionTeamId, Plan.Num(), CurrentWorthThreshold(FactionTeamId));
	for (const FFactionOrder& Line : Plan)
	{
		// Rejected lines are printed too. "Why did nobody move" is the question this log exists to
		// answer, and it is only answerable if the things that were turned down are visible.
		UE_LOG(LogTemp, Log, TEXT("[WAR_DEBUG]   %s %-9s %-18s worth %.3f  held by %d  (%s)"),
			Line.bWorthIt ? TEXT("GO  ") : TEXT("skip"),
			Line.Action == EFactionAction::Attack ? TEXT("ATTACK") : TEXT("REINFORCE"),
			*Line.PoiTag.ToString(), Line.Score, Line.HeldBy, *Line.Reason);
	}
}

// ==================== Final and extraction ====================

float URunDirectorSubsystem::GetHoldProgress() const
{
	if (Phase != ERunPhase::HoldingFinal)
	{
		return 0.0f;
	}
	return FMath::Clamp(HoldSeconds / FMath::Max(FinalHoldSeconds, 1.0f), 0.0f, 1.0f);
}

void URunDirectorSubsystem::TickFinal(const FPoiWarState& FinalState, int32 PlayersPresent, float DeltaSeconds)
{
	if (Phase == ERunPhase::FinalOpen)
	{
		if (FinalState.ControllingTeam == PolarityTeams::Players)
		{
			HoldSeconds = 0.0f;
			SetPhase(ERunPhase::HoldingFinal);
		}
		return;
	}

	if (Phase != ERunPhase::HoldingFinal)
	{
		return;
	}

	// Pushed off the point: the hold stalls where it is rather than resetting. Losing two minutes of
	// work to one bad thirty seconds is the kind of punishment that makes a team stop trying.
	if (PlayersPresent > 0 && FinalState.ControllingTeam == PolarityTeams::Players)
	{
		HoldSeconds += DeltaSeconds;
	}

	if (HoldSeconds >= FinalHoldSeconds)
	{
		AnnounceExtractionRoute();
		SetPhase(ERunPhase::Extraction);
	}
}

AExtractionRoute* URunDirectorSubsystem::AnnounceExtractionRoute()
{
	TArray<AExtractionRoute*> Pool;
	float TotalWeight = 0.0f;

	for (const TWeakObjectPtr<AExtractionRoute>& WeakRoute : Routes)
	{
		AExtractionRoute* Route = WeakRoute.Get();
		if (Route && Route->Exit && Route->Weight > 0.0f)
		{
			Pool.Add(Route);
			TotalWeight += Route->Weight;
		}
	}

	if (Pool.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] Hold finished but no extraction route is placed. Nobody is going anywhere."));
		return nullptr;
	}

	float Roll = FMath::FRand() * TotalWeight;
	AExtractionRoute* Picked = Pool.Last();
	for (AExtractionRoute* Route : Pool)
	{
		Roll -= Route->Weight;
		if (Roll <= 0.0f)
		{
			Picked = Route;
			break;
		}
	}

	AnnouncedRoute = Picked;

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Extraction route announced: %s (exit %s), chase in %.0fs"),
		*Picked->RouteTag.ToString(), *Picked->Exit->ExitTag.ToString(), Picked->ChaseLeadSeconds);

	OnExtractionRouteAnnounced.Broadcast(Picked);
	return Picked;
}

void URunDirectorSubsystem::EndRun(bool bExtracted)
{
	if (Phase == ERunPhase::Ended)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Run over after %.0fs. Extracted: %d"), RunSeconds, bExtracted ? 1 : 0);
	SetPhase(ERunPhase::Ended);
}

void URunDirectorSubsystem::SetPhase(ERunPhase NewPhase)
{
	if (Phase == NewPhase)
	{
		return;
	}

	Phase = NewPhase;
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Phase -> %s (t=%.0fs)"), PhaseName(NewPhase), RunSeconds);
	OnPhaseChanged.Broadcast(NewPhase);
}

// ==================== Audit ====================

void URunDirectorSubsystem::ReportMoneyStacks(int32 Stacks)
{
	MoneyStacksOnMap += FMath::Max(0, Stacks);
}

void URunDirectorSubsystem::DumpState() const
{
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] === run director === phase %s, t=%.0fs, hold %.0f%%"),
		PhaseName(Phase), RunSeconds, GetHoldProgress() * 100.0f);
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] final conditions earned: waves %+d, arrival %+.0fs, entry %d"),
		EarnedConditions.WaveDelta, EarnedConditions.ArrivalDelaySeconds, EarnedConditions.EntryQuality);
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] money on the map: %d stacks, target %d"),
		GetMoneyStacksPlaced(), TargetMoneyStacks);

	for (const FPoiWarState& State : PoiStates)
	{
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG]   %s (%s) held by %s, capture %.0f%% by %s%s%s%s%s%s"),
			*State.PoiTag.ToString(),
			RoleName(State.Role),
			TeamName(State.ControllingTeam),
			State.CaptureProgress * 100.0f,
			TeamName(State.CapturingTeam),
			State.bContested ? TEXT(", CONTESTED") : TEXT(""),
			State.bMissionWindowOpen ? TEXT(", window OPEN") : TEXT(""),
			State.bMissionCompleted ? TEXT(", mission done") : TEXT(""),
			State.bBannerBroken ? TEXT(", BANNER DOWN") : TEXT(""),
			State.bLoaded ? TEXT("") : TEXT(", streamed out"));
	}
}

// ==================== Console ====================
//
// Everything above can be driven without a single piece of content: place two points, run these.

struct FRunDirectorConsole
{
	static URunDirectorSubsystem* Get(UWorld* World)
	{
		return World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
	}

	static void Dump(const TArray<FString>&, UWorld* World, FOutputDevice&)
	{
		if (URunDirectorSubsystem* Director = Get(World))
		{
			Director->DumpState();
		}
	}

	static void Mission(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		URunDirectorSubsystem* Director = Get(World);
		if (!Director || Args.Num() < 1)
		{
			Ar.Log(TEXT("usage: polarity.map.mission <PoiTag>"));
			return;
		}

		const bool bDone = Director->CompleteMission(FName(*Args[0]));
		Ar.Logf(TEXT("CompleteMission(%s) -> %d"), *Args[0], bDone ? 1 : 0);
	}

	static void OpenFinal(const TArray<FString>&, UWorld* World, FOutputDevice&)
	{
		if (URunDirectorSubsystem* Director = Get(World))
		{
			Director->FinalOpensAfterSeconds = Director->GetRunSeconds();
		}
	}

	static void Route(const TArray<FString>&, UWorld* World, FOutputDevice& Ar)
	{
		if (URunDirectorSubsystem* Director = Get(World))
		{
			Ar.Logf(TEXT("route -> %s"), Director->AnnounceExtractionRoute() ? TEXT("announced") : TEXT("none placed"));
		}
	}

	/** Hand a point to a side without waiting for a fight, so the late phases can be reached in
	 *  seconds. Goes through the same SetPoiController a real capture uses, mission expiry and all,
	 *  otherwise the shortcut would test a code path nobody plays. */
	static void Capture(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		URunDirectorSubsystem* Director = Get(World);
		if (!Director || Args.Num() < 2)
		{
			Ar.Log(TEXT("usage: polarity.map.capture <PoiTag> <team: 0 players, 1 A, 2 B>"));
			return;
		}

		FPoiWarState* State = Director->FindState(FName(*Args[0]));
		if (!State)
		{
			Ar.Logf(TEXT("no point tagged %s"), *Args[0]);
			return;
		}

		Director->SetPoiController(*State, static_cast<uint8>(FCString::Atoi(*Args[1])));
	}

	/** Break the banner on a point without walking there and shooting it. Goes through the actor so
	 *  the consequence is the one the game runs, not a second copy of it. */
	static void Banner(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!Get(World) || Args.Num() < 1)
		{
			Ar.Log(TEXT("usage: polarity.map.banner <PoiTag>"));
			return;
		}

		const FName Tag(*Args[0]);
		for (TActorIterator<APoiActor> It(World); It; ++It)
		{
			if (It->PoiTag != Tag)
			{
				continue;
			}

			if (ABannerActor* TheBanner = It->Banner)
			{
				TheBanner->Break(nullptr);
				Ar.Logf(TEXT("broke the banner on %s"), *Args[0]);
			}
			else
			{
				Ar.Logf(TEXT("%s has no banner"), *Args[0]);
			}
			return;
		}

		Ar.Logf(TEXT("no point tagged %s"), *Args[0]);
	}

	/** Bench speed. A run is a quarter of an hour by design, and watching one to test a state
	 *  machine is a quarter of an hour spent watching. These numbers are only ever set from here, so
	 *  the shipped defaults in the class stay the honest ones. */
	static void Fast(const TArray<FString>&, UWorld* World, FOutputDevice& Ar)
	{
		URunDirectorSubsystem* Director = Get(World);
		if (!Director)
		{
			return;
		}

		Director->CaptureSeconds = 10.0f;
		Director->FinalHoldSeconds = 15.0f;
		Director->FinalOpensAfterSeconds = Director->GetRunSeconds() + 30.0f;

		Ar.Logf(TEXT("bench speed: capture 10s, final opens in 30s, hold 15s"));
	}
};

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapDumpCmd(
	TEXT("polarity.map.dump"),
	TEXT("Print the run director state: phase, points, missions, money budget."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Dump));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapMissionCmd(
	TEXT("polarity.map.mission"),
	TEXT("Complete the mission on a point: polarity.map.mission <PoiTag>"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Mission));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapFinalCmd(
	TEXT("polarity.map.final"),
	TEXT("Open the final point now instead of waiting for the clock."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::OpenFinal));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapRouteCmd(
	TEXT("polarity.map.route"),
	TEXT("Announce an extraction route now."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Route));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapCaptureCmd(
	TEXT("polarity.map.capture"),
	TEXT("Give a point to a side: polarity.map.capture <PoiTag> <team>"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Capture));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapBannerCmd(
	TEXT("polarity.map.banner"),
	TEXT("Break the banner on a point: polarity.map.banner <PoiTag>"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Banner));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapFastCmd(
	TEXT("polarity.map.fast"),
	TEXT("Bench speed: short capture, short hold, final opens in 30 seconds."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Fast));
