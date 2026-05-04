# HumanoidNPC — Tech Spec / План реализации

> Адресат: Sonnet 4.6.
> Визионер: Opus 4.7. Утверждено пользователем 2026-04-27.
> Реализация по этому документу. Несостыковки уже разобраны и подтверждены — см. ниже.

---

## Контекст и мотивация

Текущие враги-наследники `AShooterNPC` и `AMeleeNPC` — роботы. У них:
- Заряд хранится на теле (`UEMF_FieldComponent`).
- Игрок может **захватить тело** (channeling-плита → `EnterCapturedState` → `EnterLaunchedState`).
- К телу **притягиваются снаряды Charge Launcher** (заряд на теле).
- На тело **действуют EMF-силы** (knockback, attraction, capture).

Нужен новый враг **`AHumanoidNPC`** — органический. У него:
- Заряд тоже хранится на теле и накапливается **теми же способами** что и у родителя (melee удар игрока, попадание заряженного снаряда из Charge Launcher, и т.д.).
- **Силы на тело не действуют:** knockback от EMF, captured/launched состояния — отключены.
- Игрок **не может схватить тело** — но может **выхватить оружие из рук**.
- В руках — массив (1-3 элемента) видов оружия. После выхвата последнего оружия гуманоид переходит в **melee-режим** и больше не может получать заряд.

---

## Подтверждённые правила (после согласования с пользователем)

1. **Charge launcher** всё ещё притягивает снаряды к гуманоиду — он же хранит заряд на теле. ✓
2. **EMF-knockback от полей** (proximity attraction, channeling launch, NPC-NPC collision) — **не применяется**. ✗
3. **Channeling-плита не может захватить и швырнуть** гуманоида. ✗
4. **Игрок не может взять тело в захват** — обычный NPC-сканер должен пропускать `AHumanoidNPC`. ✗
5. **Заряд хранится на теле гуманоида** (как у родителя). Оружие в руках — просто визуальная цель для yank. Формула yank-range: `q_humanoid * q_player` (та же, что у `DroppedRangedWeapon::CalculateCaptureRange`).
6. **При каждом yank оружия — заряд тела гуманоида обнуляется.**
7. **После yank последнего оружия:**
   - Гуманоид переходит в **melee-режим**: ведёт себя как `AMeleeNPC` (но не меняет класс — гейтится через StateTree-флаг).
   - Больше **не может получать заряд** от игрока (`bChargeReceptive = false`).
   - **Заряд лочится на 0 навсегда** (через подписку на `OnChargeUpdated` или периодический форс).
8. **Класс runtime не меняется** — `AHumanoidNPC : public AMeleeNPC`, обе линии поведения наследуются, ветви в StateTree гейтятся условиями `IsInRangedMode` / `IsInMeleeMode`.

---

## Архитектура

### 1. Новый класс `AHumanoidNPC : public AMeleeNPC`

**Файлы:**
- `Polarity/Variant_Shooter/AI/HumanoidNPC.h` *(новый)*
- `Polarity/Variant_Shooter/AI/HumanoidNPC.cpp` *(новый)*

**Почему наследник `AMeleeNPC`, а не `AShooterNPC`:**
`AMeleeNPC` уже наследует от `AShooterNPC` и не отключает родительский `WeaponClass` спавн (`ShooterNPC.cpp:101`). Получаем обе линии поведения (стрельба + ближний бой + dash) бесплатно. Switching между ними — через `bIsInMeleeMode` и StateTree.

#### Новые UPROPERTY

```cpp
// ==================== Inventory ====================

/** Массив видов оружия. Index 0 = первое в руках. После yank — берётся следующий по индексу. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Humanoid|Inventory")
TArray<TSubclassOf<AShooterWeapon>> WeaponInventory;

/** 1:1 mapping с WeaponInventory — какой ADroppedRangedWeapon спавнить при yank.
 *  Если массивы разной длины — недостающие индексы fallback на null (yank без drop-актора, просто despawn). */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Humanoid|Inventory")
TArray<TSubclassOf<ADroppedRangedWeapon>> WeaponDropMapping;

// ==================== Yank Animation ====================

/** Анимация когда игрок дёргает оружие спереди */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
TObjectPtr<UAnimMontage> YankFrontMontage;

/** Анимация когда игрок дёргает оружие сзади */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
TObjectPtr<UAnimMontage> YankBackMontage;

/** Анимация когда игрок дёргает оружие слева */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
TObjectPtr<UAnimMontage> YankLeftMontage;

/** Анимация когда игрок дёргает оружие справа */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
TObjectPtr<UAnimMontage> YankRightMontage;

/** Задержка после yank перед спавном следующего оружия (длина анимации yank) */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank", meta=(ClampMin="0.1", ClampMax="3.0"))
float WeaponSwitchDelay = 0.6f;

// ==================== Mode State (BlueprintReadOnly для StateTree) ====================

/** True после yank последнего оружия — переход в режим ближнего боя */
UPROPERTY(BlueprintReadOnly, Category = "Humanoid|State")
bool bIsInMeleeMode = false;

/** True пока гуманоид может получать заряд от игрока. Сбрасывается при EnterMeleeMode. */
UPROPERTY(BlueprintReadOnly, Category = "Humanoid|State")
bool bChargeReceptive = true;
```

