// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerDeathSequenceComponent.h"
#include "Coop/CoopPlayers.h"

#include "AI/ShooterNPC.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Field/FieldSystemObjects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "HAL/IConsoleManager.h"

UPlayerDeathSequenceComponent::UPlayerDeathSequenceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPlayerDeathSequenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupTransientEffects();
	Super::EndPlay(EndPlayReason);
}

float UPlayerDeathSequenceComponent::GetTotalDuration() const
{
	return FMath::Max(0.1f, ExplosionDelay) + FMath::Max(0.0f, PostExplosionHoldDuration)
		+ FMath::Max(0.05f, FadeDuration);
}

bool UPlayerDeathSequenceComponent::StartDeathSequence()
{
	if (!bEnabled || bSequenceActive)
	{
		return false;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	if (!Character || !GetWorld())
	{
		return false;
	}

	OwnerCharacter = Character;
	ElapsedTime = 0.0f;
	bExplosionTriggered = false;
	bFadeTriggered = false;
	bSequenceActive = true;
	CurrentCameraRoll = 0.0f;
	CurrentCameraRollVelocity = CameraRollVelocity;

	GatherEnemies();
	SpawnDeathCamera();

	if (USkeletalMeshComponent* WorldMesh = Character->GetMesh())
	{
		WorldMesh->SetOwnerNoSee(false);
		WorldMesh->SetHiddenInGame(false, true);
		WorldMesh->SetVisibility(true, true);

		if (PlayerDeathMontage)
		{
			if (UAnimInstance* AnimInstance = WorldMesh->GetAnimInstance())
			{
				AnimInstance->Montage_Play(PlayerDeathMontage);
			}
		}
	}

	if (WindupVFX)
	{
		ActiveWindupVFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
			WindupVFX, Character->GetRootComponent(), NAME_None, FVector::ZeroVector,
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
	}

	if (WindupSound)
	{
		ActiveWindupAudio = UGameplayStatics::SpawnSoundAttached(WindupSound, Character->GetRootComponent());
	}

	SetComponentTickEnabled(true);
	OnSequenceStarted.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("[PLAYER_DEATH_SEQUENCE] STARTED: enemies=%d explosion=%.2fs total=%.2fs"),
		CapturedEnemies.Num(), ExplosionDelay, GetTotalDuration());
	return true;
}

void UPlayerDeathSequenceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bSequenceActive || !OwnerCharacter.IsValid())
	{
		SetComponentTickEnabled(false);
		return;
	}

	ElapsedTime += DeltaTime;
	UpdateEnemyPull(DeltaTime);
	UpdateDeathCamera(DeltaTime);

	if (!bExplosionTriggered && ElapsedTime >= FMath::Max(0.1f, ExplosionDelay))
	{
		TriggerSynchronizedExplosion();
	}

	const float FadeStartTime = FMath::Max(0.1f, ExplosionDelay) + FMath::Max(0.0f, PostExplosionHoldDuration);
	if (!bFadeTriggered && ElapsedTime >= FadeStartTime)
	{
		StartFade();
	}

	if (ElapsedTime >= GetTotalDuration())
	{
		bSequenceActive = false;
		SetComponentTickEnabled(false);
	}
}

