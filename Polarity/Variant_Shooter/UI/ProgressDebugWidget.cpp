// ProgressDebugWidget.cpp

#include "Variant_Shooter/UI/ProgressDebugWidget.h"

#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeaponHolder.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/Suppression/PlayerSuppressionComponent.h"
#include "Variant_Shooter/Progression/RunLevelComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryDefinition.h"
#include "ApexMovementComponent.h"
#include "Coop/CoopPlayers.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	FString TickBox(bool bOn, const TCHAR* Name)
	{
		return FString::Printf(TEXT("[%s] %s"), bOn ? TEXT("x") : TEXT(" "), Name);
	}

	// One handling row: the time the game uses now, and what the attachments and the mastery did to it.
	FString HandlingRow(const TCHAR* Name, float Seconds, const AShooterWeapon* Weapon,
		float UWeaponAttachmentDefinition::* Field, float MasteryValue)
	{
		const float Total = Weapon->GetAttachmentHandlingMultiplier(Field);
		const float Mastery = FMath::Max(0.01f, MasteryValue);
		return FString::Printf(TEXT("  %-14s %5.2f s   attach x%.2f   mastery x%.2f"), Name, Seconds, Total / Mastery, Mastery);
	}
}

void UProgressDebugWidget::BuildLines(TArray<FString>& OutLines) const
{
	const APlayerController* const PC = GetOwningPlayer();
	const AShooterCharacter* const Character = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		OutLines.Add(TEXT("PROGRESS DEBUG: no shooter pawn"));
		return;
	}

	// ---- run level ----
	if (const URunLevelComponent* const RunLevel = URunLevelComponent::FindFor(Character))
	{
		OutLines.Add(FString::Printf(TEXT("RUN LEVEL %d   XP %d / %d   gadgets per kind %d   suppression ignored %d%%"),
			RunLevel->GetLevel(), RunLevel->GetXP(), RunLevel->GetXPForNextLevel(), RunLevel->GetGadgetsPerKind(),
			FMath::RoundToInt(RunLevel->GetSuppressionResistance() * 100.0f)));
	}

	// ---- mastery of the gun in hand ----
	const AShooterWeapon* const Weapon = Character->GetCurrentWeapon();
	if (!Weapon)
	{
		OutLines.Add(TEXT("GUN: none in hand"));
	}
	else
	{
		const UWeaponMasteryDefinition* const Def = Weapon->MasteryDefinition;
		const UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(Character);
		const int32 GunLevel = (Def && Mastery) ? Mastery->GetLevel(Def) : 0;
		OutLines.Add(FString::Printf(TEXT("GUN %s   mastery %s   level %d / %d   XP %d / %d"),
			*Weapon->GetClass()->GetName().Replace(TEXT("_C"), TEXT("")), *GetNameSafe(Def), GunLevel,
			Def ? Def->Steps.Num() : 0, (Def && Mastery) ? Mastery->GetXP(Def) : 0, Def ? Def->GetXPToReach(GunLevel + 1) : -1));

		const FWeaponMasteryPerks Perks = Weapon->GetMasteryPerks();
		OutLines.Add(FString::Printf(TEXT("  %s  %s  %s  %s"), *TickBox(Perks.bAllowADSWhileSliding, TEXT("ADS in slide")),
			*TickBox(Perks.bAllowADSWhileAirborne, TEXT("ADS in air")), *TickBox(Perks.bAllowReloadWhileADS, TEXT("reload in ADS")),
			*TickBox(Perks.bAllowReloadWhileSliding, TEXT("reload in slide"))));
		OutLines.Add(FString::Printf(TEXT("  %s  %s  %s  %s"), *TickBox(Perks.bIgnoreADSMoveSpeedPenalty, TEXT("no ADS slow")),
			*TickBox(Perks.bAllowTacticalSprint, TEXT("tactical sprint")), *TickBox(Perks.bFastSwapAfterKill, TEXT("fast swap after kill")),
			*TickBox(Perks.bCanThrowWeapon, TEXT("throw (stub)"))));

		// ---- handling ----
		using A = UWeaponAttachmentDefinition;
		const FWeaponMasteryHandling& H = Perks.Handling;
		OutLines.Add(TEXT("HANDLING (time now, attachments, mastery)"));
		OutLines.Add(HandlingRow(TEXT("ADS in"), Weapon->GetADSBlendInTime(), Weapon, &A::ADSBlendInTimeMultiplier, H.ADSBlendInTimeMultiplier));
		OutLines.Add(HandlingRow(TEXT("ADS out"), Weapon->GetADSBlendOutTime(), Weapon, &A::ADSBlendOutTimeMultiplier, H.ADSBlendOutTimeMultiplier));
		OutLines.Add(HandlingRow(TEXT("draw"), Weapon->GetDrawLength(), Weapon, &A::DrawDurationMultiplier, H.DrawDurationMultiplier));
		OutLines.Add(HandlingRow(TEXT("holster"), Weapon->GetHolsterLength(), Weapon, &A::HolsterDurationMultiplier, H.HolsterDurationMultiplier));
		OutLines.Add(HandlingRow(TEXT("sprint > fire"), Weapon->GetSprintToFireTime(), Weapon, &A::SprintToFireTimeMultiplier, H.SprintToFireTimeMultiplier));
		OutLines.Add(HandlingRow(TEXT("reload empty"), Weapon->GetReloadStageDuration(EWeaponReloadStage::Primary), Weapon,
			&A::ReloadDurationMultiplier, H.ReloadDurationMultiplier));
		OutLines.Add(HandlingRow(TEXT("reload rounds"), Weapon->GetReloadStageDuration(EWeaponReloadStage::Secondary), Weapon,
			&A::SecondaryReloadTimeMultiplier, H.SecondaryReloadTimeMultiplier));

		// ---- ADS fatigue ----
		const float SwayMultiplier = Weapon->GetADSFatigueSwayMultiplier();
		OutLines.Add(FString::Printf(TEXT("ADS FATIGUE %3d%%   wanders %.2f deg now   delay %.1f s  ramp %.1f s  rest %.1f s   rate x%.2f  sway x%.2f"),
			FMath::RoundToInt(Character->GetADSFatigue() * 100.0f),
			Weapon->ADSFatigueSwayDegrees * SwayMultiplier * Character->GetADSFatigue(),
			Weapon->ADSFatigueDelay, Weapon->ADSFatigueRampTime, Weapon->ADSFatigueRecoveryTime,
			Weapon->GetADSFatigueRateMultiplier(), SwayMultiplier));
	}

	// ---- tactical sprint ----
	if (const UApexMovementComponent* const Apex = Character->GetApexMovement())
	{
		const float Cooldown = Apex->GetTacticalSprintCooldownRemaining();
		OutLines.Add(FString::Printf(TEXT("TACTICAL SPRINT %s   (open with this gun: %s, sprint twice)"),
			Apex->IsTacticalSprinting() ? TEXT("RUNNING") : (Cooldown > 0.0f ? *FString::Printf(TEXT("cooldown %.1f s"), Cooldown) : TEXT("ready")),
			Character->CanTacticalSprint() ? TEXT("yes") : TEXT("no")));
	}

	// ---- suppression ----
	if (const UPlayerSuppressionComponent* const Suppression = Character->FindComponentByClass<UPlayerSuppressionComponent>())
	{
		OutLines.Add(FString::Printf(TEXT("SUPPRESSION %d%%"), FMath::RoundToInt(Suppression->GetLevel() * 100.0f)));
	}

	// ---- gadgets ----
	if (const UInventoryComponent* const Bag = Character->FindComponentByClass<UInventoryComponent>())
	{
		FString Kinds;
		for (const FGadgetStack& Stack : Bag->GetGadgets())
		{
			Kinds += FString::Printf(TEXT("   %s x%d"), *GetNameSafe(Stack.Type).Replace(TEXT("DA_Grenade_"), TEXT("")), Stack.Count);
		}
		OutLines.Add(FString::Printf(TEXT("GADGETS %d / %d kinds   %d per kind   %d / %d in all%s"),
			Bag->GetGadgets().Num(), Bag->GetGadgetKinds(), Bag->GetGadgetsPerKind(),
			Bag->GetGrenadeCount(nullptr), Bag->GetGadgetTotalLimit(), *Kinds));
	}
}

