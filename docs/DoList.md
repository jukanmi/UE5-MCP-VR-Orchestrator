# DoList — 사용자 작업 목록

> **MCP 로 시도해서 안 된 것만** 여기에. 에디터 작업(에셋·BP 생성, 위젯 트리, 프로퍼티, 레벨 배치, 헤드셋 없는 PIE)은
> 클로드가 ue5 MCP 로 먼저 하고, 실패한 항목만 "무엇을 시도했고 왜 막혔는지"와 함께 등록. MCP 서버가 끊겨 있으면
> 에디터 기동·재연결까지 해본 뒤. 처음부터 사람 몫: BP 그래프 노드 편집, 헤드셋 PIE 육안 검증, GitHub UI·운영 결정.
> 완료 시 체크 후 클로드에게 알려주면 후속(커밋 등) 진행. 코드 작업·빌드는 클로드 담당 — 여기 안 씀.

---

## 1. 에디터 작업

> 공통: 가구 BP 는 이벤트 그래프 노드 **0개** — 부모 `FurnitureActor`, 컴포넌트·프로퍼티 설정만.
> 절차 상세는 `주간기록/2026-W29` BP_Chair 항목 참조(동일 패턴).

### 1-17. 고아 BP enum 에셋 삭제 (2026-09-24, `SPEC_behavior_mode_reduce.md` D4)

> 시도: MCP 로 AssetRegistry referencers 조회 → **0건** 확인. `EditorAssetLibrary.delete_asset` 호출은
> 클로드 자동 모드 권한 분류기가 "되돌릴 수 없는 삭제"로 차단. C++ enum 은 이미 2값으로 줄었고 이 에셋은 아무도 안 씀.

- [ ] 콘텐츠 브라우저에서 `/Game/Core/AI/ENPCBehaviorMode`(BP enum, 값에 `LifeStyle` 오타) 삭제 → 에디터 재시작 시 로드 에러 0 확인.
  같은 이름 C++ enum 과 혼동 방지용. 끝나면 `Content/Core/AI/ENPCBehaviorMode.uasset` 삭제를 커밋.

### 1-15. 대화창 UI 분리 — 헤드셋 육안만 남음 (2026-09-21 구현, MCP 로 에셋·배선·PIE 완료)

> `WBP_Chat` 생성·`BP_VRPawn.ChatWidgetClass` 배선·헤드셋 없는 PIE 검증은 전부 클로드가 MCP 로 끝냄
> (Memo Done 2026-09-21 참조, 스크린샷 `Saved/Screenshots/WindowsEditor/chat_panel_open2.png`).

- [ ] **헤드셋 PIE 육안** — Enter 로 뜬 대화창이 고개를 돌려도 시야에 붙어 오는지(카메라 고정 체감·멀미 여부) ·
  기본 크기/거리(`ChatPanelScale 0.08`, `ChatPanelOffset (80,0,-15)`, BP_VRPawn 디테일에서 조정 가능)가 헤드셋에서
  읽히는지 · 손 패널(옛 `ChatBox` 서브트리는 MCP 로 제거 완료)에 채팅 흔적 없고 인벤토리·퀘스트·골드 레이아웃이
  안 깨졌는지 · 실제 NPC 앞에서 입력→Enter→응답이 ChatLog 에 쌓이는지.

### 1-14. NPC RNG 패링 + 퀘스트 SFX (2026-09-21 구현, `docs/SPEC_realistic_combat.md` §3.2·로드맵 Phase4)

> 햅틱은 2026-09-21 폐기(컨트롤러→손 트래킹 전환 예정, 진동 낼 하드웨어가 없어짐). `QuestUpdateHaptic` 코드 삭제.

- [ ] **PIE — 퀘스트 갱신 소리** — 비트 전이 시 `VR_confirm` 소리가 나는지(HUD 텍스트 갱신과 동시).
  레벨 재접속 시(위젯 재생성) 소리가 안 나고 텍스트만 갱신되는지도 확인(의도된 동작).

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

---

## 2. PIE 검증

### 2-7. 적 호감도 시딩 후 아군 선공 (2026-09-20, `app/story/seed.py`)

- [ ] **PIE — 아군 선공 확인** — James/Elara/Skadi/Moca/Guard 를 도적 캠프(4300,-5600)·다리 오크(ORC_LAIR)·죽은 숲 망령 근처로 데려가서 적이 먼저 안 때려도 시야만으로 교전 붙는지.


---

