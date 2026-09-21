# DoList — 사용자 작업 목록

> 클로드가 못 하는 것(에디터 수작업·PIE 육안 검증·GitHub UI 결정)만 여기에. 완료 시 체크 후 클로드에게 알려주면 후속(커밋 등) 진행.
> 코드 작업·빌드는 클로드 담당 — 여기 안 씀.

---

## 1. 에디터 작업

> 공통: 가구 BP 는 이벤트 그래프 노드 **0개** — 부모 `FurnitureActor`, 컴포넌트·프로퍼티 설정만.
> 절차 상세는 `주간기록/2026-W29` BP_Chair 항목 참조(동일 패턴).

### 1-15. 대화창 UI 분리 — WBP_Chat 신규 + BP_VRPawn 배선 (2026-09-21 구현)

> 배경: 대화창(ChatInput/ChatLog)을 손 HUD 패널에서 뽑아 새 C++ 클래스 `UChatWidget`
> (`Source/UE5_MCP_VR/UI/BP/ChatWidget.h`)로 분리, VRPawn 에 카메라 부착 `ChatWidgetComp` 신설
> (Enter 로 열림, 포커스 잃으면 자동 닫힘). 빌드는 통과. MCP 로 먼저 시도함 — `mcp__ue5__*` 전부
> "서버 끊김"으로 세션에서 사라진 상태라 에디터를 새로 띄우고 Remote Control API(`:30010`)가 응답하는
> 것까지 확인했는데도 도구 목록에 안 뜸(2026-09-21). 세션 레벨 연결 문제라 `/mcp` 재연결이나 새 세션
> 없이는 복구 불가 — 그래서 아래는 진짜 수작업으로 남긴 항목.

- [ ] **WBP_Chat 신규 생성** — `Content/Core/interface/` 우클릭 → User Interface → Widget Blueprint,
  부모 클래스로 `UChatWidget` 지정(부모 클래스 선택 창에서 검색), 이름 `WBP_Chat`.
  - 위젯 트리에 `UEditableTextBox` 하나 추가하고 이름을 정확히 **ChatInput** 으로.
  - `UScrollBox` 하나 추가하고 이름을 정확히 **ChatLog** 로. (`BindWidgetOptional` 이 이름으로 자동 바인딩 —
    `WBP_PlayerHUD` 만들 때와 동일 패턴.)
  - 캔버스 크기는 `VRPawn.h` 의 `ChatPanelDrawSize` 기본값(500×260)에 맞추면 무난. 컴파일·저장.
- [ ] **BP_VRPawn 배선** — `Content/Blueprint/Player/BP_VRPawn.uasset` 열어 Class Defaults →
  `UI|Chat` 카테고리 → `ChatWidgetClass` 에 방금 만든 `WBP_Chat` 지정 → 컴파일·저장.
- [ ] **PIE 검증** — Enter → 대화창이 시야 중앙 부근에 뜨고 고개 돌려도 따라오는지(카메라 고정) ·
  텍스트 입력 후 Enter → 전송되고 창 닫히는지(NPC 응답 ChatLog 에 쌓이는지) · 빈 Enter → 그냥 닫히는지 ·
  손 패널(HP/스태미나/인벤토리/퀘스트/골드)에 채팅 흔적 없고 기존 기능 정상인지.

### 1-14. NPC RNG 패링 + 퀘스트 SFX/햅틱 (2026-09-21 구현, `docs/SPEC_realistic_combat.md` §3.2·로드맵 Phase4)

- [ ] **헤드셋 PIE — 패링 체감** — SmartNPC(James 등) 상대로 검을 여러 번 휘두르기. Agility 50 기준
  25% 확률로 "챙강" 금속음(`S_Hit_Metal_0`)과 함께 데미지가 안 들어가는지, 나머지는 평소처럼 맞는지.
  성공 시 서버 로그/대사에 패링 인지가 반영되는지(`emergency_report` → LLM, 예: "제법 묵직하지만…" 류
  특화 대사가 나오는지는 몇 번 시도해야 걸릴 수 있음).
- [ ] **PIE — 퀘스트 갱신 소리** — 비트 전이 시 `VR_confirm` 소리가 나는지(HUD 텍스트 갱신과 동시).
  레벨 재접속 시(위젯 재생성) 소리가 안 나고 텍스트만 갱신되는지도 확인(의도된 동작).
