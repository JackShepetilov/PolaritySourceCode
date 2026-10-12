// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterNPC.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Team.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"  // For TActorIterator
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "AI/Navigation/PolarityPathFollowingComponent.h"
#include "AI/FactionContactMemory.h"
#include "Coop/CoopPlayers.h"
#include "AI/PolarityTeams.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"
#include "Variant_Shooter/Siege/SiegeLaneFollower.h"
#include "Variant_Shooter/Siege/SiegeCampGuard.h"
#include "AI/BodySamples.h"
#include "AI/SmokeVisionSubsystem.h"
#include "PolarityCharacter.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "AI/Components/CoverFinderComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/AI/SquadSpawn/Squad.h"

static TAutoConsoleVariable<int32> CVarAITargetChoice(
	TEXT("polarity.ai.targetchoice"),
	1,
	TEXT("1: a soldier picks whom to fight by score, of those its side knows of (in view, near, hurt it lately, not already taken by its mates). 0: whoever it saw last, as before."));

// The numbers of AShooterAIController's knowledge, in one place. Starting values, for the author to turn.
namespace Knowledge
{
	/** How often a soldier re-asks who is in its view. Seconds. */
	constexpr float Interval = 0.15f;

	/** Half of the cone it sees in, degrees: 60 is 120 in front of it. */
	constexpr float ViewConeHalfAngle = 60.0f;

	/** Closer than this it is noticed whatever way the soldier faces: somebody at its elbow. cm. */
	constexpr float TouchDistance = 150.0f;

	/** Seeing, or being hit, to the side knowing it: the time it takes to say. Killed sooner, not said. */
	constexpr float CalloutDelay = 1.0f;

	/** A hit this soon after a shot was heard from the one who hit is tied to that shot. A round that flies takes a while. */
	constexpr float HitLinksToShotWithin = 1.5f;

	/** How long it stays turned to a sound or a shout with nobody showing there. */
	constexpr float LookSeconds = 3.0f;

	/** How many of those with nobody in view turn to a shot, nearest first. */
	constexpr int32 TurnersPerSound = 3;

	/** How many turn when a mate calls a side he was hit from, or a mate is seen to fall. */
	constexpr int32 TurnersPerShout = 2;

	/** Hit and no shot heard: it looks this far out along the side the hit came from. cm. */
	constexpr float DirectionLookDistance = 1500.0f;

	// ---- Whom to fight ----
	constexpr float ChoiceInterval = 0.5f;
	constexpr float ScoreSeenByMe = 40.0f;
	constexpr float ScoreSeenBySide = 20.0f;
	constexpr float ScorePerMetre = 1.0f;
	constexpr float ScoreDistanceCap = 80.0f;
	constexpr float HurtWindow = 10.0f;
	constexpr float ScorePerDamage = 1.0f;
	constexpr float ScoreHurtCap = 40.0f;
	constexpr float ScorePerMateOnHim = 10.0f;
	constexpr float ScoreCurrent = 15.0f;
	constexpr float SwitchMargin = 10.0f;
	constexpr int32 LeadsToSwitch = 2;

	// ---- Looking for a lost target ----
	/** Nobody has seen him for this long: one goes to look. */
	constexpr float SearchAfterSeconds = 9.0f;
	constexpr float SearchWalkLimit = 20.0f;
	constexpr float SearchArriveDistance = 300.0f;
	constexpr float SearchRadius = 1200.0f;
	constexpr float SearchLegSeconds = 6.0f;
}

AShooterAIController::AShooterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPolarityPathFollowingComponent>(TEXT("PathFollowingComponent")))
{
	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);
}