#### Runtime state (private/protected)

```cpp
/** Текущий индекс оружия в WeaponInventory */
int32 CurrentWeaponIndex = 0;

/** Таймер задержки между yank и спавном следующего оружия */
FTimerHandle WeaponSwitchTimer;

/** Текущая воспроизводимая yank-анимация (для AnimInstance::Montage_IsPlaying проверки) */
TObjectPtr<UAnimMontage> ActiveYankMontage;
```

#### Override-ы

| Метод родителя | Что делает override |
|---|---|
| `BeginPlay()` | **Перед `Super::BeginPlay()`** — если `WeaponInventory.Num() > 0`, скопировать `WeaponInventory[0]` в `WeaponClass` (иначе родитель спавнит `nullptr`). Если массив пуст → после `Super::BeginPlay()` сразу `EnterMeleeMode()`. **После Super** — установить `EMFVelocityModifier->bEnableViscousCapture = false` чтобы скан игрока не находил тело как target (см. `ChargeAnimationComponent.cpp:838`). |
| `ApplyKnockback(...)` | **No-op** + `UE_LOG([HUMANOID_DEBUG] ApplyKnockback ignored)`. Силы не действуют. |
| `ApplyKnockbackVelocity(...)` | **No-op**. |
| `EnterCapturedState(...)` | **No-op** + лог. |
| `EnterLaunchedState()` | **No-op** + лог. |
| `ApplyExplosionStun(...)` | **No-op** (форсы не действуют → стан от взрыва не применяется). |
| `TakeDamage(...)` | Если `!bChargeReceptive` — пропустить родительскую логику передачи заряда. См. секцию **«Блокировка приёма заряда»** ниже. |
| `Die()` | Очистить `WeaponSwitchTimer` и `ActiveYankMontage`. Вызвать `Super::Die()`. |
| `ResetForPool(...)` | `CurrentWeaponIndex = 0; bIsInMeleeMode = false; bChargeReceptive = true;` потом `Super::ResetForPool(...)`. Спавнить `WeaponInventory[0]` заново. |
| `EndPlay(...)` | Очистить `WeaponSwitchTimer`. |

#### Новые публичные методы

```cpp
/** Игрок выхватывает текущее оружие из рук гуманоида.
 *  - Спавнит ADroppedRangedWeapon на текущей world-локации Weapon
 *  - Сразу вызывает StartPull(Puller) на нём
 *  - Обнуляет заряд тела гуманоида
 *  - Воспроизводит directional montage (front/back/left/right)
 *  - Через WeaponSwitchDelay вызывает SpawnNextWeapon()
 *
 *  @param Puller Игрок, который дёргает оружие (для StartPull и расчёта направления)
 *  @return true если yank успешен, false если CanBeYanked() == false
 */
UFUNCTION(BlueprintCallable, Category = "Humanoid")
bool YankCurrentWeapon(class AShooterCharacter* Puller);

/** Возвращает range yank для текущего состояния NPC.
 *  Формула: BaseRange * max(1, 1 + ln(|q_npc * q_player| / NormCoeff))
 *  Использует параметры из текущего ADroppedRangedWeapon class (CaptureBaseRange, CaptureChargeNormCoeff).
 *  Если CanBeYanked()==false → 0. */
UFUNCTION(BlueprintPure, Category = "Humanoid")
float CalculateWeaponYankRange() const;

/** Можно ли сейчас выхватить оружие.
 *  - !bIsInMeleeMode
 *  - Weapon валидно
 *  - WeaponSwitchTimer неактивен (нет yank-в-процессе)
 *  - !bIsDead */
UFUNCTION(BlueprintPure, Category = "Humanoid")
bool CanBeYanked() const;
```

