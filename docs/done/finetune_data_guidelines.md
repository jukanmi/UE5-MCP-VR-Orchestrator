# 파인튜닝 학습 데이터 작성 가이드라인

> 전제: **가능한 다양한 상황을 학습시킨다.** 특정 NPC·특정 감정 상태·특정 카테고리에 쏠리면
> 모델이 스퓨리어스 상관관계를 배워 실제로 겪을 다양한 조합에서 무너진다(아래 §3·§4 실사례).
> 시드 소스: `finetune/data/synth/scenarios_seed_draft.yaml` — 편집기: `python finetune/dataset_editor/server.py` → `http://127.0.0.1:8010`.

---

## 1. 데이터 흐름

```
scenarios_seed_draft.yaml (사람 작성, 사실관계만)
        │  generate_stage1.py --all
        │  ├─ rules_gate(): 실제 rules.validate_and_clamp_action 통과분만 채택
        │  ├─ resolve_persona(): 코어5:범용300 = 30:70, 희소타입은 코어5 중 무작위 강제
        │  ├─ teacher_speech(): gemma4-12b 가 페르소나 말투로 speech 생성(카피마다 재발화)
        │  └─ 동적 오버샘플: 타입별 최소 20장 목표(최대 5배)
        ▼
data/processed/stage1_e4b_train.jsonl (실학습 입력, {system,user,assistant})
        ▼
train/train_stage1_e4b.py (QLoRA, chatbot venv)
```

시드 하나 추가 = 최종 jsonl 에 자동 반영(다음 `--all` 재생성 시). **시드 직접 jsonl 수정 금지** — 항상 yaml 고치고 재생성.

---

## 2. 시드 스키마

```yaml
- id: combat_equip_01          # 카테고리_동작_NN, 유일해야 함
  category: Combat             # Lifestyle/Combat/Social/Common/Task/Investigation/Talk/Reject
  npc: Skadi                   # 참조용일 뿐 고정 아님(§4) — 그래도 성격에 자연스러운 발화를 위해 지정
  situation:
    valid_targets: [Player, Self, Enemy]     # target enum — 여기 없는 값 target 이면 게이트 탈락
    nearby_furniture: [{id: Bed_01, type: Bed, occupied: false, dist: 2}]  # Sit/Sleep 시만
    danger: 0.8                # 선택
    extra: "인벤토리에 Pistol 보유, 적대적 침입자 발견"   # 자유텍스트, "인벤토리에 X 보유" 패턴이면 inventory 필드 자동 채움
    sentiment: "Hostile (Score: -60)"   # §3 — 반드시 카테고리와 독립적으로 지정
  utterance: "총 가지고 있으니까 그걸로 죽여"
  gold:
    mode: Combat                # Common/Combat/Social/Task/Investigation/Lifestyle
    facial: Angry
    actions:
      - type: Equip
        item: Pistol
      - type: Attack
        target: Enemy
    speech_hint: "무기 없다고 답함"   # Reject(무동작) 시드 전용 — teacher 에게 왜 무동작인지 힌트
```

---

## 3. 호감도(sentiment) — 카테고리와 반드시 독립

**실제 버그(2026-07-27)**: 처음엔 `Combat→Hostile, Social→Friendly` 로 카테고리 기준 결정론 매핑을 했다가 즉시 지적받고 폐기. 이렇게 하면 "적대적이면 항상 Attack" 만 학습하고 **"호감도 낮은데 의자에 앉으라고 시키는" 조합을 모델이 평생 못 봄** — 정작 실전에서 그 조합이 나오면 무너짐.

**정책**: sentiment 는 situation/action 과 **완전 독립** — 1:1:1(Hostile/Neutral/Friendly) 균등 랜덤 배정. 일괄 재적용: `python finetune/dataset_editor/bulk_set_sentiment.py`.

