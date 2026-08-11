// BasketballBall.cpp

#include "Arena/BasketballBall.h"
#include "Coop/CoopPlayers.h"

#include "Components/StaticMeshComponent.h"
#include "EMFChannelingPlateActor.h"
#include "Engine/DamageEvents.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/DamageTypes/DamageType_Basketball.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/ObjectKey.h"

ABasketballBall::ABasketballBall()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	SetRootComponent(BallMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
	}

	BallMesh->SetSimulatePhysics(true);
	BallMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	BallMesh->SetCollisionObjectType(ECC_PhysicsBody);
	BallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BallMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BallMesh->BodyInstance.bUseCCD = true;
	BallMesh->BodyInstance.bNotifyRigidBodyCollision = true;
	BallMesh->SetNotifyRigidBodyCollision(true);
	BallMesh->SetGenerateOverlapEvents(true);

	ImpactDamageType = UDamageType_Basketball::StaticClass();

	Tags.AddUnique(TEXT("BasketballBall"));
}

void ABasketballBall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyBallSettings();
}

void ABasketballBall::BeginPlay()
{
	Super::BeginPlay();

	ApplyBallSettings();

	if (BallMesh)
	{
		BallMesh->OnComponentHit.AddUniqueDynamic(this, &ABasketballBall::OnBallHit);
	}
}

void ABasketballBall::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (BallMesh)
	{
		CachedPhysicsVelocity = BallMesh->GetPhysicsLinearVelocity();
	}

	AEMFChannelingPlateActor* Plate = CapturingPlate.Get();
	if (!Plate || !BallMesh)
	{
		UpdateHoopAssist(DeltaTime);
		return;
	}

	bHoopAssistActive = false;
	const FVector TargetLocation = GetCaptureTargetLocation(Plate);
	const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, CaptureFollowInterpSpeed);

	BallMesh->SetPhysicsLinearVelocity(BallMesh->GetPhysicsLinearVelocity() * CaptureVelocityDamping);
	BallMesh->SetPhysicsAngularVelocityInDegrees(BallMesh->GetPhysicsAngularVelocityInDegrees() * CaptureVelocityDamping);
	BallMesh->SetWorldLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	CachedPhysicsVelocity = BallMesh->GetPhysicsLinearVelocity();
}

void ABasketballBall::SetCapturedByPlate(AEMFChannelingPlateActor* Plate)
{
	if (!Plate || !bCanBeCaptured || !BallMesh)
	{
		return;
	}

	CapturingPlate = Plate;
	bCaptured = true;
	BallMesh->SetSimulatePhysics(true);
	BallMesh->SetEnableGravity(false);
	BallMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	BallMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

	if (bLogCapture)
	{
		UE_LOG(LogTemp, Log, TEXT("[BasketballBall] %s captured by %s"), *GetName(), *Plate->GetName());
	}
}

void ABasketballBall::ReleasedFromCapture()
{
	CapturingPlate.Reset();
	bCaptured = false;

	if (BallMesh)
	{
		BallMesh->SetEnableGravity(true);
	}
}

void ABasketballBall::LaunchBall(const FVector& AimDirection, float HoldTime, AActor* Thrower)
{
	if (!BallMesh)
	{
		return;
	}

	ReleasedFromCapture();

	if (Thrower)
	{
		SetOwner(Thrower);
		if (APawn* ThrowerPawn = Cast<APawn>(Thrower))
		{
			SetInstigator(ThrowerPawn);
		}
	}

	const FVector LaunchDirection = BuildLaunchDirection(AimDirection);
	const float LaunchSpeed = CalculateLaunchSpeed(HoldTime);
	LastThrowHoldTime = HoldTime;
	LastLaunchVelocity = LaunchDirection * LaunchSpeed;
	LastPreImpactVelocity = LastLaunchVelocity;
	LastImpactSpeed = 0.0f;
	bReturnBounceEligibleFlight = true;
	bReturnBounceConsumed = false;

	BallMesh->SetSimulatePhysics(true);
	BallMesh->SetEnableGravity(true);
	BallMesh->SetPhysicsLinearVelocity(LastLaunchVelocity, false);
	CachedPhysicsVelocity = LastLaunchVelocity;

	if (LaunchSpinVelocityChange > 0.0f)
	{
		const FVector SpinAxis = FVector::CrossProduct(FVector::UpVector, LaunchDirection).GetSafeNormal();
		if (!SpinAxis.IsNearlyZero())
		{
			BallMesh->AddAngularImpulseInDegrees(SpinAxis * LaunchSpinVelocityChange, NAME_None, true);
		}
	}

	OnBasketballBallLaunched.Broadcast(this, Thrower, HoldTime, LastLaunchVelocity);

	if (bLogCapture)
	{
		UE_LOG(LogTemp, Log, TEXT("[BasketballBall] %s launched by %s. Hold=%.2f Velocity=(%.0f, %.0f, %.0f)"),
			*GetName(),
			Thrower ? *Thrower->GetName() : TEXT("None"),
			HoldTime,
			LastLaunchVelocity.X,
			LastLaunchVelocity.Y,
			LastLaunchVelocity.Z);
	}
}