- [ ] **(선택) 퀘스트 갱신 진동 — 햅틱 커브 수동 제작** — `QuestUpdateHaptic` 미배정 상태. 자동 생성 시도
  중 UE5 python 이 `UCurveFloat` 의 에디터 커브 리플렉션을 못 찾아 RemoteControl 서버가 멎어 에디터
  강제종료까지 발생 — 재시도 리스크 있어 보류. 필요하면 에디터에서 직접: `/Game/VR/Haptics/` 에
  `UHapticFeedbackEffect_Curve` 신규 + `UCurveFloat` 2개(Amplitude 0→1→0 짧은 펄스, Frequency 상수 1.0)
  만들어 연결 → `WBP_PlayerHUD` CDO 의 `QuestUpdateHaptic` 에 배정.

### 1-8. 인벤토리 슬롯 발동 → 사용/장착 (2026-09-07 조작 방식 변경)

> 그립 뗄 때 분기(열림=회수·닫힘=던지기)는 확인 완료(W37).

**방안 변경**: 슬롯 클릭 버튼은 **필수 아님**. 인벤토리를 연 채 오른손 **스틱으로 슬롯을 고르고
그립을 누르면** 그 슬롯이 발동된다(`VRPawn::OnGrabStart` → `InventoryComponent::ActivateItem`).
종류 분기(소비=사용 / 장비=장착 / 그 외=손에 쥐기)는 `ActivateItem` 안에 있어 그래프에서 갈라놓지 말 것.

### 1-10. VR 공간 UI — 거래 테이블 PIE 검증 (2026-09-05 구현, `SPEC_vr_ui_systems`)

> 이름표·버튼 튜닝은 2026-09-07 완료(W37). 음성 입력 구는 2026-09-12 음성 폐기로 소멸.

- [ ] **거래 테이블** — NPC 가 Trade 액션을 낼 때만 뜬다(그냥 아이템을 건네는 건 종전대로 즉시 이관).
  1. 두 사람 사이에 테이블 + 노란 접시(NPC 제공품) + 파란 접시(내가 올릴 자리) + 초록/빨강 버튼
  2. 요구 아이템을 파란 접시 위에서 그립을 놓으면 올라가 고정되고, 손으로 다시 못 집는다
  3. 초록 버튼에 손을 가져가면 성사 — 요구 수량이 모자라면 아무 일도 안 일어나고 로그만 남는다
  4. 빨강 버튼 = 취소 — 올린 물건이 인벤토리로 안 돌아가고 바닥에 떨어진다(의도된 동작)

### 1-11. 말풍선 컴포넌트 분리 후 확인 (2026-09-06 리팩토링)

  `WidgetComponent` → `NPCDialogueUIComponent` 로 바뀌었다. BP 가 갖고 있던 오버라이드
  3개(위치 Z=210 · DrawSize 500 · 위젯 클래스)는 전부 C++ 기본값으로 옮겨 두었지만,
  에디터가 구 오버라이드를 남겨 두면 위치가 어긋날 수 있다.
  - 컴포넌트 디테일에서 위치·클래스가 기본값(노란 화살표 없음)인지 보고, 남아 있으면
    "Reset to Default" 로 지울 것.
- [ ] **PIE 재확인** — 말 걸었을 때 ① 대사 표시 ② 말풍선이 카메라를 따라 도는지.
  (TTS 자막 싱크 항목은 2026-09-12 음성 폐기로 소멸.)
  - 점 애니(Thinking)는 항목에서 빠졌다 — 표시 자체가 제거됐다(커밋 `5f28e37`).

### 1-13. 필드 적 시스템 (2026-09-18 구현, `docs/SPEC_story_progression_roadmap.md` Phase 2)

> 도적 캠프(4300,-5600)·다리 오크(14400,-2300)·죽은 숲 망령(11500,15500) 스포너 3. 적 BP `/Game/Blueprint/Enemy/`, 값은 `tools/make_enemy_bps.py` 가 원본.

- [X] **헤드셋 PIE — 도적 캠프 체감** — 남문 밖 숲길(4300,-5600) 접근: 한 명이 보면 셋이 오는지 · 둘만 붙고 셋째가 3.6m 링을 도는지 · 검 스윙 타격에 펀치 소리+래그돌 플린치 · 처치 시 둔탁음+래그돌 · 도적 HP 20% 에 도망갔다 돌아오는지. 플레이어 HP 가 55초면 바닥나므로(방어 0) 체감 후 밸런스 의견.
- [X] **육안 확인** — `Saved/Screenshots/scene/enemies_front.png`(정면 3인) 이미 있음. 무기 색이 몸 텍스처라 약간 어긋남 — 신경 쓰이면 클로드에게(텍스처 3장 5분).

