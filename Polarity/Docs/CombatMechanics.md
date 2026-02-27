# POLARITY — Боевые механики

---

## 1. ДВИЖЕНИЕ ИГРОКА

### 1.1 Базовое движение

| Параметр | Значение |
|----------|----------|
| SprintSpeed | 950 |
| WalkSpeed | 700 |
| CrouchSpeed | 400 |
| JumpZVelocity | 500 |
| MaxJumpCount | 2 |
| CoyoteTime | 0.165s |
| JumpLurchVelocity | +100 |

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

| Параметр | Значение |
|----------|----------|
| Макс. скорость (цепочка выстрелов) | ~6,000 cm/s (60 м/с) |
| Характерная скорость перемещения по арене | ~2,000 cm/s (20 м/с) |
| Источник импульса | Отталкивание от снаряда своего заряда |

> Характерная скорость 20 м/с достигается комбинацией стенобега, скольжения, EMF-зарядов на уровне и rocket boost. Потолок в 60 м/с — последовательные выстрелы зарядомётом под ноги в открытом пространстве.

---

## 2. EMF / ПОЛЯРНОСТЬ

### 2.1 Заряд

| Параметр | Значение |
|----------|----------|
| BaseCharge (стабильный) | ±10.0 |
| MaxBaseCharge | ±30.0 |
| BonusCharge (временный) | макс 20.0 |
| BonusChargeDecayRate | 0.1/с |
| ChargePerMeleeHit | +2.0 |
| Итоговый заряд | Base + sign(Base) × Bonus |

### 2.2 Переключение полярности (Тап)

| Параметр | Значение |
|----------|----------|
| TapThreshold | < 0.15s |
| AnimationDuration | 0.5s |
| Cooldown | 0.3s |
| Эффект | Инвертирует знак BaseCharge |

### 2.3 Канализация (Удержание)

| Параметр | Значение |
|----------|----------|
| PlateOffset | 200 |
| PlateDimensions | 200 × 200 |
| ReverseChargeWindow | 0.2s |
| ReverseChargeDuration | 0.4s |
| Эффект (удержание) | Спавн заряженной пластины, поле игрока отключено |
| Эффект (реверс) | Пластина с обратным зарядом, запуск захваченных объектов |

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

### 3.2 Ионизация (хитскан → заряд на цели)

| Параметр     | Значение |
| ------------ | -------- |
| ChargePerHit | +0.5     |
| MaxCharge    | 20.0     |

### 3.3 EMF Снаряды (Зарядомёт)

| Параметр                  | Значение                                                   |
| ------------------------- | ---------------------------------------------------------- |
| HitDamage                 | 75.0                                                       |
| DefaultCharge             | 2.0                                                        |
| ChargeCostPerShot         | 1                                                          |
| ChargeTransferRatio       | 50%                                                        |
| bAffectedByExternalFields | Да                                                         |
| Rocket Boost              | Отталкивание от собственного снаряда при выстреле под ноги |

---

## 4. БЛИЖНИЙ БОЙ

### 4.1 Базовая атака

| Параметр | Значение |
|----------|----------|
| BaseDamage | 50.0 |
| HeadshotMultiplier | 1.5x |
| AttackRange | 150 |
| Cooldown | 0.5s |

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

### 4.4 Drop Kick

| Параметр | Значение |
|----------|----------|
| MinHeight | 100 |
| BonusDamagePerHeight | +10 за 100 cm |
| MaxBonusDamage | 100 |
| DiveSpeed | 2,500 |

### 4.5 Магнетизм цели

| Параметр | Значение |
|----------|----------|
| Range | 300 |
| PullSpeed | 800 |

### 4.6 Lunge (Бросок к цели)

| Параметр | Значение |
|----------|----------|
| LungeSpeed | 2,000 |
| MinSpeedToActivate | 300 |
| MomentumPreservation | 100% |

### 4.7 Заряд от ближнего боя

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

### 5.4 Общие параметры NPC

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
| EMFProximityAccelerationThreshold | 1,000 |
| EMFProximityKnockbackDistance | 100 |
| EMFProximityDamage | 10.0 |
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
| EMFProximity | Электро-синий | EMF столкновение |
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

