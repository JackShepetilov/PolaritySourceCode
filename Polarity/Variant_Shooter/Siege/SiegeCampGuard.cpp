// SiegeCampGuard.cpp

#include "SiegeCampGuard.h"

#include "AI/PolarityTeams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "SiegeCampSite.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "Variant_Shooter/AI/KamikazeDroneNPC.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

USiegeCampGuard::USiegeCampGuard()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
}

void USiegeCampGuard::Init(ASiegeCampSite* InCamp, const FVector& InPost, float InSpawnHealth)
{
	Camp = InCamp;
	Post = InPost;
	SpawnHealth = InSpawnHealth;
}

void USiegeCampGuard::BeginPlay()
{
	Super::BeginPlay();
	if (AShooterNPC* const NPC = Cast<AShooterNPC>(GetOwner()))
	{
		NPC->OnDamageTaken.AddDynamic(this, &USiegeCampGuard::OnOwnerDamaged);
	}
}

bool USiegeCampGuard::IsWithinLeash(const AActor* Target) const
{
	if (bReturning)
	{
		return false;
	}
	const ASiegeCampSite* const CampPtr = Camp.Get();
	const UWorld* const World = GetWorld();
	if (!Target || !CampPtr || !World)
	{
		return true;
	}
	if (Target == LastAttacker.Get() && World->GetTimeSeconds() - LastAttackedTime <= RetaliateSeconds)
	{
		return true;
	}
	return FVector::DistSquared2D(Target->GetActorLocation(), CampPtr->GetActorLocation()) <= FMath::Square(LeashRadius);
}

void USiegeCampGuard::Alert(AActor* Attacker)
{
	const UWorld* const World = GetWorld();
	if (!Attacker || bReturning || !World)
	{
		return;
	}
	LastAttacker = Attacker;
	LastAttackedTime = World->GetTimeSeconds();
	if (AShooterAIController* const AIController = Cast<AShooterAIController>(Cast<APawn>(GetOwner()) ? Cast<APawn>(GetOwner())->GetController() : nullptr))
	{
		if (AIController->GetCurrentTarget() != Attacker)
		{
			AIController->SetCurrentTarget(Attacker);
		}
	}
}

APawn* USiegeCampGuard::ResolveAttacker(AActor* DamageCauser)
{
	if (!DamageCauser)
	{
		return nullptr;
	}
	if (APawn* const AsPawn = Cast<APawn>(DamageCauser))
	{
		return AsPawn;
	}
	if (APawn* const Instigator = DamageCauser->GetInstigator())
	{
		return Instigator;
	}
	return Cast<APawn>(DamageCauser->GetOwner());
}

void USiegeCampGuard::OnOwnerDamaged(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser)
{
	APawn* const Attacker = ResolveAttacker(DamageCauser);
	if (!Attacker || Attacker == GetOwner() || !PolarityTeams::AreHostile(GetOwner(), Attacker))
	{
		return;
	}
	if (ASiegeCampSite* const CampPtr = Camp.Get())
	{
		// The whole camp answers, this guard included.
		CampPtr->AlertGuards(Attacker);
	}
	else
	{
		Alert(Attacker);
	}
}

void USiegeCampGuard::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AShooterNPC* const NPC = Cast<AShooterNPC>(GetOwner());
	const ASiegeCampSite* const CampPtr = Camp.Get();
	if (!NPC || !CampPtr || NPC->IsDead() || !NPC->HasAuthority())
	{
		return;
	}
	AShooterAIController* const AIController = Cast<AShooterAIController>(NPC->GetController());

	// Dragged too far: give up the fight and go home. The camp cannot be pulled under the turrets.
	const float FromCamp = FVector::Dist2D(NPC->GetActorLocation(), CampPtr->GetActorLocation());
	if (!bReturning && FromCamp > MaxRoamRadius)
	{
		bReturning = true;
		HomeOrderTimer = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %s is %.0f m from camp, leash %.0f m: back to its post"),
			*CampPtr->GetName(), *NPC->GetName(), FromCamp / 100.0f, MaxRoamRadius / 100.0f);
		if (AIController)
		{
			AIController->ClearCurrentTarget();
		}
	}

	// Flyers steer themselves home (AKamikazeCarrierDrone::TickSelfDriven); only walkers get orders here.
	const bool bWalker = !NPC->IsA<AFlyingDrone>() && !NPC->IsA<AKamikazeDroneNPC>();
	const bool bIdle = !AIController || !AIController->GetCurrentTarget();
	const float FromPost = FVector::Dist2D(NPC->GetActorLocation(), Post);

	if (bReturning || bIdle)
	{
		if (FromPost > PostTolerance)
		{
			HomeOrderTimer -= DeltaTime;
			if (bWalker && AIController && HomeOrderTimer <= 0.0f)
			{
				HomeOrderTimer = HomeOrderInterval;
				AIController->MoveToLocation(Post, PostTolerance * 0.5f);
			}
			return;
		}

		// Home. A reset heals, the way a Dota camp does; a guard idling at its post stays topped up.
		if (bReturning)
		{
			bReturning = false;
			UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %s is back at its post, hp %.0f -> %.0f"),
				*CampPtr->GetName(), *NPC->GetName(), NPC->CurrentHP, FMath::Max(NPC->CurrentHP, SpawnHealth));
		}
		if (NPC->CurrentHP < SpawnHealth)
		{
			NPC->CurrentHP = SpawnHealth;
		}
	}
}
