// TierBenchComponent.cpp

#include "TierBenchComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"

namespace TierBench
{
	const FName SpawnTag(TEXT("TierBenchSpawn"));

	FString ClassLabel(const UClass* Class)
	{
		FString Name = GetNameSafe(Class);
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}
}

UTierBenchComponent::UTierBenchComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the pawn and the camera have moved, so the view point is this frame's.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

AShooterCharacter* UTierBenchComponent::GetPlayer() const
{
	return Cast<AShooterCharacter>(GetOwner());
}

void UTierBenchComponent::StartRun(const TArray<TSubclassOf<AShooterNPC>>& InClasses, int32 InRounds, float InTimeoutSeconds)
{
	if (bRunning)
	{
		StopRun();
	}
	Classes = InClasses;
	Rounds = FMath::Max(InRounds, 1);
	TimeoutSeconds = FMath::Max(InTimeoutSeconds, 5.0f);
	NextIndex = 0;
	Totals.Reset();
	GapLeft = 0.5f;
	LastHealth = -1.0f;
	bRunning = Classes.Num() > 0;

	// The player's gun never runs dry on the bench: a reload is a pause the enemy did not earn.
	if (IConsoleVariable* const Ammo = IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.debug.infiniteammo")))
	{
		Ammo->Set(1, ECVF_SetByConsole);
	}

	FString Names;
	for (const TSubclassOf<AShooterNPC>& Class : Classes)
	{
		Names += TEXT(" ") + TierBench::ClassLabel(Class);
	}
	UE_LOG(LogTemp, Warning, TEXT("[TIER_BENCH] START classes:%s rounds=%d timeout=%.0f reaction=%.2f turn=%.0f"),
		*Names, Rounds, TimeoutSeconds, ReactionSeconds, TurnRateDegrees);
}

void UTierBenchComponent::StopRun()
{
	if (!bRunning)
	{
		return;
	}
	if (Current.IsValid() && !Current->IsDead())
	{
		FinishEnemy(true);
	}
	bRunning = false;
	SetFiring(false);
	PrintSummary();
}

void UTierBenchComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRun();
	Super::EndPlay(EndPlayReason);
}

void UTierBenchComponent::SetFiring(bool bFire)
{
	if (bFire == bFiring)
	{
		return;
	}
	bFiring = bFire;
	if (AShooterCharacter* const Player = GetPlayer())
	{
		if (bFire)
		{
			Player->DoStartFiring();
		}
		else
		{
			Player->DoStopFiring();
		}
	}
}

bool UTierBenchComponent::SpawnNext()
{
	UWorld* const World = GetWorld();
	if (!World || Classes.Num() == 0 || NextIndex >= Classes.Num() * Rounds)
	{
		return false;
	}

	AActor* Marker = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TierBench::SpawnTag))
		{
			Marker = *It;
			break;
		}
	}
	if (!Marker)
	{
		UE_LOG(LogTemp, Error, TEXT("[TIER_BENCH] no actor tagged %s on this level"), *TierBench::SpawnTag.ToString());
		return false;
	}

	CurrentClass = Classes[NextIndex % Classes.Num()];
	++NextIndex;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AShooterNPC* const Enemy = World->SpawnActor<AShooterNPC>(CurrentClass, Marker->GetActorTransform(), Params);
	if (!Enemy)
	{
		UE_LOG(LogTemp, Error, TEXT("[TIER_BENCH] could not spawn %s"), *TierBench::ClassLabel(CurrentClass));
		return false;
	}
	if (!Enemy->GetController())
	{
		Enemy->SpawnDefaultController();
	}
	Enemy->OnDamageTaken.AddUniqueDynamic(this, &UTierBenchComponent::HandleEnemyDamaged);

	Current = Enemy;
	Stats = FEnemyStats();
	SeenFor = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[TIER_BENCH] SPAWN %d/%d %s"), NextIndex, Classes.Num() * Rounds, *TierBench::ClassLabel(CurrentClass));
	return true;
}

void UTierBenchComponent::HandleEnemyDamaged(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser)
{
	if (DamagedNPC && DamagedNPC == Current.Get())
	{
		Stats.Taken += Damage;
	}
}