## 3. GitHub / 운영 결정


- [ ] **Visual Studio 미사용 전환 결정** (2026-09-21, 엔진 룰 캐시 버그 조사 중 발견) — `.uproject` 의
  `VisualStudioTools` 플러그인이 `Enabled: true` 인 이유: VS 확장이 프로젝트를 열 때마다 자동으로 켠다.
  이번에 이 플러그인 때문에 빌드가 통째로 막힌 사고(`UE5Rules` 캐시가 이 플러그인이 켜지기 전 시점에
  고정돼 있었음 — Installed Build 라 자동 갱신 안 됨, 강제 재컴파일로 1회성 해결)가 났다. 재발 방지 둘 중 택1:
  - (권장) **VS 로 이 프로젝트를 아예 안 엶** — 지금 빌드는 이미 클로드가 `Build.bat` 직접 실행, PIE 검증은
    UE 에디터에서 하니 VS 없어도 작업 안 끊김. VS Code 등으로 코드만 보면 됨.
  - VS 는 계속 쓰되 "Visual Studio Integration Tool for Unreal Engine" 확장만 제거(확장 관리 → "Unreal" 검색 → 제거) —
    확장 없으면 VS 로 열어도 `.uproject` 플러그인 설정을 안 건드림.
  - 아무것도 안 하면: 다음에 새 플러그인이 켜질 때마다(VS 뿐 아니라 다른 경로로도) 같은 클래스 버그 재발 가능.


- [ ] **로드맵 스펙 ↔ 구현 정렬 결정** (`SPEC_story_progression_roadmap.md` §3.1·§3.3 vs 실제) — 어느 쪽을 고칠지:
  BP 이름 `BP_Bandit/OrcVagron/KnightWraith`(구현) vs `BP_Enemy_Bandit/OrcVagron/Wraith`(스펙) ·
  flag `forest_raiders_cleared`/`wraiths_purified`(구현) vs `flag_forest_raiders_cleared`/`flag_dead_wraiths_cleared`(스펙 §3.1, §2 는 또 다름) ·
  스폰 주기 30/60/40s·망령 3/5킬(구현) vs 15s·2/4킬(스펙) · 도적 위치 숲길 외곽(구현) vs FOREST 중심(스펙) ·
  HP 240/600/300(구현) vs 50/180/80(스펙 — 공식 하한 165 라 불가, 플레이어 스윙 상한 100 이면 원샷). 클로드 추천: 이름·주기는 스펙, 위치·HP 는 구현.
- [ ] **디렉터 타임아웃 5s → 8~10s?** — 첫 전이마다 폴백(`ReadTimeout`). 대사엔 영향 없고 quest_log 각색만 빠짐. 올리면 그 턴 NPC 응답이 그만큼 늦어짐.
- [ ] **VRAM 16GB OOM 대책 결정** — 12B+e4b+PIE 동시 부하 시 Ollama 500/ReadTimeout. TTS GPU 폐기(2026-09-12) 후 재현되는지 먼저 확인, 재현 시 `num_ctx` 축소. KV 양자화는 크래시 불가(Handoff)

---

## Done

