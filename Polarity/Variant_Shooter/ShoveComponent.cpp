// ShoveComponent.cpp

#include "Variant_Shooter/ShoveComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Kismet/GameplayStatics.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

UShoveComponent::UShoveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);

	// The author's reactions (2026-10-14), MeleeReactions/Standard.
	const TCHAR* const Dir = TEXT("/Game/Variant_Shooter/Anims/MeleeReactions/Standard/");
	FrontReactions.Add(TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(FString(Dir) + TEXT("AM_React_Strong_F1.AM_React_Strong_F1"))));
	FrontReactions.Add(TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(FString(Dir) + TEXT("AM_React_Strong_F2.AM_React_Strong_F2"))));
	BackReactions.Add(TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(FString(Dir) + TEXT("AM_React_Strong_B1.AM_React_Strong_B1"))));
	BackReactions.Add(TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(FString(Dir) + TEXT("AM_React_Strong_B2.AM_React_Strong_B2"))));
}

ACharacter* UShoveComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

bool UShoveComponent::TryShove(AActor* Victim, const FVector& AttackerLocation, float Distance, float Duration, AController* Instigator)
{
	UShoveComponent* const Shove = Victim ? Victim->FindComponentByClass<UShoveComponent>() : nullptr;
	return Shove && Shove->ShoveOnAuthority(AttackerLocation, Distance, Duration, Instigator);
}

// ==================== The shove ====================

bool UShoveComponent::ShoveOnAuthority(const FVector& AttackerLocation, float Distance, float Duration, AController* Instigator)
{
	ACharacter* const Body = GetCharacter();
	UWorld* const World = GetWorld();
	UCharacterMovementComponent* const Movement = Body ? Body->GetCharacterMovement() : nullptr;
	if (!Body || !World || !Movement || !Body->HasAuthority())
	{
		return false;
	}
	if (const AShooterNPC* const NPC = Cast<AShooterNPC>(Body))
	{
		if (NPC->IsDead())
		{
			return true;
		}
		// Heavier soldiers go less far, as with every other knockback.
		Distance *= NPC->GetKnockbackDistanceMultiplier();
	}

	// A new shove replaces one still playing out.
	if (bActive)
	{
		Finish(/*bCutMontage*/ false, 0.0f);
	}

	Direction = (Body->GetActorLocation() - AttackerLocation).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = -Body->GetActorForwardVector().GetSafeNormal2D();
	}
	Duration = FMath::Max(Duration, 0.05f);
	Distance = FMath::Max(Distance, 0.0f);
	InstigatorController = Instigator;
	bIsPlayer = CoopPlayers::IsPlayer(Body);

	// A body with no controller (a dummy placed in a level) has its movement asleep: no mode, no
	// physics. A shove wakes it for good, so it can be pushed, fall and land.
	if (!Body->GetController() || Movement->MovementMode == MOVE_None)
	{
		Movement->bRunPhysicsWithNoController = true;
		if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetDefaultMovementMode();
		}
	}

	// Already in the air: no reaction to hang in, the push goes into the fall.
	if (Movement->IsFalling())
	{
		Body->LaunchCharacter(Direction * (Distance / Duration), true, false);
		return true;
	}

	// Front or back half: the attacker in front of the body or behind it.
	bFromFront = FVector::DotProduct(Body->GetActorForwardVector().GetSafeNormal2D(), -Direction) >= 0.0f;
	FacingYaw = (bFromFront ? -Direction : Direction).Rotation().Yaw;

	UAnimMontage* const Reaction = PickReaction(bFromFront);
	const bool bTurn = !bIsPlayer;

	if (bTurn)
	{
		Body->SetActorRotation(FRotator(0.0f, FacingYaw, 0.0f));
	}

	StartTime = World->GetTimeSeconds();
	PushEndTime = StartTime + Duration;
	EndTime = StartTime + FMath::Max(Duration, Reaction ? Reaction->GetPlayLength() : 0.0f);
	bWallHandled = false;
	bActive = true;
	SetComponentTickEnabled(true);

	if (Reaction)
	{
		Multicast_PlayReaction(Reaction, FacingYaw, bTurn, ReactionBlendIn);
	}
	StartPush(Direction, Distance, Duration, Reaction);

	// A soldier does nothing else for as long as this lasts.
	if (AShooterNPC* const NPC = Cast<AShooterNPC>(Body))
	{
		NPC->BeginShoveStun(EndTime - StartTime);
	}

	UE_LOG(LogTemp, Log, TEXT("[SHOVE_DEBUG] %s shoved %.0f cm over %.2f s from the %s, shaped by %s (its own root travel %.0f cm)"),
		*Body->GetName(), Distance, Duration, bFromFront ? TEXT("front") : TEXT("back"), *GetNameSafe(Reaction), RootTravel(Reaction));
	return true;
}