void UTierBenchComponent::FinishEnemy(bool bTimedOut)
{
	SetFiring(false);
	Stats.bTimedOut = bTimedOut;
	AShooterNPC* const Enemy = Current.Get();
	if (Enemy)
	{
		Enemy->OnDamageTaken.RemoveDynamic(this, &UTierBenchComponent::HandleEnemyDamaged);
		if (bTimedOut && !Enemy->IsDead())
		{
			Enemy->Destroy();
		}
	}

	const float Minutes = FMath::Max(Stats.Life, 0.01f) / 60.0f;
	UE_LOG(LogTemp, Warning, TEXT("[TIER_BENCH] ENEMY class=%s life=%.1f exposed=%.0f%% toPlayer=%.0f perMin=%.0f perExposed=%.1f taken=%.0f%s"),
		*TierBench::ClassLabel(CurrentClass), Stats.Life, 100.0f * Stats.Exposed / FMath::Max(Stats.Life, 0.01f),
		Stats.ToPlayer, Stats.ToPlayer / Minutes, Stats.ToPlayer / FMath::Max(Stats.Exposed, 0.01f), Stats.Taken,
		bTimedOut ? TEXT(" TIMEOUT") : TEXT(""));

	FClassTotals& Total = Totals.FindOrAdd(TierBench::ClassLabel(CurrentClass));
	++Total.Count;
	Total.TimedOut += bTimedOut ? 1 : 0;
	Total.Life += Stats.Life;
	Total.Exposed += Stats.Exposed;
	Total.ToPlayer += Stats.ToPlayer;
	Total.Taken += Stats.Taken;

	Current.Reset();
	GapLeft = GapSeconds;
}

void UTierBenchComponent::PrintSummary() const
{
	for (const TPair<FString, FClassTotals>& Pair : Totals)
	{
		const FClassTotals& T = Pair.Value;
		const float N = FMath::Max(T.Count, 1);
		UE_LOG(LogTemp, Warning, TEXT("[TIER_BENCH] SUMMARY class=%s n=%d timeouts=%d life=%.1f exposed=%.0f%% perMin=%.0f perExposed=%.1f taken=%.0f"),
			*Pair.Key, T.Count, T.TimedOut, T.Life / N, 100.0f * T.Exposed / FMath::Max(T.Life, 0.01f),
			T.ToPlayer / FMath::Max(T.Life / 60.0f, 0.01f), T.ToPlayer / FMath::Max(T.Exposed, 0.01f), T.Taken / N);
	}
	UE_LOG(LogTemp, Warning, TEXT("[TIER_BENCH] DONE"));
}

float UTierBenchComponent::DrainPlayerDamage()
{
	AShooterCharacter* const Player = GetPlayer();
	if (!Player)
	{
		return 0.0f;
	}
	const float Before = Player->GetCurrentHP() + Player->GetCurrentArmor();
	const float Lost = LastHealth >= 0.0f ? FMath::Max(LastHealth - Before, 0.0f) : 0.0f;
	if (Player->GetCurrentHP() < Player->GetMaxHP())
	{
		Player->RestoreHealth(Player->GetMaxHP() - Player->GetCurrentHP());
	}
	LastHealth = Player->GetCurrentHP() + Player->GetCurrentArmor();
	return Lost;
}

bool UTierBenchComponent::FindVisiblePoint(const AShooterNPC* Enemy, FVector& OutPoint) const
{
	const AShooterCharacter* const Player = GetPlayer();
	const APlayerController* const PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	UWorld* const World = GetWorld();
	if (!PC || !Enemy || !World)
	{
		return false;
	}

	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);

	// Chest first (what a player aims at), the head when only the head shows over cover.
	TArray<FVector, TInlineAllocator<2>> Points;
	Points.Add(Enemy->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f));
	if (const USkeletalMeshComponent* const Mesh = Enemy->GetMesh(); Mesh && Mesh->DoesSocketExist(TEXT("head")))
	{
		Points.Add(Mesh->GetSocketLocation(TEXT("head")));
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(TierBenchSight), false, Player);
	Query.AddIgnoredActor(Enemy);
	for (const FVector& Point : Points)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Query))
		{
			OutPoint = Point;
			return true;
		}
	}
	return false;
}

void UTierBenchComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AShooterCharacter* const Player = GetPlayer();
	if (!bRunning || !Player || !Player->HasAuthority())
	{
		return;
	}

	const float Lost = DrainPlayerDamage();

	AShooterNPC* const Enemy = Current.Get();
	if (!Enemy)
	{
		GapLeft -= DeltaTime;
		if (GapLeft <= 0.0f && !SpawnNext())
		{
			bRunning = false;
			SetFiring(false);
			PrintSummary();
		}
		return;
	}

	Stats.ToPlayer += Lost;
	if (Enemy->IsDead())
	{
		FinishEnemy(false);
		return;
	}
	Stats.Life += DeltaTime;
	if (Stats.Life >= TimeoutSeconds)
	{
		FinishEnemy(true);
		return;
	}

	FVector AimPoint;
	if (!FindVisiblePoint(Enemy, AimPoint))
	{
		SeenFor = 0.0f;
		SetFiring(false);
		return;
	}
	Stats.Exposed += DeltaTime;
	SeenFor += DeltaTime;
	if (SeenFor < ReactionSeconds)
	{
		return;
	}

	APlayerController* const PC = Cast<APlayerController>(Player->GetController());
	if (!PC)
	{
		return;
	}
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	const FRotator Want = (AimPoint - Eye).Rotation();
	const FRotator Now = FMath::RInterpConstantTo(PC->GetControlRotation(), Want, DeltaTime, TurnRateDegrees);
	PC->SetControlRotation(Now);

	const float OffDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(Now.Vector(), (AimPoint - Eye).GetSafeNormal()), -1.0f, 1.0f)));
	SetFiring(OffDegrees <= FireConeDegrees);
}

// ==================== Console ====================

namespace TierBench
{
	UTierBenchComponent* FindOrAddBench(UWorld* World, bool bCreate)
	{
		APlayerController* const PC = CoopPlayers::GetLocalController(World);
		AShooterCharacter* const Player = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
		if (!Player)
		{
			return nullptr;
		}
		UTierBenchComponent* Bench = Player->FindComponentByClass<UTierBenchComponent>();
		if (!Bench && bCreate)
		{
			Bench = NewObject<UTierBenchComponent>(Player, TEXT("TierBench"));
			Bench->RegisterComponent();
		}
		return Bench;
	}

	UClass* LoadNPCClass(const FString& Path)
	{
		FString Full = Path;
		if (!Full.Contains(TEXT(".")))
		{
			Full += TEXT(".") + FPaths::GetBaseFilename(Path) + TEXT("_C");
		}
		else if (!Full.EndsWith(TEXT("_C")))
		{
			Full += TEXT("_C");
		}
		return LoadClass<AShooterNPC>(nullptr, *Full);
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (Args.Num() == 0 || Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			if (UTierBenchComponent* const Bench = FindOrAddBench(World, false))
			{
				Bench->StopRun();
			}
			return;
		}

		TArray<FString> Paths;
		Args[0].ParseIntoArray(Paths, TEXT(","));
		TArray<TSubclassOf<AShooterNPC>> Classes;
		for (const FString& Path : Paths)
		{
			if (UClass* const Class = LoadNPCClass(Path))
			{
				Classes.Add(Class);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[TIER_BENCH] no NPC class at %s"), *Path);
			}
		}
		const int32 Rounds = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 5;
		const float Timeout = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 60.0f;

		UTierBenchComponent* const Bench = FindOrAddBench(World, true);
		if (!Bench || Classes.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TIER_BENCH] nothing to run (no player pawn or no classes)"));
			return;
		}
		Bench->StartRun(Classes, Rounds, Timeout);
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdTierBench(
	TEXT("polarity.bench.tier"),
	TEXT("Infantry tier bench: the player shoots back on his own, enemies come one at a time from the TierBenchSpawn marker. ")
	TEXT("Usage: polarity.bench.tier /Game/.../BP_A,/Game/.../BP_B [rounds=5] [timeout=60] | polarity.bench.tier stop"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TierBench::Command));