- [X] ~~1-16. Stumble 4방향 몽타주 임포트~~ (2026-09-23 임포트=사용자 / 뒤처리·몽타주·구현=클로드 MCP) —
  임포트분에 **AnimSequence 가 0개**였다(skin 포함 FBX 라 메시로만 들어감). `Downloads\` 원본으로 애니만
  재임포트 → 빈 take(`_Take_001`, 회전 변화 0도) 걸러내고 `AS_Hit_Front/Back/Left/Right` 확정, 잔재 21개 삭제,
  `AM_Stumble_*` 4개 생성(`DefaultSlot`). C++ §5.3 도 같이 구현해 슬롯 자동 바인딩(생성자) — 에디터 배정 불필요.
  **헤드셋 PIE 확인 완료**: 4방향 몽타주 재생·루트 모션 밀림·복귀 정상. Mixamo In Place 배포본이 없어
  `bEnableRootMotion=true` 로 가야 한다는 게 이번 함정(Memo Handoff).

- [X] ~~1-14. 헤드셋 PIE — 패링 체감~~ (2026-09-23 확인) — SmartNPC 상대 스윙에서 패링 발동·데미지 무효·
  `S_Hit_Metal_0` 재생 확인. 확률은 별도로 `CheckReflex(50, 2)` 4000회 = 24.7%(기대 25%) 실측.
  소리가 "챙강" 보다 "띵" 에 가깝다는 의견 — 에셋 취향 문제라 교체는 미정. (같은 절의 "퀘스트 갱신 소리" 는 미완 존치.)


- [X] ~~1-8. 인벤토리 슬롯 발동 → 사용/장착~~ (조작 방식 확정 2026-09-07, 2026-09-23 이관) — 인벤토리를 연 채
  오른손 스틱으로 슬롯을 고르고 그립 = 발동(`VRPawn::OnGrabStart` → `InventoryComponent::ActivateItem`).
  종류 분기(소비=사용 / 장비=장착 / 그 외=손에 쥐기)는 `ActivateItem` 안 — 그래프에서 갈라놓지 말 것.
  그립 뗄 때 분기(열림=회수 / 닫힘=던지기)는 W37 확인 완료. PIE 검증은 아래 별도 Done 항목.

- [x] ~~3. main 에서 ignore 파일 6개 추적 해제 결정~~ (2026-09-21 당일 해결, Memo Handoff U-1) — 커밋 불필요했음:
  원격 `origin/main` 은 이미 6개 미추적이라 stale 로컬 `main` 을 `git fetch origin main:main` 으로 FF 한 것으로
  끝(사용자 지적). 추가로 `docs/` gitignore 해제·추적 시작 커밋으로 Memo/DoList 는 이제 git 이력에 남음.

- [X] ~~3. PR #26 `refactor/dead-code` → Develop 머지 결정~~ — 2026-09-13 생성, 검증 완료.
  https://github.com/jukanmi/UE5-MCP-VR-Orchestrator/pull/26

- [X] ~~2-6. 퀘스트 giver 주민 "!" 마커~~ (`Villager/VillagerCharacter`, 2026-09-21 클로드 MCP 전항목 검증) —
  `Content/StarterContent/` 6폴더가 로컬에서 완전히 비어있어(원인 불명, gitignore 대상이라 이력 없음)
  `build_story_scene.py` 가 `Shape_Cube` 못 찾아 죽던 문제 먼저 해결(엔진 Samples 에서 복사, 495MB).
  재실행 후 `SCN_villager_Townsfolk_1` QuestOffers 3종 정상 배정. story_state 초기화 → b1→b3 재주행 →
  "!" 마커 `Visible=True`(James 2턴 만족 순간)·Interact 3회 순차 수락(`available→active`)·전부 소진 후
  `Visible=False`·4번째 Interact 도 에러 없이 기본 대사(멱등) 전부 확인.
- [X] ~~1-12. 스토리 디렉터 Phase B·C 에디터 작업~~ (`docs/SPEC_story_director.md` §3.7, 2026-09-21 전항목 완료) — 의자 착석 PIE·전 맵 육안 확인·동선 PIE(구역 트리거 9)·spawn_enemy 스폰·보스 시야 즉시 교전(danger=0.60)·시나리오 대사 톤(Guard/James) 전부 확인. 보스 처치→전이는 클로드가 MCP(`GameplayStatics.apply_damage`)·`/api/debug/say` 로 b1→end 전체 재확인.
- [X] **PIE 검증** — 인벤토리 열고 슬롯 선택 후 그립:
  빵 → HP+15·스태미나+25·수량 −1·HUD 즉시 갱신 / 검 → 오른손 부착 / 이미 장착된 것 재선택 → 해제.
- (선택) **`WBP_InventorySlot` 클릭 버튼** — 마우스/광선 클릭 경로도 원할 때만.
  슬롯 아이콘을 `Button` 으로 감싸고 `OnClicked` → `Get Owning Player Pawn` →
  `Get Component by Class(InventoryComponent)` → `ActivateItem(SlotData.ItemTemplateID)`.
  - VR 클릭 경로 자체는 이미 서 있다 — 인벤토리 열림 중 우 트리거가 `HUDInteractor` 로
    `PressPointerKey(LeftMouseButton)` 를 날린다. 추가 입력 에셋 불필요.
  - 버튼이 광선에 안 맞으면 슬롯 위젯 Visibility 를 `Visible`(= Self-Hit-Test Invisible 아님)로.
  - MCP 로는 불가 — 위젯 그래프 노드 편집 API 가 없다(1-0b 와 같은 이유).

완료 항목은 해당 주차 `docs/주간기록/2026-W##` 의 "사용자 작업" 절로 이관한다(2026-09-13 W29~W37 이관 완료 — 비어 있음).
