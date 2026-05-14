# POLARITY — Боевые механики

---

## 0. СТРУКТУРА ИГРЫ

Roguelite. **4 тира × 3 арены = 12 арен в проекте**, ран = 4 арены (по одной из 3 в каждом тире). Permadeath, мета-апгрейды между ранами, внутрирановые апгрейды через level-up choice.

### 0.1 Сеттинг и мета-нарратив

Главный герой — секретный агент, проникающий на точки интереса мирового заговора. По сеттингу — смесь ИИ-роботов (охрана секретного измерения, см. §5.1–5.4) и органических людей (картели, ЧВК, спецслужбы — см. §5.5). Распределение типов врагов по аренам определяется тематикой тира.

Тело органического NPC электризовать нельзя — yank-аются только его оружие и щит, в порядке: щит → оружие. Когда инвентарь пуст → NPC уходит в melee-режим.

Мета-нарратив: игрок-геймер играет на стриме, друг-стример комментирует. Донаты от зрителей конвертируются в мета-валюту, которая между ранами разблокирует стартовые апгрейды и ассеты.

### 0.2 Полярность игрока

Игрок зафиксирован на (+) на всех аренах, кроме одной "Cause and Effect"-арены, где управление полярностью передаётся **окружению** (статические заряды на арене, не игрок) — см. §2.2. Гейт: `EMFVelocityModifier::bAllowPolarityToggle`.

### 0.3 Стартовый арсенал

Только волновой пистолет. меч, riot shield и прочее оружие — через дроп / yank из NPC-инвентаря / мета-апгрейды.

### 0.4 Прогрессия в ране (XP по скиллам + level-up choice)

Четыре независимых пула опыта (`ESkillCategory`), у каждого свой `CurrentLevel`, своя кривая `LevelThresholds` и свой пул апгрейдов:

| Скилл    | Источник опыта                                                                                                          |
| -------- | ----------------------------------------------------------------------------------------------------------------------- |
| Movement | Действия движения (slide, wallrun, dash, double jump): активная фаза N сек → cooldown N сек. *TBD — Этап Б, сейчас 0.* |
| Melee    | Убийства в ближнем бою (`DamageType_Melee`, `DamageType_Dropkick`)                                                      |
| EMF      | Убийства через EMF-броски (`DamageType_Wallslam`, `DamageType_EMFProximity`, `DamageType_MomentumBonus`)                |
| Weapon   | Убийства из оружия (`DamageType_EMFWeapon`, `DamageType_Ranged`)                                                        |

**Маршрутизация kill-XP.** `XPConfig.KillXPRouting: TMap<TSubclassOf<UDamageType>, ESkillCategory>` — таблица DamageType → скилл. Не в таблице → 0 XP + warning в лог.

**Множитель за врага.** `XPConfig.EnemyXPMultiplier: TMap<TSubclassOf<AShooterNPC>, float>` — индивидуальный множитель к `BaseXPPerKill` категории. Кого нет в таблице → 1.0x.

**Доверие attribution.** `XPConfig.AlwaysAttributeToPlayer: TSet<TSubclassOf<UDamageType>>` — DamageType, которые засчитываются игроку всегда, даже если движок не видит цепочки `DamageCauser → Player`. Нужно для пропов и брошенных NPC, у которых `Instigator/Owner == NULL` (gameplay-implied attribution).

**Кривые уровней.** Per-skill `LevelThresholds: TArray<int32>` (cumulative XP до уровня). Длина массива = `MaxLevel`. Default — арифметика 100/250/450/.../4500 (12 уровней, дельта +50 каждый раз).

**Level-up.** На пересечении порога — `UXPSubsystem` шлёт `OnSkillLevelUp(Category, NewLevel)`. Открывается модальный `UUpgradeChoiceWidget` (3 случайных апгрейда из пула категории, исключая `IsUpgradeMaxedOut`). Игрок выбирает → `UUpgradeManagerComponent::GrantUpgrade()`. FIFO-очередь, если несколько уровней пришли одновременно.

**Лайфцикл рана.** `URunSubsystem` (`GameInstanceSubsystem`) координирует: `StartRun` / `EndRun(ERunEndReason)` / `EnterArena(int32)` / `ClearArena(int32)`. Per-run sub-systems (XP, run stats) подписываются на `OnRunStarted` и сбрасывают своё состояние.

---

## 1. ДВИЖЕНИЕ ИГРОКА

### 1.1 Базовое движение

| Параметр          | Значение |
| ----------------- | -------- |
| WalkSpeed         | 700      |
| CrouchSpeed       | 400      |
| JumpZVelocity     | 500      |
| MaxJumpCount      | 2        |
| CoyoteTime        | 0.165s   |
| JumpLurchVelocity | +100     |

### 1.2 Скольжение

| Параметр | Значение |
|----------|----------|
| MinStartSpeed | 850 |
| MinSpeed (выход) | 225 |
| SlideFriction | 0.0 |
| SlideJumpBoost | +100 |

### 1.3 Стенобег

| Параметр | Значение |
|----------|----------|
| MaxDuration | 1.5s |
| PeakSpeedMultiplier | 1.4x |
| WallJumpUpForce | 500 |
| WallJumpSideForce | 400 |
| GravityScale | 0.0 |

### 1.4 Отскок от стены

| Параметр | Значение |
|----------|----------|
| BounceElasticity | 0.8 |
| MinSpeed | 600 |

### 1.5 Воздушный рывок

| Параметр | Значение |
|----------|----------|
| AirDashSpeed | 2000 |
| AirDashCooldown | 0.8 s |
| AirDashMaxCount | 1 |

### 1.6 Воздушное пикирование

| Параметр | Значение |
|----------|----------|
| AngleThreshold | -15° |

### 1.7 Rocket Boost (зарядомёт под ноги)

| Параметр                                  | Значение                              |
| ----------------------------------------- | ------------------------------------- |
| Макс. скорость (цепочка выстрелов)        | ~4,000 cm/s (40 м/с)                  |
| Характерная скорость перемещения по арене | ~2,000 cm/s (20 м/с)                  |
| Источник импульса                         | Отталкивание от снаряда своего заряда |

> Характерная скорость 20 м/с достигается комбинацией стенобега, скольжения, EMF-зарядов на уровне и rocket boost. Потолок в 40 м/с — последовательные выстрелы зарядомётом под ноги в открытом пространстве.

---

## 2. EMF / ПОЛЯРНОСТЬ

> **Терминология.** «**Электризованный**» = получивший отрицательный заряд (ионизация наоборот).
> Применяется к цели, на которой волновой пистолет, удар ближнего боя или другой источник наложил (−).
> Электризованную цель притягивает зарядомёт (+) и можно захватить через канализацию.
> Сохранять термин в коде и в доке (в коде уже зафиксирован как `IonizationChargePerSecond`, `bElectrifyNegative`).

> **Упрощённый онбординг (текущая версия):** игрок зафиксирован на положительной
> полярности. Объекты (NPC, пропы, оружие) электризуются отрицательным — поэтому
> их всегда можно захватить. Кнопка смены полярности игрока временно убрана,
> зарезервирована для будущего уровня (механика «Cause and Effect» — управление
> полярностью окружения, не игрока). Гейт: `EMFVelocityModifier::bAllowPolarityToggle`.

### 2.1 Заряд

| Параметр          | Значение |
| ----------------- | -------- |
| BaseCharge        | +10.0    |
| MaxBaseCharge     | +50.0    |
| ChargePerMeleeHit | +5.0     |
| Полярность игрока | +1 (зафиксирована) |

### 2.2 Переключение полярности

**Зарезервировано** — кнопка убрана из текущего билда. В будущем уровне будет
переключать полярность окружения (статические заряды на арене), а не игрока.

Легаси-параметры (доступны при `bAllowPolarityToggle = true`):

| Параметр | Значение |
|----------|----------|
| AnimationDuration | 0.5s |
| Cooldown | 0.3s |

### 2.3 Канализация (Press-Press, Void Breaker-style)

Жмётся первый раз — попытка захвата. Если ничего не схватилось — кнопка
моментально доступна снова. Если схватилось — короткий lockout, потом второе
нажатие запускает захваченное вперёд (плита инвертирует свой заряд, игрок
визуально остаётся положительным).

