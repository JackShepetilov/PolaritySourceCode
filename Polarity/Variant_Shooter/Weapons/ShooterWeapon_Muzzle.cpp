// ShooterWeapon_Muzzle.cpp
// The muzzle attachment: recoil multipliers on the PRAS asset, and the two hit effects (stagger,
// ricochet). Docs/Muzzle_Attachment_Plan_2026-10-01.md.
//
// Members of AShooterWeapon kept out of ShooterWeapon.cpp, which is long enough already.
//
// WHO DECIDES WHAT
//  - Recoil: every machine rebuilds its own copy of the PRAS asset from the replicated attachment
//    list. Only the owner's copy ever drives a camera.
//  - Stagger meter: the server. The hit is resolved where ApplyWeaponHit runs (a client's own
//    machine for its hitscan, the server for projectiles), and that machine reads the zone off the
//    bone, because only it has the real hit. A client sends the zone up; the server keeps the meter
//    and applies the stun or the slow, then tells everybody to play the feedback.
//  - Ricochet: the machine that resolved the hit traces the jumps, and their damage goes through
//    ApplyWeaponHit -> ApplyDamageToTarget like any other shot, which already reports a client's
//    damage to the server. The tracer is drawn here and sent to everybody else.

#include "ShooterWeapon.h"
#include "WeaponAttachmentDefinition.h"
#include "RecoilData.h"
#include "AI/PolarityTeams.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "Templates/UnrealTemplate.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarMuzzleDebug(
	TEXT("polarity.muzzle.debug"), 0,
	TEXT("1: log [MUZZLE_DEBUG] and draw every muzzle stagger hit: bone, zone, meter. Sphere colour: ")
	TEXT("green chest, blue centre, red not counted."));

namespace MuzzleDebug
{
	static bool On() { return CVarMuzzleDebug.GetValueOnGameThread() != 0; }

	static const TCHAR* ZoneName(EMuzzleHitZone Zone)
	{
		switch (Zone)
		{
		case EMuzzleHitZone::Chest: return TEXT("CHEST");
		case EMuzzleHitZone::Center: return TEXT("CENTRE");
		default: return TEXT("none");
		}
	}

	static void Screen(int32 Key, const FColor& Color, const FString& Text)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(Key, 3.0f, Color, Text);
		}
	}
}

// ==================== Recoil ====================

void AShooterWeapon::ApplyMuzzleModifiers()
{
	const UWeaponAttachmentDefinition* const Muzzle = GetAttachmentOfType(EWeaponAttachmentType::Muzzle);
	const float H = Muzzle ? Muzzle->HorizontalRecoilMultiplier : 1.0f;
	const float V = Muzzle ? Muzzle->VerticalRecoilMultiplier : 1.0f;
	const bool bChanges = ResolvedPackRecoilData
		&& (!FMath::IsNearlyEqual(H, 1.0f) || !FMath::IsNearlyEqual(V, 1.0f));

	URecoilData* Wanted = nullptr;
	if (bChanges)
	{
		const FControllerRecoilData& Base = ResolvedPackRecoilData->ControllerRecoil;

		// Already the right copy: OnRep runs for any attachment change, and a fresh copy every time
		// would re-arm PRAS in the middle of a burst for a scope going on.
		const bool bSame = MuzzlePackRecoilData
			&& MuzzlePackRecoilData->ControllerRecoil.HorizontalRecoilStep.Equals(Base.HorizontalRecoilStep * H)
			&& MuzzlePackRecoilData->ControllerRecoil.VerticalRecoilStep.Equals(Base.VerticalRecoilStep * V);
		if (bSame)
		{
			return;
		}

		// Always from the shared asset, never from the last copy, so swapping muzzles cannot compound.
		Wanted = DuplicateObject<URecoilData>(ResolvedPackRecoilData, this);
		Wanted->SetFlags(RF_Transient);
		Wanted->ControllerRecoil.HorizontalRecoilStep = Base.HorizontalRecoilStep * H;
		Wanted->ControllerRecoil.VerticalRecoilStep = Base.VerticalRecoilStep * V;
	}
	else if (!MuzzlePackRecoilData)
	{
		return;
	}

	MuzzlePackRecoilData = Wanted;

	UE_LOG(LogTemp, Log, TEXT("[MUZZLE] %s: recoil x%.2f horizontal, x%.2f vertical (%s)."),
		*GetName(), H, V, Wanted ? TEXT("private copy") : TEXT("shared asset"));

	// The character compares pointers and re-arms PRAS when it changes; it only asks on equip, so a
	// muzzle mounted on the gun in hand has to say so.
	if (AShooterCharacter* const Character = Cast<AShooterCharacter>(PawnOwner))
	{
		if (Character->GetCurrentWeapon() == this)
		{
			Character->RefreshPackRecoil(this);
		}
	}
}