void AShooterAIController::BeginPlay()
{
	Super::BeginPlay();

	// Debug StateTree status after BeginPlay (when StateTree should be running)
	FString StateTreeStatus = TEXT("NO COMPONENT");
	if (StateTreeAI)
	{
		const bool bIsRunning = StateTreeAI->IsRunning();
		StateTreeStatus = bIsRunning ? TEXT("RUNNING") : TEXT("NOT RUNNING");
	}

	// Debug: Check if Team Sense is configured in perception
	FString TeamSenseStatus = TEXT("NOT CONFIGURED");
	if (AIPerception)
	{
		// Check if Team sense is registered
		FAISenseID TeamSenseID = UAISense::GetSenseID<UAISense_Team>();
		if (TeamSenseID.IsValid())
		{
			// Check if this perception component is listening for team sense
			const UAISenseConfig* TeamConfig = AIPerception->GetSenseConfig(TeamSenseID);
			if (TeamConfig)
			{
				TeamSenseStatus = TEXT("CONFIGURED");
			}
			else
			{
				TeamSenseStatus = TEXT("SENSE EXISTS BUT NOT IN CONFIG - Add AISenseConfig_Team to AIPerception!");
			}
		}
		else
		{
			TeamSenseStatus = TEXT("SENSE ID INVALID");
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[%s] BeginPlay - Pawn=%s - StateTreeAI: %s - TeamSense: %s"),
		*GetName(),
		GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
		*StateTreeStatus,
		*TeamSenseStatus);
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// ensure we're possessing an NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(InPawn))
	{
		// add the team tag to the pawn
		NPC->Tags.Add(TeamTag);

		// Take the side from the pawn, so the two team ids cannot disagree.
		// Both objects have to carry one (a stimulus source answers through the actor, a perception
		// listener through its controller), and until now they were two independent numbers that
		// happened to both say 1. With factions the pawn is where a designer sets the side, so the
		// controller follows it rather than holding a second opinion.
		SetGenericTeamId(FGenericTeamId::GetTeamIdentifier(NPC));

		// subscribe to the pawn's OnDeath delegate
		NPC->OnNPCDeath.AddDynamic(this, &AShooterAIController::OnPawnDeath);

		// Ensure StateTree is running (may not auto-start after dynamic spawn)
		if (StateTreeAI && !StateTreeAI->IsRunning())
		{
			StateTreeAI->StartLogic();
		}

		// Force perception update on possess (needed for checkpoint respawn)
		ForcePerceptionUpdate();

		// The knowledge rules are the foot soldier's: the one that takes cover. A tank's hull does not
		// point where its turret looks and a drone has no cone to speak of; they keep the old rule.
		bInfantry = NPC->FindComponentByClass<UCoverFinderComponent>() || FindComponentByClass<UCoverFinderComponent>();
	}
}

void AShooterAIController::BroadcastContactAt(AActor* Enemy, const FVector& Location, bool bSeenNow)
{
	if (!Enemy)
	{
		return;
	}

	if (UFactionContactMemory* const Memory = UFactionContactMemory::Get(this))
	{
		Memory->ReportContact(GetGenericTeamId().GetId(), Enemy, Location, GetPawn(), bSeenNow);
	}
}

void AShooterAIController::BroadcastContact(AActor* Enemy)
{
	if (Enemy)
	{
		BroadcastContactAt(Enemy, Enemy->GetActorLocation(), /*bSeenNow*/ true);
	}
}

bool AShooterAIController::IsHostileTo(const AActor* Other) const
{
	if (!Other || Other == GetPawn())
	{
		return false;
	}

	if (PolarityTeams::ShouldIgnorePlayers() && CoopPlayers::IsPlayer(Other))
	{
		return false;
	}

	// Neutral, not hostile, for anything that has no side at all: the engine's default attitude
	// answers Neutral when the other actor does not implement the team interface, so props, pickups
	// and decoys stay out of this the same way they stayed out of the Player-tag test.
	return FGenericTeamId::GetAttitude(this, Other) == ETeamAttitude::Hostile;
}

void AShooterAIController::OnPawnDeath(AShooterNPC* DeadNPC)
{
	// Seen to fall by one of its own: the place goes out on the radio, and the nearest with nobody
	// in view turn to it. Not seen by anybody: the side does not know yet.
	if (DeadNPC && bInfantry && UFactionContactMemory::IsKnowledgeOn())
	{
		EndSearchForTarget();

		const FVector Fell = DeadNPC->GetActorLocation();
		for (TActorIterator<AShooterAIController> It(GetWorld()); It; ++It)
		{
			AShooterAIController* const Mate = *It;
			AShooterNPC* const MateNPC = Cast<AShooterNPC>(Mate->GetPawn());
			if (Mate != this && Mate->bInfantry && MateNPC && Mate->GetGenericTeamId() == GetGenericTeamId()
				&& Mate->IsInViewCone(Fell) && Mate->SeesWithOwnEyes(DeadNPC))
			{
				MateNPC->SayTactic(TEXT("MAN DOWN: saw him fall, calling the place"));
				Mate->CallOutLater(Knowledge::CalloutDelay, [Mate, Fell]()
				{
					Mate->TurnMatesTowards(Fell, Knowledge::TurnersPerShout);
				});
				break;
			}
		}
	}

	// stop movement
	GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);

	// stop StateTree logic
	StateTreeAI->StopLogic(FString(""));

	// unpossess the pawn
	UnPossess();

	// destroy this controller
	Destroy();
}

FTargetIntent* AShooterAIController::FindIntent(ETargetIntentSource Source)
{
	return Intents.FindByPredicate([Source](const FTargetIntent& Intent) { return Intent.Source == Source; });
}

const FTargetIntent* AShooterAIController::FindIntent(ETargetIntentSource Source) const
{
	return Intents.FindByPredicate([Source](const FTargetIntent& Intent) { return Intent.Source == Source; });
}

void AShooterAIController::SetTargetIntent(ETargetIntentSource Source, AActor* Target, float Duration)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A null target is a withdrawal, not a request to look at nothing. Saying it this way means a
	// caller that computed its target and got null does not have to branch.
	if (!Target)
	{
		ClearTargetIntent(Source);
		return;
	}

	const float Now = World->GetTimeSeconds();

	FTargetIntent* Intent = FindIntent(Source);
	if (!Intent)
	{
		Intent = &Intents.AddDefaulted_GetRef();
		Intent->Source = Source;
	}

	// SetTime is only bumped when the target actually changes. A decoy refreshed sixty times a second
	// would otherwise keep winning ties against a perception intent set later, purely by being
	// rewritten more often, and "who asked most recently" would stop meaning anything.
	if (Intent->Target.Get() != Target)
	{
		Intent->SetTime = Now;
	}

	Intent->Target = Target;
	Intent->ExpiryTime = Duration > 0.0f ? Now + Duration : 0.0f;
	Intent->bSet = true;

	ResolveTargetIntents();
}

void AShooterAIController::ClearTargetIntent(ETargetIntentSource Source)
{
	const int32 Removed = Intents.RemoveAll([Source](const FTargetIntent& Intent) { return Intent.Source == Source; });
	if (Removed > 0)
	{
		ResolveTargetIntents();
	}
}

AActor* AShooterAIController::GetTargetIntent(ETargetIntentSource Source) const
{
	const FTargetIntent* Intent = FindIntent(Source);
	return Intent ? Intent->Target.Get() : nullptr;
}

