# SPEC_player_survival.md — 스태미나 소모 · 피격 데미지 비네트

**상태**: 2026-09-04 **완료** — M1 구현·PIE 검증 완료, M2(비네트) 철회. `docs/done/` 이관
**브랜치**: `feature/player-systems` (2026-09-04 결정 — 같은 `VRPawn.h/.cpp` 만 건드려 분기 불필요)
**담당 범위**: Sprint 스태미나 소모·고갈 처리 · 피격 시 HMD 데미지 비네트
**제외**: 스태미나 소모원 확장(점프·공격·구르기) · HP 자연회복 · 상태이상
**선행**: `SPEC_player_systems.md` §1 Sprint(2026-08-24 구현 완료, `da75d4e`)

---

## §1 배경 — 현행 실측 (2026-08-29)

### 1.1 이미 있는 것 (그래서 이 SPEC 이 가볍다)

| 대상 | 상태 |
|---|---|
| `FCharacterResources::Stamina` / `MaxStamina` / `StaminaRegen` | **존재**(`CharacterAttributes.h:69·72·82`), 기본 100/100/10 |
| `MaxStamina` 파생식 | `50 + Constitution×2 + Dexterity×2` (`:280`) |
| `AVRPawn::Tick` | **존재**(`VRPawn.cpp:195`) — 소모/회복을 얹을 자리 |
| `bIsSprinting` · `SetSprinting` · `ApplyMovementSpeed` | **존재**(`:500`·`:930`) |
| `AVRPawn::TakeDamage` | **존재**(`:960`) — 비네트 트리거 지점 |
| `VRCamera`(`UCameraComponent`) | **존재**(`VRPawn.h:77`) — PostProcess 부착 대상 |

### 1.2 없는 것

- 스태미나가 **아무 데서도 줄지 않는다**. 필드는 있는데 소비처가 0.
- 그래서 Sprint(600 UU/s)를 **무한히 유지**할 수 있다.
- 피격 시 시각 피드백이 없다. `TakeDamage` 는 HP 차감·사망 처리만 하고, 햅틱(컨트롤러 럼블)은 있으나 **화면에는 아무 변화가 없다**.

### 1.3 왜 묶어서 한 SPEC 인가

둘 다 **"플레이어가 자기 상태를 몸으로 느끼게 한다"** 는 한 목적이고, 손대는 파일이 `VRPawn.h`/`.cpp` 로 동일하다. 별도 SPEC 으로 쪼개면 같은 `Tick`·`TakeDamage` 를 두 브랜치가 동시에 고치게 된다(W34 에서 `llm-perf`·`reflex-table` 이 `_handle_emergency_report` 충돌로 범위를 분리해야 했던 전례).

---

## §2 결정사항

### 2.1 (2026-08-29 인터뷰) — 무효

**비네트 = PostProcess Material 신규 제작.** 엔진 기본 `VignetteIntensity` 는 **검정만 가능**해서 "피격" 이 아니라 "어두워짐" 으로 읽힌다. UMG 오버레이는 VR 에서 스크린 스페이스 위젯이 눈에 붙어 어색하다.

### 2.2 (2026-09-04 실측) — 비네트 철회

PostProcess 방식은 **이 프로젝트 렌더 설정에서 성립하지 않는다.** 머티리얼을 제작·할당하고 PIE 를 돌리면 화면 전체가 검정이 되고 디버그 프리미티브만 남았다. 그래프를 두 형태로 바꿔 봤지만 결과가 같았고, 머티리얼을 떼자 정상으로 돌아왔다:

| 시도 | 그래프 | 결과 |
|---|---|---|
| 1 | `Emissive = Lerp(SceneTexture:PostProcessInput0, 빨강, 마스크×Intensity)`, Opaque | 검정 |
| 2 | `Emissive = 빨강` + `Opacity = 마스크×Intensity`, Translucent (`Intensity=0` 이면 아무것도 안 그려져야 함) | 검정 |
| 3 | 머티리얼 미할당 | **정상** |

즉 그래프 내용과 무관하게 **PP 블렌더블을 붙이는 것 자체**가 원인이다. `Config/DefaultEngine.ini` 가 `r.ForwardShading=True`(:75) · `vr.InstancedStereo=True`(:138) · `vr.MobileMultiView=True`(:140) 이라 스테레오 PP 패스가 깨진다. 이 셋은 Quest 성능에 직결이라 비네트 하나를 위해 끌 값이 아니다.