#### Новые protected методы

```cpp
/** Перейти к следующему оружию в инвентаре. Если индекс выходит за границы → EnterMeleeMode(). */
void SpawnNextWeapon();

/** Финализировать смерть/уничтожение текущего Weapon, сбросить указатель. */
void DespawnCurrentWeapon();

/** Перейти в melee-режим: bIsInMeleeMode=true, bChargeReceptive=false,
 *  обнулить заряд и залочить его на 0, удалить Weapon. */
void EnterMeleeMode();

/** Подписка на OnChargeUpdated для лока заряда на 0 в melee-режиме. */
UFUNCTION()
void OnChargeUpdatedInMeleeMode(float ChargeValue, uint8 Polarity);

/** Выбор анимации yank по углу между forward NPC и направлением на Puller.
 *  Front: |angle| < 45°. Back: |angle| > 135°. Right: 45° <= angle <= 135°. Left: -135° <= angle <= -45°. */
UAnimMontage* SelectYankMontageForDirection(const FVector& PullerLocation) const;
```

---

### 2. Реализация `YankCurrentWeapon` — пошагово

```cpp
bool AHumanoidNPC::YankCurrentWeapon(AShooterCharacter* Puller)
{
    if (!CanBeYanked() || !Puller) return false;

    // 1. Сохранить world-transform текущего оружия
    const FTransform WeaponTransform = Weapon->GetActorTransform();

    // 2. Спавн DroppedRangedWeapon (если class задан)
    ADroppedRangedWeapon* Dropped = nullptr;
    if (WeaponDropMapping.IsValidIndex(CurrentWeaponIndex)
        && WeaponDropMapping[CurrentWeaponIndex])
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Dropped = GetWorld()->SpawnActor<ADroppedRangedWeapon>(
            WeaponDropMapping[CurrentWeaponIndex],
            WeaponTransform.GetLocation(),
            WeaponTransform.Rotator(),
            Params);

        if (Dropped)
        {
            // Установить заряд на drop = текущий заряд тела (для красивого pull-визуала, опционально)
            Dropped->SetCharge(GetCurrentCharge());  // helper или напрямую через FieldComponent
            // Сразу запустить pull к игроку
            Dropped->StartPull(Puller);
        }
    }

    // 3. Обнулить заряд тела гуманоида
    if (FieldComponent)
    {
        FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
        Desc.PointChargeParams.Charge = 0.0f;
        FieldComponent->SetSourceDescription(Desc);
    }
    if (EMFVelocityModifier) { /* также обновить charge на modifier если он держит копию */ }

    // 4. Уничтожить текущее Weapon (визуально пустые руки во время анимации)
    DespawnCurrentWeapon();

    // 5. Воспроизвести yank-анимацию
    if (UAnimMontage* Montage = SelectYankMontageForDirection(Puller->GetActorLocation()))
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->Montage_Play(Montage);
                ActiveYankMontage = Montage;
            }
        }
    }

    // 6. Запустить таймер до спавна следующего оружия
    GetWorld()->GetTimerManager().SetTimer(
        WeaponSwitchTimer,
        this, &AHumanoidNPC::SpawnNextWeapon,
        WeaponSwitchDelay, false);

    UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s: Yanked weapon idx=%d, switching in %.2fs"),
        *GetName(), CurrentWeaponIndex, WeaponSwitchDelay);
    return true;
}

void AHumanoidNPC::SpawnNextWeapon()
{
    CurrentWeaponIndex++;
    if (!WeaponInventory.IsValidIndex(CurrentWeaponIndex))
    {
        EnterMeleeMode();
        return;
    }

    // Спавн следующего оружия (зеркало кода ShooterNPC.cpp:96-107)
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.Instigator = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    Weapon = GetWorld()->SpawnActor<AShooterWeapon>(
        WeaponInventory[CurrentWeaponIndex], GetActorTransform(), SpawnParams);

    if (Weapon)
    {
        Weapon->OnShotFired.AddDynamic(this, &AShooterNPC::OnWeaponShotFired);
        // AttachWeaponMeshes вызовется внутри Weapon->BeginPlay через IShooterWeaponHolder
    }
}

void AHumanoidNPC::EnterMeleeMode()
{
    bIsInMeleeMode = true;
    bChargeReceptive = false;
    DespawnCurrentWeapon();

    // Лок заряда на 0 — обнулить и подписаться на OnChargeUpdated
    if (FieldComponent)
    {
        FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
        Desc.PointChargeParams.Charge = 0.0f;
        FieldComponent->SetSourceDescription(Desc);
    }
    OnChargeUpdated.AddDynamic(this, &AHumanoidNPC::OnChargeUpdatedInMeleeMode);

    UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s: Entered melee mode (no more charge intake)"),
        *GetName());
}

void AHumanoidNPC::OnChargeUpdatedInMeleeMode(float ChargeValue, uint8 Polarity)
{
    if (bIsInMeleeMode && !FMath::IsNearlyZero(ChargeValue) && FieldComponent)
    {
        FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
        Desc.PointChargeParams.Charge = 0.0f;
        FieldComponent->SetSourceDescription(Desc);
    }
}
```

