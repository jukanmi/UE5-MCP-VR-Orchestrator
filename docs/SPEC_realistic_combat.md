# SPEC: 실감 나는 하이브리드 전투 시스템 (Realistic Hybrid Combat System)

> **구현 현황 (2026-09-21 실측)**
>
> | 섹션 | 상태 | 비고 |
> |---|---|---|
> | §2.3 emergency_report(패링 인지) | **완료** | `SmartNPC` 패링 성공 시 `RequestEventCognition` 큐잉 |
> | §3.1 Footwork & Spacing | **완료(2026-09-25)** | 셀렉터 후보 Strafe(링 안 ±35°)·Disengage(공격 직후, 링×1.4). `Move`+`Key_Style` 변형, 이동 중 `SetFocus` 로 타겟 주시 |
> | §3.2 RNG Parry & Detection | **완료** | `AVRPawn::TryMeleeHits` → `ASmartNPC` 타겟에 `UDiceSystem::CheckReflex(Agility, ParryDifficulty)`. Guard Zone 별도 형상 없이 기존 근접 오버랩에 편승(최소 구현) |
> | §3.3 복합 감각 척수 반사 | **완료(2026-09-25)** | Startle 룰(180cm, 적대 즉시 공격 아래) · 투사체 회피(투사체가 0.1s 통지, 이동·둘러보기 선점) · 청각→시각 융합(`FPerceptionData.Context` → Python 기억). 저HP 후퇴·회복 연쇄도 셀렉터에 추가 |
> | §3.4 Progressive Hit Reactions | 부분구현 | Flinch/Knockdown 2단계만(`NPCRagdollComponent::ReactToHit`, 임계 40). 방향성 Stumble 중간 단계 없음(스태거 몽타주 에셋 부재) |
> | §3.5 Attack Tokens | **폐기(2026-09-22)** | 필드 몹 `AEnemyAIController::MaxAttackers=2`+`HoldDistance` 포위 링으로 린치 방지는 이미 됨. SmartNPC 는 다수가 동시에 플레이어를 치는 상황 자체가 없어(아군/중립) 중앙 매니저 불필요 |
>
> §3.1·3.3·3.4 상세 설계는 **§5 구현 계획**(2026-09-22) — 결정값·수치·사람 작업·검증 체크리스트 포함.

## 1. 개요 (Overview)
본 명세서는 UE5 기반 VR 환경에서 NPC 간의 전투, 그리고 **플레이어와 NPC 간의 전투**를 실감 나게 구현하기 위한 아키텍처와 핵심 메커니즘을 정의합니다. 기존의 '말뚝 딜(제자리 공격)'과 단순한 쿨타임 기반 전투에서 벗어나, 거리 조절, 합(Clash), 물리적 타격감, 그리고 시각/청각 인지와 난수가 결합된 전투를 목표로 합니다.

**핵심 설계 철학: Brain(LLM)과 Spine(C++ 척수반사)의 분리**
- **LLM (뇌):** 1~2초의 지연 시간이 있지만, 전체적인 전술(후퇴, 우회), 감정 표현(도발, 비명), 전투 상황 인지 등을 담당합니다.
- **C++ (척수):** 0ms의 반응 속도로 쳐내기(Parry), 회피, 타격에 의한 비틀거림, 거리 조절(Footwork) 및 감각(시각/청각) 인지 반사 등을 전담합니다.

---

## 2. 플레이어 전투 적용 여부 (Player vs NPC)
**결론: 네, 완벽하게 적용됩니다.** 

본 프로젝트의 베이스 클래스인 `CombatCharacter`는 NPC와 몬스터뿐만 아니라, **VR 플레이어의 무기 상호작용**에도 동일하게 엮여 있습니다.
1. **동일한 물리 피해 파이프라인:** 플레이어가 VR 컨트롤러로 무기를 휘둘러 적을 타격할 때, NPC끼리 싸울 때와 동일한 `KineticDamage::ApplyToNPC`를 거칩니다. 스윙 속도와 무기 무게에 따라 적이 움찔(Flinch)하거나 크게 비틀거립니다(Stumble).
2. **0ms 방어 및 쳐내기 시스템:** 플레이어의 무기 궤적을 실시간으로 감지하여 NPC가 주사위(RNG)를 굴려 확률적으로 막아냅니다.
3. **LLM의 플레이어 인지:** 플레이어가 NPC의 가드를 부수거나 무기를 쳐내면, C++ '척수'가 즉시 `emergency_report`를 Python 서버로 보내 상황에 맞는 대사(Bark)를 생성합니다.

---

## 3. 핵심 전투 메커니즘 (Core Combat Mechanics)