| Параметр                | Значение                                                                |
| ----------------------- | ----------------------------------------------------------------------- |
| PlateOffset             | 200                                                                     |
| PlateDimensions         | 200 × 200                                                               |
| CaptureToLaunchLockout  | 0.25s (анти-спам окно после успешного захвата)                          |
| ReverseChargeDuration   | 0.4s                                                                    |
| Эффект (захват)         | Спавн пластины +, поле игрока отключено, удержание ОДНОЙ цели           |
| Эффект (запуск)         | Пластина инвертируется в −, захваченный объект отталкивается вперёд     |

**Состояния (state machine идентична легаси-hold-mode — lockout живёт как таймер внутри Channeling, не как отдельное состояние, чтобы анимации не имели лишних переходов):**
1. `Ready` — простой
2. `HidingWeapon` — wind-up анимация (0.15s)
3. *(сразу после wind-up — синхронный одноразовый скан конуса)*
4. `Channeling` — цель захвачена, ждёт второго нажатия. Внутри: `CaptureLockoutTimeRemaining` (0.25s) блокирует второе нажатие.
5. `ReverseChanneling` — запуск (0.4s)
6. `FinishingAnimation` → `ShowingWeapon` → `Cooldown` → `Ready`

Если на скане цель не найдена — переход 3 → `FinishingAnimation` напрямую.

Гейт: `ChargeAnimationComponent::bUsePressPressCaptureMode = true`.
При `false` возвращается легаси hold-mode (зажать → канализация, отпустить → выход).

### 2.4 Силы притяжения / отталкивания

| Параметр | Значение |
|----------|----------|
| Одинаковые заряды | Отталкивание |
| Противоположные заряды | Притяжение |
| MaxForce | 100,000 |
| OppositeChargeMinDistance | 35 |
| MaxSourceDistance | 10,000 |

### 2.5 Захват NPC (Hard Hold)

| Параметр | Значение |
|----------|----------|
| CaptureBaseRange | 500 |
| Формула дальности | BaseRange × max(1, 1 + ln(\|q₁×q₂\| / 50)) |
| CaptureBaseSpeed | 1500 |
| CaptureSnapDistance | 50 |
| CaptureReleaseTimeout | 0.5s |

---

## 3. ОРУЖИЕ

### 3.1 Хитскан (Волновой пистолет)

Электризатор без прямого урона. **Единственное** назначение — наложение (−) заряда на цель. Стартовое и единственное оружие в начале рана. Зарядомёт, меч и прочее — через дроп / yank / мета-апгрейды.

| Параметр          | Значение |
| ----------------- | -------- |
| Damage            | 0        |
| RefireRate        | 0.15s    |
| MaxRange          | 10,000   |
| MagazineSize      | inf      |
| AimVariance       | 0°       |
| FullAuto          | Нет      |
| ChargeCostPerShot | 0        |

<!--
Легаси: волновая винтовка (предыдущая версия оружия). Закомментирована до решения
о её судьбе — может быть восстановлена как отдельный ствол / мета-апгрейд.

| Параметр          | Значение |
| ----------------- | -------- |
| Damage            | 4.0      |
| RefireRate        | 0.096s   |
| MaxRange          | 10,000   |
| MagazineSize      | inf      |
| AimVariance       | 0°       |
| FullAuto          | Да       |
| ChargeCostPerShot | 0.1      |
-->


### 3.2 Электризация (основная функция волнового пистолета)

Каждый выстрел волнового пистолета накладывает (−) заряд на цель — это его *единственное* боевое назначение. Игрок (+) → цель (−) → притяжение зарядомёта и возможность захвата через канализацию (см. §2.3, §2.5). Заряд накапливается с каждым попаданием до `MaxIonizationCharge`.

Эффект работает независимо от полярности игрока — даже если `bAllowPolarityToggle` включён и игрок временно (−), пистолет всё равно даёт цели (−).

| Параметр                      | Значение                                                |
| ----------------------------- | ------------------------------------------------------- |
| IonizationChargePerSecond     | 5.0 (модуль)                                            |
| MaxIonizationCharge (модуль)  | 20.0                                                    |
| Знак                          | Отрицательный (−) при `bElectrifyNegative = true`       |
| Легаси-режим                  | `bElectrifyNegative = false` → +20 (старое поведение)   |

### 3.3 EMF Снаряды (Зарядомёт)

| Параметр                  | Значение                                                   |
| ------------------------- | ---------------------------------------------------------- |
| HitDamage                 | 75.0                                                       |
| DefaultCharge             | 2.0                                                        |
| ChargeCostPerShot         | 1                                                          |
| ChargeTransferRatio       | 50%                                                        |
| bAffectedByExternalFields | Да                                                         |
| Rocket Boost              | Отталкивание от собственного снаряда при выстреле под ноги |

### 3.4 Ближнее оружие (ShooterWeapon_Melee)

Подбираемое оружие ближнего боя. Класс `AShooterWeapon_Melee`.

| Параметр                | Значение                                          |
| ----------------------- | ------------------------------------------------- |
| MeleeDamage             | 25                                                |
| MeleeHeadshotMultiplier | 1.5x                                              |
| AttackRange             | 200                                               |
| AttackRadius            | 40                                                |
| AttackAngle             | 15°                                               |
| RefireRate              | 0.4s                                              |
| bFullAuto               | Нет (один удар за нажатие)                        |
| MaxHitCount             | 0 (0 = бесконечно, иначе ломается после N ударов) |

**Momentum:**

| Параметр | Значение |
|----------|----------|
| MomentumDamagePerSpeed | +10 за 100 cm/s |
| MaxMomentumDamage | 50.0 |
| bPreserveMomentum | Да |
| MomentumPreservationRatio | 1.0 |
| bTransferMomentumOnHit | Да |
| MomentumTransferMultiplier | 1.0 |




**Drop Kick:**

| Параметр | Значение |
|----------|----------|
| bEnableDropKick | Да |
| DropKickMinHeightDifference | 100 |
| DropKickPitchThreshold | 45° |
| DropKickDamagePerHeight | +10 за 100 cm |
| DropKickMaxBonusDamage | 100 |


При взятом в руки мече игрок не может бить рукой обычным ударом из пункта 4. Отличия заключаются в том, что для меча нет выпада, притягивания игрока и цели и knockback, а так же нет кд на дропкик - важно для спидрана, но создаёт двойственность - нельзя откинуть условный дрон ударом в стену и убить сразу же с помощью мечом, а та же самая механика обычной атаки позволяет так сделать


---

## 4. БЛИЖНИЙ БОЙ

### 4.1 Базовая атака

| Параметр           | Значение     |
| ------------------ | ------------ |
| BaseDamage         | 50.0         |
| HeadshotMultiplier | 1.5x         |
| AttackRange        | 150          |
| Cooldown           | 4s, 2 заряда |

### 4.2 Momentum Damage

| Параметр | Значение |
|----------|----------|
| BonusDamagePerSpeed | +10.0 за 100 cm/s |
| MaxMomentumDamage | 50.0 |

### 4.3 Knockback

| Параметр | Значение |
|----------|----------|
| BaseDistance | 200 |
| DistancePerSpeed | +0.15 за 1 cm/s |

### 4.4 Кулдаун ближнего боя (зарядная система)

| Параметр | Значение |
|----------|----------|
| MeleeTotalCooldown | 8.0s |
| MeleeMaxCharges | 2 |
| Время на 1 заряд | 4.0s (Total / MaxCharges) |

- Обычная атака расходует **1 заряд при попадании**. Промахи бесплатные.
- Drop Kick расходует **все заряды** (до 2) при попадании и сбрасывает таймер восстановления.
- Drop Kick может быть выполнен при 1 заряде.
- При 0 зарядов атаковать нельзя.
- Заряды восстанавливаются по одному (каждый за 4s).
- UI показывает кулдаун только при попадании (промахи не отображаются).

### 4.5 Drop Kick

| Параметр | Значение |
|----------|----------|
| MinHeight | 100 |
| BonusDamagePerHeight | +10 за 100 cm |
| MaxBonusDamage | 100 |
| DiveSpeed | 2,500 |

### 4.6 Магнетизм цели

| Параметр | Значение |
|----------|----------|
| Range | 300 |
| PullSpeed | 800 |

### 4.7 Lunge (Бросок к цели)

| Параметр | Значение |
|----------|----------|
| LungeSpeed | 2,000 |
| MinSpeedToActivate | 300 |
| MomentumPreservation | 100% |

### 4.8 Заряд от ближнего боя