> ⚠️ **Verify before coding:** `OnChargeUpdated` — это `FChargeUpdatedDelegate_NPC` из `ShooterNPC.h:32`. Прочитай где он broadcast-ится в `ShooterNPC.cpp` — вероятно в `Tick`. Если broadcast зовут **до** того как заряд физически меняется — твой setter не сработает; перенеси лок на `OnPolarityChanged` или используй проверку через `Tick`.

---

### 3. Блокировка приёма заряда (`bChargeReceptive`)

**Где меняется заряд тела NPC:**
1. **Melee удар игрока** — `ShooterNPC::ChargeChangeOnMeleeHit` применяется где-то в `TakeDamage` или в melee-обработчике игрока. Найди в `ShooterNPC.cpp` место где `FieldComponent->SetSourceDescription` зовётся под `UDamageType_Melee` (или подобное).
2. **Попадание EMF projectile из Charge Launcher** — в `EMFProjectile.cpp` при коллизии. Найди по grep `SetCharge|ChargeChange|FieldComponent->Set`.

**План блокировки:**
В обоих местах в `AHumanoidNPC` добавить проверку `bChargeReceptive` перед применением. Самый чистый путь — переопределить `TakeDamage`:

```cpp
float AHumanoidNPC::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (!bChargeReceptive)
    {
        // Снять charge-side-effect: временно сохранить заряд, дать родителю отработать,
        // восстановить заряд после.
        const float SavedCharge = GetCurrentCharge();
        const float Result = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
        if (FieldComponent && !FMath::IsNearlyEqual(GetCurrentCharge(), SavedCharge))
        {
            FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
            Desc.PointChargeParams.Charge = SavedCharge;  // в melee-режиме = 0
            FieldComponent->SetSourceDescription(Desc);
        }
        return Result;
    }
    return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}
```

> Этот save/restore подход **проще и безопаснее**, чем хирургическое отключение каждой charge-transfer точки родителя. Ничего не сломаем в иерархии damage handling, а заряд просто всегда возвращается к 0 в melee-режиме.

---

### 4. Интеграция yank в `ChargeAnimationComponent.cpp`

**Файл:** `Polarity/ChargeAnimationComponent.cpp`. Sсан capture-target — функция, содержащая enum `ECaptureTargetType` (строка ~800) и цикл `for (const FOverlapResult& Overlap : Overlaps)` (строка ~826).

**Изменения:**

#### A. Добавить новый case в enum (строка 800):
```cpp
enum class ECaptureTargetType { None, NPC, Prop, DroppedWeapon, DroppedRangedWeapon,
                                UpgradePickup, ScriptedPickup, HumanoidWeapon };
```