**대안 검토**: 카메라 앞 메시(평면/반구) 방식은 렌더 경로와 무관하게 동작하지만, 고정 거리 평면이라 양안 수렴 부담이 있고 FOV 에 맞춘 크기·거리 육안 튜닝이 따라붙는다. **채택하지 않고 비네트 자체를 제거**하기로 했다(2026-09-04 결정).

**피격 시각 피드백 대체**: `WBP_PlayerHUD` 에 `HealthBar`/`HealthText` 추가. 원래 이 위젯은 인벤토리 전용이라 HP 표시가 아예 없었다. 컨트롤러 햅틱(`PlayHitReceivedFeedback`)은 그대로 유지.

---

## §3 M1 — 스태미나 소모·고갈

### 3.1 튜닝 파라미터 (전부 `UPROPERTY(EditAnywhere, Category="VR|Stamina")`)

C++ 기본값에 확정하고 에디터에 노출한다(CLAUDE.md §9 — 바이너리에만 두지 말 것).

| 이름 | 기본값 | 의미 |
|---|--:|---|
| `SprintStaminaCostPerSec` | 12.0 | Sprint 지속 시 초당 소모 |
| `StaminaRegenDelaySec` | 1.5 | Sprint 중단 후 회복 시작까지 지연 |
| `SprintUnlockStaminaRatio` | 0.25 | 고갈 후 Sprint 재허용 임계(MaxStamina 대비 비율) |

> 회복 속도는 신규 파라미터를 만들지 않고 **기존 `Resources.StaminaRegen`(기본 10/s)** 을 쓴다. 스탯 파생값이므로 캐릭터 능력치가 회복력에 반영되는 게 자연스럽다.

### 3.2 상태 플래그

```cpp
// 고갈로 강제 해제된 상태. 회복이 SprintUnlockStaminaRatio 를 넘을 때까지 Sprint 재진입 차단.
// 이게 없으면 고갈→해제→즉시재시도 가 매 틱 반복되며 덜덜 떨린다.
bool bStaminaExhausted = false;
float TimeSinceSprintStopped = 0.f;
```

### 3.3 `Tick` 로직

```
if (bIsSprinting && 실제 이동 중)
    Stamina -= SprintStaminaCostPerSec * DeltaTime
    TimeSinceSprintStopped = 0
    if (Stamina <= 0)
        Stamina = 0
        bStaminaExhausted = true
        SetSprinting(false)          // → ApplyMovementSpeed() 가 Walk 로 복귀
else
    TimeSinceSprintStopped += DeltaTime
    if (TimeSinceSprintStopped >= StaminaRegenDelaySec)
        Stamina = Min(MaxStamina, Stamina + Resources.StaminaRegen * DeltaTime)
        if (bStaminaExhausted && Stamina >= MaxStamina * SprintUnlockStaminaRatio)
            bStaminaExhausted = false
```

**"실제 이동 중" 판정 필수** — 제자리에서 스틱만 최대로 밀고 벽에 막혀 있을 때도 소모할지가 갈린다. `GetVelocity().SizeSquared2D() > KINDA_SMALL_NUMBER` 로 판정해 **실제로 움직일 때만** 소모한다(벽에 붙어 스태미나가 마르는 건 부자연스럽다).

### 3.4 Sprint 진입 차단

`SetSprinting(true)` 진입부에 가드 추가:

```cpp
if (bNewSprinting && bStaminaExhausted) return;   // 고갈 중엔 무시
```

### 3.5 자세 상호작용

`ApplyMovementSpeed` 가 Crouching/Prone 에서 자체 속도를 덮어쓰므로 Sprint 는 이미 자동 억제된다(`da75d4e` 커밋 메시지). **소모도 같이 멈춰야 한다** — `bIsSprinting` 이 true 여도 자세가 Standing 이 아니면 소모하지 않는다.

---

## §4 M2 — 피격 데미지 비네트 (철회)

2026-09-04 제거. 사유·실측은 §2.2. 이 절이 규정하던 `M_DamageVignette` 에셋, `DamageVignetteMaterial`/`VignetteMaxIntensity`/`VignetteFadeSec`/`VignetteMID`/`VignetteAlpha` 프로퍼티, `UpdateVignette()`, `TakeDamage` 누적 블록, `BeginPlay` 의 `AddBlendable` 은 전부 코드베이스에 없다.