void ABasketballBall::StartHoopAssist(const FVector& TargetLocation, float Duration, float Strength, float VelocityBlend)
{
	if (!BallMesh || Duration <= 0.0f || Strength <= 0.0f || IsCapturedByPlate())
	{
		return;
	}

	bHoopAssistActive = true;
	HoopAssistTarget = TargetLocation;
	HoopAssistTimeRemaining = Duration;
	HoopAssistStrength = Strength;
	HoopAssistVelocityBlend = FMath::Clamp(VelocityBlend, 0.0f, 1.0f);
}

float ABasketballBall::CalculateLaunchSpeed(float HoldTime) const
{
	const float SafeMaxCharge = FMath::Max(0.01f, MaxThrowChargeTime);
	const float ChargeAlpha = FMath::Clamp(HoldTime / SafeMaxCharge, 0.0f, 1.0f);
	return FMath::Lerp(MinLaunchSpeed, MaxLaunchSpeed, ChargeAlpha);
}

bool ABasketballBall::IsCombatScoreReady() const
{
	return RequiredCombatScore <= 0 || CurrentCombatScore >= RequiredCombatScore;
}

float ABasketballBall::GetCombatScoreNormalized() const
{
	if (RequiredCombatScore <= 0)
	{
		return 1.0f;
	}

	return FMath::Clamp(static_cast<float>(CurrentCombatScore) / static_cast<float>(RequiredCombatScore), 0.0f, 1.0f);
}

void ABasketballBall::AddCombatScore(int32 Amount)
{
	if (Amount <= 0 || RequiredCombatScore <= 0)
	{
		return;
	}

	const int32 SafeRequiredScore = RequiredCombatScore;
	const int32 OldScore = CurrentCombatScore;
	CurrentCombatScore = FMath::Clamp(CurrentCombatScore + Amount, 0, SafeRequiredScore);

	if (CurrentCombatScore != OldScore)
	{
		const float NormalizedScore = GetCombatScoreNormalized();
		OnBasketballCombatScoreAdded.Broadcast(CurrentCombatScore, SafeRequiredScore, NormalizedScore);
		OnBasketballCombatScoreChanged.Broadcast(CurrentCombatScore, SafeRequiredScore, NormalizedScore);
	}
}

void ABasketballBall::ResetCombatScore()
{
	const int32 SafeRequiredScore = FMath::Max(0, RequiredCombatScore);
	const float ResetNormalizedScore = SafeRequiredScore == 0 ? 1.0f : 0.0f;
	if (CurrentCombatScore == 0)
	{
		OnBasketballCombatScoreChanged.Broadcast(CurrentCombatScore, SafeRequiredScore, ResetNormalizedScore);
		return;
	}

	CurrentCombatScore = 0;
	OnBasketballCombatScoreChanged.Broadcast(CurrentCombatScore, SafeRequiredScore, ResetNormalizedScore);
}

void ABasketballBall::ApplyBallSettings()
{
	if (!BallMesh)
	{
		return;
	}

	const float SafeDiameter = FMath::Max(10.0f, BallDiameter);
	constexpr float EngineSphereDiameter = 100.0f;
	const float UniformScale = SafeDiameter / EngineSphereDiameter;
	BallMesh->SetRelativeScale3D(FVector(UniformScale));

	BallMesh->BodyInstance.bUseCCD = bUseCCD;
	BallMesh->SetNotifyRigidBodyCollision(true);
	BallMesh->SetLinearDamping(LinearDamping);
	BallMesh->SetAngularDamping(AngularDamping);
	BallMesh->SetMassOverrideInKg(NAME_None, FMath::Max(0.1f, BallMassKg), true);
	BallMesh->SetPhysMaterialOverride(BallPhysicalMaterial);
	BallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BallMesh->SetGenerateOverlapEvents(true);
}