void AShooterAIController::ResolveTargetIntents()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Drop what no longer counts: run out of time, or the actor it pointed at is gone. Doing it here
	// rather than at each read means nobody else has to remember, and it is the same walk we need for
	// the comparison anyway.
	Intents.RemoveAll([Now](const FTargetIntent& Intent)
	{
		if (!Intent.bSet || !Intent.Target.IsValid())
		{
			return true;
		}
		return Intent.ExpiryTime > 0.0f && Now >= Intent.ExpiryTime;
	});

	// Testing aid: with polarity.ai.IgnorePlayers on, a player is not a target for anybody. Filtered
	// here rather than at acquisition so that flipping the cvar mid-run also drops the targets that
	// were already standing, and so no path into the AI (perception, arena script, decoy) can put a
	// player back. Lets a faction war be watched from inside without being the most interesting
	// thing on the battlefield.
	const bool bIgnorePlayers = PolarityTeams::ShouldIgnorePlayers();

	// A lane creep is on a leash (author 2026-09-30): what stands too far from its lane is not its
	// business, and a target that walks off past the leash drops out here, so the answer falls back to
	// the core and the march takes the creep back to the lane.
	const USiegeLaneFollower* const LaneFollower = GetPawn() ? GetPawn()->FindComponentByClass<USiegeLaneFollower>() : nullptr;

	// A forest camp's guard is on a leash around its camp (Docs/Lane_Camps_Design_2026-09-30.md), and
	// never marches on the base: with nothing to fight it keeps to its post (USiegeCampGuard).
	const USiegeCampGuard* const CampGuard = GetPawn() ? GetPawn()->FindComponentByClass<USiegeCampGuard>() : nullptr;

	// Highest priority wins; equal priorities go to whoever asked most recently, which is how
	// perception and the arena behaved back when they simply overwrote each other.
	const FTargetIntent* Winner = nullptr;
	for (const FTargetIntent& Intent : Intents)
	{
		if (bIgnorePlayers && CoopPlayers::IsPlayer(Intent.Target.Get()))
		{
			continue;
		}
		if (LaneFollower && !LaneFollower->IsWithinLeash(Intent.Target.Get()))
		{
			continue;
		}
		if (CampGuard && !CampGuard->IsWithinLeash(Intent.Target.Get()))
		{
			continue;
		}

		if (!Winner
			|| Intent.Source > Winner->Source
			|| (Intent.Source == Winner->Source && Intent.SetTime > Winner->SetTime))
		{
			Winner = &Intent;
		}
	}

	AActor* NewTarget = Winner ? Winner->Target.Get() : nullptr;

	// On a siege map, an NPC with nothing to fight marches on the base. This runs under every intent:
	// the moment perception, script or anything else names a target, that request wins back — which
	// is the "a visible player matters, the core is the default" rule the siege wants. No core on the
	// map means SiegeCore is null and every non-siege fight behaves exactly as before.
	if (!NewTarget && !CampGuard)
	{
		NewTarget = SiegeCore.Get();
	}

	if (NewTarget == TargetEnemy)
	{
		return;
	}

	TargetEnemy = NewTarget;

	// SetFocus drives the pawn's rotation, so writing it unconditionally would make the NPC twitch
	// every time anything touched an intent. Hence the early out above.
	if (NewTarget)
	{
		SetFocus(NewTarget);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	SetTargetIntent(ETargetIntentSource::Perception, Target);
}

void AShooterAIController::ClearCurrentTarget()
{
	ClearTargetIntent(ETargetIntentSource::Perception);
}

void AShooterAIController::DistractTo(AActor* Decoy, float Seconds)
{
	if (!Decoy)
	{
		return;
	}

	const bool bIsNew = GetTargetIntent(ETargetIntentSource::Distraction) != Decoy;

	// The duration IS the lock. Nothing refuses anybody: the decoy simply outranks perception for as
	// long as it has been given, and the moment it expires perception's standing request wins again
	// without a single line of special handling.
	SetTargetIntent(ETargetIntentSource::Distraction, Decoy, FMath::Max(0.0f, Seconds));

	if (bIsNew)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[COOP_DEBUG] %s distracted by %s for %.1fs"),
			*GetNameSafe(GetPawn()), *Decoy->GetName(), Seconds);
	}
}

bool AShooterAIController::IsDistracted() const
{
	// No time comparison here any more: an expired intent is removed by ResolveTargetIntents, which
	// Tick calls, so the presence of the entry is the answer.
	return GetTargetIntent(ETargetIntentSource::Distraction) != nullptr;
}

AActor* AShooterAIController::GetDistraction() const
{
	return GetTargetIntent(ETargetIntentSource::Distraction);
}

void AShooterAIController::EndDistraction()
{
	AActor* const Previous = GetTargetIntent(ETargetIntentSource::Distraction);
	if (!Previous)
	{
		return;
	}

	ClearTargetIntent(ETargetIntentSource::Distraction);

	// Note what is NOT here any more. This used to have to null TargetEnemy by hand, because a spent
	// decoy is an ordinary prop and the sense task only looks for somebody new while it has no
	// target, so the NPC would stand there shooting the prop forever. Now dropping the intent is
	// enough: whatever perception last asked for wins again, and if it asked for nothing the answer
	// becomes null on its own and the search restarts.
	UE_LOG(LogTemp, Verbose, TEXT("[COOP_DEBUG] %s stops being distracted by %s"),
		*GetNameSafe(GetPawn()), *Previous->GetName());
}

void AShooterAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Keep the siege-core companion fresh before the resolve below reads it. A world holds one or
	// two cores, so the walk is cheap even at scale.
	SiegeCoreRefreshTimer -= DeltaTime;
	if (SiegeCoreRefreshTimer <= 0.0f)
	{
		SiegeCoreRefreshTimer = SiegeCoreRefreshInterval;
		RefreshSiegeCore();
	}

	// Only expiries need this; everything else resolves at the moment it is written. Cheap enough to
	// run unconditionally: the array holds at most one entry per source.
	ResolveTargetIntents();

	UpdateKnowledge(DeltaTime);
}

bool AShooterAIController::SeesWithOwnEyes(const AActor* Target) const
{
	const APawn* const Observer = GetPawn();
	const UWorld* const World = GetWorld();
	if (!Observer || !Target || !World)
	{
		return false;
	}

	if (const APolarityCharacter* const Body = Cast<APolarityCharacter>(Target))
	{
		if (Body->IsHiddenFromAI())
		{
			return false;
		}
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TargetKnowledge), false, Observer);
	QueryParams.AddIgnoredActor(Target);

	const FVector Eyes = Observer->GetPawnViewLocation();
	BodySamples::FPoints Points;
	BodySamples::Get(Target, Points);

	for (const FVector& Point : Points)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eyes, Point, ECC_Visibility, QueryParams)
			&& !USmokeVisionSubsystem::IsSightBlockedInWorld(World, Eyes, Point))
		{
			return true;
		}
	}
	return false;
}

bool AShooterAIController::IsInViewCone(const FVector& Point) const
{
	const APawn* const Body = GetPawn();
	if (!Body)
	{
		return false;
	}

	const FVector To = Point - Body->GetActorLocation();
	if (To.SizeSquared2D() <= FMath::Square(Knowledge::TouchDistance))
	{
		return true;
	}

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(Knowledge::ViewConeHalfAngle));
	return FVector::DotProduct(Body->GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) >= CosHalfAngle;
}

