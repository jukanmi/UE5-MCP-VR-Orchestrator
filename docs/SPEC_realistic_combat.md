# SPEC: 실감 나는 하이브리드 전투 시스템 (Realistic Hybrid Combat System)

> **구현 현황 (2026-09-21 실측)**
>
> | 섹션 | 상태 | 비고 |
> |---|---|---|
> | §2.3 emergency_report(패링 인지) | **완료** | `SmartNPC` 패링 성공 시 `RequestEventCognition` 큐잉 |
> | §3.1 Footwork & Spacing | 부분구현 | `SpacingIdealRange` 링 지점 직선 이동만, EQS(`TacticalPositionsQuery`) 미경유. Strafing/Disengage 없음 |
> | §3.2 RNG Parry & Detection | **완료** | `AVRPawn::TryMeleeHits` → `ASmartNPC` 타겟에 `UDiceSystem::CheckReflex(Agility, ParryDifficulty)`. Guard Zone 별도 형상 없이 기존 근접 오버랩에 편승(최소 구현) |
> | §3.3 복합 감각 척수 반사 | 부분구현 | Sight/Hearing 개별 반사(`TryReflexReact`)는 있으나 청각→시야 스캔 융합, 투사체 회피, Startle 없음 |
> | §3.4 Progressive Hit Reactions | 부분구현 | Flinch/Knockdown 2단계만(`NPCRagdollComponent::ReactToHit`, 임계 40). 방향성 Stumble 중간 단계 없음(스태거 몽타주 에셋 부재) |
> | §3.5 Attack Tokens | 부분구현 | `AEnemyAIController::bEngaged`+`MaxAttackers=2` 로 필드 몹 한정 존재. `SmartNPC`/LLM 전투 경로엔 없음, 중앙 매니저(`NPCManager`) 아님 |
>
> §3.1·3.3·3.4·3.5 완전 스펙화는 별도 세션 — 신규 EQS 쿼리, 감각융합 상태머신, 방향성 스태거 몽타주(애니메이션 에셋 신규 필요), `SmartNPC` 까지 아우르는 중앙 공격토큰 매니저 재설계가 각각 필요해 며칠 단위 작업으로 판단(2026-09-21).

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
- `SmartNPCAIController` 내부에 근접전용 EQS(`TacticalPositionsQuery`)를 활용.
- 타겟을 중심으로 원을 그리며 걷기(Strafing), 공격 후 뒤로 빠지기(Disengage) 등의 상태를 C++ 하위 로직으로 구현.

### 3.2. 확률 기반 쳐내기 및 공격 감지 (RNG Parry & Detection)
- **VR 플레이어 공격 궤적 감지:** NPC 캡슐 바깥의 '위험 감지 구역(Guard Zone)'을 통해 진입하는 무기의 속도(Velocity)를 0ms 단위로 계산.
- **NPC 공격 감지:** `GameplayTag (State.Action.Attacking)` 이벤트를 통해 즉각 감지.
- **DiceSystem 결합 (RNG 방어):** 공격이 감지되면 기존에 구현된 `UDiceSystem::CheckReflex(Agility, Difficulty)`를 즉시 호출합니다.
  - `bSuccess == true`: "챙강!" 소리와 함께 쳐내기(Parry) 또는 긴급 회피(Backstep) 발동.
  - `bSuccess == false`: 미처 반응하지 못하고 타격을 허용.

### 3.3. 복합 감각 척수 반사 (Integrated Sensory Reflex)
단순 타격뿐만 아니라 시야와 청각(Perception)에 감지된 외부 자극에도 0ms 척수 반사로 대응합니다. **가장 중요한 점은 자극을 개별적으로 뇌(LLM)에 보고하는 것이 아니라, 반사 행동(돌아보기) 이후 관측된 결과까지 병합하여 하나의 완결된 컨텍스트로 묶어 보낸다는 점입니다.**

- **청각 + 시각 연계 (Look & Observe):** 등 뒤에서 큰 소리(Hearing)가 나면, 즉시 고개와 상체를 홱 돌립니다(0ms 반사). 이후 약 0.5초간 시야(Sight)를 스캔하여 "소리가 난 곳에 플레이어가 무기를 들고 서 있음"을 감지하면, `[소리 감지됨 + 돌아봤더니 플레이어 발견]` 이라는 복합 정보를 파이썬 서버로 보냅니다. (이로 인해 LLM은 "거기 누구냐?!" 대신 "쥐새끼처럼 숨어들었군!" 같은 지능적인 대사를 출력할 수 있습니다.)
- **투사체 즉각 회피:** 화살, 마법, 수류탄이 시야에 감지되면 뇌(LLM)가 판단하기 전에 즉시 굴러서(Dodge) 회피.
- **깜짝 놀람 (Startle):** 플레이어가 코너에서 갑자기 튀어나와 근거리에 포착되면, 뒤로 흠칫 물러나는 모션을 취함.

### 3.4. Progressive Hit Reactions (점진적 피격 물리 반응)
- **Tier 1 (Flinch):** 약한 타격. 상체만 살짝 흔들림.
- **Tier 2 (Stumble):** 묵직한 타격(가드 브레이크). 방향성 몽타주를 사용해 피격 방향으로 2~3보 크게 비틀거림.
- **Tier 3 (Knockdown / Ragdoll):** 강한 질량 무기에 의한 넉다운.

### 3.5. Attack Tokens (다대일 전투 조율)
- C++ 매니저를 통해 공격 토큰을 제한하여, 다수의 NPC에게 불합리하게 린치당하는 것을 방지.

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

