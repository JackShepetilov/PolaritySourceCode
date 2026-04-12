// ShootableButtonComponent.h
// Reusable scene component activated by shooting.
// Add via Blueprint "Add Component" — configure mesh, materials, SFX in editor.
// Owner must forward TakeDamage → HandleOwnerTakeDamage(), or use auto-binding.
//
// Two modes:
//   One-shot (bAutoDisableOnPress = true): Press disables, needs Reset() to re-enable.
//   Toggle  (bAutoDisableOnPress = false): Each Press flips on/off state.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ShootableButtonComponent.generated.h"

class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnButtonPressed, UShootableButtonComponent*, Button, AActor*, Activator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnButtonReset, UShootableButtonComponent*, Button);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class POLARITY_API UShootableButtonComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UShootableButtonComponent();

	// ── Delegates ──────────────────────────────────────────────

	/** Fired when the button is activated (shot or pressed). */
	UPROPERTY(BlueprintAssignable, Category = "Button|Events")
	FOnButtonPressed OnButtonPressed;

	/** Fired when the button is reset (re-enabled). */
	UPROPERTY(BlueprintAssignable, Category = "Button|Events")
	FOnButtonReset OnButtonReset;

	// ── Public API ─────────────────────────────────────────────

	/** Call from owner's TakeDamage. Returns true if the button was activated. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	bool HandleOwnerTakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser);

	/** Manual activation (e.g. interact key). Respects cooldown. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	bool Press(AActor* Activator = nullptr);

	/** Re-enable the button + broadcast OnButtonReset. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	void Reset();

	/** Set enabled state + update material. Does NOT broadcast delegates. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetEnabled(bool bNewEnabled);

	UFUNCTION(BlueprintPure, Category = "Button")
	bool IsEnabled() const { return bIsEnabled; }

	/** In toggle mode: returns current toggle state (true = "on"). */
	UFUNCTION(BlueprintPure, Category = "Button")
	bool IsToggledOn() const { return bIsToggledOn; }

	/** Get the button mesh (child subobject). */
	UFUNCTION(BlueprintPure, Category = "Button")
	UStaticMeshComponent* GetButtonMesh() const { return ButtonMesh; }

	// ── Settings ───────────────────────────────────────────────

	/** If true, button auto-disables after Press (one-shot mode: needs Reset() to re-enable).
	 *  If false, toggle mode: each Press() flips internal state and swaps material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|Settings")
	bool bAutoDisableOnPress = true;

	/** Initial toggle state for toggle mode. Ignored when bAutoDisableOnPress = true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|Settings", meta = (EditCondition = "!bAutoDisableOnPress"))
	bool bStartToggledOn = true;

	/** Cooldown after activation before the button accepts another hit (seconds).
	 *  Prevents burst fire from triggering multiple times. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|Settings", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ActivationCooldown = 0.5f;

	// ── Visuals ────────────────────────────────────────────────

	/** Material for "on" state:
	 *  One-shot mode: button is enabled/shootable.
	 *  Toggle mode: toggled ON. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|Visual")
	TObjectPtr<UMaterialInterface> OnMaterial;

	/** Material for "off" state:
	 *  One-shot mode: button is disabled (waiting for Reset).
	 *  Toggle mode: toggled OFF. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|Visual")
	TObjectPtr<UMaterialInterface> OffMaterial;

	/** Which material slot to swap on the ButtonMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|Visual", meta = (ClampMin = "0"))
	int32 MaterialSlotIndex = 0;

	// ── SFX ────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|SFX")
	TObjectPtr<USoundBase> PressSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button|SFX")
	TObjectPtr<USoundBase> ResetSound;

protected:
	virtual void BeginPlay() override;

	// ── State ──────────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Button|State")
	bool bIsEnabled = true;

	// ── Child Mesh ─────────────────────────────────────────────

	/** Button mesh — created as subobject, visible and editable in editor viewport.
	 *  Assign a Static Mesh and position it in the Blueprint editor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Button|Components")
	TObjectPtr<UStaticMeshComponent> ButtonMesh;

private:
	float LastActivationTime = -100.f;
	bool bIsToggledOn = true;

	/** Auto-bound to owner's OnTakeAnyDamage in BeginPlay.
	 *  No manual TakeDamage forwarding needed in owner classes. */
	UFUNCTION()
	void OnOwnerTakeAnyDamage(AActor* DamagedActor, float Damage,
		const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	void UpdateMaterial();
	bool IsCooldownReady() const;
};