다시 시도하게 되면 PostProcess 가 아닌 **카메라 부착 메시** 로 출발할 것 — 같은 벽에 두 번 부딪히지 않도록.

---

## §4-1 M2 대체 — HUD 체력·스태미나 게이지

`UPlayerHUDWidget` 은 HP 조회를 이미 갖고 있었고(`IPlayerBase::GetPlayerAttributes` 경유) 바인딩 지점만 비어 있었다. 여기에 스태미나를 같은 패턴으로 얹었다.

- `HealthBar`·`StaminaBar`(`UProgressBar`) · `HealthText`·`StaminaText`(`UTextBlock`) — 전부 `BindWidgetOptional`. WBP 에 같은 이름 위젯이 없으면 무동작이라 위젯 없는 WBP 도 그대로 돈다.
- `GetStaminaPercent` / `GetCurrentStamina` / `GetMaxStamina` `BlueprintPure` — WBP 가 직접 그릴 때 쓰는 경로. HP 쪽과 대칭.
- `NativeTick` 에서 HP 조회에 쓰던 `Attr` 를 재사용해 속성 조회는 틱당 1회 유지.
- **스태미나 텍스트는 정수부가 바뀔 때만 `SetText`.** Sprint 중 스태미나는 매 프레임 변하므로 HP 처럼 값 비교만 하면 90Hz 로 폰트 셰이핑이 다시 돈다. 바 퍼센트는 매번 반영.
- `WBP_PlayerHUD` 구조: 루트 `InventoryContainer`(WrapBox) 아래 `StatusPanel`(SizeBox 300×112) → `StatusBox`(VerticalBox) → HealthText·HealthBar·StaminaText·StaminaBar. `UProgressBar` 는 고유 높이가 없어 SizeBox 로 높이를 확정하지 않으면 0 으로 접힌다.

---

## §5 완료 조건

### 5.1 빌드 (클로드)

```
& "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" `
  UE5_MCP_VREditor Win64 Development -Project="C:\github\UE5_MCP_VR\UE5_MCP_VR.uproject" -WaitMutex
```
exit 0 + 신규 경고 0.

### 5.2 PIE (사용자)

1. Sprint 지속 → 스태미나 감소 → 0 도달 시 **자동으로 Walk 전환**
2. 고갈 직후 스틱 다시 최대로 밀기 → **Sprint 재진입 안 됨**(25% 회복 전까지)
3. 25% 이상 회복 후 → Sprint 재진입 됨
4. Sprint 중단 → **1.5초 후** 회복 시작(즉시 아님)
5. 벽에 막혀 제자리에서 스틱 최대 → **스태미나 안 줄어듦**
6. 크라우치 상태로 스틱 최대 → Sprint 미발동 + 스태미나 안 줄어듦
7. 손목 HUD 에 **HP·스태미나 수치가 표시**되는지 — `-- / --` 그대로면 `BindWidgetOptional` 바인딩 실패
8. Sprint 중 **스태미나 바가 줄어드는 게 보이는지** (#1~#4 를 눈으로 확인하는 수단)
9. 피격 → **HP 바·수치가 즉시 감소**

---

## §6 미결 사항

- [x] ~~`M_DamageVignette` 머티리얼 제작~~ — 2026-09-04 철회(§2.2)
- [x] ~~스태미나 HUD 표시~~ — 2026-09-04 구현(§4-1)
- [x] ~~`WBP_PlayerHUD` 위젯의 `Is Variable` 확인~~ — PIE 에서 `100 / 100` 표시 확인(2026-09-04).
      `bIsVariable` 이 false 여도 `BindWidgetOptional` 은 위젯 트리를 이름으로 찾으므로 바인딩된다.
- [x] ~~구르기~~ — 대쉬로 구현(`AVRPawn::OnDash`, 2026-09-04). 상세는 `done/SPEC_player_systems.md` §7.
- [ ] **스태미나 소모원 확장** — 점프·공격. 대쉬는 구현됨(`DashStaminaCost` 20).
- [ ] **NPC 스태미나** — `SmartNPC` 도 같은 필드를 갖지만 소모처가 없다. NPC 전투 밸런스와 엮여 별도 판단
