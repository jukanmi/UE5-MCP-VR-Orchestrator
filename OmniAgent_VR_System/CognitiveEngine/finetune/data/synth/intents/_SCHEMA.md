# intent 테이블 스키마 — Stage1 시나리오 생성기 입력

`scenario_builder.py` 가 이 YAML 들을 읽어 시나리오를 조합한다.
파일 1개 = 액션 카테고리 1개. 최상위는 액션 스펙의 리스트.

```yaml
- action: Sit                      # EAction 이름 (CATEGORY_ACTION_MAP 과 정확히 일치)
  mode: Lifestyle                  # 이 액션이 나올 때 batch Mode
  facial: [Neutral, Happy, Tired]  # 상황상 자연스러운 표정 후보 (생성기가 무작위 선택)
  target_kind: furniture           # player | self | enemy | npc | furniture | none
  furniture_types: [Chair, Bench, Stool]   # target_kind=furniture 일 때만
  loc_kind: none                   # none | point   (point 면 "x,y,z" 좌표 파라미터 부여)
  item_kind: none                  # none | inventory | world   (inventory=NPC 소지품에서 선택)
  style_kind: none                 # none | emote | move
  danger: [0.0, 0.2]               # situation.danger 범위 (전투류만 높게)
  extra_events:                    # situation.extra 후보 (없으면 빈 리스트). 한국어 한 줄.
    - 여관 난롯가에 빈 의자가 놓여 있음
  intents:                         # 플레이어 발화(한국어) 12개 이상 — 아래 규칙 준수
    - 의자에 앉아
    - 잠깐 앉아서 쉬어
```

## intents 작성 규칙 (품질 핵심)

1. **12~16개**. 서로 다른 어법을 섞는다:
   - 직접 명령 ("의자에 앉아")
   - 존댓말 요청 ("앉으셔도 됩니다")
   - 간접·상황 발화 ("다리 아파 보이는데?") — 액션을 명시하지 않고 유도
   - 질문형 ("좀 쉬었다 갈까?")
   - 반말/거친 말투 ("앉으라고")
2. 하나도 **번역투·영어 금지**. 실제 한국인이 VR 게임에서 말할 법한 구어.
3. 액션이 명확히 유도되어야 한다 — 읽었을 때 그 액션 말고 다른 액션이 더 자연스러우면 안 된다.
   (예: Sleep intent 에 "좀 쉬어"만 쓰면 Sit 과 구분 안 됨 → "침대에 누워 자"처럼 눕기가 명확해야 함)
4. 슬롯은 리터럴로 박지 말 것. 가구 ID·아이템명·좌표는 생성기가 주입한다.
   가구를 가리킬 땐 종류 이름("의자","침대","책상")까지만 쓴다.
5. 같은 문장 반복 금지. 어미·길이 다양하게(3자~20자).

## target_kind 의미

| 값 | 파라미터 | 비고 |
|----|----------|------|
| player | target=Player | 플레이어 대상 |
| self | target=Self | 자기 자신 |
| enemy | target=Enemy | 적 |
| npc | target=<다른 NPC 이름> | valid_targets 에 NPC 추가됨 |
| furniture | target=<가구 ID> | furniture_types 에서 뽑아 Chair_01 식 ID 생성 |
| none | 파라미터 없음 | StandUp/Read/Pray 처럼 제자리 액션 |

`ACTION_REQUIRED_PARAMS`(app/schemas/actions.py)를 반드시 만족해야 한다.
불만족 시 `rules.validate_and_clamp_action` 이 액션을 제거해 시나리오가 통째로 탈락한다.

## 금지

- speech/대사(NPC 가 뭐라 말할지)는 여기 쓰지 않는다 — 별도 Sonnet 단계가 생성한다.
- 페르소나 이름·아이템 고유명 하드코딩 금지.