### 1-1. 기타 에디터 잔짐 (Memo 이관, 선택)
- [x] 래그돌→기상 몽타주 포즈 스냅 완화 — AnimBP 캐시 포즈 블렌드
 (2026-09-18 MCP Done - C++ 구현 완료, BP 노드 연결만 남음)
- [x] **NPC `HandObject` 제시 몽타주** — 현재 `ExecuteHandObject` 는 `EquipItem` 만 불러 손에 붙이기만 한다.
 (2026-09-18 MCP Done - Wizard_Spell1 활용 AM_NPC_HandObject 생성 및 DA 할당 완료)
  "내미는" 동작이 없어 플레이어 눈에는 그냥 들고 선 것으로 보인다. 팔을 앞으로 뻗는 짧은 몽타주를
  구해 `DA_NPC_Actions` 의 `HandObject` 키에 할당하면 C++ 수정 없이 재생된다(다른 액션과 같은 경로).
  - 에셋이 없어 보류된 항목 — 2026-09-07 `Refactoring.md` 폐기하며 이관.

---

## 2. PIE 검증

### 2-7. 적 호감도 시딩 후 아군 선공 (2026-09-20, `app/story/seed.py`)

- [x] ~~서버 재시작~~ — 2026-09-20 15:04 클로드가 재기동(pid 27556, `.venv` python, cwd CognitiveEngine, 로그 `Saved/Logs/cognitive_server.log`). 이전 터미널 서버는 종료됨 — 터미널에서 직접 보고 싶으면 이 프로세스 끄고 다시 띄울 것. story_state 는 b5 그대로.
- [ ] **PIE — 아군 선공 확인** — James/Elara/Skadi/Moca/Guard 를 도적 캠프(4300,-5600)·다리 오크(ORC_LAIR)·죽은 숲 망령 근처로 데려가서 적이 먼저 안 때려도 시야만으로 교전 붙는지.



---

## 3. GitHub / 운영 결정

- [x] ~~main 에서 ignore 파일 6개 추적 해제 결정~~ (2026-09-21 당일 해결, Memo Handoff U-1) — 커밋 불필요했음: 원격
  `origin/main` 은 이미 6개 미추적이라 stale 로컬 `main` 을 `git fetch origin main:main` 으로 FF 한 것으로 끝(사용자 지적).
  추가로 `docs/` gitignore 해제·추적 시작 커밋으로 Memo/DoList 는 이제 git 이력에 남음.

- [ ] **Visual Studio 미사용 전환 결정** (2026-09-21, 엔진 룰 캐시 버그 조사 중 발견) — `.uproject` 의
  `VisualStudioTools` 플러그인이 `Enabled: true` 인 이유: VS 확장이 프로젝트를 열 때마다 자동으로 켠다.
  이번에 이 플러그인 때문에 빌드가 통째로 막힌 사고(`UE5Rules` 캐시가 이 플러그인이 켜지기 전 시점에
  고정돼 있었음 — Installed Build 라 자동 갱신 안 됨, 강제 재컴파일로 1회성 해결)가 났다. 재발 방지 둘 중 택1:
  - (권장) **VS 로 이 프로젝트를 아예 안 엶** — 지금 빌드는 이미 클로드가 `Build.bat` 직접 실행, PIE 검증은
    UE 에디터에서 하니 VS 없어도 작업 안 끊김. VS Code 등으로 코드만 보면 됨.
  - VS 는 계속 쓰되 "Visual Studio Integration Tool for Unreal Engine" 확장만 제거(확장 관리 → "Unreal" 검색 → 제거) —
    확장 없으면 VS 로 열어도 `.uproject` 플러그인 설정을 안 건드림.
  - 아무것도 안 하면: 다음에 새 플러그인이 켜질 때마다(VS 뿐 아니라 다른 경로로도) 같은 클래스 버그 재발 가능.

- [X] **PR #26 `refactor/dead-code` → Develop 머지 결정** — https://github.com/jukanmi/UE5-MCP-VR-Orchestrator/pull/26 (2026-09-13 생성, 검증 완료)