void UPlayerDeathSequenceComponent::GatherEnemies()
{
	CapturedEnemies.Reset();
	AShooterCharacter* Character = OwnerCharacter.Get();
	if (!Character || EnemyCaptureRadius <= 0.0f)
	{
		return;
	}

	const FVector PlayerLocation = Character->GetActorLocation();
	const float RadiusSq = FMath::Square(EnemyCaptureRadius);

	for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
	{
		AShooterNPC* NPC = *It;
		if (!IsValid(NPC) || NPC->IsDead())
		{
			continue;
		}

		const FVector Delta = NPC->GetActorLocation() - PlayerLocation;
		const float DistanceSq = Delta.SizeSquared();
		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		FEnemyTarget& Target = CapturedEnemies.AddDefaulted_GetRef();
		Target.NPC = NPC;
		Target.StartLocation = NPC->GetActorLocation();
		Target.StartDistanceSq = DistanceSq;
		Target.HoldDirection = Delta.GetSafeNormal2D();
		if (Target.HoldDirection.IsNearlyZero())
		{
			// Golden-angle spacing keeps coincident enemies from choosing the same hold direction.
			const float Angle = CapturedEnemies.Num() * 2.39996323f;
			Target.HoldDirection = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		}
	}

	CapturedEnemies.Sort([](const FEnemyTarget& A, const FEnemyTarget& B)
	{
		return A.StartDistanceSq < B.StartDistanceSq;
	});

	if (MaximumCapturedEnemies > 0 && CapturedEnemies.Num() > MaximumCapturedEnemies)
	{
		CapturedEnemies.SetNum(MaximumCapturedEnemies);
	}

	for (FEnemyTarget& Target : CapturedEnemies)
	{
		if (AShooterNPC* NPC = Target.NPC.Get())
		{
			NPC->EnterCapturedState(EnemyCaptureMontageOverride);
			NPC->SetActorEnableCollision(false);
			if (UCharacterMovementComponent* Move = NPC->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
				Move->DisableMovement();
			}

			if (EnemyCaptureVFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAttached(
					EnemyCaptureVFX, NPC->GetRootComponent(), NAME_None, FVector::ZeroVector,
					FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
			}
		}
	}
}

void UPlayerDeathSequenceComponent::UpdateEnemyPull(float DeltaTime)
{
	AShooterCharacter* Character = OwnerCharacter.Get();
	if (!Character || bExplosionTriggered || ElapsedTime < EnemyPullStartDelay)
	{
		return;
	}

	const float PullDuration = FMath::Max(0.01f, ExplosionDelay - EnemyPullStartDelay);
	const float NormalizedTime = FMath::Clamp((ElapsedTime - EnemyPullStartDelay) / PullDuration, 0.0f, 1.0f);
	const float PullAlpha = EnemyPullCurve
		? FMath::Clamp(EnemyPullCurve->GetFloatValue(NormalizedTime), 0.0f, 1.0f)
		: FMath::SmoothStep(0.0f, 1.0f, NormalizedTime);
	const FVector PlayerLocation = Character->GetActorLocation();

	for (FEnemyTarget& Target : CapturedEnemies)
	{
		AShooterNPC* NPC = Target.NPC.Get();
		if (!NPC || NPC->IsDead())
		{
			continue;
		}

		const FVector HoldLocation = PlayerLocation + Target.HoldDirection * EnemyHoldRadius
			+ FVector(0.0f, 0.0f, EnemyHoldHeightOffset);
		const FVector NewLocation = FMath::Lerp(Target.StartLocation, HoldLocation, PullAlpha);
		NPC->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);

		if (bFacePlayerDuringPull)
		{
			FRotator DesiredRotation = (PlayerLocation - NewLocation).Rotation();
			DesiredRotation.Pitch = 0.0f;
			DesiredRotation.Roll = 0.0f;
			NPC->SetActorRotation(FMath::RInterpTo(NPC->GetActorRotation(), DesiredRotation, DeltaTime, 10.0f));
		}
	}
}

