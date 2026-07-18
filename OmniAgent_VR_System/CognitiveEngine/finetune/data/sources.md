# 파인튜닝 데이터 소스 대장

> 딥리서치(docs/lorebook_research_prompt.md) 결과 + HF 실존 검증. M2/M3 데이터 생성기가 소비할 외부 소스 목록.
> ⚠️ 라이선스·URL 은 사용 직전 재확인. GitHub 소스는 딥리서치 URL 환각 가능 — 미검증 표시 준수.

## 다운로드·실검토 완료 (2026-07-18) — raw/ 에 저장(gitignore, 921MB)

딥리서치 서술 아닌 **실제 샘플 검토 결과**. 딥리서치 과대평가분 교정.

| 소스 | ID / 파일 | 라이선스 | 언어 | 규모 | 실제 내용 | 우리 활용 | 우선 |
|---|---|---|---|---|---|---|---|
| **LIGHT quest_stems** | parl.ai `quest_stems.tar.gz` | MIT | EN | **7,487** | character·persona·description·goal + **timeline**(과거·미래 액션 시퀀스) + world graph | **Stage2 plan 핵심** — goal→goal, timeline→steps 직결. 판타지 MUD | ★★★ |
| Korean RP (KREW) | `huggingface-KREW/korean-role-playing` | Apache-2.0 | **KO** | 35K(4서브셋) | 한국어 롤플레잉, `*반갑게 웃으며*` 지문 확인 | speech 한국어화 + 지문→Emote. **세팅은 일상대화(판타지 아님)** → utterance/speech·negative 만 | ★★★ |
| Multi-Character | `agentlans/multi-character-dialogue` | CC-BY-4.0 | EN | 10K jsonl | 3인 씬 setting+characters+conversation+`setting after`. **conversation 순수대화, 액션태그 없음** | 다중NPC TurnTo/타겟팅 구조. 액션은 narrative 에서 합성 | ★★ |
| Synthetic Social | `agentlans/synthetic-social-dialogues` | CC-BY-4.0 | EN | 8K | psych params(mood/stress/energy/traits/intent) 풍부하나 **현대배경(변호사/화상통화)·게임액션 전무** | affinity→tone 매핑 **스키마 참고만**. 직접변환 불가 → 딥리서치 과대평가 | ★ |
| NPC-Dialogue_v2 | `chimbiwide/NPC-Dialogue_v2` | Apache-2.0 | EN | 1.7K | 캐릭터카드+16턴 순수대화. **세팅 잡다(캘커타 밀수꾼 등)·액션 없음** | 순수대화 negative + persona speech 참고만 | ★ |

교정 포인트:
- **synthetic-social·NPC-v2 강등**: 딥리서치는 ★★★/★★ 로 봤으나 실물은 게임액션 전무·세팅 부적합. speech/구조 참고용.
- **직접 액션 변환 가능한 외부 소스는 LIGHT quest_stems 뿐** (goal+timeline). 나머지는 speech 품질·negative·다중NPC 구조 보강.
- **Stage1 액션 라벨 주력은 여전히 자체 시드**(scenarios_seed_draft.yaml) — 외부는 다양성·자연스러움 보강.
- CC-BY-4.0(agentlans 2종) = 학습 가능하나 **출처 표기 의무** → 배포 시 NOTICE 명시.

## LIGHT quest_stems 샘플 구조 (Stage2 매핑 근거)

```
goal: "steal worm from bird"                    → Stage2 goal
short/mid/long_motivation: 계층적 동기           → context 보강
timeline: [eat grass, go meadow, follow Bird,   → Stage2 steps (Move/Follow/UseItem 매핑)
           eat Worm, go the crook, drink water]
```
액션 어휘(go/follow/eat/drink)를 우리 EAction 으로 매핑하는 어댑터 필요 — `adapters/light_quests.py`.

## LIGHT 어댑터 실행 결과 (2026-07-18, 비전투 다양성 검증)

`synth/adapters/light_quests.py` 로 7,486 퀘스트 → Stage2 plan seed 변환(`processed/stage2_light.jsonl`, gitignore).

