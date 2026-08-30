// RunDirectorSubsystem.cpp

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"

#include "Variant_Shooter/Map/PoiActor.h"
#include "Variant_Shooter/Map/FactionHq.h"
#include "Variant_Shooter/Map/ExtractionRoute.h"
#include "Variant_Shooter/Run/RunLaunchPoint.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

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

			Line(Colour, FString::Printf(TEXT("  %-16s %-8s %-7s %3.0f%%%s%s%s"),
				*State.PoiTag.ToString(),
				RoleName(State.Role),
				TeamName(State.ControllingTeam),
				State.CaptureProgress * 100.0f,
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

	// Drawn before the phase check: "nothing is happening" is exactly when somebody wants to look.
	if (CVarMapHud.GetValueOnGameThread() != 0)
	{
		DrawOverlay(*this);
	}

	if (Phase == ERunPhase::NotStarted || Phase == ERunPhase::Ended)
	{
		return;
	}

	RunSeconds += DeltaTime;

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

bool URunDirectorSubsystem::TryClaimLootSpawn(FName PoiTag, int32 MoneyStacks)
{
	FPoiWarState* State = FindState(PoiTag);
	if (!State || State->bLootSpawned)
	{
		return false;
	}

	State->bLootSpawned = true;
	State->MoneyStacksPlaced = MoneyStacks;
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

void URunDirectorSubsystem::NotifySabotage(uint8 FactionTeamId, ESabotageKind Kind)
{
	switch (Kind)
	{
	case ESabotageKind::Reinforcements: NoReinforcements.Add(FactionTeamId); break;
	case ESabotageKind::Vehicles:       NoVehicles.Add(FactionTeamId); break;
	case ESabotageKind::Power:          NoPower.Add(FactionTeamId); break;
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Faction %s lost a function to sabotage (kind %d)."),
		TeamName(FactionTeamId), static_cast<int32>(Kind));
}

bool URunDirectorSubsystem::CanFactionReinforce(uint8 FactionTeamId) const
{
	return !NoReinforcements.Contains(FactionTeamId);
}

bool URunDirectorSubsystem::CanFactionFieldVehicles(uint8 FactionTeamId) const
{
	return !NoVehicles.Contains(FactionTeamId);
}

bool URunDirectorSubsystem::FactionHasPower(uint8 FactionTeamId) const
{
	return !NoPower.Contains(FactionTeamId);
}

bool URunDirectorSubsystem::GetSortieTarget(uint8 FactionTeamId, const FVector& From,
	FVector& OutTarget, FName& OutPoiTag) const
{
	const FPoiWarState* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();

	for (const FPoiWarState& State : PoiStates)
	{
		if (State.Role == EPoiRole::Headquarters || State.ControllingTeam == FactionTeamId)
		{
			continue;
		}

		// Distance decides, but a point where somebody is already fighting is worth a detour: that
		// is what turns two armies into one war instead of two parallel garrison swaps.
		float Score = FVector::Dist(From, State.Location);
		if (State.bContested)
		{
			Score *= 0.5f;
		}

		if (Score < BestScore)
		{
			BestScore = Score;
			Best = &State;
		}
	}

	if (!Best)
	{
		return false;
	}

	OutTarget = Best->Location;
	OutPoiTag = Best->PoiTag;
	return true;
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

int32 URunDirectorSubsystem::GetMoneyStacksPlaced() const
{
	int32 Total = 0;
	for (const FPoiWarState& State : PoiStates)
	{
		Total += State.MoneyStacksPlaced;
	}
	return Total;
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
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG]   %s (%s) held by %s, capture %.0f%% by %s%s%s%s%s"),
			*State.PoiTag.ToString(),
			RoleName(State.Role),
			TeamName(State.ControllingTeam),
			State.CaptureProgress * 100.0f,
			TeamName(State.CapturingTeam),
			State.bContested ? TEXT(", CONTESTED") : TEXT(""),
			State.bMissionWindowOpen ? TEXT(", window OPEN") : TEXT(""),
			State.bMissionCompleted ? TEXT(", mission done") : TEXT(""),
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

	/** Break a faction function straight at the director, for testing the consequence without a
	 *  mesh, a hitbox and a Blueprint in between. */
	static void Sabotage(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		URunDirectorSubsystem* Director = Get(World);
		if (!Director || Args.Num() < 2)
		{
			Ar.Log(TEXT("usage: polarity.map.sabotage <team: 1 A, 2 B> <reinforcements|vehicles|power>"));
			return;
		}

		const uint8 Team = static_cast<uint8>(FCString::Atoi(*Args[0]));
		const FString Kind = Args[1].ToLower();

		if (Kind == TEXT("reinforcements"))
		{
			Director->NotifySabotage(Team, ESabotageKind::Reinforcements);
		}
		else if (Kind == TEXT("vehicles"))
		{
			Director->NotifySabotage(Team, ESabotageKind::Vehicles);
		}
		else if (Kind == TEXT("power"))
		{
			Director->NotifySabotage(Team, ESabotageKind::Power);
		}
		else
		{
			Ar.Logf(TEXT("unknown kind '%s'"), *Args[1]);
		}
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

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapSabotageCmd(
	TEXT("polarity.map.sabotage"),
	TEXT("Break a faction function: polarity.map.sabotage <team> <reinforcements|vehicles|power>"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Sabotage));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMapFastCmd(
	TEXT("polarity.map.fast"),
	TEXT("Bench speed: short capture, short hold, final opens in 30 seconds."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FRunDirectorConsole::Fast));