UAnimMontage* UShoveComponent::PickReaction(bool bFront) const
{
	const TArray<TSoftObjectPtr<UAnimMontage>>& List = bFront ? FrontReactions : BackReactions;
	TArray<UAnimMontage*, TInlineAllocator<4>> Loaded;
	for (const TSoftObjectPtr<UAnimMontage>& Soft : List)
	{
		if (UAnimMontage* const Montage = Soft.LoadSynchronous())
		{
			Loaded.Add(Montage);
		}
	}
	return Loaded.Num() > 0 ? Loaded[FMath::RandRange(0, Loaded.Num() - 1)] : nullptr;
}

float UShoveComponent::RootTravel(const UAnimMontage* Montage)
{
	if (!Montage || !Montage->HasRootMotion())
	{
		return 0.0f;
	}
	const FTransform Whole = Montage->ExtractRootMotionFromTrackRange(0.0f, Montage->GetPlayLength(), FAnimExtractContext());
	return static_cast<float>(Whole.GetTranslation().Size2D());
}

// ==================== On every machine ====================

void UShoveComponent::Multicast_PlayReaction_Implementation(UAnimMontage* Montage, float Yaw, bool bTurn, float BlendIn)
{
	PlayReactionLocal(Montage, Yaw, bTurn, BlendIn);
}

void UShoveComponent::PlayReactionLocal(UAnimMontage* Montage, float Yaw, bool bTurn, float BlendIn)
{
	ACharacter* const Body = GetCharacter();
	UAnimInstance* const Anim = Body && Body->GetMesh() ? Body->GetMesh()->GetAnimInstance() : nullptr;
	if (!Body || !Anim || !Montage)
	{
		return;
	}
	if (bTurn)
	{
		Body->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
	}

	// The reaction's root motion is extracted (the root stays on the capsule) and not applied: the
	// push carries its shape. Applied, it would override the push outright.
	if (!bRootMotionSaved)
	{
		SavedRootMotionMode = static_cast<uint8>(Anim->RootMotionMode.GetValue());
		bRootMotionSaved = true;
	}
	Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	Anim->Montage_PlayWithBlendIn(Montage, FAlphaBlendArgs(BlendIn));
}

void UShoveComponent::Multicast_EndReaction_Implementation(bool bCut, float BlendOut)
{
	EndReactionLocal(bCut, BlendOut);
}

void UShoveComponent::EndReactionLocal(bool bCut, float BlendOut)
{
	ACharacter* const Body = GetCharacter();
	UAnimInstance* const Anim = Body && Body->GetMesh() ? Body->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}
	if (bCut)
	{
		for (const TArray<TSoftObjectPtr<UAnimMontage>>* const List : { &FrontReactions, &BackReactions })
		{
			for (const TSoftObjectPtr<UAnimMontage>& Soft : *List)
			{
				if (UAnimMontage* const Montage = Soft.Get(); Montage && Anim->Montage_IsPlaying(Montage))
				{
					Anim->Montage_Stop(BlendOut, Montage);
				}
			}
		}
	}
	if (bRootMotionSaved)
	{
		Anim->SetRootMotionMode(static_cast<ERootMotionMode::Type>(SavedRootMotionMode));
		bRootMotionSaved = false;
	}
}

void UShoveComponent::Client_StartPush_Implementation(FVector Velocity, float Duration, UAnimMontage* Shape)
{
	StartPushLocal(Velocity, Duration, Shape);
}

void UShoveComponent::Client_StopPush_Implementation()
{
	StopPushLocal();
}

