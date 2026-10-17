// PlayerSuppressionComponent.cpp

#include "Variant_Shooter/Suppression/PlayerSuppressionComponent.h"

#include "AI/PolarityTeams.h"
#include "Variant_Shooter/Progression/RunLevelComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ApexMovementComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarPlayerSuppression(
	TEXT("polarity.suppress.enabled"),
	1,
	TEXT("1 - rounds passing close suppress players (vignette, slower sight, sway). 0 - off."));

UPlayerSuppressionComponent::UPlayerSuppressionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

bool UPlayerSuppressionComponent::IsOwnerLocal() const
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

void UPlayerSuppressionComponent::NotifyHostileShot(const AActor* Shooter, const FVector& Start, const FVector& Direction, float MaxRange)
{
	UWorld* const World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client || CVarPlayerSuppression.GetValueOnGameThread() == 0)
	{
		return;
	}

	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	TArray<APawn*> Pawns;
	CoopPlayers::GetAll(World, Pawns);
	for (APawn* const Pawn : Pawns)
	{
		if (!IsValid(Pawn) || Pawn == Shooter || !PolarityTeams::AreHostile(Shooter, Pawn))
		{
			continue;
		}
		UPlayerSuppressionComponent* const Suppression = Pawn->FindComponentByClass<UPlayerSuppressionComponent>();
		if (!Suppression || Suppression->NearMissRadius <= 0.0f)
		{
			continue;
		}

		// Closest approach of the straight line to the chest. A round going the other way, or one
		// that would pass further out than the range it is good for, says nothing.
		const FVector Chest = Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
		const float Along = FVector::DotProduct(Chest - Start, Dir);
		if (Along <= 0.0f || Along > MaxRange)
		{
			continue;
		}
		const FVector Closest = Start + Dir * Along;
		const float Miss = FVector::Dist(Closest, Chest);
		if (Miss > Suppression->NearMissRadius)
		{
			continue;
		}

		// Stopped by cover on the way: the round never came past. This is what makes cover the
		// answer to being suppressed.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SuppressionNearMiss), false);
		Params.AddIgnoredActor(Shooter);
		Params.AddIgnoredActor(Pawn);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, Closest, ECC_Visibility, Params))
		{
			continue;
		}

		Suppression->AddSuppression(Suppression->LevelPerNearMiss * (1.0f - 0.5f * Miss / Suppression->NearMissRadius));
	}
}

void UPlayerSuppressionComponent::AddSuppression(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	ApplyLocally(Amount);

	// The server's copy is for the record; the one that changes the sight is the owner's.
	if (GetOwner() && GetOwner()->HasAuthority() && !IsOwnerLocal())
	{
		Client_AddSuppression(Amount);
	}
}

void UPlayerSuppressionComponent::Client_AddSuppression_Implementation(float Amount)
{
	ApplyLocally(Amount);
}

void UPlayerSuppressionComponent::ApplyLocally(float Amount)
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// The run level raises the resistance [author, 2026-10-16]; the higher of the two stands.
	const URunLevelComponent* const RunLevel = URunLevelComponent::FindFor(GetOwner());
	const float Resist = FMath::Clamp(FMath::Max(Resistance, RunLevel ? RunLevel->GetSuppressionResistance() : 0.0f), 0.0f, 1.0f);
	Level = FMath::Clamp(Level + Amount * (1.0f - Resist), 0.0f, 1.0f);
	LastNearMissTime = World->GetTimeSeconds();
	SetComponentTickEnabled(true);
}

float UPlayerSuppressionComponent::GetADSSpeedMultiplier() const
{
	return FMath::Lerp(1.0f, ADSSpeedAtFull, FMath::Max(Level, GetHeadacheLevel()));
}

// ==================== Headache ====================

void UPlayerSuppressionComponent::ApplyHeadache(float Strength, float Seconds)
{
	if (Strength <= 0.0f || Seconds <= 0.0f)
	{
		return;
	}
	ApplyHeadacheLocally(Strength, Seconds);

	// The server keeps its own copy as well: it is the one that has to refuse the sprint and the
	// slide to the moves it replays.
	if (GetOwner() && GetOwner()->HasAuthority() && !IsOwnerLocal())
	{
		Client_ApplyHeadache(Strength, Seconds);
	}
}

void UPlayerSuppressionComponent::Client_ApplyHeadache_Implementation(float Strength, float Seconds)
{
	ApplyHeadacheLocally(Strength, Seconds);
}

void UPlayerSuppressionComponent::ApplyHeadacheLocally(float Strength, float Seconds)
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const bool bHeld = Now < HeadacheUntil;
	HeadacheStrength = FMath::Clamp(bHeld ? FMath::Max(HeadacheStrength, Strength) : Strength, 0.0f, 1.0f);
	HeadacheUntil = FMath::Max(HeadacheUntil, Now + Seconds);
	SetComponentTickEnabled(true);
}

float UPlayerSuppressionComponent::GetHeadacheLevel() const
{
	const UWorld* const World = GetWorld();
	if (!World || HeadacheStrength <= 0.0f)
	{
		return 0.0f;
	}
	const float Past = World->GetTimeSeconds() - HeadacheUntil;
	if (Past <= 0.0f)
	{
		return HeadacheStrength;
	}
	return HeadacheStrength * FMath::Clamp(1.0f - Past / FMath::Max(HeadacheFadeSeconds, 0.05f), 0.0f, 1.0f);
}