### 6.5 EMF Proximity Knockback

| Параметр | Значение |
|----------|----------|
| AccelerationThreshold | 1,000 |
| KnockbackDistance | 100 |
| KnockbackDuration | 0.5s |
| Damage | 10.0 |
| DamageDelay | 0.2s |
| TriggerCooldown | 0.5s |

### 6.6 Физ. пропы

| Параметр | Значение |
|----------|----------|
| CollisionVelocityThreshold | 800 |
| DamagePerVelocity | 10 за 100 |
| ExplosionDamage (если вкл.) | 50 |
| ExplosionRadius | 300 |
| ExplosionImpulse | 1,600 |

### 6.7 Оглушение взрывом пропа (Explosion Stun)

При взрыве EMF-пропа все NPC в радиусе взрыва получают стан — переходят в состояние Knockback без перемещения (AI замораживается на месте, стрельба прекращается, pathfinding останавливается). На оглушённых NPC проигрывается Anim Montage.

| Параметр | Значение |
|----------|----------|
| bApplyExplosionStun | Да |
| ExplosionStunDuration | 2.0s |
| ExplosionStunMontage | null (fallback на KnockbackMontage NPC) |

**Логика:** `Explode()` → sphere overlap по `ECC_Pawn` в радиусе взрыва → для каждого `AShooterNPC` вызывается `ApplyExplosionStun()` → `bIsInKnockback = true`, montage, таймер на `EndKnockbackStun()`.

**Делегат:** `OnNPCStunnedByExplosion(StunnedNPC, ExplodedProp, StunDuration)` — BlueprintAssignable, вызывается для каждого оглушённого NPC. Позволяет привязать логику в блупринте (VFX на NPC, UI-индикатор стана и т.д.).

---

## 7. ОБРАТНАЯ СВЯЗЬ

### 7.1 Хит-маркер

| Тип | Цвет | Длительность |
|-----|------|-------------|
| Обычный | Белый | 0.15s |
| Хэдшот | Красноватый | 0.15s |
| Убийство | Красный, ×1.5 размер | 0.4s |

---

## 8. БОЕВЫЕ ПЕТЛИ

### Петля 1: EMF на уровне → Набор высоты → Дропкик
> Найди точечный заряд или пластину → Подпрыгни на них → Сделай эффектный дропкик
> Дропкик → Ваншот наземного противника и большой урон + отбрасывание для воздушного → потенциальный удар об стену

### Петля 2: Ионизация → Самонаводящиеся снаряды
> Обстреляй цель из ионизирующей волновой винтовки → переключись на зарядомёт → убедись что твой заряд отрицателен → стреляй в сторону противника → снаряд сам притянется и нанесет 75 урона

### Петля 3: Зарядомёт → Rocket Boost → Дропкик
> Выстрели себе под ноги из зарядомёта → набери высоту → Сделай дропкик → Получи заряд за удар в ближнем бою → Можно выполнить опять

### Петля 4: Канализация → Захват → Уничтожение захваченного
> Переключи полярность → притягивай / отталкивай врагов и снаряды
> Можно стрелять в захваченного противника из винтовки → Надежный способ расправиться с одиночной целью

### Петля 5: Канализация → Захват NPC → Запуск
> Удерживай → пластина → захват NPC
> Отпусти + тап → реверс → NPC летит как снаряд
> NPC → стена = wallslam урон
> NPC → другой NPC = collision урон

### Петля 6: Канализация → Захват предмета → Запуск
> Удерживай → пластина → захват пропа
> Отпусти + тап → реверс → проп летит как снаряд
> проп → коллизия = взрыв → отбрасывание + урон + оглушение ближайших NPC (2s)

### Петля 7: Разные заряды противников 
> Сообщи противнику А положительный заряд(например, ионизируй)
> Сообщи противнику B отрицательный заряд(например, ударь в ближнем бою когда у тебя положительный)
> EMF Proximity → взрыв = урон + отбрасывание