Удар в ближнем бою — второй (помимо волнового пистолета) способ электризовать цель. Каждое попадание сдвигает заряды в противоположных направлениях: игрок получает (+), цель получает (−). Оба модуля растут — у игрока копится потенциал для зарядомёта/канализации, цель становится более «магнитной» для (+) снарядов и захватываемой каналом.

| Параметр | Значение |
|----------|----------|
| ChargePerHit (игрок) | +2.0 |
| ChargeChangeOnNPC | -25.0 (электризация цели) |

---

## 5. ВРАГИ

### 5.1 Стрелок (ShooterNPC) — *ИИ-робот*

| Параметр | Значение |
|----------|----------|
| HP | 100 |
| AimRange | 10,000 |
| AimVariance | 10° |
| BurstShotCount | 5 |
| BurstCooldown | 1.5s |
| BaseSpread (точность) | 2° |
| MaxSpread | 20° |
| MaxTargetSpeed (для spread) | 1,200 |
| WallRunSpreadMultiplier | 1.3x |
| InAirSpreadMultiplier | 1.2x |
| RetreatDistance | 500 |
| RetreatProximityTrigger | 250 / 1.5s |
| KnockbackDistanceMultiplier | 1.0 |

### 5.2 Мили (MeleeNPC) — *ИИ-робот*

| Параметр | Значение |
|----------|----------|
| HP | 100 |
| AttackDamage | 25 |
| AttackRange | 150 |
| AttackCooldown | 1.0s |
| AttackMagnetismSpeed | 600 |
| DamageWindowStart | 0.2s |
| DamageWindowDuration | 0.3s |
| TraceRadius | 40 |
| TraceDistance | 120 |
| DashDuration | 0.3s |
| DashCooldown | 2.0s |
| DashKnockbackMultiplier | 2.5x |
| bDashTracksTarget | Да |
| KnockbackDistanceMultiplier | 1.0 |

### 5.3 Дрон (FlyingDrone) — *ИИ-робот*

| Параметр | Значение |
|----------|----------|
| HP | 300 |
| FlySpeed | 600 |
| VerticalSpeed | 400 |
| MinHoverHeight | 200 |
| MaxHoverHeight | 450 |
| DefaultHoverHeight | 300 |
| DashSpeed | 1,500 |
| DashDuration | 0.3s |
| DashCooldown | 2.0s |
| EvasiveDashCooldown | 3.0s |
| bExplodeOnDeath | Да |
| ExplosionRadius | 200 |
| ExplosionDamage | 30 |
| EngageRange | 2,000 |
| KnockbackDistanceMultiplier | 1.0 |

### 5.4 Снайперская турель (SniperTurretNPC) — *ИИ-робот*

Стационарная турель с прогрессивным прицеливанием. Наследует `AShooterNPC`. Не двигается, не нокбэчится, не оглушается, не захватывается. Стреляет независимо от координатора.

| Параметр | Значение |
|----------|----------|
| HP | 100 |
| BurstShotCount | 1 (один точный выстрел) |
| BurstCooldown | 0.0s |
| bUseCoordinator | Нет (стреляет независимо) |
| KnockbackDistanceMultiplier | 0.0 (иммунна к нокбэку) |
| PerceptionDelay | 0.75s |

**Прицеливание:**

| Параметр                    | Значение                    |
| --------------------------- | --------------------------- |
| AimDuration                 | 3.0s (время полной наводки) |
| PostFireCooldownDuration    | 1.5s                        |
| DamageRecoveryDelay         | 1.5s                        |
| AimInterruptDamageThreshold | 1.0                         |


**Вращение:**

| Параметр            | Значение |
| ------------------- | -------- |
| TurretRotationSpeed | 90°/s    |
| MaxPitchUp          | 90°      |
| MaxPitchDown        | 60°      |

**Стейт-машина прицеливания (ETurretAimState):**
1. **Idle** — нет цели
2. **Aiming** — прогресс наводки 0.0 → 1.0 за `AimDuration`
3. **Firing** — наводка завершена, выстрел
4. **DamageRecovery** — сброс после получения урона ≥ порога
5. **PostFireCooldown** — кулдаун после выстрела

**Прерывание:** удар ≥ `AimInterruptDamageThreshold` сбрасывает прогресс и переводит в DamageRecovery. Потеря LOS сбрасывает прогресс мгновенно (без задержки восстановления).

**Иммунитеты:** гравитация отключена, CharacterMovement отключён, оглушение и захват сбрасываются каждый тик.

**Делегаты:** `OnAimProgressChanged(Progress, State)`, `OnTurretFired`.

### 5.5 Гуманоид (HumanoidNPC) — *Органика*

Стрелок-наследник `MeleeNPC` с инвентарём ранжированного оружия. **Тело гуманоида нельзя взять в захват ни одним из EMF-методов** — игрок вместо этого выдёргивает (yank) оружие из рук поочерёдно. После того как весь инвентарь изъят, гуманоид переходит в melee-режим: бежит на игрока и больше не принимает заряд.

| Параметр | Значение |
|----------|----------|
| HP | 100 (наследует от ShooterNPC) |
| WeaponInventory | Массив `TSubclassOf<AShooterWeapon>` по порядку yank'а |
| WeaponDropMapping | 1:1 с WeaponInventory — какой `ADroppedRangedWeapon` спавнится на yank |
| WeaponSwitchDelay | 0.6s (между yank и появлением следующего ствола в руках) |

**Иммунитеты тела.** Все EMF-воздействия на сам труп игнорируются — оружие в руках при этом обычным образом притягивается/отталкивается через свой собственный заряд:

| Эффект | Поведение |
|--------|-----------|
| Hard Hold (захват каналом) | Тело не цепляется (`bEnableViscousCapture = false`) |
| Knockback / KnockbackVelocity | `ApplyKnockback*` переопределены пустыми |
| EnterCapturedState (плита канала) | Игнорируется |
| EnterLaunchedState (реверс-запуск) | Игнорируется |
| Приём заряда после входа в melee | Принудительно держится на 0 через `OnChargeUpdated`-хук |

**Yank (выдёргивание оружия).** Канализация в радиусе → текущее оружие пропадает из рук, спавнится `ADroppedRangedWeapon` который притягивается к игроку:

| Параметр | Значение |
|----------|----------|
| Базовый радиус | `ADroppedRangedWeapon::CaptureBaseRange` (по умолчанию 500) |
| Формула радиуса | `BaseRange × max(1, 1 + ln(\|q_npc × q_player\| / NormCoeff))` |
| После yank'а | Заряд тела обнуляется; следующий yank требует нового накопления |
| Анимация | Направленный монтаж — Front (<45°), Back (>135°), Right/Left по бокам |
| Условие | NPC жив, не в melee-режиме, нет активного yank-таймера |

**Переход в melee-режим (`EnterMeleeMode`).** Срабатывает когда `WeaponInventory[CurrentWeaponIndex+1]` становится невалидным (последнее оружие изъято):

1. `bIsInMeleeMode = true`, `bChargeReceptive = false`
2. Текущее оружие уничтожается, заряд тела блокируется на 0
3. Если задан `MeleeAnimClass` — свапается AnimBP меша
4. Если задан `MeleeMaxWalkSpeed > 0` — меняется `MaxWalkSpeed` для агрессивного бега
5. Бродкастится `OnEnteredMeleeMode` (BP — показать нож/мачете, сменить overlay)
6. Дальше — стандартный цикл `MeleeNPC` (Chase → Pursue → Attack) с базовым AttackRange/Cooldown

При `ResetForPool` с непустым инвентарём — кешированные ranged-значения (AnimBP, MaxWalkSpeed) восстанавливаются, бродкастится `OnExitedMeleeMode` (BP — скрыть нож).

**Делегаты:**

| Делегат | Когда | Назначение |
|---------|-------|-----------|
| `OnEnteredMeleeMode(Humanoid)` | Последнее оружие выдернуто | BP: показать melee-меш, сменить материал/overlay |
| `OnExitedMeleeMode(Humanoid)` | `ResetForPool` вернул ranged-инвентарь | BP: скрыть melee-меш, вернуть ranged-вид |

**Дизайн-смысл.** Гуманоид — единственный противник, которого нельзя моментально нейтрализовать ни пинком, ни плитой канала, ни реверс-запуском. Игрок обязан сначала разоружить его серией yank'ов (требует заряда — копится стрельбой и ближним боем по другим целям), и только потом добивает уже в melee. Это превращает гуманоидов в опорных «приоритетных» противников арены и одновременно в насос ресурсов: каждый yank — новое оружие в инвентаре игрока.