#### B. В NPC-ветке (строка ~835) **отсечь HumanoidNPC**:
```cpp
if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
{
    // Гуманоид обрабатывается отдельной веткой (yank weapon вместо capture body)
    if (Cast<AHumanoidNPC>(NPC))
    {
        // Skip — handled below
    }
    else
    {
        // ... существующий код NPC-капчура без изменений ...
        continue;
    }
}

// Новая ветка для HumanoidNPC
if (AHumanoidNPC* Humanoid = Cast<AHumanoidNPC>(HitActor))
{
    if (!Humanoid->CanBeYanked()) continue;

    // Charge sign check (нужен противоположный знак)
    UEMFVelocityModifier* HumanoidMod = Humanoid->FindComponentByClass<UEMFVelocityModifier>();
    if (!HumanoidMod) continue;
    const float HumanoidCharge = HumanoidMod->GetCharge();
    if (FMath::IsNearlyZero(HumanoidCharge)
        || HumanoidCharge * static_cast<float>(ChannelingChargeSign) > 0.0f)
    {
        continue;
    }

    // Range
    const FVector ToTarget = Humanoid->GetActorLocation() - CameraLoc;
    const float DistSq = ToTarget.SizeSquared();
    const float YankRange = Humanoid->CalculateWeaponYankRange();
    if (DistSq > YankRange * YankRange || DistSq < 1.0f) continue;

    // Angle (как у других целей)
    const FVector DirToTarget = ToTarget.GetUnsafeNormal();
    const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);
    if (AngleCos < GetMaxAngleCosForDistance(FMath::Sqrt(DistSq))) continue;

    if (AngleCos > BestAngleCos)
    {
        BestAngleCos = AngleCos;
        BestTarget = Humanoid;
        BestTargetType = ECaptureTargetType::HumanoidWeapon;
    }
    continue;
}
```

#### C. В switch (строка ~1093) добавить case:
```cpp
case ECaptureTargetType::HumanoidWeapon:
    CaptureHumanoidWeapon(Cast<AHumanoidNPC>(BestTarget));
    break;
```

#### D. Новая функция в `UChargeAnimationComponent`:
```cpp
void UChargeAnimationComponent::CaptureHumanoidWeapon(AHumanoidNPC* Humanoid)
{
    if (!Humanoid) return;
    ReleaseCapturedNPC();  // освободить предыдущий target

    AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
    if (ShooterChar)
    {
        Humanoid->YankCurrentWeapon(ShooterChar);
    }
    // Не сохраняем CurrentCapturedNPC — yank это одиночный action, не hold
}
```

**Объявление в `ChargeAnimationComponent.h`:** добавить forward-decl `class AHumanoidNPC;` и метод `void CaptureHumanoidWeapon(AHumanoidNPC*);`.

---

### 5. StateTree-conditions

**Файлы:**
- `Polarity/Variant_Shooter/AI/HumanoidStateTreeTasks.h` *(новый)*
- `Polarity/Variant_Shooter/AI/HumanoidStateTreeTasks.cpp` *(новый)*

**StateTree не поддерживает инверсию** (см. CLAUDE.md), поэтому делаем **обе** условные пары:

```cpp
USTRUCT()
struct FStateTreeIsInRangedModeInstanceData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category = "Context")
    TObjectPtr<AHumanoidNPC> Character;
};

USTRUCT(DisplayName = "Is In Ranged Mode (Humanoid)", Category = "Humanoid")
struct POLARITY_API FStateTreeIsInRangedModeCondition : public FStateTreeConditionCommonBase
{
    GENERATED_BODY()
    using FInstanceDataType = FStateTreeIsInRangedModeInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
    // returns Character && !Character->bIsInMeleeMode
};

USTRUCT(DisplayName = "Is In Melee Mode (Humanoid)", Category = "Humanoid")
struct POLARITY_API FStateTreeIsInMeleeModeCondition : public FStateTreeConditionCommonBase
{
    // Зеркальный, returns Character && Character->bIsInMeleeMode
};
```

**StateTree-ассет** пользователь сделает в редакторе — два корневых child-state:
- `Ranged Branch` (gated by `Is In Ranged Mode`) → reuse `FStateTreeShootAtTargetTask` + sense + face logic.
- `Melee Branch` (gated by `Is In Melee Mode`) → reuse `FStateTreeMeleeAttackTask` + `FStateTreeMeleeDashTask` + chase.

Существующие таски уже принимают `AShooterNPC*` или `AMeleeNPC*` — `AHumanoidNPC` наследник обоих, биндинг сработает автоматически.

---

## Edge cases

| Сценарий | Поведение |
|---|---|
| Yank во время yank-анимации | `CanBeYanked()` возвращает false (timer активен) — повторный yank невозможен. |
| Death во время yank-анимации | `Die()` override чистит `WeaponSwitchTimer`, не спавнит следующее оружие. Drop-таблица родителя (`DroppedRangedWeaponTable`) работает обычным способом для текущего активного оружия. |
| Pool reset (sustain mode) | `ResetForPool` сбрасывает `CurrentWeaponIndex=0`, `bIsInMeleeMode=false`, `bChargeReceptive=true`, отписывается от `OnChargeUpdated`, спавнит `WeaponInventory[0]`. |
| Пустой `WeaponInventory` | В `BeginPlay` сразу `EnterMeleeMode()` — гуманоид с самого начала melee. |
| Yank когда у NPC заряд = 0 | `CalculateWeaponYankRange()` возвращает 0 → не пройдёт range-check в скане → yank невозможен. Игрок должен сначала зарядить NPC. |
| EMF-снаряд в melee-режиме | `bChargeReceptive=false` → save/restore в `TakeDamage` обнулит обратно. Damage от снаряда применяется как обычно. |