bool AShooterAIController::Notices(const AActor* Actor) const
{
	if (!Actor || !bInfantry || !Actor->IsA<APawn>() || !UFactionContactMemory::IsKnowledgeOn())
	{
		return true;
	}
	if (Actor == BelievedTarget.Get())
	{
		return bTargetLocationKnown;
	}
	return IsInViewCone(Actor->GetActorLocation());
}

void AShooterAIController::UpdateSeenEnemies(float Now)
{
	const APawn* const Body = GetPawn();
	AShooterNPC* const NPC = Cast<AShooterNPC>(GetPawn());

	// Whom to ask about: what the sight sense has, the target, and everybody the side knows of
	// (told on the radio where he is, and from here there is a clear line to him: that is seeing
	// him, at any distance).
	TArray<AActor*> Candidates;
	if (AIPerception)
	{
		AIPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Candidates);
	}
	if (TargetEnemy)
	{
		Candidates.AddUnique(TargetEnemy);
	}
	if (const UFactionContactMemory* const Memory = UFactionContactMemory::Get(this))
	{
		TArray<FFactionContact> Contacts;
		Memory->GetContacts(GetGenericTeamId().GetId(), Contacts);
		for (const FFactionContact& Contact : Contacts)
		{
			Candidates.AddUnique(Contact.Enemy.Get());
		}
	}

	for (FSeenEnemy& Seen : SeenEnemies)
	{
		Seen.bSeenThisPass = false;
	}

	for (AActor* const Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate == Body || !Candidate->IsA<APawn>() || !IsHostileTo(Candidate))
		{
			continue;
		}
		if (const AShooterNPC* const Other = Cast<AShooterNPC>(Candidate); Other && Other->IsDead())
		{
			continue;
		}

		const FVector Where = Candidate->GetActorLocation();
		if (!IsInViewCone(Where) || !SeesWithOwnEyes(Candidate))
		{
			continue;
		}

		FSeenEnemy* Seen = SeenEnemies.FindByPredicate([Candidate](const FSeenEnemy& Entry) { return Entry.Enemy.Get() == Candidate; });
		if (!Seen)
		{
			Seen = &SeenEnemies.AddDefaulted_GetRef();
			Seen->Enemy = Candidate;
			Seen->FirstSeenTime = Now;
		}
		Seen->bSeenThisPass = true;
		Seen->LastLocation = Where;

		// The side has it once it has been said, and for as long as it stays in view after that:
		// "somebody sees him now" is read off how fresh this is.
		if (Now - Seen->FirstSeenTime >= Knowledge::CalloutDelay)
		{
			if (!Seen->bCalledOut)
			{
				Seen->bCalledOut = true;
				if (NPC)
				{
					NPC->SayTactic(TEXT("CONTACT: calling where he is"));
				}
			}
			BroadcastContactAt(Candidate, Where, /*bSeenNow*/ true);
		}
	}

	for (int32 Index = SeenEnemies.Num() - 1; Index >= 0; --Index)
	{
		const FSeenEnemy& Seen = SeenEnemies[Index];
		if (Seen.bSeenThisPass)
		{
			continue;
		}

		// Out of view: the side keeps the place, and this one is not looking at it any more.
		if (Seen.bCalledOut && Seen.Enemy.IsValid())
		{
			BroadcastContactAt(Seen.Enemy.Get(), Seen.LastLocation, /*bSeenNow*/ false);
		}
		SeenEnemies.RemoveAtSwap(Index);
	}
}