**Опциональный щит** (`UNPCRiotShieldComponent`). BP-наследники гуманоида могут носить riot shield — отдельный yank-таргет, который приоритетнее оружия. Vanilla `BP_HumanoidNPC` остаётся без щита (no-op путь компонента). Подробности и параметры — см. §12 ЩИТ → 12.8 NPC-сторона.

### 5.6 Общие параметры NPC

| Параметр | Значение |
|----------|----------|
| DeferredDestructionTime | 5.0s |
| HitReactionCooldown | 0.5s |
| WallSlamVelocityThreshold | 800 |
| WallSlamDamagePerVelocity | 10 за 100 сверх порога |
| WallSlamCooldown | 0.2s |
| WallBounceElasticity | 0.5 |
| WallBounceMinVelocity | 200 |
| NPCCollisionImpulseMultiplier | 0.7 |
| NPCCollisionDamageMultiplier | 0.4x |
| NPCCollisionMinVelocity | 300 |
| LaunchedMinSpeed | 200 |

---

## 6. УРОН И СТОЛКНОВЕНИЯ

### 6.1 Типы урона

| Тип | Цвет числа | Когда |
|-----|------------|-------|
| Melee | Белый | Ближний бой |
| Ranged | Белый | Стрельба |
| Wallslam | Оранжевый | Удар о стену |
| Dropkick | Оранжевый | Drop kick |
| MomentumBonus | Оранжевый | Бонус от скорости |
| EMFWeapon | Электро-синий | EMF оружие |

### 6.2 Числа урона (батчинг)

| Параметр | Значение |
|----------|----------|
| bEnableBatching | Да |
| BatchingWindow | 0.5s |
| MinDamageToShow | 1.0 |
| DamageForMaxScale | 100.0 |
| MinScale / MaxScale | 0.8 / 2.0 |
| PoolSize | 20 |

### 6.3 Wall Slam

| Параметр | Значение |
|----------|----------|
| VelocityThreshold | 800 |
| DamagePerVelocity | 10 за 100 сверх порога |
| Cooldown | 0.2s |

### 6.4 NPC → NPC столкновения

| Параметр | Значение |
|----------|----------|
| ImpulseMultiplier | 0.7 |
| DamageMultiplier | 0.4x от wallslam |
| MinVelocity | 300 |

### 6.5 Физ. пропы

| Параметр | Значение |
|----------|----------|
| CollisionVelocityThreshold | 800 |
| DamagePerVelocity | 10 за 100 |
| ExplosionDamage (если вкл.) | 50 |
| ExplosionRadius | 300 |
| ExplosionImpulse | 1,600 |
| bSpawnHealthOnExplode | Да |
| HealthPickupClass | Пикап HP |

**Спавн HP при взрыве:** Взрывные пропы (`bCanExplode`) при детонации спавнят пикап здоровья. Это даёт игроку дополнительный источник хила помимо убийств NPC через пропы.

### 6.6 Оглушение взрывом пропа (Explosion Stun)

При взрыве EMF-пропа все NPC в радиусе взрыва получают стан — переходят в состояние Knockback без перемещения (AI замораживается на месте, стрельба прекращается, pathfinding останавливается). На оглушённых NPC проигрывается Anim Montage.

| Параметр | Значение |
|----------|----------|
| bApplyExplosionStun | Да |
| ExplosionStunDuration | 2.0s |
| ExplosionStunMontage | null (fallback на KnockbackMontage NPC) |

**Логика:** `Explode()` → sphere overlap по `ECC_Pawn` в радиусе взрыва → для каждого `AShooterNPC` вызывается `ApplyExplosionStun()` → `bIsInKnockback = true`, `bStunnedByExplosion = true`, `StopMovementImmediately()`, montage, таймер на `EndKnockbackStun()`.

**Делегаты:**
- `OnNPCStunnedByExplosion(StunnedNPC, ExplodedProp, StunDuration)` — на пропе, вызывается для каждого оглушённого NPC
- `OnStunStart(StunnedNPC, Duration)` — на NPC, BlueprintAssignable, при входе в стан
- `OnStunEnd(StunnedNPC)` — на NPC, BlueprintAssignable, при выходе из стана

**Флаг `bStunnedByExplosion`:** устанавливается при оглушении от пропа или дрона, сбрасывается в `EndKnockbackStun()`. Если NPC погибает с этим флагом — дропается хилка независимо от типа урона.

### 6.8 Реверс-запуск: хоминг, мгновенный подрыв, заморозка столкновений

#### Мягкий хоминг (Soft Homing)

При реверс-запуске пропа/NPC — лёгкое отклонение траектории к ближайшему врагу в конусе. Работает на EMFPhysicsProp и EMFVelocityModifier.

| Параметр | Значение по умолчанию |
|----------|----------------------|
| bEnableReverseLaunchHoming | true |
| HomingConeHalfAngle | 15° |
| HomingMaxRange | 3,000 cm |
| HomingStrength | 0.15 (почти незаметный) |
| HomingRampUpTime | 0.1s |

**Логика:** `FindHomingTarget()` → sphere overlap ECC_Pawn → фильтр по конусу + !IsDead() → score = Dot / (Dist / MaxRange) → лучшая цель → `AimDir = Lerp(AimDir, DirToTarget, Strength * RampAlpha)`.

#### Мгновенный подрыв взрывных пропов

Взрывные пропы (`bCanExplode`) в реверс-полёте при контакте с NPC — пропускают кинетический урон, сразу вызывают `Explode()`. NPC получает ExplosionDamage (50) + стан, а не 100+ кинетического урона.

#### Заморозка NPC-NPC столкновений (Impact Freeze)

При столкновении запущенного NPC с другим NPC — оба останавливаются, knockback уменьшается в 5x.

| Параметр | Значение по умолчанию |
|----------|----------------------|
| NPCCollisionPostImpactKnockbackMultiplier | 0.2 |
| NPCCollisionImpactVFX | null (слот) |
| NPCCollisionImpactVFXScale | 1.5 |
| NPCCollisionImpactSound | null (слот) |

### 6.9 Взрыв дрона (Drone Explosion)

Дрон (`bExplodeOnDeath = true`) взрывается при смерти. Ручной overlap вместо ApplyRadialDamage для обхода friendly-fire.

| Параметр | Значение |
|----------|----------|
| ExplosionRadius | 200 |
| ExplosionDamage | 30 |
| DamageType | DamageType_DroneExplosion |
| DamageCauser | PlayerPawn (обход friendly-fire) |
| Falloff | Линейный (100% → 0% от центра к краю) |
| bApplyExplosionStun | true |
| ExplosionStunDuration | 2.0s |
| ExplosionStunMontage | null (fallback на KnockbackMontage NPC) |

**Оглушение:** Логика идентична пропам — `TriggerExplosion()` делает отдельный sphere overlap, для каждого живого NPC в радиусе вызывает `ApplyExplosionStun()`. Оглушённые дроном NPC получают `bStunnedByExplosion = true` и дропают хилки при убийстве.

**Debug:** `[Drone Explosion]` логи: количество overlaps, для каждого попадания — дистанция, scale, урон, блокировки LOS, оглушённые NPC.

---

## 7.  ПИКАПЫ

### 7.1 Пикапы здоровья

Дропаются с NPC **только** при:
1. **Убийство от пропа** — кинетический урон (столкновение) или взрыв (DamageCauser = AEMFPhysicsProp)
2. **Убийство от дрона** — кинетический урон при столкновении с летящим дроном (DamageCauser = AFlyingDrone)
3. **Само-уничтожение дрона** — дрон врезается в стену при полёте от channeling (DamageCauser = дрон сам, self wallslam)
4. **Убийство от взрыва дрона** — DamageType = DamageType_DroneExplosion (DamageCauser = PlayerPawn для обхода friendly-fire)
5. **Убийство оглушённого взрывом NPC** — любой тип урона, если `bStunnedByExplosion = true` (оглушён пропом или дроном)

Не дропаются при: убийстве из винтовки, зарядомёта, мили, wallslam, dropkick (если NPC не оглушён взрывом).

**Friendly-fire bypass:** Урон типа Wallslam пропускается через проверку friendly-fire, чтобы NPC-NPC столкновения корректно передавали DamageCauser.