서빙(`dialogue.py::_collect_stage1_context`)이 실제로 매 턴 `db_manager.get_affinity()` 로 이 값을 조회해 시스템 프롬프트에 `Current sentiment toward player: {sentiment}` 로 주입한다 — **학습 데이터가 이 필드를 다양하게 안 채우면 서빙-학습 정합이 깨진다.** 시드 작성 시 sentiment 미지정하면 편집기 저장 시 `Neutral (Score: 0)` 기본값.

---

## 4. 페르소나(npc) — 특정 NPC 고정 금지

**실제 버그 2건(2026-07-27)**:
1. Skadi 한테 무기를 줘도 안 쓰고 말로만 죽인다는 버그 → 원인: `Equip` 액션이 시드 코퍼스에 **0개**. 시드 부재는 오버샘플로도 못 고침(복제할 원본이 없음).
2. Equip 시드를 Elara/James/Guard 앞으로 정밀 작성했는데도 `resolve_persona()` 가 70% 확률로 랜덤 범용 페르소나로 바꿔치기 → 코어 NPC 는 실제로 그 예제를 거의 못 봄(Skadi 3회, 나머지 0회).

**정책**: `seed['npc']` 는 발화 톤 참조용일 뿐 페르소나 배정에 안 씀. 희소 타입(코퍼스 20장 미만) 포함 시드는 `force_core=True` 로 **코어 5명 중 무작위** 강제 배정(범용 30:70 스위치 안 탐). 오버샘플 카피마다 페르소나를 **재굴림**해서(예전엔 1번 굴려 복제 → 다양성 0) 5장이면 5명 중 여러 명에게 분산 노출됨. "이 상황은 Skadi 전용"이 아니라 "코어 5명 전체가 배워야 할 범용 행동"이라는 게 이 프로젝트의 결정.

Moca(ASMR 힐러) 처럼 전투 시드가 자연스럽지 않은 NPC 는 억지로 Equip 시드 안 씀 — "범용"은 **논리적으로 말이 되는 조합에 한해** 분산이지, 모든 NPC 에게 모든 상황을 강제하는 게 아님.

---

## 5. 액션 타입 커버리지 — 34종 전체

`app/schemas/actions.py::CATEGORY_ACTION_MAP` 이 유일한 소스. 편집기 상단 대시보드(`http://127.0.0.1:8010`)가 코어 5 NPC × 전체 타입 표를 보여줌 — **0(빨강)·20 미만(노랑)** 인 칸이 다음에 채워야 할 구멍.

2026-07-27 기준 처음부터 완전 0이었던 타입: `Equip`(무기 인벤토리 있는데도 미사용 버그의 직접 원인), `Idle`, `Unequip`. 발견 즉시 시드 추가로 해소 — **커버리지는 정적이지 않다, 새 버그가 나올 때마다 이 표부터 확인할 것.**

필수 파라미터는 `app/agents/subgraphs/rules.py::ACTION_REQUIRED_PARAMS` 기준 — 하나라도 없으면 `rules_gate()` 에서 조용히 탈락(에러 안 뜨고 그냥 채택 안 됨). 시드 저장 전 편집기 "검증만" 버튼으로 확인할 것. 예: `Equip`/`UseItem`/`Unequip` 은 `item` 또는 `target_id` 중 하나 필수.

---

## 6. 오버샘플 — 하드코딩 목록 금지

`RARE_ACTIONS = {...}` 식 수동 목록은 **폐기됨**(2026-07-23 결정 — "매번 발견할 때마다 추가하면 두더지잡기"). 대신 시드 코퍼스 내 타입별 **실빈도를 매 실행마다 계산**해서 20장 미만이면 자동 오버샘플(최대 5배). 새 액션 타입을 시드에 추가하기만 하면 이 로직이 알아서 부족분을 감지 — 별도 목록 수정 불필요.

---

## 7. 카테고리 정의 (8종)