### 3.1. Footwork & Spacing (거리 조절 및 풋워크)
- 전투 셀렉터(`UNPCActionComponent::TrySelectCombatAction`)의 후보에 **Strafe·Disengage** 를 추가한다. EQS 는 쓰지 않는다 — 타겟 중심 링 위 지점은 삼각함수 한 줄이고, EQS 에셋은 노드 그래프라 MCP 로 못 만들며 이미 `Move` 가 링 지점 직선 이동을 하고 있다.
- **Strafe**: 타겟 기준 현재 각도에서 ±`StrafeArcDeg` 만큼 돈 링 지점(반경 `SpacingIdealRange`)으로 이동. 이동 중 **타겟을 계속 바라본다**(옆걸음).
- **Disengage**: 공격 직후 링 바깥(`SpacingIdealRange × DisengageRangeMul`)으로 빠진다. 뒤로 빠지는 동안도 타겟을 본다.
- 둘 다 `EAction::Move` + `Key_Style` 변형으로 실현 — 새 `EAction` 없음(4곳 수정·ST·Python 스키마 무변경).

### 3.2. 확률 기반 쳐내기 및 공격 감지 (RNG Parry & Detection)
- **VR 플레이어 공격 궤적 감지:** NPC 캡슐 바깥의 '위험 감지 구역(Guard Zone)'을 통해 진입하는 무기의 속도(Velocity)를 0ms 단위로 계산.
- **NPC 공격 감지:** `GameplayTag (State.Action.Attacking)` 이벤트를 통해 즉각 감지.
- **DiceSystem 결합 (RNG 방어):** 공격이 감지되면 기존에 구현된 `UDiceSystem::CheckReflex(Agility, Difficulty)`를 즉시 호출합니다.
  - `bSuccess == true`: "챙강!" 소리와 함께 쳐내기(Parry) 또는 긴급 회피(Backstep) 발동.
  - `bSuccess == false`: 미처 반응하지 못하고 타격을 허용.

### 3.3. 복합 감각 척수 반사 (Integrated Sensory Reflex)
자극을 개별로 뇌(LLM)에 보고하지 않고, 반사 행동(돌아보기) 이후 관측된 결과까지 병합해 하나의 문맥으로 보낸다.

- **청각→시각 융합 (Look & Observe):** Hearing 반사(`TurnTo`/`Investigate`)가 발동하면 `FusionWindow`(0.5s)를 연다. 창 안에서 자극 위치 근처(`FusionRadius`)의 액터가 Sight 로 잡히면, 그 Sight 인지에 **문맥 문자열**(`"heard Drop 320cm behind, turned, saw Player"`)을 실어 보낸다. Python 은 이 문맥을 NPC 장기 기억 Event 로 남겨 다음 대사가 "거기 누구냐" 대신 "쥐새끼처럼 숨어들었군"이 되게 한다.
- **투사체 즉각 회피:** `AEnemyProjectile`(임프 마법)·`AKineticProjectile`(플레이어 투척) 이 NPC 를 향해 날아오면 투사체가 직접 NPC 에 알려 `Dodge` 반사. 뇌 미경유, 주사위(`CheckReflex(Agility, ProjectileDodgeDifficulty)`) 통과 시만 회피.
- **깜짝 놀람 (Startle):** 근거리(`StartleRange`)에서 **처음** 포착된 대상에 대해 자극 반대 방향 `Dodge`(뒤로 흠칫). 반사 룰 테이블 행 하나로 끝난다(`MaxDistance` 이미 지원).

### 3.4. Progressive Hit Reactions (점진적 피격 물리 반응)
- **Tier 1 (Flinch):** 약한 타격. 상체만 살짝 흔들림. — 구현됨.
- **Tier 2 (Stumble):** 묵직한 타격(가드 브레이크). 피격 방향으로 2~3보 크게 비틀거림. 4방향 몽타주가 있으면 몽타주, 없으면 절차적(Flinch + `LaunchCharacter` 밀림 + 짧은 이동 잠금) 폴백.
- **Tier 3 (Knockdown / Ragdoll):** 강한 질량 무기에 의한 넉다운. — 구현됨.
- `UNPCRagdollComponent::ReactToHit` 3분기이므로 SmartNPC·EnemyCharacter·VillagerCharacter 세 호출자에 자동 적용.

### 3.5. Attack Tokens — 폐기
- 필드 몹은 `AEnemyAIController::MaxAttackers`+`HoldDistance` 포위 링이 이미 린치를 막는다. SmartNPC 는 다수가 동시에 플레이어를 치는 설계가 아니라 중앙 매니저가 필요 없다(2026-09-22).