// ==================== Hits ====================

void AShooterWeapon::ApplyMuzzleHit(const FHitResult& Hit, AActor* HitActor, float ActualDamage,
	float ShotDamage, const FVector& HitDirection, TSubclassOf<UDamageType> DamageType)
{
	if (bApplyingRicochet || !HitActor || !PawnOwner)
	{
		return;
	}

	const UWeaponAttachmentDefinition* const Muzzle = GetAttachmentOfType(EWeaponAttachmentType::Muzzle);
	if (!Muzzle || Muzzle->MuzzleEffect == EMuzzleEffect::None)
	{
		return;
	}

	if (!Cast<AShooterNPC>(HitActor) || !PolarityTeams::AreHostile(PawnOwner, HitActor))
	{
		return;
	}

	switch (Muzzle->MuzzleEffect)
	{
	case EMuzzleEffect::Stagger:
	{
		// Damage that a shield ate does not count: the meter is about rounds getting through.
		const EMuzzleHitZone Zone = ClassifyMuzzleHitZone(Muzzle, Hit, HitActor);
		if (MuzzleDebug::On())
		{
			const FColor Color = ActualDamage <= 0.0f || Zone == EMuzzleHitZone::None ? FColor::Red
				: Zone == EMuzzleHitZone::Chest ? FColor::Green : FColor::Blue;
			DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 4.0f, 8, Color, false, 3.0f);
			DrawDebugString(GetWorld(), Hit.ImpactPoint + FVector(0, 0, 8), Hit.BoneName.ToString(), nullptr, Color, 3.0f);
			UE_LOG(LogTemp, Log, TEXT("[MUZZLE_DEBUG] hit %s bone=%s comp=%s zone=%s dmg=%.1f%s"),
				*HitActor->GetName(), *Hit.BoneName.ToString(), *GetNameSafe(Hit.GetComponent()),
				MuzzleDebug::ZoneName(Zone), ActualDamage, ActualDamage <= 0.0f ? TEXT(" (no damage: not counted)") : TEXT(""));
			MuzzleDebug::Screen(-1, Color, FString::Printf(TEXT("[MUZZLE] %s  bone %s  -> %s  %.0f dmg"),
				*HitActor->GetName(), *Hit.BoneName.ToString(), MuzzleDebug::ZoneName(Zone), ActualDamage));
		}
		if (ActualDamage <= 0.0f || Zone == EMuzzleHitZone::None)
		{
			return;
		}
		if (HasAuthority())
		{
			ResolveMuzzleStagger(HitActor, Zone, ActualDamage);
		}
		else
		{
			Server_ReportMuzzleHit(HitActor, static_cast<uint8>(Zone), ActualDamage);
		}
		break;
	}
	case EMuzzleEffect::Ricochet:
		RunRicochet(Muzzle, Hit, HitActor, ShotDamage * Muzzle->RicochetDamageFraction, DamageType);
		break;
	default:
		break;
	}
}