| 카테고리 | 의미 | 예시 |
|---|---|---|
| Lifestyle | 가구 연동(Sit/Sleep) + 제자리(Read/Pray/Dance/Sing) | "의자에 앉아" |
| Combat | 전투 행동 | "죽여버려" |
| Social | 대인 상호작용(GiveItem/Trade/Comfort) | "이거 좀 줘" |
| Common | 이동·대기·상태 전환(Move/Wait/Stop/Idle/Follow) | "따라와" |
| Task | 물리 조작(PickUp/Drop/Craft/Repair/UseItem/Equip/Unequip) | "저거 주워" |
| Investigation | 정찰·추적(Investigate/Track/Scout) | "저기 확인해" |
| Talk | 순수 대화, actions=[] | 잡담 |
| Reject | **무동작 방어** — 가구/타겟 없음 등으로 액션 불가 | "제단에서 기도해"(제단 없음) → `actions: []` + `speech_hint` |

`Reject` 는 "모델이 억지로 행동 지어내지 않고 정직하게 못 한다고 답하기"를 가르치는 카테고리 — 무동작도 정답인 상황을 반드시 일정 비율 포함해야 함(현재 30개, 전체의 ~7%).

---

## 8. 새 시드 작성 체크리스트

1. `id` 는 `카테고리소문자_동작_NN` 형식, 기존과 중복 안 되게
2. `situation.valid_targets` 에 gold action 의 target 이 반드시 포함돼야 함(없으면 게이트 탈락)
3. `situation.sentiment` — 카테고리 보고 정하지 말 것(§3). 1:1:1 랜덤이 기본, 특정 조합을 의도적으로 테스트하려는 게 아니면 굳이 손대지 않아도 됨(비워두면 편집기가 Neutral 기본)
4. `gold.actions` 각 항목의 필수 파라미터(§5) 확인 — 편집기 "검증만"으로 즉시 확인
5. Reject 카테고리면 `actions: []` + `speech_hint` 필수
6. 저장 후 편집기 대시보드에서 해당 타입 카운트가 올라갔는지 확인
7. 다양성 목적이면 같은 상황을 여러 NPC/여러 발화 문구로 반복 작성 — **npc 필드는 발화 자연스러움용, 실제 배정은 §4 로직이 처리**

---

## 9. UE5 ↔ Python 실제 JSON 프로토콜

학습 데이터가 최종적으로 흉내내야 할 실물 계약. 시드의 `situation`(§2)이 이 중 **UE5→Python prompt payload** 의 필드(`valid_targets`/`nearby_furniture`/`npc_inventory` 등)와 반드시 형태가 같아야 학습-서빙 정합이 유지된다. §1 JSON 키 규칙: Parameters 내부는 snake_case, 최상위는 PascalCase — `NPCActionKeys`(C++)·`DIALOGUE_ACTION_FIELD_MAP`(Python) 이 유일 소스.

### 9.1 공통 봉투(Envelope) — 양방향 모든 메시지가 이 형태로 래핑

```json
{
  "protocol_version": 1,
  "msg_id": "74A82588-469B-4C3A-9D12-...",
  "ref_msg_id": null,
  "auth_token": "<WS_AUTH_TOKEN>",
  "timestamp": 1753600000.123,
  "type": "prompt",
  "payload": { "...": "type 별로 다름, 아래 §9.2" }
}
```
`type` ∈ `state_update` | `prompt` | `action_failed` | `emergency_report` | `location_decision`. 소스: `app/schemas/envelope.py::MessageEnvelope` / C++ `FEnvelopeBuilder::Build*()`.

### 9.2 UE5 → Python 배출 (요청)