---

## 4. 실행 흐름 예시 (Sequence)

**[상황] 플레이어(VR)가 도적(Enemy)과 대치 중**

1. **[C++]** 도적이 플레이어 주변을 맴돎 (Footwork).
2. **[플레이어]** VR 컨트롤러로 검을 강하게 휘두름.
3. **[C++]** 도적의 '방어 구역'에서 무기의 높은 Velocity를 감지.
4. **[C++]** 도적이 `UDiceSystem::CheckReflex(민첩성:60, 난이도:2)` 굴림.
5. **[C++]** 결과는 30% 확률로 **성공!** 도적이 즉각 무기를 들어 올려 검을 방어(Parry) 후 챙강! 사운드 발생. (0ms)
6. **[C++ -> Python]** 척수반사가 `emergency_report` JSON 전송 (`event: "parried_player", target: "player"`).
7. **[Python (LLM)]** 1초 뒤 LLM이 상황을 인지, `Speak` 명령 하달.
8. **[UE5]** 쳐내기 후 틈을 노리며 도적이 외침: *"제법 묵직하지만, 내겐 안 통한다!"*


---

## 5. 구현 계획 (2026-09-22 확정)

> 원칙: 새 `EAction`·새 컴포넌트·새 EQS 에셋 **0**. 전부 기존 셀렉터·반사 테이블·래그돌 컴포넌트 안에서 끝낸다. 사람 작업은 §5.3 애니메이션 에셋 1건뿐이며, 없어도 절차적 폴백으로 동작한다.

### 5.1. §3.1 Footwork — 셀렉터 후보 2개 추가

**변경 파일**: `NPCActionComponent.h/.cpp` 만.

| 항목 | 값 | 근거 |
|---|---|---|
| `CombatWeight_Strafe` | 0.6 × Agility/`CombatStatNorm` | Dodge(0.5)보다 약간 높게 — 링 안에서 가만히 서 있는 시간을 줄이는 게 목적 |
| Strafe 발동 조건 | `SpacingMinRange ≤ Dist ≤ SpacingMaxRange` (링 안) | 링 밖이면 기존 `Move`(SpacingUrge)가 담당 — 상호 배타 |
| `StrafeArcDeg` | 35° (좌우 랜덤) | 500cm 링에서 호 길이 ≈ 305cm, 달리기 1초 남짓 |
| `CombatWeight_Disengage` | `LastCombatChoice == Attack` 이면 1.2, 아니면 0.15 | 공격 후 빠지기가 주 용도. 아닐 때도 소량 남겨 예측 불가성 |
| `DisengageRangeMul` | 1.4 (기본 링 500 → 700cm) | `SpacingMaxRange`(900) 안이라 다음 틱에 `Move` 접근 가중치가 안 튄다 |
| Key_Style | `"Strafe"` / `"Disengage"` | `ExecuteMove` 에서 스타일 분기: 둘 다 `Run` 속도 + **타겟 주시** |

**타겟 주시 구현**: 스타일이 Strafe/Disengage 면 이동 시작 시 `CMC->bOrientRotationToMovement=false`, `bUseControllerDesiredRotation=true`, `AIController->SetFocus(Target)`; `OnActionCompleted`/`AbortCurrentAction` 공통 리셋(`ResetActionRuntimeState`)에서 원복 + `ClearFocus`. 원복을 리셋 경로 하나에만 두는 이유: 중단·워치독·완료 세 경로가 전부 거기로 모인다.

**목적지**: `Angle = atan2(Owner - Target)`, Strafe = `Target + (cos, sin)(Angle ± Arc) × SpacingIdealRange`, Disengage = `Target + AwayDir × SpacingIdealRange × Mul`. 둘 다 `UNavigationSystemV1::ProjectPointToNavigation`(반경 200) 실패 시 후보 가중치 0(이번 틱 불참).

**대상**: SmartNPC 전투 셀렉터만. `AEnemyAIController` 는 `HoldAround` 포위 링이 이미 Strafe 역할 — 손대지 않는다.

### 5.2. §3.3 감각 융합 — 반사 테이블 확장 + 문맥 1필드

**변경 파일**: `NPCActionComponent.h/.cpp`, `SmartNPCAIController.cpp`, `GameStateData.h`(`FPerceptionData`), `EnvelopeBuilder.cpp`, `EnemyProjectile.cpp`, `KineticProjectile.cpp`, Python `envelope.py`·`main.py`.