| Параметр | Значение |
|----------|----------|
| HealAmount | 25 HP |
| MagnetRadius | 500 |
| MagnetSpeed | 1,500 |
| Lifetime | 15s |


### 7.4 Магнитное притяжение пикапов

Пикапы (HP) используют прямое преследование — каждый кадр двигаются строго к игроку без инерции. Скорость нарастает квадратично от 10% до 100% MagnetSpeed. Невозможно промахнуться мимо игрока.

---

## 8. ОБРАТНАЯ СВЯЗЬ

### 7.1 Хит-маркер

| Тип | Цвет | Длительность |
|-----|------|-------------|
| Обычный | Белый | 0.15s |
| Хэдшот | Красноватый | 0.15s |
| Убийство | Красный, ×1.5 размер | 0.4s |

---

## 9. БОЕВЫЕ ПЕТЛИ

### Петля 1: EMF на уровне → Набор высоты → Дропкик
> Найди точечный заряд или пластину → Подпрыгни на них → Сделай эффектный дропкик
> Дропкик → Ваншот наземного противника и большой урон + отбрасывание для воздушного → потенциальный удар об стену

### Петля 2: Электризация → Самонаводящиеся снаряды
> Обстреляй цель из волнового пистолета (электризует отрицательным) → переключись на зарядомёт → стреляй в сторону противника → положительный снаряд притянется к отрицательной цели и нанесёт 75 урона

### Петля 3: Зарядомёт → Rocket Boost → Дропкик
> Выстрели себе под ноги из зарядомёта → набери высоту → Сделай дропкик → Получи заряд за удар в ближнем бою → Можно выполнить опять

### Петля 4: Канализация → Захват → Уничтожение захваченного
> Жми кнопку канала → захват одной цели в конусе → стреляй в захваченного противника из винтовки → надёжный способ расправиться с одиночной целью на расстоянии

### Петля 5: Канализация → Захват NPC → Запуск
> Жми → захват NPC (lockout 0.25s)
> Жми ещё раз → реверс-плита → NPC летит как снаряд
> NPC → стена = wallslam урон
> NPC → другой NPC = collision урон + оглушение выжившего

### Петля 6: Канализация → Захват предмета → Запуск → HP
> Жми → захват пропа (lockout 0.25s)
> Жми ещё раз → реверс-плита → проп летит как снаряд
> проп → коллизия = взрыв → отбрасывание + урон + оглушение ближайших NPC (2s) + спавн пикапа HP → потенциально уничтожить всех противников в зоне оглушения мечом с AoE уроном

---

## 10. AI КООРДИНАТОР (AICombatCoordinator)

Синглтон-актор, управляющий тремя системами: **токены атаки**, **боевой круг** и **роли/давление**. Автоматически создаётся через `GetCoordinator()` или размещается вручную (один на арену/сублевел).

### 10.1 Система токенов

Заменяет плоский лимит одновременных атакующих на типизированные пулы. Каждый NPC автоматически определяется как Ranged (ShooterNPC, FlyingDrone) или Melee (MeleeNPC).

| Параметр | По умолчанию | Эффект |
|----------|-------------|--------|
| MaxRangedTokens | 2 | Макс. одновременных стрелков |
| MaxMeleeTokens | 1 | Макс. одновременных melee |
| MaxSpecialTokens | 1 | Зарезервировано (боссы, гранаты) |
| MinTimeBetweenAttacks | 0.1s | Пауза между выдачей токенов. Увеличить → реже стреляют |
| ProximityOverrideDistance | 250cm | NPC ближе этого расстояния атакует без токена |
| bAllowTokenStealing | Да | NPC с LOS может отобрать токен у NPC без LOS |
| AttackPermissionTimeout | 2.0s | Через сколько отбирается неиспользованный токен |

**Кражка токенов:** NPC с прямой видимостью на игрока и ближе к нему может забрать токен у NPC без видимости. Предотвращает ситуацию, когда токен держит NPC за стеной.

**Retaliation:** `GrantRetaliationPermission()` обходит все лимиты — NPC, которого обстреляли, всегда может ответить.

**Тюнинг агрессивности:**
- Спокойный бой: `MaxRangedTokens=1`, `MinTimeBetweenAttacks=1.0s`
- Умеренный: `MaxRangedTokens=2`, `MinTimeBetweenAttacks=0.5s`
- Агрессивный: `MaxRangedTokens=3`, `MinTimeBetweenAttacks=0.1s`

### 10.2 Боевой круг (Battle Circle)

Слотовое позиционирование NPC вокруг игрока в трёх концентрических кольцах. Заменяет рандомное перемещение по NavMesh.

| Кольцо | Радиус (см) | Кто по умолчанию |
|--------|------------|------------------|
| Inner | 400–600 | MeleeNPC, Aggressors |
| Middle | 600–1,200 | ShooterNPC, Supporters |
| Outer | 1,200–2,000 | FlyingDrone |

| Параметр | По умолчанию | Эффект |
|----------|-------------|--------|
| bUseBattleCircle | Да | Вкл/выкл (выкл → рандомный NavMesh) |
| SlotRecalculationInterval | 0.5s | Как часто слоты пересчитываются вслед за игроком |

Слоты равномерно распределяются по кольцу (360° / кол-во NPC в кольце). При гибели NPC слоты перегенерируются. `PickNewDestination` в StateTree-тасках сначала запрашивает слот у координатора, и только при отсутствии слота использует рандомную точку.

### 10.3 Роли и система давления

Динамическое назначение ролей каждые 0.1s на основе позиции NPC и состояния игрока.

| Роль | Условие | Поведение |
|------|---------|-----------|
| Aggressor | Активно атакует, или ближайший без роли | Всегда минимум 1. Стремится к Middle/Inner |
| Supporter | По умолчанию | Средняя дистанция, ждёт токен |
| Flanker | Угол от взгляда игрока ≥ 90° | Заходит со спины |
| Pressurer | HP игрока < 30% или Armor < 10% | Подталкивает к ресурсным петлям |

| Параметр | По умолчанию | Эффект |
|----------|-------------|--------|
| LowHPThreshold | 0.3 (30%) | Ниже этого HP → MeleeNPC получают Pressurer, идут на Inner кольцо |
| LowArmorThreshold | 0.1 (10%) | Ниже этого Armor → NPC группируются плотнее (Middle кольцо) |
| FlankerMinAngle | 90° | Мин. угол от взгляда игрока для роли Flanker |

**Связь с ресурсными петлями:**
- HP < 30% → MeleeNPC идут на Inner → игроку проще ударить в ближнем бою → хилки (петля 1, 3)
- Armor < 10% → NPC группируются ближе → игроку проще захватить через channeling → броня (петля 4, 5)

### 10.4 Отладка

Три независимых флага в Details панели координатора:

| Флаг | Что рисует |
|------|-----------|
| bDrawDebug | Статус токенов над NPC, сводка пулов над игроком, сфера engagement range |
| bDrawBattleCircle | Кольца Inner/Middle/Outer, сферы слотов, линии NPC→слот |
| bDrawRoleDebug | Имена ролей + угол над NPC, стрелка взгляда игрока, конус фланкера, HP/Armor + статус давления |

---

## 11. АПГРЕЙДЫ

### 11.1 Архитектура

- **UUpgradeDefinition** — Data Asset, поля: `UpgradeTag` (GameplayTag), **`Category`** (`ESkillCategory` — определяет в какой пул choice-а попадает), **`MaxLevel`** (default 1), `DisplayName`, `Description`, `Icon`, `Tier`, `ComponentClass`. Подкласс `UUpgradeDefinition_X` добавляет `TArray<FXLevelData> LevelData` — designer редактирует per-level параметры в редакторе, длина массива через `PostEditChangeProperty` авто-синкается с `MaxLevel`.
- **UUpgradeComponent** — рантайм-логика, дин. компонент на `ShooterCharacter`. Хранит **`CurrentLevel` (1+)**. Хуки: `OnUpgradeActivated()` (на первом гранте, Lv 1), **`OnLevelChanged(Old, New)`** (на повышении уровня, Lv N → N+1), `OnUpgradeDeactivated()`, и игровые: `OnWeaponFired/Changed`, `OnOwnerTookDamage/DealtDamage`, `GetDamageMultiplier()`. Подкласс читает `LevelData[CurrentLevel-1]` и применяет к персонажу.
- **UUpgradeManagerComponent** — на персонаже. `GrantUpgrade(Def)` создаёт компонент при первом гранте + `OnUpgradeActivated`; на повторе — повышает `CurrentLevel` + `OnLevelChanged`; на максимуме — no-op (`false`). Геттеры: **`GetUpgradeLevel(Tag)`** (0 если не владеет), **`IsUpgradeMaxedOut(Def)`**. Делегаты: `OnUpgradeGranted`, `OnUpgradeRemoved`, **`OnUpgradeLeveledUp(Def, NewLevel)`**. `GetCombinedDamageMultiplier()` перемножает множители всех апгрейдов.
- **UUpgradeRegistry** — каталог всех `UUpgradeDefinition` (Data Asset с `AllUpgrades: TArray<UUpgradeDefinition*>`). `UUpgradeChoiceWidget` ролит pool фильтром `Category == LevelledUpCategory && !IsUpgradeMaxedOut`.