**`prompt`** — 플레이어 음성/제스처 명령, 파인튜닝 학습 데이터가 직접 대응하는 타입:
```json
{
  "player_id": "Player_01",
  "voice_transcript": "침대에 누워",
  "target_npc_id": "Moca",
  "requires_replan": false,
  "current_plan": {"Moca": {"goal": "...", "steps": ["..."]}},
  "npc_inventory": {"Moca": [{"id": "HealPotion", "name": "체력 물약", "count": 1}]},
  "valid_targets": ["Player", "Self", "Enemy", "Moca", "1001", "Bed_01"],
  "nearby_furniture": [{"id": "Bed_01", "type": "Bed", "occupied": false, "dist_m": 1.7}]
}
```
→ 시드 `situation.valid_targets`/`nearby_furniture`는 이 실제 필드와 1:1로 대응(§2). `npc_inventory` 는 시드에선 `situation.extra: "인벤토리에 X 보유"` 자유텍스트로 근사(`generate_stage1.py` 가 정규식으로 파싱).

**`state_update`** — 주기적 상태 동기화(LLM 미호출, 캐싱만). **`emergency_report`** — 위험/소음 감지 배치(`report_type`: `perception`|`combat_victory`). **`action_failed`** — 실행 실패 콜백(`ref_msg_id` 로 원 명령 추적). **`location_decision`** — EQS 후보 중 LLM 이 하나 선택.

### 9.3 Python → UE5 수신 (응답, `ModeActionRequest`)

```json
{
  "Mode": "Lifestyle",
  "ActionBatches": {
    "Moca": {
      "AgentID": "Moca",
      "Mode": "Lifestyle",
      "Actions": [
        {"ActionType": "Dialogue", "FacialState": "Tired",
         "Parameters": {"text": "네, 여기 누울게요...", "emotion": "Tired"}},
        {"ActionType": "Sleep", "FacialState": "Tired",
         "Parameters": {"target_id": "Bed_01"}}
      ]
    }
  },
  "NpcPlans": {},
  "PlanAchieved": {"Moca": false}
}
```
최상위 PascalCase(`Mode`/`ActionBatches`/`Actions`/`ActionType`/`FacialState`/`Parameters`), **`Parameters` 내부만 snake_case**(`target_id`/`item`/`target_loc`/`style` 등 — `NPCActionKeys.h` 상수 고정, 리터럴 문자열 금지). 이 JSON 은 `interface_output.py` 가 학습/추론 모델의 `DialogueResponse`(mode/facial/speech/tone/actions/plan_achieved, §학습 jsonl assistant 필드와 동일 shape)를 변환해서 만든다 — **모델이 배우는 형식(DialogueResponse)과 UE5 가 최종 수신하는 형식(ModeActionRequest)은 다른 레이어**, 시드 작성 시 헷갈리지 말 것.

### 9.4 새 타입 추가 시

`EnvelopeBuilder::Build*()`(C++) + `schemas/envelope.py::EEnvelopeType`(Python) + `interface_input.py` 수신 분기를 **반드시 동시에** 수정 — 한쪽만 고치면 메시지 무음 무시(CLAUDE.md §5). `/add-envelope` 커맨드가 양쪽 체크리스트 자동 진행.

---

## 10. 재생성·검증

```bash
python finetune/data/synth/generate_stage1.py --all --seed 42
```
teacher(gemma4-12b, Ollama GPU) 호출량이 카피 수만큼 늘어남(2026-07-27 구조 변경 — 카피마다 재발화라 다양성은 얻지만 시간은 늘어남). 완료 판정은 로그의 `완료:` 문자열 기준으로 — **PID 로 생존 판정하지 말 것**(git-bash/Windows PID 매핑 불일치로 오판 잦음, 로그 마커나 산출 파일 mtime 으로 확인).

재생성 후 다음으로 검증:
- 코어 5 NPC × 새로 추가한 타입 노출 횟수(§5 대시보드)
- `finetune/eval/broad_eval.py` — gold 재현율 + held-out 신규 발화 일반화 확인
- 학습은 데이터 확정 후에만(중간에 여러 번 갈아엎을 수 있으니 매번 자동 학습 연결 금지)