EMuzzleHitZone AShooterWeapon::ClassifyMuzzleHitZone(const UWeaponAttachmentDefinition* Muzzle,
	const FHitResult& Hit, const AActor* HitActor) const
{
	if (!Muzzle || !HitActor)
	{
		return EMuzzleHitZone::None;
	}

	// Has a chest at all? Then only the chest counts, arms and legs and head do nothing.
	const ACharacter* const AsCharacter = Cast<ACharacter>(HitActor);
	const USkeletalMeshComponent* const Mesh = AsCharacter
		? AsCharacter->GetMesh()
		: HitActor->FindComponentByClass<USkeletalMeshComponent>();
	bool bHasChest = false;
	if (Mesh)
	{
		for (const FName& Bone : Muzzle->StaggerBoneNames)
		{
			if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
			{
				bHasChest = true;
				break;
			}
		}
	}
	if (bHasChest)
	{
		return Muzzle->StaggerBoneNames.Contains(Hit.BoneName) ? EMuzzleHitZone::Chest : EMuzzleHitZone::None;
	}

	// No chest (a drone, the carrier, the tank): how close to the middle of the part that was hit
	// did the shot's LINE pass. The impact point itself always sits on the surface, so measuring to
	// it would call every hit on a round body "the edge".
	const UPrimitiveComponent* const Part = Hit.GetComponent();
	const FBox Box = (Part && Part->GetOwner() == HitActor)
		? Part->Bounds.GetBox()
		: HitActor->GetComponentsBoundingBox(/*bNonColliding*/ false);
	if (!Box.IsValid)
	{
		return EMuzzleHitZone::None;
	}
	const FVector Extent = Box.GetExtent();
	const float HalfSize = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
	if (HalfSize <= KINDA_SMALL_NUMBER)
	{
		return EMuzzleHitZone::None;
	}

	const FVector LineDir = (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal();
	const float Miss = LineDir.IsNearlyZero()
		? FVector::Dist(Hit.ImpactPoint, Box.GetCenter())
		: FMath::PointDistToLine(Box.GetCenter(), LineDir, Hit.ImpactPoint);

	return Miss <= HalfSize * Muzzle->CenterRadiusFraction ? EMuzzleHitZone::Center : EMuzzleHitZone::None;
}

void AShooterWeapon::Server_ReportMuzzleHit_Implementation(AActor* Target, uint8 Zone, float Damage)
{
	// Same loose checks as Server_ReportDamage: a hit that cannot happen is dropped, a laggy honest
	// one is not.
	if (!IsValid(Target) || !PawnOwner || Damage <= 0.0f
		|| Zone == static_cast<uint8>(EMuzzleHitZone::None) || Zone > static_cast<uint8>(EMuzzleHitZone::Center))
	{
		return;
	}
	static constexpr float RangeMarginCm = 500.0f;
	if (FVector::Dist(PawnOwner->GetActorLocation(), Target->GetActorLocation()) > GetMaxHitscanRange() + RangeMarginCm)
	{
		return;
	}
	ResolveMuzzleStagger(Target, static_cast<EMuzzleHitZone>(Zone), FMath::Min(Damage, GetMaxReportedSingleHitDamage()));
}

void AShooterWeapon::ResolveMuzzleStagger(AActor* Target, EMuzzleHitZone Zone, float Damage)
{
	AShooterNPC* const NPC = Cast<AShooterNPC>(Target);
	const UWeaponAttachmentDefinition* const Muzzle = GetAttachmentOfType(EWeaponAttachmentType::Muzzle);
	if (!HasAuthority() || !NPC || NPC->IsDead() || !Muzzle || Muzzle->MuzzleEffect != EMuzzleEffect::Stagger)
	{
		return;
	}

	const bool bChest = Zone == EMuzzleHitZone::Chest;
	TMap<TWeakObjectPtr<AActor>, FMuzzleMeter>& Meters = bChest ? MuzzleChestMeters : MuzzleCenterMeters;

	// Dead and pooled enemies leave stale keys behind; sweep now and then rather than every hit.
	if (Meters.Num() > 32)
	{
		for (auto It = Meters.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	const float Now = GetWorld()->GetTimeSeconds();
	FMuzzleMeter& Meter = Meters.FindOrAdd(Target);
	if (Now < Meter.ImmuneUntil)
	{
		if (MuzzleDebug::On())
		{
			UE_LOG(LogTemp, Log, TEXT("[MUZZLE_DEBUG] %s immune for %.1f s more"), *NPC->GetName(), Meter.ImmuneUntil - Now);
			MuzzleDebug::Screen(static_cast<int32>(NPC->GetUniqueID()), FColor::Orange,
				FString::Printf(TEXT("[MUZZLE] %s IMMUNE %.1f s"), *NPC->GetName(), Meter.ImmuneUntil - Now));
		}
		return;
	}
	if (Now - Meter.LastHitTime > Muzzle->StaggerMemory)
	{
		if (MuzzleDebug::On() && Meter.Damage > 0.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("[MUZZLE_DEBUG] %s meter forgot %.0f (%.1f s since last hit)"),
				*NPC->GetName(), Meter.Damage, Now - Meter.LastHitTime);
		}
		Meter.Damage = 0.0f;
	}
	Meter.Damage += Damage;
	Meter.LastHitTime = Now;

	const float Threshold = bChest ? Muzzle->StaggerDamageThreshold : Muzzle->CenterSlowDamageThreshold;
	if (Meter.Damage < Threshold)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MUZZLE] %s: %s meter on %s %.0f / %.0f"), *GetName(),
			bChest ? TEXT("chest") : TEXT("centre"), *NPC->GetName(), Meter.Damage, Threshold);
		if (MuzzleDebug::On())
		{
			UE_LOG(LogTemp, Log, TEXT("[MUZZLE_DEBUG] %s %s meter %.0f / %.0f"), *NPC->GetName(),
				bChest ? TEXT("chest") : TEXT("centre"), Meter.Damage, Threshold);
			MuzzleDebug::Screen(static_cast<int32>(NPC->GetUniqueID()), FColor::Yellow,
				FString::Printf(TEXT("[MUZZLE] %s %s meter %.0f / %.0f"), *NPC->GetName(),
					bChest ? TEXT("chest") : TEXT("centre"), Meter.Damage, Threshold));
		}
		return;
	}
	Meter.Damage = 0.0f;

	if (bChest)
	{
		Meter.ImmuneUntil = Now + Muzzle->StaggerImmunityTime;
		NPC->ApplyExplosionStun(Muzzle->StaggerStunDuration, Muzzle->StaggerStunMontage);
		Multicast_PlayMuzzleProc(NPC, 0);
		UE_LOG(LogTemp, Log, TEXT("[MUZZLE] %s: stunned %s for %.1f s."), *GetName(), *NPC->GetName(),
			Muzzle->StaggerStunDuration);
	}
	else
	{
		NPC->ApplyTimedSlow(this, Muzzle->CenterSlowDuration, Muzzle->CenterSlowMoveMultiplier,
			Muzzle->CenterSlowTurnMultiplier, Muzzle->CenterSlowFlyerTimeMultiplier, Muzzle->CenterSlowOverlay);
		Multicast_PlayMuzzleProc(NPC, 1);
		UE_LOG(LogTemp, Log, TEXT("[MUZZLE] %s: slowed %s for %.1f s."), *GetName(), *NPC->GetName(),
			Muzzle->CenterSlowDuration);
	}
}