Подробный шаблон создания нового апгрейда (struct LevelData → подкласс Definition → подкласс Component → DataAsset → Registry) — в `Polarity/Upgrades/Upgrades/AUTHORING_GUIDE.md`.

### 11.2 Получение апгрейдов

Два пути: **(1) world-pickup** через EMF channeling и **(2) level-up choice** при пересечении XP-порога.

- **AUpgradePickup** — актор в мире. Два оверлапа: `PickupCollision` (захват EMF) и `TooltipTrigger` (тултип). Игрок подбирает через channeling — `StartPull()` → плавная интерполяция к камере → `CompletePull()` → `GrantUpgrade()`.
- **Level-up choice** — `UUpgradeChoiceWidget` (модал) подписан на `UXPSubsystem::OnSkillLevelUp`. Ролит N=3 случайных апгрейда из `Registry.AllUpgrades` фильтром по категории и `IsUpgradeMaxedOut`, ставит `SetGamePaused(true)`, ждёт выбор → `GrantUpgrade()` → unpause. FIFO-очередь, если уровни пришли одновременно.


### 11.3 Философия апгрейдов

Апгрейды не делают игрока сильнее напрямую — они **расширяют боевой словарь**, добавляя новые петли и взаимодействия поверх базовых механик. Каждый апгрейд привязан к определённому стилю игры и поощряет рискованное поведение:

- **Скилл-потолок, а не числа:** апгрейды требуют выполнения условий (360° поворот, попадание в снаряд, движение на врага) — бонус получает только тот, кто играет агрессивно и точно
- **Синергия с ядром:** каждый апгрейд усиливает одну из существующих боевых петель (стрельба, движение, EMF), а не создаёт изолированную механику
- **Риск-награда:** бонусы значительны, но требуют подставляться (бежать на врага, крутиться под огнём, стрелять по своим снарядам)
- **Не обязательны:** базовый геймплей полностью функционален без апгрейдов — они для мастеринга, не для прогрессии
  
  *прим. от не ИИ - ВСЕГДА ТОЛЬКО МАКСИМАЛЬНО ГИГАЧАДСКИЕ АПГРЕЙДЫ*

### 11.4 Список апгрейдов

Сгруппировано по категории скилла (`ESkillCategory`). Под каждым апгрейдом — короткое
дизайнерское описание, условие активации, техническое имя класса + DataAsset, и параметры.
Multi-level апгрейды показывают per-level таблицы; single-level — одну таблицу.

---

#### Movement

##### Air Dash

**Описание.** Расширяет базовый воздушный рывок: больше зарядов, короче кулдаун, сильнее
импульс. Это «прорывной» апгрейд для паркур-стиля — поощряет жить в воздухе.
**Условие.** Пассивный — изменяет параметры существующего рывка.
**Тех. имена.** `UUpgrade_AirDash`, `DA_UpgradeDefinition_AirDash`.

Multi-level: каждый уровень увеличивает все три параметра. Конкретные числа — в `LevelData`
DataAsset'а (дизайнер настраивает). Reference-имплементация для шаблона multi-level —
именно AirDash (см. `Polarity/Upgrades/Upgrades/AUTHORING_GUIDE.md`).

| Per-level параметр |
|---|
| MaxCharges |
| CooldownSeconds |
| ImpulseMultiplier |

##### Testosterone Boost

**Описание.** Пассивный множитель урона хитскана и melee в зависимости от направления
движения относительно цели — бежишь на врага и бьёшь сильнее, отступаешь — слабее.
Награждает агрессивное сближение.
**Условие.** Пассивный — направление и скорость движения проверяются при каждом ударе.
**Тех. имена.** `UUpgrade_ForwardMomentum`, `DA_UpgradeDefinition_ForwardMomentum`.

| Параметр | Значение |
|----------|----------|
| ForwardBonusMultiplier | +25% (при движении к цели) |
| BackwardPenaltyMultiplier | -50% (при движении от цели) |
| MinSpeedThreshold | 100 cm/s |
| MaxSpeedForFullEffect | 1,200 cm/s |

---

#### Melee

##### Drop Kick

**Описание.** Анлок-апгрейд. Базовый melee не имеет дроп-кика, пока этот апгрейд не получен:
взятие включает гейт `bDropKickUnlocked` на `MeleeAttackComponent`. После этого работают
все настройки дроп-кика из §4.5 (MinHeight, BonusDamage, DiveSpeed и т.д.).
**Условие.** Пассивный — анлок.
**Тех. имена.** `UUpgrade_DropKick`, `DA_UpgradeDefinition_DropKick`.

##### Combo

**Описание.** Каждое успешное мили-попадание (кулак или меч) в окне `ResetWindow` после
предыдущего инкрементит счётчик; счётчик кормит curve, которая возвращает множитель
play-rate анимации (и `RefireRate` оружия). Чем длиннее цепочка — тем быстрее удары.
Multikill от Charged Punch и Drop Kick засчитывается за N хитов.
**Условие.** Не промахиваться, не делать паузу длиннее окна.
**Тех. имена.** `UUpgrade_Combo`, `DA_UpgradeDefinition_Combo`.

**Уровень 1** — комбо сбрасывается на любой промах (агрессивный стиль).

| Параметр | Значение |
|---|---|
| Curve (count → mult) | `C_ComboCountToMultiplier_v1` (дизайнер) |
| ResetWindow | 1.5s |
| MaxMultiplier | 3.0 |
| bResetOnMiss | **true** |

**Уровень 2** — комбо устойчиво к промахам, сбрасывается только по таймауту.

| Параметр | Значение |
|---|---|
| Curve (count → mult) | `C_ComboCountToMultiplier_v2` (дизайнер) |
| ResetWindow | 1.5s |
| MaxMultiplier | 3.0 |
| bResetOnMiss | **false** |

##### Backstab *(рабочее название — рассматривается «Your Eternal Reward» / «Sapper»)*

**Описание.** Мили-удар по оглушённому NPC сзади наносит тройной урон. Поощряет тактическое
поведение: оглуши врага взрывом пропа / дроном → зайди со спины → ваншот.
**Условие.** Цель в knockback-стейте (от взрыва, столкновения или сильного удара) **И**
игрок в back-конусе цели.
**Тех. имена.** `UUpgrade_Backstab`, `DA_UpgradeDefinition_Backstab`.

Single-level (массив `LevelData` с одним элементом — масштабируется до Lv 2 одной строкой).

| Параметр | Значение |
|---|---|
| DamageMultiplier | 3.0x |
| BackConeHalfAngle | 90° (вся задняя полусфера) |
| bRequireStunnedByExplosion | false (любой knockback засчитывается) |

---

#### Electrokinesis

##### Air Kick

**Описание.** Когда игрок и EMF-проп оба в воздухе, мили-удар по пропу мгновенно запускает
его в направлении камеры (без анимации канализации). Уровень 1 — точечный урон одному NPC
при попадании пропа + проп отскакивает; Уровень 2 — взрыв при попадании в NPC с фиксированным
уроном и радиусом, независимо от настроек самого пропа.
**Условие.** Оба (игрок и проп) airborne, мили попадает по `AEMFPhysicsProp`.
**Тех. имена.** `UUpgrade_AirKick`, `DA_UpgradeDefinition_AirKick`.

**Глобальные параметры** (одинаковы для обоих уровней):

| Параметр | Значение |
|---|---|
| LaunchSpeed | 3,000 cm/s |
| KickSpinSpeed | 720°/s |
| PropAirborneTraceDistance | 80 cm |

**Уровень 1** — single-target impact, без взрыва.