void AShooterAIController::UpdateTargetChoice(float Now)
{
	if (Now < TargetChoiceTimer)
	{
		return;
	}
	TargetChoiceTimer = Now + Knowledge::ChoiceInterval;

	if (CVarAITargetChoice.GetValueOnGameThread() == 0)
	{
		ClearTargetIntent(ETargetIntentSource::Coordinator);
		return;
	}

	AShooterNPC* const NPC = Cast<AShooterNPC>(GetPawn());
	const UFactionContactMemory* const Memory = UFactionContactMemory::Get(this);
	if (!NPC || !Memory)
	{
		return;
	}

	const uint8 Team = GetGenericTeamId().GetId();
	const FVector Here = NPC->GetActorLocation();

	HurtBy.RemoveAll([Now](const FHurtRecord& Hurt) { return !Hurt.Source.IsValid() || Now - Hurt.Time > Knowledge::HurtWindow; });

	// Whom the rest of the side is on, once for the whole pass.
	TArray<const AActor*, TInlineAllocator<16>> MatesTargets;
	for (TActorIterator<AShooterAIController> It(GetWorld()); It; ++It)
	{
		const AShooterAIController* const Mate = *It;
		if (Mate != this && Mate->GetPawn() && Mate->GetGenericTeamId() == GetGenericTeamId() && Mate->GetCurrentTarget())
		{
			MatesTargets.Add(Mate->GetCurrentTarget());
		}
	}

	const USiegeLaneFollower* const LaneFollower = NPC->FindComponentByClass<USiegeLaneFollower>();
	const USiegeCampGuard* const CampGuard = NPC->FindComponentByClass<USiegeCampGuard>();

	struct FCandidate
	{
		AActor* Enemy = nullptr;
		float Score = 0.0f;
	};
	TArray<FCandidate, TInlineAllocator<8>> List;

	// One number for each enemy the side knows of. Nobody is special in it: a player is fought
	// because he is near, in view and hurting, the same as anybody else.
	auto Consider = [&](AActor* Enemy, const FVector& Place, bool bSeenByMe, bool bSeenBySide)
	{
		if (!IsValid(Enemy) || !Enemy->IsA<APawn>() || !IsHostileTo(Enemy)
			|| List.ContainsByPredicate([Enemy](const FCandidate& Entry) { return Entry.Enemy == Enemy; }))
		{
			return;
		}
		if (const AShooterNPC* const Other = Cast<AShooterNPC>(Enemy); Other && Other->IsDead())
		{
			return;
		}
		if ((LaneFollower && !LaneFollower->IsWithinLeash(Enemy)) || (CampGuard && !CampGuard->IsWithinLeash(Enemy)))
		{
			return;
		}

		float Score = 0.0f;
		Score += bSeenByMe ? Knowledge::ScoreSeenByMe : (bSeenBySide ? Knowledge::ScoreSeenBySide : 0.0f);
		Score -= FMath::Min(Knowledge::ScoreDistanceCap, static_cast<float>(FVector::Dist(Here, Place)) / 100.0f * Knowledge::ScorePerMetre);

		// Hurt by him lately: worth less the longer ago it was, nothing after HurtWindow.
		for (const FHurtRecord& Hurt : HurtBy)
		{
			if (Hurt.Source.Get() == Enemy)
			{
				const float Left = 1.0f - (Now - Hurt.Time) / Knowledge::HurtWindow;
				Score += FMath::Min(Knowledge::ScoreHurtCap, Hurt.Damage * Knowledge::ScorePerDamage) * FMath::Max(0.0f, Left);
			}
		}

		// The side spreads over its enemies rather than all taking the same one.
		int32 MatesOnHim = 0;
		for (const AActor* const Taken : MatesTargets)
		{
			MatesOnHim += Taken == Enemy ? 1 : 0;
		}
		Score -= Knowledge::ScorePerMateOnHim * static_cast<float>(MatesOnHim);

		FCandidate& Entry = List.AddDefaulted_GetRef();
		Entry.Enemy = Enemy;
		Entry.Score = Score;
	};

	auto SeenByMe = [this](const AActor* Enemy)
	{
		return SeenEnemies.ContainsByPredicate([Enemy](const FSeenEnemy& Entry) { return Entry.Enemy.Get() == Enemy; });
	};

	TArray<FFactionContact> Contacts;
	Memory->GetContacts(Team, Contacts);
	for (const FFactionContact& Contact : Contacts)
	{
		AActor* const Enemy = Contact.Enemy.Get();
		if (!Enemy)
		{
			continue;
		}
		const bool bMine = SeenByMe(Enemy);
		Consider(Enemy, (bMine || Contact.bCurrentlySeen) ? Enemy->GetActorLocation() : Contact.LastKnownLocation,
			bMine, Contact.bCurrentlySeen);
	}

	// In view and not yet said on the radio.
	for (const FSeenEnemy& Seen : SeenEnemies)
	{
		Consider(Seen.Enemy.Get(), Seen.LastLocation, /*bSeenByMe*/ true, /*bSeenBySide*/ false);
	}

	if (List.Num() == 0)
	{
		// The side knows of nobody: whatever perception or a script named stands.
		ClearTargetIntent(ETargetIntentSource::Coordinator);
		TargetContender = nullptr;
		ContenderLeads = 0;
		return;
	}

	AActor* Current = GetTargetIntent(ETargetIntentSource::Coordinator);
	if (!Current)
	{
		Current = TargetEnemy;
	}

	// The one it is on already is worth a little more than it scores, or two enemies level with
	// each other would be swapped between on every pass.
	int32 BestIndex = 0;
	int32 HeldIndex = INDEX_NONE;
	for (int32 Index = 0; Index < List.Num(); ++Index)
	{
		if (List[Index].Enemy == Current)
		{
			List[Index].Score += Knowledge::ScoreCurrent;
			HeldIndex = Index;
		}
	}
	for (int32 Index = 1; Index < List.Num(); ++Index)
	{
		if (List[Index].Score > List[BestIndex].Score)
		{
			BestIndex = Index;
		}
	}

	AActor* Chosen = List[BestIndex].Enemy;
	if (HeldIndex != INDEX_NONE && BestIndex != HeldIndex)
	{
		// Better by a margin, and for more than one pass: a mate running across the line of fire
		// does not turn the gun.
		ContenderLeads = (TargetContender.Get() == Chosen) ? ContenderLeads + 1 : 1;
		TargetContender = Chosen;
		if (List[BestIndex].Score - List[HeldIndex].Score < Knowledge::SwitchMargin || ContenderLeads < Knowledge::LeadsToSwitch)
		{
			Chosen = List[HeldIndex].Enemy;
		}
		else
		{
			TargetContender = nullptr;
			ContenderLeads = 0;
			NPC->SayTactic(TEXT("NEW TARGET: this one matters more"));
		}
	}
	else
	{
		TargetContender = nullptr;
		ContenderLeads = 0;
	}

	SetTargetIntent(ETargetIntentSource::Coordinator, Chosen);
}