void AShooterWeapon::Multicast_PlayMuzzleProc_Implementation(AActor* Target, uint8 Kind)
{
	PlayMuzzleProcLocally(Target, Kind);
}

void AShooterWeapon::PlayMuzzleProcLocally(AActor* Target, uint8 Kind)
{
	const UWorld* const World = GetWorld();
	const UWeaponAttachmentDefinition* const Muzzle = GetAttachmentOfType(EWeaponAttachmentType::Muzzle);
	if (!World || World->GetNetMode() == NM_DedicatedServer || !IsValid(Target) || !Muzzle)
	{
		return;
	}

	const bool bStun = Kind == 0;
	if (Muzzle->StaggerFX && Target->GetRootComponent())
	{
		if (UNiagaraComponent* const FX = UNiagaraFunctionLibrary::SpawnSystemAttached(Muzzle->StaggerFX,
			Target->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true))
		{
			FX->SetColorParameter(FName("Color"), bStun ? FLinearColor(1.0f, 0.25f, 0.1f) : FLinearColor(0.3f, 0.6f, 1.0f));
		}
	}
	if (USoundBase* const Sound = bStun ? Muzzle->StaggerSound.Get() : Muzzle->CenterSlowSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Target->GetActorLocation());
	}
}

// ==================== Ricochet ====================