| Параметр | Значение |
|---|---|
| bExplodeOnImpact | false |
| ImpactDamage | 25 |

Реализация: prop переводится в «weak-impact» путь (`ExplosionMinCharge = ∞`,
`WeakImpactDamage = ImpactDamage`). Прoп отскакивает от первой цели и может попасть ещё.

**Уровень 2** — взрыв радиусом при первом попадании в NPC.

| Параметр | Значение |
|---|---|
| bExplodeOnImpact | true |
| FixedExplosionDamage | 100 |
| FixedExplosionRadius | 300 cm |

Override'ы пропа на момент launch'а: `bCanExplode=true`, `ExplosionMinCharge=0`,
`bScaleExplosionWithCharge=false`, `ExplosionDamageFalloff=5` (почти плоский урон по радиусу).

##### Charged Punch

**Описание.** Удержание кнопки мили дольше `MinHoldTime` накапливает заряд за счёт
неиспользованных HP-пикапов (общий пул, см. ниже §11.5). На отпускание игрок делает
бросковый выпад вперёд: capsule-sweep вдоль траектории наносит урон **всем целям на линии**
(pierce-through), затем игрок физически летит к точке прицела через MOVE_Flying.
**Условие.** Hold-button > MinHoldTime, в пуле есть хоть один пикап.
**Тех. имена.** `UUpgrade_ChargedPunch`, `DA_UpgradeDefinition_ChargedPunch`.

Single-level (массив на 1 элемент — multi-level capable).

| Параметр | Значение |
|---|---|
| MinHoldTime | 0.15s |
| MaxHoldTime | 1.5s |
| PickupsPerSecond | 3.0 |
| MaxDistance | 1,000 cm |
| MaxBonusDamage | 100 |
| LungeDuration | 0.15s |
| CapsuleRadius | 60 |
| AirAttackMontage | (дизайнер) — анимация выпада в FP-виде |
| HoldTimeToDistance | (дизайнер) curve, fallback linear до MaxDistance |
| HoldTimeToBonusDamage | (дизайнер) curve, fallback linear до MaxBonusDamage |

Multikill (количество убитых одним выпадом) → кормит Combo.

##### Health Blast

**Описание.** Каждый HP-пикап, подобранный на полном HP, копится в общий пул. При попытке
канализации в пустоту (без цели в конусе) накопленные хилки выстреливают шотган-веером
вперёд — каждая хилка отдельный снаряд с уроном и нокбэком, плюс отдача игроку назад.
Превращает «лишние» хилки в movement + damage burst.
**Условие.** Канал нажат, нет цели в конусе захвата, в пуле есть пикапы.
**Тех. имена.** `UUpgrade_HealthBlast`, `DA_UpgradeDefinition_HealthBlast`.

| Параметр | Значение |
|---|---|
| DamagePerPickup | 30 |
| PlayerKnockbackPerPickup | 400 |
| TargetKnockbackPerPickup | 600 |
| ProjectileSpeed | 3,000 |
| ProjectileLifetime | 2.0s |
| SpreadHalfAngle | 15° |
| MaxStoredPickups | 10 (cap пула, см. §11.5) |
| EmptyCaptureDelay | 0.2s |
| Cooldown | 0.5s |

---

#### Weapon (Gunslinger)

##### 360 Shot

**Описание.** Выполни поворот на 360° за отведённое время → следующий выстрел из винтовки
наносит массивный бонусный урон.
**Условие.** Поворот ≥ 360° за `SpinTimeWindow` секунд; бонус сгорает за `ChargedDuration`
или после одного выстрела; кулдаун.
**Тех. имена.** `UUpgrade_360Shot`, `DA_UpgradeDefinition_360Shot`.

| Параметр         | Значение |
| ---------------- | -------- |
| BonusDamage      | 500      |
| SpinTimeWindow   | 1.5s     |
| ChargedDuration  | 1.0s     |
| MinRotationSpeed | 480°/s   |
| RecoilMultiplier | 2.0x     |
| CooldownDuration | 10.0s    |

##### Charge Flip

**Описание.** Попади в EMF-снаряд из хитскан-винтовки → снаряд взрывается и стреляет
усиленными лучами по всем видимым целям. Цепная реакция с другими снарядами в полёте.
Буквально монетка из ULTRAKILL.
**Условие.** Попадание хитсканом по летящему `EMFProjectile`.
**Тех. имена.** `UUpgrade_ChargeFlip`, `DA_UpgradeDefinition_ChargeFlip`.

| Параметр | Значение |
|----------|----------|
| DamageMultiplier | 2.0x |
| IonizationChargePerHit | 5.0 |
| MaxIonizationCharge | 20.0 |
| MaxChainDepth | 10 |

##### Suppression Fire

**Описание.** Пассивный эффект: попадания хитсканом по `ShooterNPC` подавляют их точность —
паттерн стрельбы превращается в «бублик» (гарантированный промах вокруг игрока). Длительность
зависит от скорости игрока. Апгрейд для спидраннеров.
**Условие.** Пассивный — каждое попадание хитсканом по NPC накладывает суппрессию.
**Тех. имена.** `UUpgrade_SuppressionFire`, `DA_UpgradeDefinition_SuppressionFire`.

| Параметр | Значение |
|----------|----------|
| MinSuppressionDuration | 0.5s |
| MaxSuppressionDuration | 3.0s |
| MinSpeedThreshold | 100 cm/s |
| MaxSpeedForFullEffect | 1,200 cm/s |
| DiminishingReturnsFactor | 0.5 |

---

### 11.5 Shared health-pickup pool

Общий счётчик HP-пикапов на `UUpgradeManagerComponent`, в который кормятся все апгрейды,
работающие с «накопленными хилками». Сейчас потребителей два: **Health Blast** (расходует
весь пул на шотган) и **Charged Punch** (тратит N хилок за секунду удержания).

| Параметр | Значение |
|---|---|
| StoredHealthPickups | 0+ (runtime, инкрементится на HP-pickup при full HP) |
| MaxStoredHealthPickups | 10 (EditAnywhere на UpgradeManager) |

API:
- `AddStoredHealthPickup()` — инкремент, return false если на cap'е.
- `ConsumeStoredHealthPickups(N)` — возвращает реально израсходованное количество.
- `ResetStoredHealthPickups()` — сброс на death/respawn.
- `OnStoredHealthPickupsChanged(Cur, Max)` — multicast delegate для UI/SFX.

Каждый upgrade — потребитель — при активации может поднять `MaxStoredHealthPickups` если
у его определения есть собственный cap выше дефолтного (см. `Upgrade_HealthBlast::OnUpgradeActivated`).

---

## 12. ЩИТ (RIOT SHIELD)

Подбираемая экипировка с собственным HP. Поглощает входящие выстрелы и милитные удары, умеет делать выпад (bash), бросается как метательный снаряд (оглушает NPC при попадании). Носится в одной из рук, активное оружие остаётся доступным с ограничениями. На NPC появляется как опциональный компонент с приоритетным yank-таргетом перед оружием.

### 12.1 Состояния (`ERiotShieldState`)

| Состояние | Описание |
|-----------|----------|
| `Lowered` | Опущен, не блокирует, минимальная видимость на экране |
| `Raised` | Поднят, блокирует входящие хиты, замедляет движение |
| `Transitioning` | Переходит между Raised/Lowered за `StateTransitionTime` |
| `Bashing` | Активный выпад вперёд с уроном (только из Raised) |

### 12.2 Здоровье и блокирование

| Параметр | Значение |
|----------|----------|
| MaxHealth | 200 |
| HealthDrainingDamageTypes | `TArray<TSubclassOf<UDamageType>>` (BP) — типы урона, расходующие HP щита |
| Прочие типы урона | Блокируются без расхода HP |
| Поведение при HP = 0 | Спавн `BreakGeometryCollection`-шрапнели + Destroy актора |
| BreakImpulse / BreakAngularImpulse | 600 / 100 |
| BreakGibLifetime | 3.0s |

**Логика блокирования.** Входящий хит попадает в `ShieldMesh` → `ARiotShield::TakeDamage`. Если DamageType входит в `HealthDrainingDamageTypes` — снимает HP щита. В любом случае хит **не передаётся** на игрока.

### 12.3 Замедление в поднятом состоянии

| Параметр | Значение |
|----------|----------|
| SpeedMultiplierWhenRaised | 0.6 (60% от базового MaxWalkSpeed) |