void UPlayerSuppressionComponent::UpdateHeadachePicture()
{
	if (!IsOwnerLocal())
	{
		return;
	}
	const float Ache = GetHeadacheLevel();
	UWorld* const World = GetWorld();
	if (!HeadacheVolume && Ache > 0.0f && World && HeadacheMaterial)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		HeadacheVolume = World->SpawnActor<APostProcessVolume>(GetOwner()->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (HeadacheVolume)
		{
			HeadacheVolume->bUnbound = true;
			HeadacheVolume->Priority = 11.0f;
			HeadacheMID = UMaterialInstanceDynamic::Create(HeadacheMaterial, this);
			HeadacheVolume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, HeadacheMID));
		}
	}
	if (HeadacheVolume)
	{
		if (HeadacheMID)
		{
			HeadacheMID->SetScalarParameterValue(TEXT("Strength"), Ache);
		}
		HeadacheVolume->BlendWeight = Ache > 0.0f ? 1.0f : 0.0f;
		HeadacheVolume->bEnabled = Ache > 0.0f;
	}
}

FRotator UPlayerSuppressionComponent::GetSwayRotation(float ADSAlpha) const
{
	const UWorld* const World = GetWorld();
	const float Aim = FMath::Clamp(ADSAlpha, 0.0f, 1.0f);
	// The headache sways from the hip as well: the head, not the nerves under fire.
	const float Amount = FMath::Max(SwayDegreesAtFull * Level * Aim,
		HeadacheSwayDegreesAtFull * GetHeadacheLevel() * FMath::Lerp(HeadacheHipSwayShare, 1.0f, Aim));
	if (!World || Amount <= KINDA_SMALL_NUMBER)
	{
		return FRotator::ZeroRotator;
	}

	// Two sines that never line up: a slow figure the hand can follow, not a shake.
	const float Phase = World->GetTimeSeconds() * SwayFrequency * 2.0f * PI;
	return FRotator(FMath::Sin(Phase) * Amount, FMath::Sin(Phase * 0.63f + 1.3f) * Amount * 1.3f, 0.0f);
}

void UPlayerSuppressionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* const World = GetWorld();
	if (World && World->GetTimeSeconds() - LastNearMissTime >= DecayDelay)
	{
		Level = FMath::Max(0.0f, Level - DeltaTime / FMath::Max(DecaySeconds, 0.05f));
	}

	UpdateVignette();

	// While the headache holds: no sprint and no slide, here and on the server alike, renewed every
	// frame so they come back on their own when it runs out.
	const bool bAching = World && World->GetTimeSeconds() < HeadacheUntil && HeadacheStrength > 0.0f;
	if (bAching)
	{
		const ACharacter* const Body = Cast<ACharacter>(GetOwner());
		if (UApexMovementComponent* const Apex = Body ? Cast<UApexMovementComponent>(Body->GetCharacterMovement()) : nullptr)
		{
			Apex->SuppressSprint(0.2f);
			Apex->SuppressSlide(0.2f);
		}
	}
	UpdateHeadachePicture();

	if (Level <= 0.0f && GetHeadacheLevel() <= 0.0f)
	{
		SetComponentTickEnabled(false);
	}
}

void UPlayerSuppressionComponent::UpdateVignette()
{
	if (!IsOwnerLocal())
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!VignetteVolume && Level > 0.0f && World)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		VignetteVolume = World->SpawnActor<APostProcessVolume>(GetOwner()->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (VignetteVolume)
		{
			VignetteVolume->bUnbound = true;
			VignetteVolume->bEnabled = true;
			VignetteVolume->Priority = 10.0f;
			VignetteVolume->Settings.bOverride_VignetteIntensity = true;
		}
	}

	if (VignetteVolume)
	{
		// Blended in by weight, so at nothing the level's own picture is untouched.
		VignetteVolume->Settings.VignetteIntensity = VignetteAtFull;
		VignetteVolume->BlendWeight = Level;
		VignetteVolume->bEnabled = Level > 0.0f;
	}
}

void UPlayerSuppressionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (VignetteVolume)
	{
		VignetteVolume->Destroy();
		VignetteVolume = nullptr;
	}
	if (HeadacheVolume)
	{
		HeadacheVolume->Destroy();
		HeadacheVolume = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// ==================== Console ====================

static FAutoConsoleCommandWithWorldAndArgs GCmdHeadacheMe(
	TEXT("polarity.suppress.headache"),
	TEXT("A microwave headache on every player: strength 0-1 (default 0.85), seconds (default 6). To look at the picture."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const float Strength = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.85f;
		const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 6.0f;
		TArray<APawn*> Pawns;
		CoopPlayers::GetAll(World, Pawns);
		for (APawn* const Pawn : Pawns)
		{
			if (UPlayerSuppressionComponent* const Suppression = Pawn ? Pawn->FindComponentByClass<UPlayerSuppressionComponent>() : nullptr)
			{
				Suppression->ApplyHeadache(Strength, Seconds);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GCmdSuppressMe(
	TEXT("polarity.suppress.me"),
	TEXT("Suppress every player by the amount given (default 1 = full). To look at the effect without being shot at."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const float Amount = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.0f;
		TArray<APawn*> Pawns;
		CoopPlayers::GetAll(World, Pawns);
		for (APawn* const Pawn : Pawns)
		{
			if (UPlayerSuppressionComponent* const Suppression = Pawn ? Pawn->FindComponentByClass<UPlayerSuppressionComponent>() : nullptr)
			{
				Suppression->AddSuppression(Amount);
			}
		}
	}));