FVector ABasketballBall::GetCaptureTargetLocation(const AEMFChannelingPlateActor* Plate) const
{
	if (!Plate)
	{
		return GetActorLocation();
	}

	return Plate->GetActorLocation() + Plate->GetActorRotation().RotateVector(CaptureTargetLocalOffset);
}

FVector ABasketballBall::BuildLaunchDirection(const FVector& AimDirection) const
{
	FVector LaunchDirection = AimDirection.GetSafeNormal();
	if (LaunchDirection.IsNearlyZero())
	{
		LaunchDirection = GetActorForwardVector();
	}

	LaunchDirection.Z = FMath::Max(LaunchDirection.Z, LaunchUpwardBias);
	return LaunchDirection.GetSafeNormal();
}

bool ABasketballBall::QualifiesForReturnBounce(const FVector& PreImpactVelocity, const FVector& ImpactNormal,
	bool bCharacterImpact) const
{
	const float ImpactSpeed = PreImpactVelocity.Size();
	if (ImpactSpeed < MinBounceImpactSpeed)
	{
		return false;
	}

	// As in Air Mail, capsule normals are not useful for an arced throw into an NPC.
	if (bCharacterImpact)
	{
		return true;
	}

	const FVector SurfaceNormal = ImpactNormal.GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		return false;
	}

	const FVector VelocityDirection = PreImpactVelocity / ImpactSpeed;
	const float CosToNormal = FVector::DotProduct(-VelocityDirection, SurfaceNormal);
	const float MaxAngleToNormal = 90.0f - FMath::Clamp(MinBounceAngleDeg, 0.0f, 90.0f);
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(MaxAngleToNormal));
	return CosToNormal >= CosLimit;
}

AShooterCharacter* ABasketballBall::GetReturnTargetCharacter() const
{
	if (AShooterCharacter* Thrower = Cast<AShooterCharacter>(GetInstigator()))
	{
		return Thrower;
	}

	if (AShooterCharacter* Thrower = Cast<AShooterCharacter>(GetOwner()))
	{
		return Thrower;
	}

	// Nobody threw it (fresh ball, or the thrower left): bounce back to whoever is closest.
	return Cast<AShooterCharacter>(CoopPlayers::GetNearest(GetWorld(), GetActorLocation()));
}