int32 UProgressDebugWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	TArray<FString> Lines;
	BuildLines(Lines);

	// Mono, so the handling columns line up.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Mono", 11);
	const float LineHeight = 17.0f;
	const FVector2D Origin(24.0f, AllottedGeometry.GetLocalSize().Y * 0.18f);
	const FVector2D Size(720.0f, LineHeight * Lines.Num() + 16.0f);

	FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
		AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Origin - FVector2D(8.0f, 8.0f))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));

	++Layer;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, LineHeight), FSlateLayoutTransform(Origin + FVector2D(0.0f, Index * LineHeight))),
			Lines[Index], Font, ESlateDrawEffect::None, FLinearColor::White);
	}
	return Layer;
}

// ==================== Toggle ====================

namespace ProgressDebug
{
	static void CmdToggle(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* const PC = CoopPlayers::GetLocalController(World);
		if (!PC)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PROGRESS_DEBUG] no local player"));
			return;
		}

		// The panel on this player's screen, if any. Debug only, so a plain map by controller.
		static TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UProgressDebugWidget>> Panels;
		UProgressDebugWidget* const Existing = Panels.FindRef(PC).Get();

		const bool bWant = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Existing;
		if (!bWant && Existing)
		{
			Existing->RemoveFromParent();
			Panels.Remove(PC);
		}
		else if (bWant && !Existing)
		{
			if (UProgressDebugWidget* const Panel = CreateWidget<UProgressDebugWidget>(PC, UProgressDebugWidget::StaticClass()))
			{
				Panel->AddToPlayerScreen(13);
				Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
				Panels.Add(PC, Panel);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[PROGRESS_DEBUG] panel %s"), bWant ? TEXT("on") : TEXT("off"));
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdProgressDebug(
	TEXT("polarity.debug.progress"),
	TEXT("Progression debug panel: run level, gun mastery, handling, ADS fatigue, tactical sprint, gadgets. Usage: polarity.debug.progress [0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProgressDebug::CmdToggle)
);