FVector UShoveComponent::StartPush(const FVector& Dir, float Distance, float Duration, UAnimMontage* Shape)
{
	ACharacter* const Body = GetCharacter();
	if (!Body || Distance <= 0.5f || Duration <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	// Distance = peak speed x duration x mean strength.
	float Average = 0.5f;
	GetPushShape(Shape, Average);
	const FVector Velocity = Dir * (Distance / (Duration * FMath::Max(Average, 0.05f)));

	StartPushLocal(Velocity, Duration, Shape);
	if (bIsPlayer && !Body->IsLocallyControlled())
	{
		Client_StartPush(Velocity, Duration, Shape);
	}
	return Velocity;
}

void UShoveComponent::StartPushLocal(const FVector& Velocity, float Duration, UAnimMontage* Shape)
{
	ACharacter* const Body = GetCharacter();
	UCharacterMovementComponent* const Movement = Body ? Body->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	StopPushLocal();

	float Average = 0.5f;
	const TSharedPtr<FRootMotionSource_ConstantForce> Push = MakeShared<FRootMotionSource_ConstantForce>();
	Push->InstanceName = TEXT("MeleeShove");
	Push->AccumulateMode = ERootMotionAccumulateMode::Additive;
	Push->Priority = 5;
	Push->Force = Velocity;
	Push->Duration = Duration;
	Push->StrengthOverTime = GetPushShape(Shape, Average);
	Push->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
	PushID = Movement->ApplyRootMotionSource(Push);
	UE_LOG(LogTemp, Log, TEXT("[SHOVE_DEBUG] %s push id %d force %s for %.2f s, mean strength %.2f, sources now %d"),
		*Body->GetName(), PushID, *Velocity.ToCompactString(), Duration, Average,
		Movement->CurrentRootMotion.RootMotionSources.Num() + Movement->CurrentRootMotion.PendingAddRootMotionSources.Num());
}

void UShoveComponent::StopPushLocal()
{
	ACharacter* const Body = GetCharacter();
	UCharacterMovementComponent* const Movement = Body ? Body->GetCharacterMovement() : nullptr;
	if (Movement && PushID != 0)
	{
		Movement->RemoveRootMotionSourceByID(PushID);
	}
	PushID = 0;
}

// ==================== While it plays out (server) ====================

void UShoveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* const Body = GetCharacter();
	const UWorld* const World = GetWorld();
	if (!bActive || !Body || !World || !Body->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (const UCharacterMovementComponent* const Probe = Body->GetCharacterMovement())
	{
		UE_LOG(LogTemp, Log, TEXT("[SHOVE_DEBUG] tick %s loc %s vel %s mode %d sources %d additive %d animRM %d tick %d"),
			*Body->GetName(), *Body->GetActorLocation().ToCompactString(), *Probe->Velocity.ToCompactString(),
			static_cast<int32>(Probe->MovementMode.GetValue()), Probe->CurrentRootMotion.RootMotionSources.Num(),
			Probe->CurrentRootMotion.HasAdditiveVelocity() ? 1 : 0, Probe->HasAnimRootMotion() ? 1 : 0, Probe->IsComponentTickEnabled() ? 1 : 0);
	}

	// Off the ground: cut the reaction, fall with what it has.
	if (const UCharacterMovementComponent* const Movement = Body->GetCharacterMovement(); Movement && Movement->IsFalling())
	{
		UE_LOG(LogTemp, Log, TEXT("[SHOVE_DEBUG] %s left the ground, falling"), *Body->GetName());
		Finish(/*bCutMontage*/ true, AirborneBlendOut);
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (!bWallHandled && Now < PushEndTime)
	{
		FHitResult Wall;
		if (FindWallAhead(Wall))
		{
			OnWallSlam(Wall);
			return;
		}
	}

	if (Now >= EndTime)
	{
		Finish(/*bCutMontage*/ false, 0.0f);
	}
}

bool UShoveComponent::FindWallAhead(FHitResult& OutHit) const
{
	const ACharacter* const Body = GetCharacter();
	const UCapsuleComponent* const Capsule = Body ? Body->GetCapsuleComponent() : nullptr;
	const UWorld* const World = GetWorld();
	if (!Capsule || !World)
	{
		return false;
	}

	const FVector Start = Body->GetActorLocation();
	const FVector End = Start + Direction * WallProbeDistance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoveWall), false, Body);
	FCollisionResponseParams Responses;
	Capsule->InitSweepCollisionParams(Params, Responses);

	// A little thinner than the body, so the floor it stands on is not taken for a wall.
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() * 0.9f,
		Capsule->GetScaledCapsuleHalfHeight() * 0.8f);
	if (!World->SweepSingleByChannel(OutHit, Start, End, Body->GetActorQuat(), Capsule->GetCollisionObjectType(), Shape, Params, Responses))
	{
		return false;
	}

	// A wall: something standing across the push, not a body and not a slope it walks up.
	return OutHit.GetActor() && !OutHit.GetActor()->IsA<APawn>()
		&& OutHit.ImpactNormal.Z < 0.7f && FVector::DotProduct(OutHit.ImpactNormal.GetSafeNormal2D(), -Direction) > 0.5f;
}