bool ABasketballBall::ComputeReturnBounceVelocity(FVector& OutVelocity) const
{
	AShooterCharacter* ReturnTarget = GetReturnTargetCharacter();
	if (!ReturnTarget || !BallMesh)
	{
		return false;
	}

	FVector TargetLocation = ReturnTarget->GetActorLocation();
	if (UCameraComponent* Camera = ReturnTarget->GetFirstPersonCameraComponent())
	{
		TargetLocation = Camera->GetComponentLocation();
	}
	TargetLocation.Z += ReturnTargetHeightOffset;

	const FVector ToTarget = TargetLocation - BallMesh->GetComponentLocation();
	const float Distance = ToTarget.Size();
	if (Distance < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeReturnSpeed = FMath::Max(ReturnSpeed, 100.0f);
	OutVelocity = (ToTarget / Distance) * SafeReturnSpeed;
	const float FlightTime = Distance / SafeReturnSpeed;
	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.0f;
	OutVelocity.Z += 0.5f * -GravityZ * FlightTime;
	return true;
}

bool ABasketballBall::TryReturnBounce(AActor* OtherActor, const FHitResult& Hit, const FVector& PreImpactVelocity)
{
	if (!bEnableReturnBounce || bReturnBounceConsumed || !bReturnBounceEligibleFlight || !BallMesh)
	{
		return false;
	}

	AShooterCharacter* ReturnTarget = GetReturnTargetCharacter();
	if (OtherActor && (OtherActor == GetOwner() || OtherActor == GetInstigator() || OtherActor == ReturnTarget))
	{
		return false;
	}

	const bool bCharacterImpact = OtherActor && OtherActor->IsA<APawn>();
	if (!QualifiesForReturnBounce(PreImpactVelocity, Hit.ImpactNormal, bCharacterImpact))
	{
		return false;
	}

	FVector ReturnVelocity;
	if (!ComputeReturnBounceVelocity(ReturnVelocity))
	{
		return false;
	}

	bReturnBounceConsumed = true;
	bReturnBounceEligibleFlight = false;
	BallMesh->SetPhysicsLinearVelocity(ReturnVelocity, false);
	CachedPhysicsVelocity = ReturnVelocity;

	if (ReturnSpinSpeed > 0.0f)
	{
		const FVector SpinAxis = FVector::CrossProduct(FVector::UpVector, ReturnVelocity).GetSafeNormal();
		if (!SpinAxis.IsNearlyZero())
		{
			BallMesh->SetPhysicsAngularVelocityInDegrees(SpinAxis * ReturnSpinSpeed, false);
		}
	}

	if (bLogReturnBounces)
	{
		UE_LOG(LogTemp, Log, TEXT("[BasketballBall] %s returned after hitting %s. PreImpactSpeed=%.0f"),
			*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("surface"), PreImpactVelocity.Size());
	}

	return true;
}

void ABasketballBall::UpdateHoopAssist(float DeltaTime)
{
	if (!bHoopAssistActive || !BallMesh || DeltaTime <= 0.0f || IsCapturedByPlate())
	{
		return;
	}

	HoopAssistTimeRemaining -= DeltaTime;
	if (HoopAssistTimeRemaining <= 0.0f)
	{
		bHoopAssistActive = false;
		return;
	}

	const FVector CurrentVelocity = BallMesh->GetPhysicsLinearVelocity();
	const float CurrentSpeed = CurrentVelocity.Size();
	if (CurrentSpeed <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector ToTarget = (HoopAssistTarget - BallMesh->GetComponentLocation()).GetSafeNormal();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	const float AssistedSpeed = FMath::Clamp(CurrentSpeed, MinAssistSpeed, MaxAssistSpeed);
	const FVector DesiredVelocity = ToTarget * AssistedSpeed;
	const FVector SteeredVelocity = FMath::VInterpTo(CurrentVelocity, DesiredVelocity, DeltaTime, HoopAssistStrength);
	const FVector FinalVelocity = FMath::Lerp(SteeredVelocity, DesiredVelocity, HoopAssistVelocityBlend);
	BallMesh->SetPhysicsLinearVelocity(FinalVelocity, false);
	CachedPhysicsVelocity = FinalVelocity;
}

float ABasketballBall::CalculateImpactDamage(float ImpactSpeed) const
{
	const float SafeMinSpeed = FMath::Max(0.0f, MinDamageImpactSpeed);
	const float SafeFullSpeed = FMath::Max(SafeMinSpeed + 1.0f, FullDamageImpactSpeed);
	const float Alpha = FMath::Clamp((ImpactSpeed - SafeMinSpeed) / (SafeFullSpeed - SafeMinSpeed), 0.0f, 1.0f);
	return FMath::Lerp(MinImpactDamage, FullImpactDamage, Alpha);
}

float ABasketballBall::CalculateImpactStunDuration(float ImpactSpeed) const
{
	const float SafeMinSpeed = FMath::Max(0.0f, MinDamageImpactSpeed);
	const float SafeFullSpeed = FMath::Max(SafeMinSpeed + 1.0f, FullDamageImpactSpeed);
	const float Alpha = FMath::Clamp((ImpactSpeed - SafeMinSpeed) / (SafeFullSpeed - SafeMinSpeed), 0.0f, 1.0f);
	return FMath::Lerp(MinImpactStunDuration, FullImpactStunDuration, Alpha);
}

void ABasketballBall::AwardCombatScoreForHit(AShooterNPC* HitNPC, bool bKilled)
{
	if (bKilled)
	{
		AddCombatScore(CombatScoreOnKill);
		return;
	}

	if (bAwardCombatScoreOnNonLethalHit)
	{
		AddCombatScore(CombatScoreOnNonLethalHit);
	}
}

void ABasketballBall::OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!BallMesh || !OtherActor || OtherActor == this || IsCapturedByPlate())
	{
		return;
	}

	const FVector BallVelocity = BallMesh->GetPhysicsLinearVelocity();
	const FVector PreImpactVelocity = CachedPhysicsVelocity.SizeSquared() > BallVelocity.SizeSquared()
		? CachedPhysicsVelocity
		: BallVelocity;
	LastPreImpactVelocity = PreImpactVelocity;
	TryReturnBounce(OtherActor, Hit, PreImpactVelocity);

	AShooterNPC* HitNPC = Cast<AShooterNPC>(OtherActor);
	if (!HitNPC || HitNPC->IsDead())
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const TObjectKey<AActor> HitKey(HitNPC);
	if (float* LastHitTime = LastCombatHitTimes.Find(HitKey))
	{
		if (Now - *LastHitTime < PerTargetHitCooldown)
		{
			return;
		}
	}

	const FVector ToTarget = (HitNPC->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	const float DirectedImpactSpeed = FMath::Max(0.0f, FVector::DotProduct(PreImpactVelocity, ToTarget));
	const float ImpactSpeed = FMath::Max(DirectedImpactSpeed, PreImpactVelocity.Size() * 0.35f);
	LastImpactSpeed = ImpactSpeed;
	if (ImpactSpeed < MinDamageImpactSpeed)
	{
		if (bLogCombatHits)
		{
			UE_LOG(LogTemp, Log, TEXT("[BasketballBall] Hit %s ignored: speed=%.0f below %.0f. PreVel=(%.0f, %.0f, %.0f) PostVel=(%.0f, %.0f, %.0f)"),
				*HitNPC->GetName(),
				ImpactSpeed,
				MinDamageImpactSpeed,
				PreImpactVelocity.X,
				PreImpactVelocity.Y,
				PreImpactVelocity.Z,
				BallVelocity.X,
				BallVelocity.Y,
				BallVelocity.Z);
		}
		return;
	}
	LastCombatHitTimes.Add(HitKey, Now);

	const float Damage = CalculateImpactDamage(ImpactSpeed);
	const float StunDuration = CalculateImpactStunDuration(ImpactSpeed);
	const bool bWasDead = HitNPC->IsDead();

	TSubclassOf<UDamageType> DamageTypeClass = ImpactDamageType;
	if (!DamageTypeClass)
	{
		DamageTypeClass = UDamageType_Basketball::StaticClass();
	}
	FPointDamageEvent DamageEvent(Damage, Hit, PreImpactVelocity.GetSafeNormal(), DamageTypeClass);

	AController* DamageInstigator = nullptr;
	if (APawn* InstigatorPawn = GetInstigator())
	{
		DamageInstigator = InstigatorPawn->GetController();
	}
	const float AppliedDamage = HitNPC->TakeDamage(Damage, DamageEvent, DamageInstigator, this);

	const bool bKilled = !bWasDead && HitNPC->IsDead();
	if (!bKilled && StunDuration > 0.0f)
	{
		HitNPC->ApplyExplosionStun(StunDuration);
	}

	float AppliedHealing = 0.0f;
	if (AppliedDamage > 0.0f && HealFractionOfDamageDealt > 0.0f)
	{
		AShooterCharacter* Thrower = Cast<AShooterCharacter>(GetInstigator());
		if (!Thrower)
		{
			Thrower = Cast<AShooterCharacter>(GetOwner());
		}

		if (Thrower && !Thrower->IsDead())
		{
			const float RequestedHealing = AppliedDamage * HealFractionOfDamageDealt;
			const float MissingHealth = FMath::Max(0.0f, Thrower->GetMaxHP() - Thrower->GetCurrentHP());
			AppliedHealing = FMath::Min(RequestedHealing, MissingHealth);
			Thrower->RestoreHealth(RequestedHealing);
		}
	}

	AwardCombatScoreForHit(HitNPC, bKilled);

	if (bLogCombatHits)
	{
		UE_LOG(LogTemp, Log, TEXT("[BasketballBall] Hit %s speed=%.0f damage=%.1f applied=%.1f healed=%.1f stun=%.2f killed=%d score=%d/%d"),
			*HitNPC->GetName(), ImpactSpeed, Damage, AppliedDamage, AppliedHealing, StunDuration,
			bKilled ? 1 : 0, CurrentCombatScore, RequiredCombatScore);
	}
}