void AShooterAIController::UpdateKnowledge(float DeltaTime)
{
	const UWorld* const World = GetWorld();
	if (!World || !GetPawn())
	{
		return;
	}

	KnowledgeTimer -= DeltaTime;
	if (BelievedTarget.Get() == TargetEnemy && KnowledgeTimer > 0.0f)
	{
		return;
	}
	KnowledgeTimer = Knowledge::Interval;

	const float Now = World->GetTimeSeconds();
	const bool bOn = bInfantry && UFactionContactMemory::IsKnowledgeOn();

	if (bOn)
	{
		UpdateSeenEnemies(Now);
		UpdateTargetChoice(Now);
	}
	else
	{
		SeenEnemies.Reset();
	}

	if (bLookingAround && (Now >= LookUntil || SeesAnyEnemy()))
	{
		bLookingAround = false;
		if (!TargetEnemy)
		{
			ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	AActor* const Target = TargetEnemy;
	if (!IsValid(Target))
	{
		BelievedTarget = nullptr;
		BelievedTime = -1.0f;
		bSeesTargetMyself = false;
		bTargetLocationKnown = false;
		return;
	}

	if (BelievedTarget.Get() != Target)
	{
		BelievedTarget = Target;
		BelievedTime = -1.0f;
		bSeesTargetMyself = false;
	}

	// A thing does not hide: a turret, the core and a decoy are where they are.
	const bool bCanHide = bOn && Target->IsA<APawn>();

	const bool bSeesNow = !bCanHide
		|| SeenEnemies.ContainsByPredicate([Target](const FSeenEnemy& Entry) { return Entry.Enemy.Get() == Target; });
	if (bSeesNow)
	{
		BelievedTargetLocation = Target->GetActorLocation();
		BelievedTime = Now;
	}
	bSeesTargetMyself = bSeesNow;

	// The radio. Somebody of the side has him in view: he is where he is. Nobody has: the newest
	// place anybody knew him at, which is also how a hit taken by one of them reaches the rest.
	bool bSideSeesNow = false;
	if (!bSeesNow)
	{
		FFactionContact Contact;
		const UFactionContactMemory* const Memory = UFactionContactMemory::Get(this);
		if (Memory && Memory->GetContact(GetGenericTeamId().GetId(), Target, Contact))
		{
			bSideSeesNow = Contact.bCurrentlySeen;
			if (Contact.LastSeenTime > BelievedTime)
			{
				BelievedTargetLocation = Contact.LastKnownLocation;
				BelievedTime = Contact.LastSeenTime;
			}
		}
	}

	// Named by something that is not a sense (an arena's script) and never seen by anybody: it was
	// told where, once.
	if (BelievedTime < 0.0f)
	{
		BelievedTargetLocation = Target->GetActorLocation();
		BelievedTime = Now;
	}

	bTargetLocationKnown = bSeesNow || bSideSeesNow;
}

FVector AShooterAIController::GetBelievedTargetLocation() const
{
	const AActor* const Target = BelievedTarget.Get();
	if (!Target)
	{
		return FVector::ZeroVector;
	}
	return bTargetLocationKnown ? Target->GetActorLocation() : BelievedTargetLocation;
}

bool AShooterAIController::GetKnownPlaceOf(const AActor* Enemy, FVector& OutPlace) const
{
	if (!Enemy)
	{
		return false;
	}
	if (!bInfantry || !Enemy->IsA<APawn>() || !UFactionContactMemory::IsKnowledgeOn()
		|| SeenEnemies.ContainsByPredicate([Enemy](const FSeenEnemy& Entry) { return Entry.Enemy.Get() == Enemy; }))
	{
		OutPlace = Enemy->GetActorLocation();
		return true;
	}
	if (Enemy == BelievedTarget.Get())
	{
		OutPlace = GetBelievedTargetLocation();
		return true;
	}

	const UFactionContactMemory* const Memory = UFactionContactMemory::Get(this);
	return Memory && Memory->GetKnownPlace(GetGenericTeamId().GetId(), Enemy, OutPlace);
}

float AShooterAIController::GetSecondsTargetUnknown() const
{
	const UWorld* const World = GetWorld();
	if (!World || bTargetLocationKnown || BelievedTime < 0.0f)
	{
		return 0.0f;
	}
	return World->GetTimeSeconds() - BelievedTime;
}

float AShooterAIController::GetTargetLocationError() const
{
	const AActor* const Target = BelievedTarget.Get();
	return Target ? static_cast<float>(FVector::Dist(Target->GetActorLocation(), GetBelievedTargetLocation())) : -1.0f;
}

FVector AShooterAIController::GetFocalPointOnActor(const AActor* Actor) const
{
	if (Actor && Actor == BelievedTarget.Get() && !bTargetLocationKnown)
	{
		// A sound or a shout to one side: that is where the head is, until somebody shows there.
		return bLookingAround ? LookPlace : BelievedTargetLocation;
	}
	return Super::GetFocalPointOnActor(Actor);
}

void AShooterAIController::LookTowards(const FVector& Place, float Seconds)
{
	const UWorld* const World = GetWorld();
	if (!World || !bInfantry || IsDistracted() || SeesAnyEnemy())
	{
		return;
	}

	// Fighting a thing (a turret, the core): that is where the gun stays.
	if (TargetEnemy && !TargetEnemy->IsA<APawn>())
	{
		return;
	}

	bLookingAround = true;
	LookPlace = Place;
	LookUntil = World->GetTimeSeconds() + Seconds;

	// With a target the tasks hold the focus on it and GetFocalPointOnActor answers with LookPlace.
	// With none, nothing is aiming this NPC at all.
	if (!TargetEnemy)
	{
		SetFocalPoint(Place);
	}
}

void AShooterAIController::CallOutLater(float Delay, TFunction<void()> Call)
{
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [Call = MoveTemp(Call)]() { Call(); }),
		FMath::Max(0.05f, Delay), false);
}

void AShooterAIController::TurnMatesTowards(const FVector& Place, int32 Count)
{
	const APawn* const Body = GetPawn();
	if (!Body || Count <= 0)
	{
		return;
	}

	const FVector Here = Body->GetActorLocation();
	TArray<AShooterAIController*, TInlineAllocator<16>> Mates;
	for (TActorIterator<AShooterAIController> It(GetWorld()); It; ++It)
	{
		AShooterAIController* const Mate = *It;
		if (Mate != this && Mate->bInfantry && Mate->GetPawn() && Mate->GetGenericTeamId() == GetGenericTeamId()
			&& !Mate->SeesAnyEnemy())
		{
			Mates.Add(Mate);
		}
	}
	Mates.Sort([&Here](const AShooterAIController& A, const AShooterAIController& B)
	{
		return FVector::DistSquared(Here, A.GetPawn()->GetActorLocation()) < FVector::DistSquared(Here, B.GetPawn()->GetActorLocation());
	});

	for (int32 Index = 0; Index < Mates.Num() && Index < Count; ++Index)
	{
		Mates[Index]->LookTowards(Place, Knowledge::LookSeconds);
		if (AShooterNPC* const MateNPC = Cast<AShooterNPC>(Mates[Index]->GetPawn()))
		{
			MateNPC->SayTactic(TEXT("TURNING to where a mate called"));
		}
	}
}

void AShooterAIController::ReportShotNoise(const UObject* WorldContext, APawn* Shooter, const FVector& Location, float Range)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !Shooter || Range <= 0.0f || World->GetNetMode() == NM_Client || !UFactionContactMemory::IsKnowledgeOn())
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const float RangeSq = FMath::Square(Range);

	TArray<AShooterAIController*, TInlineAllocator<16>> Free;
	for (TActorIterator<AShooterAIController> It(World); It; ++It)
	{
		AShooterAIController* const Listener = *It;
		const APawn* const Body = Listener->GetPawn();

		// Its own side's guns it knows: it knows where its own stand. Only somebody else's shot is news.
		if (!Listener->bInfantry || !Body || Body == Shooter || !Listener->IsHostileTo(Shooter)
			|| FVector::DistSquared(Body->GetActorLocation(), Location) > RangeSq)
		{
			continue;
		}

		Listener->HeardShooter = Shooter;
		Listener->HeardPlace = Location;
		Listener->HeardTime = Now;

		// With somebody in view it has a fight of its own.
		if (!Listener->SeesAnyEnemy())
		{
			Free.Add(Listener);
		}
	}

	// The place is known to all who heard; the nearest few turn to it. Who fired is known to none
	// of them until one sees him.
	Free.Sort([&Location](const AShooterAIController& A, const AShooterAIController& B)
	{
		return FVector::DistSquared(Location, A.GetPawn()->GetActorLocation()) < FVector::DistSquared(Location, B.GetPawn()->GetActorLocation());
	});
	for (int32 Index = 0; Index < Free.Num() && Index < Knowledge::TurnersPerSound; ++Index)
	{
		Free[Index]->LookTowards(Location, Knowledge::LookSeconds);
		if (AShooterNPC* const NPC = Cast<AShooterNPC>(Free[Index]->GetPawn()))
		{
			NPC->SayTactic(TEXT("SHOT over there, not ours: looking who"));
		}
	}
}