void AShooterWeapon::RunRicochet(const UWeaponAttachmentDefinition* Muzzle, const FHitResult& FirstHit,
	AActor* FirstTarget, float JumpDamage, TSubclassOf<UDamageType> DamageType)
{
	UWorld* const World = GetWorld();
	if (!World || !Muzzle || JumpDamage <= 0.0f || Muzzle->RicochetCount <= 0)
	{
		return;
	}

	// Every jump's own hit goes back through ApplyWeaponHit, which would start a chain of its own.
	TGuardValue<bool> Guard(bApplyingRicochet, true);

	TArray<AActor*> Struck;
	Struck.Add(FirstTarget);
	FVector From = FirstHit.ImpactPoint;

	for (int32 Jump = 0; Jump < Muzzle->RicochetCount; ++Jump)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MuzzleRicochet), false, this);
		Params.AddIgnoredActor(PawnOwner);
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, From, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Muzzle->RicochetRange), Params);

		// Nearest enemy not yet hit by this round that the jump can actually reach.
		AShooterNPC* Best = nullptr;
		FVector BestPoint = FVector::ZeroVector;
		float BestDistSq = TNumericLimits<float>::Max();
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AShooterNPC* const Candidate = Cast<AShooterNPC>(Overlap.GetActor());
			if (!Candidate || Candidate->IsDead() || Struck.Contains(Candidate)
				|| !PolarityTeams::AreHostile(PawnOwner, Candidate))
			{
				continue;
			}
			const FVector Point = Candidate->GetComponentsBoundingBox(/*bNonColliding*/ false).GetCenter();
			const float DistSq = FVector::DistSquared(From, Point);
			if (DistSq >= BestDistSq)
			{
				continue;
			}

			FCollisionQueryParams SightParams(SCENE_QUERY_STAT(MuzzleRicochetSight), false, this);
			SightParams.AddIgnoredActor(PawnOwner);
			SightParams.AddIgnoredActor(Candidate);
			SightParams.AddIgnoredActors(Struck);
			if (World->LineTraceTestByChannel(From, Point, ECC_Visibility, SightParams))
			{
				continue;
			}

			Best = Candidate;
			BestPoint = Point;
			BestDistSq = DistSq;
		}

		if (!Best)
		{
			break;
		}

		// A real hit on the body for the bone and the surface, the way a trace of the gun gets one.
		const FVector Dir = (BestPoint - From).GetSafeNormal();
		const FVector TraceEnd = BestPoint + Dir * 200.0f;
		FCollisionQueryParams HitParams(SCENE_QUERY_STAT(MuzzleRicochetHit), false, this);
		HitParams.AddIgnoredActor(PawnOwner);
		HitParams.AddIgnoredActors(Struck);
		HitParams.bReturnPhysicalMaterial = true;
		FHitResult JumpHit;
		const bool bTraced = World->LineTraceSingleByObjectType(JumpHit, From, TraceEnd,
			FCollisionObjectQueryParams(ECC_Pawn), HitParams);
		if (!bTraced || JumpHit.GetActor() != Best)
		{
			JumpHit = FHitResult(Best, Cast<UPrimitiveComponent>(Best->GetRootComponent()), BestPoint, -Dir);
			JumpHit.TraceStart = From;
			JumpHit.TraceEnd = TraceEnd;
		}
		if (JumpHit.BoneName.IsNone())
		{
			JumpHit.BoneName = ResolveHitBone(Best, From, TraceEnd);
		}

		// Drawn before the damage, so a kill's ragdoll does not move the tracer's end.
		PlayRicochetLocally(From, JumpHit.ImpactPoint);
		if (HasAuthority())
		{
			Multicast_PlayRicochet(From, JumpHit.ImpactPoint, /*bShooterHasIt*/ false);
		}
		else
		{
			Server_ReportRicochet(From, JumpHit.ImpactPoint);
		}

		UE_LOG(LogTemp, Log, TEXT("[MUZZLE] %s: ricochet %d -> %s for %.1f."), *GetName(), Jump + 1,
			*Best->GetName(), JumpDamage);

		ApplyWeaponHit(JumpHit, JumpDamage, Dir, /*ImpulseForce*/ 0.0f, 1.0f, DamageType);
		SpawnImpactEffect(JumpHit);

		Struck.Add(Best);
		From = JumpHit.ImpactPoint;
	}
}