void UPlayerDeathSequenceComponent::SpawnDeathCamera()
{
	AShooterCharacter* Character = OwnerCharacter.Get();
	if (!Character || !GetWorld())
	{
		return;
	}

	FVector StartLocation = Character->GetActorLocation() + FVector(0.0f, 0.0f, 70.0f);
	FRotator ViewRotation = Character->GetActorRotation();
	if (UCameraComponent* FirstPersonCamera = Character->GetFirstPersonCameraComponent())
	{
		StartLocation = FirstPersonCamera->GetComponentLocation();
		ViewRotation = FirstPersonCamera->GetComponentRotation();
	}

	const FRotator HorizontalBasis(0.0f, ViewRotation.Yaw, 0.0f);
	StartLocation += HorizontalBasis.RotateVector(CameraStartLocalOffset);
	CameraVelocity = HorizontalBasis.RotateVector(CameraInitialVelocity);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Character;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	DeathCamera = GetWorld()->SpawnActor<ACameraActor>(StartLocation, ViewRotation, SpawnParams);
	if (!DeathCamera)
	{
		return;
	}

	if (UCameraComponent* CameraComponent = DeathCamera->GetCameraComponent())
	{
		const float ExistingFOV = Character->GetFirstPersonCameraComponent()
			? Character->GetFirstPersonCameraComponent()->FieldOfView : CameraMinimumFOV;
		CameraComponent->SetFieldOfView(FMath::Clamp(ExistingFOV, CameraMinimumFOV, CameraMaximumFOV));
	}

	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		PC->SetViewTargetWithBlend(DeathCamera, CameraBlendInDuration, VTBlend_Cubic);
	}
}

FVector UPlayerDeathSequenceComponent::CalculateCompositionFocus(float& OutBoundingRadius) const
{
	OutBoundingRadius = 0.0f;
	const AShooterCharacter* Character = OwnerCharacter.Get();
	if (!Character)
	{
		return FVector::ZeroVector;
	}

	TArray<FVector, TInlineAllocator<16>> Points;
	Points.Add(Character->GetActorLocation() + FVector(0.0f, 0.0f, CameraFocusHeightOffset));
	for (const FEnemyTarget& Target : CapturedEnemies)
	{
		// Keep the last NPC positions in the composition after the synchronized kill so the
		// camera does not tighten its FOV while their Geometry Collections are still scattering.
		if (const AShooterNPC* NPC = Target.NPC.Get())
		{
			Points.Add(NPC->GetActorLocation() + FVector(0.0f, 0.0f, CameraFocusHeightOffset * 0.5f));
		}
	}

	FVector Focus = FVector::ZeroVector;
	for (const FVector& Point : Points)
	{
		Focus += Point;
	}
	Focus /= FMath::Max(1, Points.Num());

	for (const FVector& Point : Points)
	{
		OutBoundingRadius = FMath::Max(OutBoundingRadius, FVector::Distance(Point, Focus));
	}
	OutBoundingRadius = FMath::Max(OutBoundingRadius, 90.0f);
	return Focus;
}