- [x] **월드 아이템을 Python 에 보낼지 결정** (2026-09-18 MCP Done - NPCManager Prompt에 nearby_items 추가) — 2026-09-07 `ItemManager::SerializeActiveItemsToJson`
  스텁을 삭제하며 남기는 메모. 그 함수는 `{"type":"world_items_state","count":N,"data":[]}` 를
  만들었지만 `data` 가 항상 비어 있었고, 호출자도 0이었다. Python 쪽 `EEnvelopeType` 에 대응
  타입이 없어 보내도 무음 무시된다.
  - 만들려면 세 곳을 동시에 고쳐야 한다: UE5 `EnvelopeBuilder::Build*()` ·
    `schemas/envelope.py::EEnvelopeType` · `interface_input.py` 수신 분기.
  - **먼저 정할 것은 소비처다** — NPC 프롬프트에 "주변 아이템"을 넣을 것인지, 아니면 별도
    조회 도구인지. 그에 따라 보낼 범위(전체 월드 vs NPC 반경)와 필드가 달라진다.
    지금 만들면 소비처가 정해질 때 스키마를 다시 뜯게 된다.

- [ ] **로드맵 스펙 ↔ 구현 정렬 결정** (`SPEC_story_progression_roadmap.md` §3.1·§3.3 vs 실제) — 어느 쪽을 고칠지:
  BP 이름 `BP_Bandit/OrcVagron/KnightWraith`(구현) vs `BP_Enemy_Bandit/OrcVagron/Wraith`(스펙) ·
  flag `forest_raiders_cleared`/`wraiths_purified`(구현) vs `flag_forest_raiders_cleared`/`flag_dead_wraiths_cleared`(스펙 §3.1, §2 는 또 다름) ·
  스폰 주기 30/60/40s·망령 3/5킬(구현) vs 15s·2/4킬(스펙) · 도적 위치 숲길 외곽(구현) vs FOREST 중심(스펙) ·
  HP 240/600/300(구현) vs 50/180/80(스펙 — 공식 하한 165 라 불가, 플레이어 스윙 상한 100 이면 원샷). 클로드 추천: 이름·주기는 스펙, 위치·HP 는 구현.
- [ ] **디렉터 타임아웃 5s → 8~10s?** — 첫 전이마다 폴백(`ReadTimeout`). 대사엔 영향 없고 quest_log 각색만 빠짐. 올리면 그 턴 NPC 응답이 그만큼 늦어짐.
- [ ] **VRAM 16GB OOM 대책 결정** — 12B+e4b+PIE 동시 부하 시 Ollama 500/ReadTimeout. TTS GPU 폐기(2026-09-12) 후 재현되는지 먼저 확인, 재현 시 `num_ctx` 축소. KV 양자화는 크래시 불가(Handoff)

---

## Done
- [X] ~~2-6. 퀘스트 giver 주민 "!" 마커~~ (`Villager/VillagerCharacter`, 2026-09-21 클로드 MCP 전항목 검증) —
  `Content/StarterContent/` 6폴더가 로컬에서 완전히 비어있어(원인 불명, gitignore 대상이라 이력 없음)
  `build_story_scene.py` 가 `Shape_Cube` 못 찾아 죽던 문제 먼저 해결(엔진 Samples 에서 복사, 495MB).
  재실행 후 `SCN_villager_Townsfolk_1` QuestOffers 3종 정상 배정. story_state 초기화 → b1→b3 재주행 →
  "!" 마커 `Visible=True`(James 2턴 만족 순간)·Interact 3회 순차 수락(`available→active`)·전부 소진 후
  `Visible=False`·4번째 Interact 도 에러 없이 기본 대사(멱등) 전부 확인.
- [X] ~~1-12. 스토리 디렉터 Phase B·C 에디터 작업~~ (`docs/SPEC_story_director.md` §3.7, 2026-09-21 전항목 완료) — 의자 착석 PIE·전 맵 육안 확인·동선 PIE(구역 트리거 9)·spawn_enemy 스폰·보스 시야 즉시 교전(danger=0.60)·시나리오 대사 톤(Guard/James) 전부 확인. 보스 처치→전이는 클로드가 MCP(`GameplayStatics.apply_damage`)·`/api/debug/say` 로 b1→end 전체 재확인.
- [x] **`BP_SmartNPC` 의 `DialogueWidget` 확인** — 네이티브 서브오브젝트 클래스가 (2026-09-18 MCP Done)
- [x] **Phase C `spawn_enemy` 매핑** — `UStorySubsystem` 의 `enemy_id → TSubclassOf<AActor>` UPROPERTY 채우기, `loc` 이름→좌표 레지스트리. (2026-09-18 MCP Done)