void AShooterWeapon::Server_ReportRicochet_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End)
{
	PlayRicochetLocally(Start, End);
	Multicast_PlayRicochet(Start, End, /*bShooterHasIt*/ true);
}

void AShooterWeapon::Multicast_PlayRicochet_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End,
	bool bShooterHasIt)
{
	// The server drew it before sending; the shooter drew it when they traced it.
	if (HasAuthority() || (bShooterHasIt && PawnOwner && PawnOwner->IsLocallyControlled()))
	{
		return;
	}
	PlayRicochetLocally(Start, End);
}

void AShooterWeapon::PlayRicochetLocally(const FVector& Start, const FVector& End)
{
	const UWorld* const World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UWeaponAttachmentDefinition* const Muzzle = GetAttachmentOfType(EWeaponAttachmentType::Muzzle);

	if (Muzzle && Muzzle->RicochetTracerFX)
	{
		if (UNiagaraComponent* const Beam = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),
			Muzzle->RicochetTracerFX, Start, (End - Start).Rotation()))
		{
			Beam->SetVectorParameter(FName("BeamStart"), Start);
			Beam->SetVectorParameter(FName("BeamEnd"), End);
			Beam->SetFloatParameter(FName("Distance"), FVector::Dist(Start, End));
			Beam->SetColorParameter(FName("BeamColor"), BeamColor);
		}
	}
	else
	{
		SpawnBeamEffectLocally(Start, End, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f);
	}

	if (Muzzle && Muzzle->RicochetSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Muzzle->RicochetSound, Start);
	}
}

// ==================== Where the flash comes from ====================

USceneComponent* AShooterWeapon::GetMuzzleFXAnchor(bool bFirstPerson, FName& OutSocket) const
{
	OutSocket = MuzzleSocketName;
	USkeletalMeshComponent* const Gun = bFirstPerson ? FirstPersonMesh : ThirdPersonMesh;

	UStaticMeshComponent* const Part = GetAttachmentMeshComponent(EWeaponAttachmentType::Muzzle, bFirstPerson);
	if (!Part)
	{
		return Gun;
	}

	const UWeaponAttachmentDefinition* const Def = GetAttachmentOfType(EWeaponAttachmentType::Muzzle);
	FName Socket = Def ? Def->FXSocketName : NAME_None;
	if (Socket.IsNone() || !Part->DoesSocketExist(Socket))
	{
		Socket = NAME_None;
		const TArray<FName> Names = Part->GetAllSocketNames();
		for (const FName& Name : Names)
		{
			if (Name.ToString().Contains(TEXT("Muzzle")))
			{
				Socket = Name;
				break;
			}
		}
		if (Socket.IsNone() && Names.Num() == 1)
		{
			Socket = Names[0];
		}
	}

	if (Socket.IsNone())
	{
		return Gun;
	}
	OutSocket = Socket;
	return Part;
}

FVector AShooterWeapon::GetMuzzleFXLocation(bool bFirstPerson) const
{
	FName Socket;
	const USceneComponent* const Anchor = GetMuzzleFXAnchor(bFirstPerson, Socket);
	return Anchor ? Anchor->GetSocketLocation(Socket) : GetActorLocation();
}
