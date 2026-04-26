# POLARITY — Боевые механики

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

### 3.1 Хитскан (Волновая винтовка)

| Параметр          | Значение |
| ----------------- | -------- |
| Damage            | 4.0      |
| RefireRate        | 0.096s   |
| MaxRange          | 10,000   |
| MagazineSize      | inf      |
| AimVariance       | 0°       |
| FullAuto          | Да       |
| ChargeCostPerShot | 0.1      |

### 3.2 Электризация (хитскан → отрицательный заряд на цели)

Винтовка теперь электризует цель отрицательным зарядом независимо от полярности
игрока. Игрок (+) → цель (−) → притяжение, цель всегда может быть захвачена через
канализацию.

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

| Параметр | Значение |
|----------|----------|
| ChargePerHit (игрок) | +2.0 |
| ChargeChangeOnNPC | -25.0 |

---

## 5. ВРАГИ

### 5.1 Стрелок (ShooterNPC)

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

### 5.2 Мили (MeleeNPC)

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

### 5.3 Дрон (FlyingDrone)

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

### 5.4 Снайперская турель (SniperTurretNPC)

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

### 5.5 Общие параметры NPC

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
> Обстреляй цель из волновой винтовки (электризует отрицательным) → переключись на зарядомёт → стреляй в сторону противника → положительный снаряд притянется к отрицательной цели и нанесёт 75 урона

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

Система апгрейдов состоит из четырёх компонентов:

- **UUpgradeDefinition** — Data Asset с метаданными: `UpgradeTag` (GameplayTag), `DisplayName`, `Description`, `Icon`, `ComponentClass`, `Tier`
- **UUpgradeComponent** — абстрактный рантайм-компонент, прикрепляется к персонажу при получении. Хуки: `OnUpgradeActivated()`, `OnUpgradeDeactivated()`, `OnWeaponFired()`, `OnWeaponChanged()`, `OnOwnerTookDamage()`, `OnOwnerDealtDamage()`, `GetDamageMultiplier()`
- **UUpgradeManagerComponent** — менеджер на персонаже. API: `GrantUpgrade()`, `RemoveUpgrade()`, `HasUpgrade()`. Маршрутизирует события всем активным апгрейдам. `GetCombinedDamageMultiplier()` — перемножает множители всех апгрейдов
- **UUpgradeRegistry** — каталог всех апгрейдов (Data Asset с `TArray<UUpgradeDefinition*>`). Используется для save/load через GameplayTags

### 11.2 Получение апгрейдов

**AUpgradePickup** — актор в мире. Два оверлапа: `PickupCollision` (захват EMF) и `TooltipTrigger` (тултип). Игрок подбирает через EMF channeling — `StartPull()` → плавная интерполяция к камере → `CompletePull()` → `GrantUpgrade()`.


### 11.3 Философия апгрейдов

Апгрейды не делают игрока сильнее напрямую — они **расширяют боевой словарь**, добавляя новые петли и взаимодействия поверх базовых механик. Каждый апгрейд привязан к определённому стилю игры и поощряет рискованное поведение:

- **Скилл-потолок, а не числа:** апгрейды требуют выполнения условий (360° поворот, попадание в снаряд, движение на врага) — бонус получает только тот, кто играет агрессивно и точно
- **Синергия с ядром:** каждый апгрейд усиливает одну из существующих боевых петель (стрельба, движение, EMF), а не создаёт изолированную механику
- **Риск-награда:** бонусы значительны, но требуют подставляться (бежать на врага, крутиться под огнём, стрелять по своим снарядам)
- **Не обязательны:** базовый геймплей полностью функционален без апгрейдов — они для мастеринга, не для прогрессии
  
  *прим. от не ИИ - ВСЕГДА ТОЛЬКО МАКСИМАЛЬНО ГИГАЧАДСКИЕ АПГРЕЙДЫ*

### 11.4 Список апгрейдов

#### 360 Shot

Выполни поворот на 360° за отведённое время → следующий выстрел из винтовки наносит массивный бонусный урон.

| Параметр         | Значение |
| ---------------- | -------- |
| BonusDamage      | 500      |
| SpinTimeWindow   | 1.5s     |
| ChargedDuration  | 1.0s     |
| MinRotationSpeed | 480°/s   |
| RecoilMultiplier | 2.0x     |
| CooldownDuration | 10.0s    |


#### Charge Flip

Попади в EMF-снаряд из хитскан-винтовки → снаряд взрывается и стреляет усиленными лучами по всем видимым целям. Цепная реакция с другими снарядами.

| Параметр | Значение |
|----------|----------|
| DamageMultiplier | 2.0x |
| IonizationChargePerHit | 5.0 |
| MaxIonizationCharge | 20.0 |
| MaxChainDepth | 10 |
*прим. - буквально монетка из ULTRAKILL*
#### TESTOSTERONE BOOST

Пассивный множитель урона хитскана и melee в зависимости от направления движения относительно цели.

| Параметр | Значение |
|----------|----------|
| ForwardBonusMultiplier | +25% (при движении к цели) |
| BackwardPenaltyMultiplier | -50% (при движении от цели) |
| MinSpeedThreshold | 100 cm/s |
| MaxSpeedForFullEffect | 1,200 cm/s |

#### Suppression Fire

Пассивный эффект: попадания хитсканом по ShooterNPC подавляют их точность — паттерн стрельбы превращается в «бублик» (гарантированный промах вокруг игрока). Длительность зависит от скорости игрока.

| Параметр | Значение |
|----------|----------|
| MinSuppressionDuration | 0.5s |
| MaxSuppressionDuration | 3.0s |
| MinSpeedThreshold | 100 cm/s |
| MaxSpeedForFullEffect | 1,200 cm/s |
| DiminishingReturnsFactor | 0.5 |
*прим. для спидраннеров*