Применяется при `Raise`, кешированная скорость восстанавливается при `Lower` / `ThrowAway` / `Break`.

### 12.4 Bash (выпад)

| Параметр | Значение |
|----------|----------|
| BashDamage | 30 |
| BashRange | 150 |
| BashRadius | 50 |
| BashDuration | 0.3s |
| BashCooldown | 0.4s |
| BashDamageWindow (alpha) | 0.25 .. 0.6 |
| BashImpulse | 800 (вдоль camera forward) |
| BashDistance (fallback) | 80 cm (если `BashLocationCurve` не задана) |
| BashLocationCurve / BashRotationCurve | `UCurveVector` (alpha 0..1) — программная анимация позы |
| BashDamageType | `TSubclassOf<UDamageType>` (BP) |

**Логика урона.** Sphere trace вперёд от камеры в окне Damage Window (между `BashDamageWindowOpen` и `BashDamageWindowClose`). Каждый hit-актор получает `BashDamage` + импульс. Один актор — один хит за выпад (внутренний `BashHitActorsThisSwing`).

### 12.5 Камера, поза, качание (sway)

| Параметр | Значение |
|----------|----------|
| RaisedRelativeLocation / Rotation | (60, 0, -10) / (0, 0, 0) |
| LoweredRelativeLocation / Rotation | (50, 30, -60) / (-30, 0, 0) |
| StateTransitionTime | 0.18s — интерполяция между позами |
| CameraOffsetWhenRaised | `FVector` (BP) — сдвиг камеры в сторону, чтобы оружие визуально ушло, а прозрачное окошко щита оказалось по центру экрана |
| RunSwayInfluence | 1.0 (множитель характерного покачивания при беге игрока) |
| RecoilSwayInfluence | 1.0 (множитель сwa отдачи активного оружия) |

### 12.6 Бросок (Throw)

Активный выкид щита игроком. Спавнится `ARiotShieldPickup` с физикой, летит баллистически. На удар — оглушает NPC.

| Параметр | Значение |
|----------|----------|
| ThrowSpawnOffset | (80, 0, -10) cm от камеры |
| ThrowLinearImpulse | (1500, 0, 200) — преимущественно вперёд + чуть вверх |
| ThrowAngularImpulse | (0, 600, 0) — вращение вокруг yaw |

**Stun-on-impact** (полная аналогия с брошенным yanked-оружием `ADroppedRangedWeapon`):

| Параметр | Значение |
|----------|----------|
| bCanStunOnImpact | true (взводится в `ARiotShield::ThrowAway` после `SpawnAsThrown`) |
| StunDuration | 2.0s |
| StunImpactVelocityThreshold | 400 cm/s (ниже — не оглушает, чтобы катящийся/устаканивающийся щит не спамил стан) |
| StunCooldown | 0.5s (между стан-событиями одного пикапа) |
| StunMontage | `UAnimMontage` (BP, опционально) — null → fallback на NPC's `KnockbackMontage` |
| Цели | `AShooterNPC` (HumanoidNPC immune by spec — `ApplyExplosionStun` no-op) |

Passively-placed pickups (без вызова `SpawnAsThrown`) держат `bCanStunOnImpact = false` — не оглушают NPC, которые в них врежутся.

### 12.7 Подбор пикапа

| Параметр | Значение |
|----------|----------|
| SphereCollision (radius) | 60 cm |
| Канал overlap | `ECC_Pawn` |
| ReacquireDelay | 0.6s — лочит пикап от подбора thrower-ом, пока летит назад/отскакивает |
| Условие подбора | `OtherActor` — `AShooterCharacter`, `!HasShield()`, `ShieldClass != null` |

При успешном overlap → спавн `ARiotShield`, `EquipShield`, `Destroy` пикапа.

### 12.8 NPC-сторона: щит у `AHumanoidNPC`

`UNPCRiotShieldComponent` — опциональный компонент. Активируется **только** если в BP заданы `ShieldMeshAsset` И `PickupClass`; vanilla `BP_HumanoidNPC` остаётся неизменным (no-op путь).

| Параметр | Значение |
|----------|----------|
| AttachSocketName | `HandGrip_R` (тот же сокет, что и у TP-оружия) |
| RelativeLocation / Rotation / Scale | UPROPERTY (BP) — оффсет меша на сокете |
| bAimAtPlayer | true — yaw меша интерполируется к игроку каждый Tick (`TG_PostUpdateWork`, после анимации) |
| AimYawInterpSpeed | 8.0 |
| AimYawOffsetDeg | 0° (для алайнмента блокирующей стороны меша к камере игрока) |
| YankBaseRange | 600 |
| YankNormCoeff | 50 |
| Формула yank-радиуса | `BaseRange × max(1, 1 + ln(\|q_npc × q_player\| / NormCoeff))` (идентична оружию из §5.5) |
| Collision-профиль меша | `QueryOnly` / `WorldDynamic` / Block по всем каналам (включая `ECC_Pawn`). Query-only — line traces блокируются (пули, лучи, sweep-проджектайлы), физическое тело-в-тело столкновение отключено (capsule NPC/игрока не толкается щитом) |

**Очерёдность yank-а: `щит → оружие → melee`.** Пока щит активен, `AHumanoidNPC::CanBeYanked()` (для оружия) возвращает `false`. Сканер `ChargeAnimationComponent::CaptureScan` выбирает таргет:

1. `CanShieldBeYanked()` → `ECaptureTargetType::HumanoidShield`
2. иначе `CanBeYanked()` → `ECaptureTargetType::HumanoidWeapon`
3. иначе пропуск

`CaptureHumanoidShield()` → `Humanoid->YankShield(Puller)` → `ShieldComponent->TryYank(Puller)`.

**Yank-flow — scripted pull, не физический бросок** (canonical pattern из `ADroppedRangedWeapon::StartPull` — yank всегда магнитный, гарантированно долетает):

| Параметр | Значение |
|----------|----------|
| PullDuration | 0.4s |
| PullTargetOffset (camera-relative) | (60, 0, -10) cm |
| Lerp | `InterpEaseInOut`, exponent 2.0 |
| Коллизия в полёте | `NoCollision` на mesh + sphere — pickup проходит сквозь стены/NPC/препятствия |
| При arrival (`Alpha >= 1`) | Спавн `ARiotShield` на игроке + `EquipShield` (тот же путь, что и у `OnOverlap`) |
| Если puller погиб mid-flight | Graceful fallback: восстановление физики, pickup валится как обычный thrown |

**Yank-анимация.** При успешном yank-е щита проигрывается тот же directional `YankFront/Back/Left/Right` montage из `AHumanoidNPC::SelectYankMontageForDirection`, что и у оружия — визуально единообразно.

**После yank-а щита.** Заряд тела NPC обнуляется (`SetCharge(0)`); следующий yank (уже оружия) требует нового накопления. `bIsInMeleeMode` не меняется — гуманоид остаётся в ranged-режиме, продолжает стрелять.

**Щит как проводник заряда (ionization rule).** Оружия, меняющие заряд (волновая винтовка через `ApplyHitscanIonization`, лазер через `ApplyIonization`), **передают заряд только через щит**:

| Куда попадает выстрел/луч | Поведение |
|---------------------------|-----------|
| В щит (HitComponent имеет тег `NPCShield`) | Заряд проходит на тело NPC как обычно |
| В тело NPC (capsule/skeletal) при активном щите | Ионизация **полностью пропускается** — `UNPCRiotShieldComponent::ShouldBlockBodyIonization` возвращает true |
| В тело NPC при выкинутом/выломанном щите | Стандартная ионизация (как у обычного гуманоида) |

Дизайн-смысл: NPC прячется за щитом → игрок обязан попасть **в сам щит**, чтобы зарядить тело противника → зарядка → yank щита → теперь NPC голый, обычный flow с оружием. Стрелять "сбоку" в открытое тело можно — нанесёт обычный урон, но без накопления заряда (yank не активируется).

Реализация — статический хелпер `UNPCRiotShieldComponent::ShouldBlockBodyIonization(HitTarget, HitComp)`. Вызывается в начале каждого ionization-метода у оружий. Тег `NPCShieldTag = "NPCShield"` ставится на runtime-mesh в `TryActivate`.

**ResetForPool.** При возврате NPC в пул через `AHumanoidNPC::ResetForPool` → `ShieldComponent->ResetForPool()` → щит пересоздаётся (если был yank-нут в прошлой жизни).