- **비전투 100%** (전투 goal 12.8%뿐, plan 레벨 Combat 0%). Common 86%·Task 12%·Social 1%.
- EAction 분포: Move 13K·PickUp 7.5K·Equip 5.2K·UseItem 4.8K·Follow 4.8K·Drop 3.9K·GiveItem 2.9K·Comfort 1.3K.
- 샘플: 상인(향 판매·손님 응대·귀가)·여행자·선원·사제 등 중세 일상 다단계 플랜 — 딱 원하던 비전투 다양성.
- 잔여 작업: goal/steps 영문→한국어(teacher), character→우리 5 NPC 리맵, 인자파싱 정제(GiveItem 의 target/item 분리).

**커버 못하는 비전투 모드**: LIGHT 에도 Lifestyle(Sit/Sleep/Read/Pray/Dance/Sing)·Investigation(Track/Scout) 거의 없음 → **자체 시드가 유일 소스**. 외부 데이터로 안 되는 영역 확정.

## 결론 — 비전투 다양성 소스 정리

| 목적 | 소스 | 상태 |
|---|---|---|
| 비전투 다단계 플랜(Stage2) | **LIGHT quest_stems 7,486** | ✅ 추출 완료 |
| 한국어 구어체·Emote | KREW | 추출 대기 |
| 다중 NPC 사교 | multi-character | 합성 대기 |
| Lifestyle·Investigation | **자체 시드만** | seed 확장 필요 |
| persona/speech 다양성 | (선택) IlyaGusev/gpt_roleplay_realm CC-BY | 미도입 |

HF RP 데이터셋 대부분 순수대화(액션 없음)·다수 NSFW → 구조화 비전투 액션 소스는 사실상 LIGHT 유일.

## 추출 실행 결과 (2026-07-18 오후 세션)

### LIGHT 어댑터 v2 — graph 기반 인자 분리 + NPC 리맵
`adapters/light_quests.py` 재작성. graph(agents/objects/rooms)로 인자 토큰 분류:
- `give incense customer` → `{type: GiveItem, target: customer, item: incense}` — target+item 동시 분리 1,575 스텝
- Move 는 rooms 매칭 → loc 슬롯 (`Move(loc=grand ballroom)`)
- NPC 리맵(직업 키워드): James 1,043 · Guard 720 · Elara 553 · Skadi 281 · Moca 209 = **2,806 매칭** (미매칭 4,680은 동물/괴수 — 골든셋 제외)

### 한국어화 파이프라인 — `synth/koreanize_stage2.py`
teacher gemma4-12b(로컬 Ollama) 배치 호출, Ollama format 스키마 강제(context_ko/goal_ko/steps_ko).
- 품질필터(NPC매칭·스텝3+·motivation 有·비전투) 2,805 풀 → NPC 균형 샘플링
- 프롬프트: 페르소나 각색 규칙(치유사에 절도 임무 금지 → 회수/조사로 각색)·현대어 금지·few-shot 2개
- 검증기: goal 추상어/영어/문장어미 거부, 스텝 연결어미(하며/하고) 거부, 실패 시 1회 재시도
- **산출: `synth/golden_plan_seed_light.yaml` 92개** (140 샘플, 성공률 66%). NPC 균형: Elara 21·Moca 21·James 19·Guard 17·Skadi 14
- 검수 시 주의(검증기 빈틈): ① "~시키고" 류 연결어미 잔존 ② 마침표 붙은 "한다." 문장어미 ③ 스텝 내 "시스템" 현대어 — 표본상 ~25% 항목에 경미 누출, 사람 검수에서 걸러낼 것
- 실패 48건 전부 내용 검증 탈락(타임아웃 0) — teacher 재호출로 회수 가능하나 92개로 스펙(50~100) 충족

### KREW — `adapters/krew_emote.py`
- **Emote 페어 1,698**(`processed/krew_emote.jsonl`): 지문→스타일 매핑. Smile 936·Nod 258·Gaze 246·Excited 177·Surprise 28…
- 발화 코퍼스 9,444(`processed/krew_utterances.jsonl`): **현대 일상 잡담(카페·웹툰)** — 게임 직접 투입 불가, 구어체 말투 참고용으로 강등. utterance 다양화는 별도 판타지 각색 필요
- Emote 스타일명은 임시 — C++ emote 어휘 확정 시 정합 필요