---

## Порядок реализации

1. **Скелет класса.** `HumanoidNPC.h/.cpp` с пустыми override-ами no-op для `ApplyKnockback`, `EnterCapturedState`, `EnterLaunchedState`, `ApplyExplosionStun`. Скомпилировать. Убедиться что класс появляется в Blueprint picker.
2. **Inventory.** `BeginPlay` подменяет `WeaponClass` на `WeaponInventory[0]` до `Super::BeginPlay()`. Тестировать спавн с одним оружием в массиве.
3. **Yank-механика без интеграции с channeling.** Реализовать `YankCurrentWeapon`, `SpawnNextWeapon`, `EnterMeleeMode`. Тестировать через BP-кнопку или debug-команду.
4. **Блокировка заряда.** Override `TakeDamage` с save/restore. Подписка `OnChargeUpdated` для лока на 0.
5. **StateTree-conditions.** `IsInRangedMode` + `IsInMeleeMode`. Дать пользователю собрать ассет StateTree в редакторе.
6. **Интеграция в ChargeAnimationComponent.** Скан + новая ветка + `CaptureHumanoidWeapon`. Тестировать что yank триггерится из той же channeling-кнопки.
7. **Directional yank montage.** `SelectYankMontageForDirection` после того как пользователь подгонит анимации.

---

## Файлы

📝 **Headers (full recompile required):**
- `Polarity/Variant_Shooter/AI/HumanoidNPC.h` *(new)*
- `Polarity/Variant_Shooter/AI/HumanoidStateTreeTasks.h` *(new)*
- `Polarity/ChargeAnimationComponent.h` *(modify — forward decl + method declaration)*

📝 **CPP-only (Live Coding compatible):**
- `Polarity/Variant_Shooter/AI/HumanoidNPC.cpp` *(new)*
- `Polarity/Variant_Shooter/AI/HumanoidStateTreeTasks.cpp` *(new)*
- `Polarity/ChargeAnimationComponent.cpp` *(modify capture scan + new function)*

---

## Debug logging

Используй тег `[HUMANOID_DEBUG]` для всех логов в этом классе. Пользователь будет фильтровать по нему в Output Log.

Ключевые логи:
- `[HUMANOID_DEBUG] BeginPlay: WeaponInventory size=N, starting weapon=...`
- `[HUMANOID_DEBUG] %s: Yanked weapon idx=%d, switching in %.2fs`
- `[HUMANOID_DEBUG] %s: Entered melee mode (no more charge intake)`
- `[HUMANOID_DEBUG] %s: ApplyKnockback ignored (immune)`
- `[HUMANOID_DEBUG] %s: TakeDamage charge restored (melee mode)`

---

## Performance note

⚡ Все добавления — O(1) или O(N) с малым N (3 оружия макс). Скан в `ChargeAnimationComponent` уже O(N_overlaps) — добавление одной ветки не меняет complexity. `OnChargeUpdated` срабатывает на каждом изменении заряда (редко, не в Tick) — overhead negligible.

---

## Что **не** делать

- ❌ Не менять класс на лету (`AMeleeNPC` runtime cast/swap) — UE такого не умеет, гейтить через флаг + StateTree.
- ❌ Не переделывать `FieldComponent` чтобы заряд хранился на оружии — прямая логика (заряд на теле, оружие как визуальная цель) проще и пользователь подтвердил.
- ❌ Не добавлять «умные» оптимизации без запроса (см. CLAUDE.md). Простой save/restore в `TakeDamage` лучше чем рефакторинг damage-pipeline.
- ❌ Не пиши код пока не подтвердишь у пользователя один момент: **`OnChargeUpdated` broadcast timing** — не приведёт ли подписка к рекурсии (subscriber меняет charge → broadcast → subscriber снова срабатывает). Прочитай где он broadcast-ится в `ShooterNPC.cpp` и убедись что safe. Если нет — используй `Tick`-based лок.
