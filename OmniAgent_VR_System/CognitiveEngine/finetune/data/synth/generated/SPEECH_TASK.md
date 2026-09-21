# Stage1 학습데이터 — NPC 대사(speech) 작성 작업 지시

너는 중세 판타지 VR 게임의 NPC 대사 작가다. 배치 파일 1개를 받아 각 항목에 대해
**NPC 가 그 순간 말할 대사**와 **플레이어 발화 변주**를 쓴다.

## 입력

`batches/batch_NNN.json` — 항목 150개 안팎의 JSON 배열. 각 항목:

| 필드 | 뜻 |
|------|-----|
| `id` | 고유 ID (출력에 그대로 실을 것) |
| `persona` | NPC 의 name/role/traits/**speech_style**(말투 예시) |
| `utterance` | 플레이어가 한 말 |
| `action` | NPC 가 지금 수행할 액션. `없음(대화만)` 이면 물리 행동 없음 |
| `negative_kind` | 비어 있으면 일반. `refuse_item`·`refuse_invalid`·`smalltalk`·`question` 중 하나면 아래 특칙 |
| `sentiment` | 플레이어에 대한 호감도 (Hostile/Neutral/Friendly + 점수) |
| `situation` | 상황 한 줄 (없을 수 있음) |
| `inventory` | NPC 소지품 |

## 출력

`speech/batch_NNN.jsonl` — 입력과 **같은 개수·같은 순서**의 JSONL. 한 줄 = 한 객체:

```json
{"id":"gen_follow_024_p0","utterance":"옆에 딱 붙어서 와","speech":"알겠다. 네 뒤를 놓치지 않고 따르지.","tone":"calmly"}
```

- `id`: 입력 그대로.
- `utterance`: 입력 발화의 **자연스러운 변주**. 뜻(유도되는 액션)은 절대 바꾸지 말 것.
  전체의 약 1/3 은 입력 그대로 둬라 — 원문과 변주가 섞여야 한다.
- `speech`: NPC 대사. 한국어 구어 1~3문장, 10~90자.
- `tone`: 영어 부사 한 단어 (calmly, coldly, warmly, sternly, wearily, cheerfully …).

## speech 품질 규칙 (이게 학습 품질을 결정한다)

1. **페르소나 말투를 그대로 따른다.** `speech_style` 의 어투·존댓말/반말·문장 길이를 복제하라.
   같은 상황이라도 기사와 해적과 학자는 완전히 다르게 말해야 한다.
2. **액션과 모순 금지.** 액션이 `Sit(target=Chair_02)` 면 앉겠다는 취지, `Flee` 면 물러나겠다는 취지.
   액션이 `없음(대화만)` 인데 뭔가 행동하겠다고 말하면 안 된다.
3. **sentiment 를 태도에 반영하되 액션 수행 여부는 바꾸지 마라.**
   Hostile 이어도 액션은 한다 — 다만 퉁명스럽게/마지못해. Friendly 면 흔쾌히.
4. **플레이어 발화를 그대로 되뇌지 마라(앵무새 금지).** "의자에 앉아" → "의자에 앉겠다" 수준의
   단순 복창은 감점. NPC 자신의 관점·이유·감각이 한 조각이라도 들어가야 한다.
5. **금지**: 영어 단어, 괄호 지문, 이모지, 번역투("~하도록 하겠습니다" 남발),
   추상적 미사여구("내 그림자조차 네 눈에 비치게 하리라" 류). 실제 사람이 말하듯 구체적으로.
6. `inventory`·`situation` 을 대사에 자연스럽게 녹이면 좋다 — 단 억지로 나열하지 말 것.
7. 배치 안에서 **같은 문장을 반복하지 마라.** 비슷한 액션이 여러 번 나와도 매번 다르게 써라.

## negative_kind 특칙 (액션 없음)

- `smalltalk` / `question`: 순수 대화. 질문이면 페르소나가 아는 범위에서 답한다. 행동 예고 금지.
- `refuse_item`: 플레이어가 달라는 물건을 **NPC 는 갖고 있지 않다**. 없다고 말한다.
  `inventory` 에 있는 걸 주겠다고 말하지 마라 — 요구된 물건이 없다는 게 요점이다.
- `refuse_invalid`: 대상이 없거나 불가능한 요구. 왜 못 하는지 짧게 말하며 거절한다.

## 절대 금지 (2026-08-08 사고 재발 방지)

- `scenario_builder.py` 를 **실행하지 마라**. 실행하면 `batches/`·`scenarios.yaml` 이 재생성되어
  다른 에이전트들이 작업 중인 입력이 통째로 뒤바뀐다(실제로 한 번 날아갔다).
- `batches/*.json`, `scenarios.yaml`, `intents/*.yaml` 을 **수정하지 마라**. 읽기 전용이다.
- 네가 쓰는 파일은 오직 `speech/batch_NNN.jsonl` 하나다.

## 작업 절차

1. 배정된 `batches/batch_NNN.json` 을 읽는다.
2. 전 항목에 대해 위 규칙대로 작성한다. **건너뛰기·요약·"나머지는 동일" 금지.**
3. `speech/batch_NNN.jsonl` 로 저장한다 (UTF-8, 줄당 JSON 객체 1개).
4. 검증 후 보고:
   ```
   python -c "import json;src=json.load(open(r'<batch json>',encoding='utf-8'));out=[json.loads(l) for l in open(r'<out jsonl>',encoding='utf-8')];print(len(src),len(out),[a['id']==b['id'] for a,b in zip(src,out)].count(False),len({o['speech'] for o in out}))"
   ```
   출력은 `입력수 출력수 ID불일치수 고유대사수`. 개수가 다르거나 ID 불일치가 있으면 고쳐라.
   고유 대사 수가 출력 수의 95% 미만이면 중복이 많은 것이니 다시 써라.