void AShooterAIController::NoteHitFrom(AActor* Source, float Damage)
{
	const UWorld* const World = GetWorld();
	AShooterNPC* const NPC = Cast<AShooterNPC>(GetPawn());
	if (!World || !Source || !NPC || !IsHostileTo(Source))
	{
		return;
	}

	if (!bInfantry || !UFactionContactMemory::IsKnowledgeOn())
	{
		// The old rule: being shot tells the whole side where the shot came from, at once.
		BroadcastContactAt(Source, Source->GetActorLocation(), /*bSeenNow*/ true);
		return;
	}

	const float Now = World->GetTimeSeconds();

	FHurtRecord* Hurt = HurtBy.FindByPredicate([Source](const FHurtRecord& Entry) { return Entry.Source.Get() == Source; });
	if (!Hurt)
	{
		Hurt = &HurtBy.AddDefaulted_GetRef();
		Hurt->Source = Source;
	}
	else if (Now - Hurt->Time > Knowledge::HurtWindow)
	{
		Hurt->Damage = 0.0f;
	}
	Hurt->Damage += Damage;
	Hurt->Time = Now;

	// In view already: the contact is being called as it is.
	if (SeenEnemies.ContainsByPredicate([Source](const FSeenEnemy& Entry) { return Entry.Enemy.Get() == Source; }))
	{
		return;
	}

	const bool bHeardTheShot = HeardShooter.Get() == Source && Now - HeardTime <= Knowledge::HitLinksToShotWithin;
	const bool bMayCall = Now >= NextHitCalloutTime;
	if (bMayCall)
	{
		NextHitCalloutTime = Now + Knowledge::CalloutDelay + 0.5f;
	}

	if (bHeardTheShot)
	{
		// Hit, and the shot was heard: the two together are an enemy at that place. Known to this one
		// at once, to the side when it has said so.
		const FVector Place = HeardPlace;
		if (Source == BelievedTarget.Get() && !bTargetLocationKnown)
		{
			BelievedTargetLocation = Place;
			BelievedTime = Now;
		}
		LookTowards(Place, Knowledge::LookSeconds);
		NPC->SayTactic(TEXT("HIT: the fire is from over there"));

		if (bMayCall)
		{
			CallOutLater(Knowledge::CalloutDelay, [this, WeakSource = TWeakObjectPtr<AActor>(Source), Place]()
			{
				if (AActor* const Enemy = WeakSource.Get())
				{
					BroadcastContactAt(Enemy, Place, /*bSeenNow*/ false);
					if (AShooterNPC* const Caller = Cast<AShooterNPC>(GetPawn()))
					{
						Caller->SayTactic(TEXT("CALLING IT: taking fire from over there"));
					}
				}
			});
		}
		return;
	}

	// Hit and nothing heard (a silenced gun, or one too far off): a side it came from and no more.
	const FVector Here = NPC->GetActorLocation();
	const FVector Place = Here + (Source->GetActorLocation() - Here).GetSafeNormal2D() * Knowledge::DirectionLookDistance;
	LookTowards(Place, Knowledge::LookSeconds);
	NPC->SayTactic(TEXT("HIT: from that side, did not hear the shot"));

	if (bMayCall)
	{
		CallOutLater(Knowledge::CalloutDelay, [this, Place]()
		{
			TurnMatesTowards(Place, Knowledge::TurnersPerShout);
			if (AShooterNPC* const Caller = Cast<AShooterNPC>(GetPawn()))
			{
				Caller->SayTactic(TEXT("CALLING IT: hit, from that side"));
			}
		});
	}
}

bool AShooterAIController::TickSearchForTarget(bool bAllowed)
{
	UWorld* const World = GetWorld();
	AShooterNPC* const NPC = Cast<AShooterNPC>(GetPawn());
	AActor* const Target = BelievedTarget.Get();
	UFactionContactMemory* const Memory = UFactionContactMemory::Get(this);

	bool bWanted = bAllowed && World && NPC && Target && Memory && bInfantry && UFactionContactMemory::IsKnowledgeOn()
		&& Target->IsA<APawn>() && !bTargetLocationKnown && !bLookingAround
		&& GetSecondsTargetUnknown() >= Knowledge::SearchAfterSeconds
		&& !NPC->IsReloadingWeapon();

	if (bWanted)
	{
		// Who does not go. A gun that is held on its target (a scope) stays where it sees from. One
		// told to hold a ground goes only as far as its ground. One falling back, or of a squad on
		// the defensive, does not go looking.
		const AShooterWeapon* const Gun = NPC->GetWeapon();
		const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(NPC);
		const bool bHoldsGround = NPC->HasHoldArea()
			? !NPC->IsInsideHoldArea(BelievedTargetLocation)
			: NPC->GetCombatTask() == ESquadTask::Hold;
		bWanted = !(Gun && Gun->HasAIAimHold()) && !bHoldsGround
			&& NPC->GetCombatTask() != ESquadTask::Withdraw
			&& !(Coordinator && Coordinator->GetPostureFor(NPC) == ESquadPosture::Defense);
	}

	if (!bWanted || !Memory->TryClaimSearch(GetGenericTeamId().GetId(), Target, NPC))
	{
		EndSearchForTarget();
		return false;
	}

	const float Now = World->GetTimeSeconds();
	const FVector Here = NPC->GetActorLocation();

	if (!bSearchingForTarget)
	{
		bSearchingForTarget = true;
		bSearchArrived = false;
		SearchGoal = BelievedTargetLocation;
		SearchLegUntil = Now + Knowledge::SearchWalkLimit;
		MoveToLocation(SearchGoal, 100.0f);
		NPC->SayTactic(TEXT("GOING TO CHECK the place he was last seen"));
	}
	else if (FVector::Dist2D(Here, SearchGoal) <= Knowledge::SearchArriveDistance || Now >= SearchLegUntil)
	{
		if (!bSearchArrived)
		{
			bSearchArrived = true;
			NPC->SayTactic(TEXT("NOBODY HERE: looking round from here"));
		}

		// Round the place he was lost at, not towards where he is.
		FNavLocation Next;
		UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (NavSys && NavSys->GetRandomReachablePointInRadius(BelievedTargetLocation, Knowledge::SearchRadius, Next))
		{
			SearchGoal = Next.Location;
			MoveToLocation(SearchGoal, 100.0f);
		}
		SearchLegUntil = Now + Knowledge::SearchLegSeconds;
	}
	else if (GetMoveStatus() == EPathFollowingStatus::Idle && SearchLegUntil > Now + 1.0f)
	{
		// Not walking and not there: no way to that point. Another one, shortly. Not asked again
		// for the same point: an order to walk that goes nowhere, every tick, makes it spin.
		SearchLegUntil = Now + 1.0f;
	}

	// Looks where it walks.
	SetFocalPoint(SearchGoal + FVector(0.0f, 0.0f, 60.0f));
	return true;
}