**(a) Look & Observe**
1. `TryReflexReact` 가 Hearing 룰로 `TurnTo`/`Investigate`/`Scan` 을 큐잉하면 `PendingFusion = {EventType, SourceID, StimulusLoc, Until = Now + FusionWindow}` 저장.
2. `SmartNPCAIController` Sight 인지 경로에서 `ActionComp->TryConsumeFusion(ActorLoc, TargetID, OutContext)` 호출 — `Now < Until` 이고 `Dist(ActorLoc, StimulusLoc) < FusionRadius` 면 문맥 문자열 반환 후 Pending 소거.
3. 문맥이 있으면 `FPerceptionData.Context` 에 실어 `RequestEventCognition`. 기존 디바운스 큐가 Hearing·Sight 를 같은 배치에 묶으므로 억제 로직 불필요 — Python 은 Context 가 있는 항목만 기억에 쓴다.
4. 창이 닫힐 때까지 아무것도 못 보면 아무 일도 없다(기존 동작). "돌아봤는데 없더라" 문맥은 YAGNI.

| 항목 | 값 |
|---|---|
| `FusionWindow` | 0.5 s (스펙 원문) — `TurnTo` 몽타주 없는 회전은 0.3s 안에 끝난다 |
| `FusionRadius` | 400 cm — 소음 위치 오차 + 플레이어 이동 |
| Context 포맷 | `"heard {EventType} {Distance:.0f}cm, turned, saw {TargetID}"` (영문 — SLM 프롬프트가 영문) |

**Envelope 동기화**(`python_backend.md` 규칙): `FPerceptionData.Context: FString`(기본 빈) ↔ `PerceptionData.context: str = ""`. `EnvelopeBuilder` 는 비어 있으면 필드 생략. `main.py::_handle_emergency_report` 에서 `for p in perceptions: if p.context: _record_event_memory_bg(agent_id, f"{agent_id}: {p.context}", "fusion-memory")` — danger 게이트 **앞**에 둔다(친화 대상 발견도 기억 가치 있음). 테스트: `tests/test_emergency_notify_only.py` 에 context 1건 → memory add_entry 호출 확인.

**(b) 투사체 회피** — 투사체가 알린다(퍼셉션 시야에 투사체를 등록하는 것보다 짧다).
- `AEnemyProjectile::Tick`·`AKineticProjectile::Tick` 에 0.1s 간격 체크: 진행 방향 `ProjectileWarnRange`(800cm) 안, `dot(Velocity, ToNPC) > 0.9`, NPC 가 투사체를 보고 있음(`dot(NPCForward, ToProjectile) > 0.3`) 인 `ASmartNPC` 마다 `ActionComp->TryReflexReact(ESenseType::Sight, TEXT("Projectile"), ShooterID, 1.0, Dist, ProjectileLoc)`. 투사체당 NPC 1회(`TSet` 로 통지 기록).
- 반사 룰 행 추가: `{Sight, EventTypeContains="Projectile", MaxDistance 800, Cooldown 0.5, Actions {Dodge:100}}`. `DoesReflexRuleMatch` 는 Sight 룰의 `EventTypeContains` 가 비어 있지 않으면 부분매치 — 기존 Sight 룰은 전부 빈 문자열이라 영향 없음(코드 확인됨).
- 반사 `Dodge` 방향 = 투사체 진행 방향의 수평 수직(좌우 랜덤). `TryReflexReact` 의 `switch` 에 `Dodge` 케이스 추가(현재 없음).
- 주사위: `Dodge` 케이스에서 `CheckReflex(Agility, ProjectileDodgeDifficulty=2)` 실패 시 큐잉 안 함(맞는다).
- **선점**: 현재 `TryReflexReact` 는 `bIsBusy` 면 거부 → 전투 중엔 거의 안 튄다. 투사체(`EventType=="Projectile"`)에 한해 진행 중 액션이 `Move`/`Idle`/`Scan`/`TurnTo` 면 `AbortCurrentAction()` 후 큐잉. `Attack`/`Block` 중이면 거부 유지(모션 충돌).
- 플레이어 투척물은 아군 NPC 도 피한다(관계 무관) — 물건 던지면 움찔 피하는 게 자연스럽다.

**(c) Startle** — 룰 행 1개: `{Sight, EventTypeContains="FirstSight", Relation Any, MaxDistance 180, Cooldown 15, Actions {Dodge:100}}`. 방향 = 자극 반대. "처음 포착"만: `SmartNPCAIController` 가 Sight 를 `TryReflexReact` 로 넘길 때 `Stimulus.WasSuccessfullySensed && !bWasVisibleBefore`(직전 시야 상실 후 재획득 포함) 이면 `EventType="FirstSight"`. 룰 테이블 맨 위에 둔다(좁은 조건 우선 규칙).