void UShoveComponent::OnWallSlam(const FHitResult& Wall)
{
	ACharacter* const Body = GetCharacter();
	const UWorld* const World = GetWorld();
	if (!Body || !World)
	{
		return;
	}
	bWallHandled = true;

	StopPushLocal();
	if (bIsPlayer && !Body->IsLocallyControlled())
	{
		Client_StopPush();
	}

	if (WallSlamDamage > 0.0f)
	{
		UGameplayStatics::ApplyDamage(Body, WallSlamDamage, InstigatorController.Get(), Wall.GetActor(), UDamageType::StaticClass());
	}

	// The opposite reaction, facing as it is, and a push off the wall as long as its own root travel.
	bFromFront = !bFromFront;
	if (UAnimMontage* const Bounce = PickReaction(bFromFront))
	{
		Multicast_PlayReaction(Bounce, FacingYaw, !bIsPlayer, ReactionBlendIn);
		const float Length = Bounce->GetPlayLength();
		StartPush(-Direction, RootTravel(Bounce), Length, Bounce);
		PushEndTime = World->GetTimeSeconds();          // no second wall check on the way back
		EndTime = World->GetTimeSeconds() + Length;
		if (AShooterNPC* const NPC = Cast<AShooterNPC>(Body))
		{
			NPC->BeginShoveStun(Length);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[SHOVE_DEBUG] %s hit the wall %s: %.0f damage, bounce"),
		*Body->GetName(), *GetNameSafe(Wall.GetActor()), WallSlamDamage);
}

void UShoveComponent::Finish(bool bCutMontage, float BlendOut)
{
	ACharacter* const Body = GetCharacter();
	bActive = false;
	SetComponentTickEnabled(false);
	StopPushLocal();
	if (!Body)
	{
		return;
	}
	if (bIsPlayer && !Body->IsLocallyControlled())
	{
		Client_StopPush();
	}
	Multicast_EndReaction(bCutMontage, BlendOut);
}

UCurveFloat* UShoveComponent::GetPushShape(const UAnimMontage* Montage, float& OutAverage)
{
	// Built once per montage and kept for the session.
	static TMap<TWeakObjectPtr<const UAnimMontage>, TPair<TStrongObjectPtr<UCurveFloat>, float>> Shapes;
	static TStrongObjectPtr<UCurveFloat> Fade;

	if (Montage && Montage->HasRootMotion())
	{
		if (const TPair<TStrongObjectPtr<UCurveFloat>, float>* const Known = Shapes.Find(Montage); Known && Known->Key.IsValid())
		{
			OutAverage = Known->Value;
			return Known->Key.Get();
		}

		// The root's speed along its overall travel, sampled across the montage, peak 1.
		const float Length = Montage->GetPlayLength();
		const FVector Overall = Montage->ExtractRootMotionFromTrackRange(0.0f, Length, FAnimExtractContext()).GetTranslation().GetSafeNormal2D();
		constexpr int32 Samples = 24;
		TArray<float, TInlineAllocator<Samples>> Speeds;
		float Peak = 0.0f;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float From = Length * Index / Samples;
			const float To = Length * (Index + 1) / Samples;
			const FVector Step = Montage->ExtractRootMotionFromTrackRange(From, To, FAnimExtractContext()).GetTranslation();
			const float Speed = FMath::Max(static_cast<float>(FVector::DotProduct(Step, Overall)), 0.0f);
			Speeds.Add(Speed);
			Peak = FMath::Max(Peak, Speed);
		}

		if (Peak > KINDA_SMALL_NUMBER && !Overall.IsNearlyZero())
		{
			UCurveFloat* const Curve = NewObject<UCurveFloat>(GetTransientPackage());
			float Sum = 0.0f;
			for (int32 Index = 0; Index < Samples; ++Index)
			{
				const float Strength = Speeds[Index] / Peak;
				Curve->FloatCurve.AddKey((Index + 0.5f) / Samples, Strength);
				Sum += Strength;
			}
			OutAverage = Sum / Samples;
			Shapes.Add(Montage, TPair<TStrongObjectPtr<UCurveFloat>, float>(TStrongObjectPtr<UCurveFloat>(Curve), OutAverage));
			return Curve;
		}
	}

	// No root motion to take the shape from: from full strength down to nothing, linearly.
	if (!Fade.IsValid())
	{
		Fade.Reset(NewObject<UCurveFloat>(GetTransientPackage(), TEXT("ShoveFadeCurve")));
		Fade->FloatCurve.AddKey(0.0f, 1.0f);
		Fade->FloatCurve.AddKey(1.0f, 0.0f);
	}
	OutAverage = 0.5f;
	return Fade.Get();
}