void AShooterAIController::EndSearchForTarget()
{
	if (!bSearchingForTarget)
	{
		return;
	}
	bSearchingForTarget = false;
	bSearchArrived = false;

	if (UFactionContactMemory* const Memory = UFactionContactMemory::Get(this))
	{
		Memory->ReleaseSearch(GetGenericTeamId().GetId(), GetPawn());
	}

	// Its own walk, and only that: this is the one place that started it.
	StopMovement();
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	const bool bIsSight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
	const bool bIsTeam = Stimulus.Type == UAISense::GetSenseID<UAISense_Team>();

	// If this is a Team sense event, broadcast to Blueprint
	if (bIsTeam && Stimulus.WasSuccessfullySensed())
	{
		// Broadcast Blueprint event for team perception
		OnTeamPerceptionReceived.Broadcast(Actor, Stimulus.StimulusLocation);
	}

	// If this is a Sight sense event, broadcast appropriate Blueprint events
	if (bIsSight)
	{
		if (Stimulus.WasSuccessfullySensed())
		{
			// Enemy spotted via sight
			OnEnemySpotted.Broadcast(Actor, Stimulus.StimulusLocation);
		}
		else
		{
			// Enemy lost (sight sense returned false = no longer visible)
			OnEnemyLost.Broadcast(Actor);
		}

		// The side learns what this pair of eyes learned. Both edges matter and they mean different
		// things: gaining sight writes a position somebody is looking at, losing it leaves the last
		// position with nobody watching, which is exactly the thing worth searching.
		// A foot soldier says it itself, from its own cone and a moment after it sees
		// (UpdateSeenEnemies): the sense's edge is at once, and its cone is the blueprint's.
		const bool bCallsItself = bInfantry && UFactionContactMemory::IsKnowledgeOn();
		if (IsHostileTo(Actor) && !bCallsItself)
		{
			BroadcastContactAt(Actor, Stimulus.StimulusLocation, Stimulus.WasSuccessfullySensed());
		}
	}

	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);

	// If we successfully detected an enemy, broadcast to teammates
	if (bSharePerceptionWithTeam && Stimulus.WasSuccessfullySensed() && Actor)
	{
		// Only broadcast sight sense detections to avoid spam (don't re-broadcast team events)
		if (bIsSight)
		{
			BroadcastEnemyToTeam(Actor, Actor->GetActorLocation());
		}
	}
}

void AShooterAIController::RefreshSiegeCore()
{
	const UWorld* const World = GetWorld();
	const APawn* const PossessedPawn = GetPawn();
	ABuildableActor* const Core = (World && PossessedPawn)
		? ABuildableActor::FindNearestCore(World, PossessedPawn->GetActorLocation())
		: nullptr;
	if (Core != SiegeCore.Get())
	{
		SiegeCore = Core;
		UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s fallback core: %s"),
			*GetNameSafe(GetPawn()), Core ? *Core->GetName() : TEXT("none"));
	}

	// New companion, new answer: an NPC that was standing idle must now turn towards the march.
	ResolveTargetIntents();
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	// Broadcast Blueprint event
	OnEnemyLost.Broadcast(Actor);

	// On a siege map, losing sight of the current target hands it back to the core. Without this the
	// standing Perception intent would keep the NPC chasing a player it cannot see, and the resolved
	// answer would never reach the siege fallback. Maps without a core keep the old pursuit rule.
	if (Actor && GetTargetIntent(ETargetIntentSource::Perception) == Actor && SiegeCore.IsValid())
	{
		ClearCurrentTarget();
	}

	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}

void AShooterAIController::ForcePerceptionUpdate()
{
	if (AIPerception)
	{
		AIPerception->RequestStimuliListenerUpdate();
	}
}

void AShooterAIController::BroadcastEnemyToTeam(AActor* DetectedEnemy, const FVector& LastKnownLocation)
{
	if (!DetectedEnemy || !GetPawn())
	{
		return;
	}

	UAIPerceptionSystem* PerceptionSystem = UAIPerceptionSystem::GetCurrent(GetWorld());
	if (!PerceptionSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] BroadcastEnemyToTeam: No PerceptionSystem!"), *GetName());
		return;
	}

	// Create team stimulus event
	FAITeamStimulusEvent TeamEvent(
		GetPawn(),              // Broadcaster - our pawn
		DetectedEnemy,          // Enemy - the detected actor
		LastKnownLocation,      // Where we saw them
		TeamPerceptionRadius,   // How far to broadcast (radius)
		0.0f,                   // Info age - 0 means fresh info
		1.0f                    // Strength - 1.0 = high confidence
	);

	// Send the event to the perception system (this internally calls UAISense_Team::RegisterEvent)
	PerceptionSystem->OnEvent(TeamEvent);

}