### 5.3. §3.4 Stumble — `ReactToHit` 3분기

**변경 파일**: `NPCRagdollComponent.h/.cpp` 만. 호출자 3곳 무변경.

| 항목 | 값 | 근거 |
|---|---|---|
| `StumbleThreshold` | 20 | Knockdown 40 의 절반. 한손검 보통 스윙 ½mv² 가 15~30 대(PIE 실측치 재확인 필요)라 "세게 치면 비틀" |
| 방향 양자화 | `NoteHit` 의 `Direction` 을 Owner 로컬로 → `|x|>|y|` 면 Front/Back, 아니면 Left/Right | 기존 NoteHit 이미 방향 보유 |
| `StumbleMontages` | `TMap<EStumbleDir, UAnimMontage*>` UPROPERTY(EditAnywhere) 4칸 | 사람이 채움. 비면 폴백 |
| 절차적 폴백 | `Flinch()` + `LaunchCharacter(HitDir2D × StumbleLaunchSpeed(450), Z 0)` + `StumbleLockTime`(0.6s) 동안 `CMC->DisableMovement()` → `SetMovementMode(Walking)` | 몽타주 없이도 오늘 동작 |
| 가드 브레이크 | `Block` 태그 활성 중 raw ≥ Knockdown 이면 Knockdown 대신 Stumble | 막다가 뚫리는 연출, 넉다운보다 가벼움 |
| 넉다운/기상 중 | 무시(Flinch 와 동일 규율) | |

**사람 작업(DoList)**: NPC 스켈레톤이 Mixamo(`FlinchRootBone=Spine`)이므로 리타겟 없이 Mixamo 에서 "Stumble Backwards"·"Hit To Body"·좌/우 hit reaction FBX 를 **Without Skin** 으로 받아 임포트. 이후 `AM_Stumble_Front/Back/Left/Right` 몽타주 생성·컴포넌트 슬롯 배정은 클로드가 MCP 로 시도, 다운로드·임포트만 사람.

### 5.4. 순서·규모·검증

| 순서 | 항목 | 예상 | 검증(클로드, 헤드셋 없이 MCP PIE) |
|---|---|---|---|
| 1 | §5.3 Stumble (폴백) | C++ ~80줄, 반나절 | `ue_run_python` 으로 NPC 스폰 → `ApplyPointDamage` 25/45/25(Block 중) 3케이스 → 로그 `[Ragdoll] Stumble dir=Back` + 위치 변위 ≥ 150cm + 0.6s 뒤 이동 복귀. SceneCapture PNG |
| 2 | §5.1 Footwork | C++ ~120줄, 반나절 | 플레이어 더미 배치, Combat 진입 → 로그 10틱 중 Strafe/Disengage 1회 이상, Strafe 중 `dot(Forward, ToTarget) > 0.8` 유지, Attack 다음 틱 Disengage 확률 실측 ≥ 50% |
| 3 | §5.2(c) Startle | 룰 1행 + 컨트롤러 5줄 | 시야 밖 150cm 에 플레이어 순간이동 → `[Reflex] rule='Startle'` + 후방 변위. 이미 보이던 대상엔 미발동 |
| 4 | §5.2(b) 투사체 | C++ ~60줄 + Python 0 | 임프 스포너 → 아군 NPC 에 발사 → `[Reflex] Projectile` + Dodge 몽타주. `bIsBusy(Move)` 중 선점, `Attack` 중 거부 각 1회 |
| 5 | §5.2(a) 융합 | C++ ~60줄 + Python 10줄 + 테스트 1 | Drop 소음 뒤 0.3s 안에 플레이어 시야 진입 → 서버 로그 `fusion-memory` + memory 파일에 문맥 1줄. 0.5s 밖 진입은 없음. pytest 통과 |
| 6 | §5.3 몽타주 | 사람 임포트 후 슬롯 배정 | 4방향 각 1회 재생 확인 |

**SoL-Pi**: 각 단계 `python tools/sol_pi.py verify engine`(5단계는 `all`). 에디터 열려 있으면 Live Coding.

**코드 확인 완료(2026-09-22)**: `DoesReflexRuleMatch` 는 Sense 무관하게 `EventTypeContains` 비어있지 않으면 부분매치(126행) → Sight+"Projectile"/"FirstSight" 룰 그대로 동작. `ParseMoveStyle` 은 미지 문자열을 Warning + `Walk` 폴백(323행) → `"Strafe"`/`"Disengage"` 는 `EMoveType` 에 추가하거나, 파서 전에 스타일을 가로채 `Run` 으로 치환하고 주시 플래그만 세울 것(후자가 짧다).