void UPlayerDeathSequenceComponent::UpdateDeathCamera(float DeltaTime)
{
	AShooterCharacter* Character = OwnerCharacter.Get();
	if (!DeathCamera || !Character || DeltaTime <= 0.0f)
	{
		return;
	}

	CameraVelocity += FVector(0.0f, 0.0f, GetWorld()->GetGravityZ() * CameraGravityScale) * DeltaTime;
	CameraVelocity *= FMath::Exp(-FMath::Max(0.0f, CameraLinearDrag) * DeltaTime);

	const FVector CurrentLocation = DeathCamera->GetActorLocation();
	const FVector DesiredLocation = CurrentLocation + CameraVelocity * DeltaTime;
	FVector NewLocation = DesiredLocation;

	if (CameraCollisionRadius > KINDA_SMALL_NUMBER)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerDeathCamera), false, Character);
		for (const FEnemyTarget& Target : CapturedEnemies)
		{
			if (AShooterNPC* NPC = Target.NPC.Get())
			{
				Params.AddIgnoredActor(NPC);
			}
		}

		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, CurrentLocation, DesiredLocation, FQuat::Identity,
			ECC_Camera, FCollisionShape::MakeSphere(CameraCollisionRadius), Params))
		{
			NewLocation = Hit.Location + Hit.Normal * 1.0f;
			const float IntoSurface = FVector::DotProduct(CameraVelocity, Hit.Normal);
			const FVector TangentialVelocity = FVector::VectorPlaneProject(CameraVelocity, Hit.Normal)
				* (1.0f - FMath::Clamp(CameraSurfaceFriction, 0.0f, 1.0f));
			const FVector BounceVelocity = Hit.Normal * FMath::Max(0.0f, -IntoSurface)
				* FMath::Clamp(CameraRestitution, 0.0f, 1.0f);
			CameraVelocity = TangentialVelocity + BounceVelocity;
		}
	}

	DeathCamera->SetActorLocation(NewLocation);

	float BoundingRadius = 0.0f;
	const FVector Focus = CalculateCompositionFocus(BoundingRadius);
	FRotator DesiredRotation = (Focus - NewLocation).Rotation();
	CurrentCameraRollVelocity *= FMath::Exp(-FMath::Max(0.0f, CameraAngularDrag) * DeltaTime);
	CurrentCameraRoll += CurrentCameraRollVelocity * DeltaTime;
	DesiredRotation.Roll = CurrentCameraRoll;
	DeathCamera->SetActorRotation(FMath::RInterpTo(
		DeathCamera->GetActorRotation(), DesiredRotation, DeltaTime, CameraLookAtInterpSpeed));

	if (UCameraComponent* CameraComponent = DeathCamera->GetCameraComponent())
	{
		const float Distance = FMath::Max(1.0f, FVector::Distance(NewLocation, Focus));
		const float RequiredFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(BoundingRadius / Distance))
			+ CameraCompositionFOVPadding;
		const float DesiredFOV = FMath::Clamp(RequiredFOV, CameraMinimumFOV, CameraMaximumFOV);
		CameraComponent->SetFieldOfView(FMath::FInterpTo(
			CameraComponent->FieldOfView, DesiredFOV, DeltaTime, CameraFOVInterpSpeed));
	}
}

void UPlayerDeathSequenceComponent::TriggerSynchronizedExplosion()
{
	if (bExplosionTriggered)
	{
		return;
	}
	bExplosionTriggered = true;

	AShooterCharacter* Character = OwnerCharacter.Get();
	if (!Character)
	{
		return;
	}

	if (ActiveWindupVFX)
	{
		ActiveWindupVFX->Deactivate();
		ActiveWindupVFX = nullptr;
	}
	if (ActiveWindupAudio)
	{
		ActiveWindupAudio->FadeOut(0.08f, 0.0f);
		ActiveWindupAudio = nullptr;
	}

	const FVector PlayerLocation = Character->GetActorLocation();
	if (PlayerExplosionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), PlayerExplosionVFX, PlayerLocation);
	}
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, PlayerLocation);
	}

	SpawnPlayerGeometryCollection();
	if (USkeletalMeshComponent* WorldMesh = Character->GetMesh())
	{
		WorldMesh->SetVisibility(false, true);
		WorldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	for (FEnemyTarget& Target : CapturedEnemies)
	{
		AShooterNPC* NPC = Target.NPC.Get();
		if (!NPC || NPC->IsDead())
		{
			continue;
		}

		const FVector NPCLocation = NPC->GetActorLocation();
		if (EnemyExplosionVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), EnemyExplosionVFX, NPCLocation);
		}
		if (EnemyExplosionSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, EnemyExplosionSound, NPCLocation);
		}
		NPC->TriggerCinematicDismemberment(Character, EnemyDismembermentImpulseMultiplier);
	}

	if (ExplosionCameraShake)
	{
		if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(ExplosionCameraShake, ExplosionCameraShakeScale);
			}
		}
	}

	OnSynchronizedExplosion.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[PLAYER_DEATH_SEQUENCE] EXPLODED: enemies=%d"), CapturedEnemies.Num());
}