- [X] **PIE 검증** — 인벤토리 열고 슬롯 선택 후 그립:
  빵 → HP+15·스태미나+25·수량 −1·HUD 즉시 갱신 / 검 → 오른손 부착 / 이미 장착된 것 재선택 → 해제.
- (선택) **`WBP_InventorySlot` 클릭 버튼** — 마우스/광선 클릭 경로도 원할 때만.
  슬롯 아이콘을 `Button` 으로 감싸고 `OnClicked` → `Get Owning Player Pawn` →
  `Get Component by Class(InventoryComponent)` → `ActivateItem(SlotData.ItemTemplateID)`.
  - VR 클릭 경로 자체는 이미 서 있다 — 인벤토리 열림 중 우 트리거가 `HUDInteractor` 로
    `PressPointerKey(LeftMouseButton)` 를 날린다. 추가 입력 에셋 불필요.
  - 버튼이 광선에 안 맞으면 슬롯 위젯 Visibility 를 `Visible`(= Self-Hit-Test Invisible 아님)로.
  - MCP 로는 불가 — 위젯 그래프 노드 편집 API 가 없다(1-0b 와 같은 이유).

- [x] ~~퀘스트 로그 WBP~~ — 2026-09-18 MCP 로 `WBP_PlayerHUD` StatusBox 하단에 `QuestLogText`(TextBlock, 노랑 18pt) 추가·저장.
  바인딩은 C++ `PlayerHUDWidget::QuestLogText`(BindWidgetOptional) + `OnStoryUpdated` 구독 — 그래프 0노드. 위치/폰트는 디자이너에서 취향껏.

- [x] ~~시나리오 NPC 배치~~ — 2026-09-18 MCP 로 `Sample` 레벨에 `NPC_Guard/James/Skadi/Elara`(BP_SmartNPC, AgentID 세팅) Moca 반경 5m 배치·저장. 위치 조정은 자유.

- [x] ~~트리거 9구역~~ — 2026-09-18 텔레포트 PIE 로 전부 수신 확인(클로드). 헤드셋 걷기 검증은 선택.

- [x] ~~PIE 검증(B)~~ — 2026-09-18 클로드가 헤드셋 없이 b1→end 완주(UE 로그 `[Story] 비트 갱신` 8건, state `end`). HUD 텍스트 육안만 남음(선택).

- [x] ~~작가 콘텐츠 교체~~ — 2026-09-18 `STORY_SCENARIO.md` 7비트 + `s_moca_herbs` 로 교체 완료. NPC 페르소나·RAG 지식도 시나리오판으로 seed 됨(옛 해적 세계관은 `docs/archive/npc_pirate_2026-09-18/`).

- [x] ~~보스 NPC 배치~~ — 2026-09-18 MCP 로 `BOSS_Commander_Vorg`(마을 +80m)·`BOSS_DemonLord`(+150m) 배치, 호감도 시드 완료. 위치는 "전초기지/마왕성" 느낌 나게 옮겨도 됨(AgentID 만 유지).

- [x] ~~HerbBasket 드롭 배치~~ — `build_story_scene.py` 가 숲 빈터에 배치, 2026-09-18 획득→`서브 완료` 확인.

- [x] **itch.io 수동 다운로드 3팩** (2026-09-18 MCP 완료 - import_itch_assets.py 작성 및 임포트)(CC0, 로그인 불필요, "Download Now → No thanks" → zip 을 `RawAssets/` 에):
  [Bestiary Dungeon Monsters](https://quaternius.itch.io/bestiary-dungeon-monsters-kit)(오크·고블린·해골 — 오크 메시 교체용) ·
  [Fantasy Props MegaKit](https://quaternius.itch.io/fantasy-props-megakit)(횃불·통·상자, UE 프로젝트 포함) ·
  [Universal Animation Library](https://quaternius.itch.io/universal-animation-library). 받으면 알려주면 임포트·교체는 클로드.

완료 항목은 해당 주차 `docs/주간기록/2026-W##` 의 "사용자 작업" 절로 이관한다(2026-09-13 W29~W37 이관 완료 — 비어 있음).