### multichar — 판타지 필터
전체 13,929 중 판타지풍 17%·3인+ 7,050 → **판타지+3인 1,227** 추출(`processed/multichar_fantasy.jsonl`: setting·characters·speakers). TurnTo/SignalAllies 시나리오 합성 재료로 사용 — setting 은 영문, 각색 시 teacher 경유.

## 미검증 — 사용 전 URL·라이선스 직접 확인 필수

딥리서치 서술이며 URL 환각 가능. 실존·스키마 확인 후에만 파이프라인 편입.

| 소스 | 딥리서치 URL | 유형 | 노림 | 검증 방법 |
|---|---|---|---|---|
| ParlAI LIGHT | parl.ai/projects/light | MUD 로그, MIT | Task/Common 물리액션 대량(PickUp/Drop/Move) | ParlAI 공식 — 실존 유력, 스키마 확인 |
| ATOMIC-LIGHT / LIGHT-Quests | openreview PDF | 논문+데이터 | **7단계 goal-step → Stage2 plan** 핵심 | 논문 확인, 데이터 배포처 추적 |
| DAGGER (LIGHT 확장) | cis.upenn.edu PDF | 논문 | GPT-4 증강 아이템/캐릭터 | 방법론 참고용(데이터 아닐 수) |
| Mantella Skyrim | github art-from-the-machine/Mantella-LLM-Fine-Tuning | Alpaca 8.8K QA | 짧은 인게임 발화·Follow/Offended 트리거 | **게임 원본 종속 라이선스 주의** |
| Multi-Character/Social(agentlans) 외 GitHub 프레임워크 | ScenePulse·FatbodyDnD·World-Forge·HawThorne·BadPeople | 프롬프트/스키마 | 파서·상태주입 **설계 참고**(데이터 아님) | 스키마 차용만, 데이터 미포함 |

## Taxonomy 사각지대 (외부 소스로 부족 → 합성 오버샘플 필수)

딥리서치 결론 — 외부 데이터만으론 아래 액션 결핍. `generate_stage1.py` 에서 인위 증강:
- **Block / Dodge**: 텍스트 로그는 Attack 편중, 방어자 능동 묘사 희소
- **Repair / Dance / Sing**: 직업·장소 종속 유희 행동, 범용셋에 없음
- **Track**: 장거리 흔적 추적 — 공간 맥락 필요, 대화셋에서 추출 불가

→ 우리 `scenarios_seed_draft.yaml` 이 이미 이 액션들 커버 중(Block10 Dodge10 Dance9 Sing9 Track12). 외부 소스는 **다양성 보강**, 사각지대는 **자체 시드가 주력**.

## 편입 전략 (M2 데이터 파이프라인)

3계층 혼합:
1. **자체 시드**(scenarios_seed_draft.yaml 400개) — taxonomy 전 커버·Rules 정합 보장. 사각지대 액션 주력.
2. **KREW 한국어** — utterance/speech 를 네이티브 구어체로 교체·지문→Emote 추출. teacher paraphrase 품질 대체.
3. **agentlans 2종** — 감정분기·다중NPC 시나리오를 situation.extra + gold.actions(TurnTo/SignalAllies/Comfort) 로 변환.

각 외부 소스는 `finetune/data/synth/adapters/<source>.py` 로 우리 스키마 변환 → 최종 `rules.py` 검증 게이트 통과분만 학습셋. 라이선스 혼합(CC-BY + Apache + 자체) → 데이터셋 카드에 소스별 출처·라이선스 표기.

## 다음 작업

- [ ] ParlAI LIGHT·LIGHT-Quests 실배포처 확인(Stage2 plan 핵심)
- [ ] agentlans 2종 `dataset_structure`·`dataset_preview` 로 실제 스키마 확인 후 어댑터 필드 매핑
- [ ] KREW 4개 서브셋 중 판타지/상황극 적합분 선별
- [ ] Mantella 라이선스(게임 종속) 판정 — 부적합 시 제외