void UPlayerDeathSequenceComponent::SpawnPlayerGeometryCollection()
{
	AShooterCharacter* Character = OwnerCharacter.Get();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Character || !Mesh || !PlayerDeathGeometryCollection || !GetWorld())
	{
		return;
	}

	const FTransform MeshTransform = Mesh->GetComponentTransform();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		MeshTransform.GetLocation(), MeshTransform.Rotator(), SpawnParams);
	if (!GCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GC = GCActor->GetGeometryCollectionComponent();
	if (!GC)
	{
		GCActor->Destroy();
		return;
	}

	GCActor->SetActorScale3D(Mesh->GetComponentScale());
	GC->SetCollisionProfileName(GibCollisionProfile);
	GC->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GC->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GC->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GC->SetRestCollection(PlayerDeathGeometryCollection);

	for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetNumMaterials(); ++MaterialIndex)
	{
		if (UMaterialInterface* Material = Mesh->GetMaterial(MaterialIndex))
		{
			GC->SetMaterial(MaterialIndex, Material);
		}
	}

	GC->SetSimulatePhysics(true);
	GC->RecreatePhysicsState();

	UUniformScalar* Strain = NewObject<UUniformScalar>(GCActor);
	Strain->Magnitude = 999999.0f;
	GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain, nullptr, Strain);

	URadialVector* LinearVelocity = NewObject<URadialVector>(GCActor);
	LinearVelocity->Magnitude = PlayerDismembermentImpulse;
	LinearVelocity->Position = MeshTransform.GetLocation();
	GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity, nullptr, LinearVelocity);

	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = PlayerDismembermentAngularImpulse;
	AngularVelocity->Position = MeshTransform.GetLocation();
	GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity, nullptr, AngularVelocity);

	GCActor->SetLifeSpan(PlayerGibLifetime);
}

void UPlayerDeathSequenceComponent::StartFade()
{
	if (bFadeTriggered)
	{
		return;
	}
	bFadeTriggered = true;

	if (AShooterCharacter* Character = OwnerCharacter.Get())
	{
		if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraFade(
					0.0f, 1.0f, FMath::Max(0.05f, FadeDuration), FadeColor, false, true);
			}
		}
	}

	OnFadeStarted.Broadcast();
}

void UPlayerDeathSequenceComponent::CleanupTransientEffects()
{
	if (ActiveWindupVFX)
	{
		ActiveWindupVFX->DeactivateImmediate();
		ActiveWindupVFX = nullptr;
	}
	if (ActiveWindupAudio)
	{
		ActiveWindupAudio->Stop();
		ActiveWindupAudio = nullptr;
	}
	if (DeathCamera)
	{
		DeathCamera->Destroy();
		DeathCamera = nullptr;
	}
	CapturedEnemies.Reset();
}

// Uses the real damage path so the test covers armor depletion, Die(), terminal-run cleanup,
// camera launch, enemy pull, synchronized dismemberment, fade and menu travel.
static FAutoConsoleCommandWithWorld GPolarityPlayerKillCommand(
	TEXT("polarity.player.kill"),
	TEXT("Set the local ShooterCharacter HP to zero through lethal damage and test the death sequence."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PLAYER_DEATH_SEQUENCE] polarity.player.kill: no world"));
			return;
		}

		// Console command: kills the player who typed it, i.e. this machine's.
		APlayerController* LocalPC = CoopPlayers::GetLocalController(World);
		AShooterCharacter* Character = LocalPC ? Cast<AShooterCharacter>(LocalPC->GetPawn()) : nullptr;
		if (!Character)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PLAYER_DEATH_SEQUENCE] polarity.player.kill: no ShooterCharacter"));
			return;
		}

		const float LethalDamage = FMath::Max(1.0f,
			Character->GetCurrentHP() + Character->GetCurrentArmor() + 1.0f);
		FDamageEvent DamageEvent;
		Character->TakeDamage(LethalDamage, DamageEvent, LocalPC, Character);
		UE_LOG(LogTemp, Log, TEXT("[PLAYER_DEATH_SEQUENCE] polarity.player.kill: applied %.1f lethal damage"),
			LethalDamage);
	}));
