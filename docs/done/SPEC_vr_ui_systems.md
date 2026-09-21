# SPEC_vr_ui_systems.md — VR 몰입형 공간 UI 시스템 설계

**상태**: 2026-09-05 **4종 모두 구현 완료** (PIE 검증 대기 → DoList 1-10).
- §2.1 손목 게이지: 이 SPEC 작성 전부터 이미 구현돼 있었다(`HUDWidgetComp`@왼손 컨트롤러 + `AVRPawn::UpdateHUDPanelGaze` 의 내적 판정·`FInterpTo` 페이드 = §4.1 설계와 동일).
- §2.2 Thinking/음성: `ASmartNPC::ShowThinking` 점 애니 + `ThinkingTimeoutSec`(30초) 워치독(§4.4), `UVoiceInputComponent::GetInputLevel`(RMS) → 왼손 구 크기.
- §2.3 아이템 툴팁: §4.2 대로 폰에 위젯 1개 풀링(`AVRPawn::UpdateItemTooltip`). WBP 없이 `UItemTooltipWidget` 이 위젯 트리를 C++ 로 구성 — `TextRender` 는 폰트 폴백이 없어 한글이 두부가 된다.
- §2.4 거래: `ATradeSessionActor`. §4.3 물리 방식(롤백 없음) 채택, 스폰 트리거는 **LLM Trade 액션 한정**(2026-09-05 결정) — 근접 릴리즈 즉시 건네기는 그대로 둔다.
**브랜치**: 미정 (`feature/vr-ui-systems` 권장)
**담당 범위**: 2D 화면을 덮는 비몰입형 HUD를 대체할, 공간 기반(Diegetic) 및 월드 스페이스(World Space) 위젯 4종 신규 설계.

---

## 1. 개요 및 목표

LLM 에이전트와 상호작용하는 VR 게임 특성상, 일반 PC/콘솔 게임의 평면 UI(Viewport HUD)는 현장감과 몰입도를 심각하게 해칩니다.
본 SPEC은 플레이어의 행동(손목 보기, 아이템 근접, NPC와 대화)에 자연스럽게 반응하는 4가지 VR 특화 공간 UI의 구현 규격을 정의합니다.

---

## 2. 세부 기능 명세 및 아키텍처

### 2.1. 손목형 서바이벌 게이지 (Wrist Status UI)

*   **설명**: 플레이어의 상태(HP, 스태미나 등)를 왼쪽 손목 안쪽에 부착된 스마트워치 또는 홀로그램 팔찌 형태로 표시.
*   **위치**: `Source/UE5_MCP_VR/Core/Components/WristUIComponent.h/.cpp`
*   **컴포넌트 설계**:
    *   `USceneComponent`를 상속받아 내부에 `UWidgetComponent`를 포함.
    *   `VRPawn` 생성자에서 왼쪽 컨트롤러/모션 컨트롤러에 Attach.
*   **상호작용 및 최적화**:
    *   **시선 추적(Gaze Dot Product)**: 항상 켜두면 시야를 방해하므로, 플레이어의 카메라(HMD) Forward 벡터와 손목 위젯의 Up 벡터 내적(Dot Product)을 계산하여 **플레이어가 손목을 바라보는 각도에서만 투명도(Opacity) 1.0으로 서서히 나타남**.
*   **데이터 연동**: `CharacterAttributes`의 `OnHealthChanged`, `OnStaminaChanged` 델리게이트에 바인딩.

### 2.2. 음성 인식 & AI 사고 인디케이터 (Voice & Thinking Feedback)

*   **설명**: 내 목소리가 잘 들어가고 있는지(음성 파형), NPC가 내 말을 듣고 생각 중인지(로딩/Thinking)를 시각적으로 알려줌.
*   **위치**: `Source/UE5_MCP_VR/UI/Widgets/NPCDialogueWidget.h` (기존 위젯 확장) 및 `Core/Components/VoiceInputComponent.h`
*   **A. Voice 인디케이터 (플레이어)**:
    *   `VoiceInputComponent`가 마이크 볼륨(Amplitude)을 틱마다 계산하여 위젯으로 브로드캐스트.
    *   마이크 버튼(Trigger)을 누르면 허공이나 손목 위젯 부근에 3D 파형 애니메이션 재생.
*   **B. Thinking 인디케이터 (NPC)**:
    *   파이썬 서버(LLM)에 메시지가 전송되고 답변(Envelope)이 오기 전까지의 **대기 시간(Latency)** 동안 발생.
    *   기존 말풍선(`NPCDialogueWidget`) 상단에 `[...Thinking...]` 텍스트나 말줄임표 애니메이션 활성화.
    *   NPC의 행동 상태(`CurrentAction`)가 `Processing`일 때 `SetVisibility(Visible)`.

### 2.3. 물리적 아이템 툴팁 (World Space Hover Info)

*   **설명**: 바닥에 떨어진 아이템 근처에 손(모션 컨트롤러)을 가져가면, 아이템 정보가 허공에 홀로그램으로 나타남.
*   **위치**: `Source/UE5_MCP_VR/Inventory/BP/DroppedItemBase.cpp`
*   **구현 설계**:
    *   `ADroppedItemBase`에 `UWidgetComponent` (Widget Class: `WBP_ItemTooltip`) 추가. 기본 상태 `Hidden`.
    *   `UBoxComponent` 또는 `USphereComponent`로 'Hover 거리' 정의.
    *   플레이어의 손 충돌체가 이 범위 안으로 들어오면(`OnComponentBeginOverlap`) 위젯을 띄우고, 플레이어 시야 방향으로 `FindLookAtRotation`을 수행하여 항상 플레이어 쪽을 향하게 함(빌보드 효과).
*   **데이터 연동**: 스폰 시 할당된 `ItemTemplateID`를 통해 `ItemManager`에서 `FItemData`를 읽어와 이름, 티어 색상(Common/Rare 등) 반영.

### 2.4. 거래 / 교환 전용 패널 (Barter UI)

*   **설명**: NPC에게 아이템을 건넬 때(GiveItem), 강제 전달이 아닌 허공에 물리적인 교환 테이블(UI 패널) 스폰.
*   **위치**: `Source/UE5_MCP_VR/UI/Trade/TradeSessionActor.h/.cpp` (액터 기반 관리)
*   **상호작용 설계 (VR 물리 버튼)**:
    *   포인터(레이저) 방식이 아닌, 플레이어가 직접 아이템을 슬롯 위에 Drop하면 UI에 등록됨.
    *   패널 하단의 `[수락]`, `[거절]` 버튼은 3D 충돌체를 가진 물리 버튼(`UBoxComponent`)으로 만들어 손가락으로 직접 누를 수 있게 설계.
*   **트랜잭션(Transaction) 안전성**:
    *   아이템 증발/복제를 막기 위해, 교환 중인 아이템은 플레이어 인벤토리에서 '임시 차감(Locked)' 상태로 둠.
    *   수락 시 양측 인벤토리에 확정(`Commit`), 거절 시 원상 복구(`Rollback`).

---

## 3. 커밋 및 구현 순서 (Roadmap)

본 SPEC은 도메인 분리 규칙(`SPEC_source_layout.md`)을 준수하여 작성됩니다.

1.  `feat: 손목형 서바이벌 게이지 (WristUIComponent) 부착 및 시선 추적 토글 로직`
2.  `feat: 아이템 호버 툴팁 위젯 및 빌보드 회전 처리 (DroppedItemBase 확장)`
3.  `feat: NPC 말풍선 Thinking 애니메이션 및 플레이어 음성 파형 인디케이터 연동`
4.  `feat: 3D 물리 버튼 기반 거래 세션 액터(TradeSessionActor) 및 인벤토리 롤백 시스템`

---

## 4. 구현 시 고려사항 및 핵심 리스크 (Risk & Considerations)

실제 개발 시 다음과 같은 기술적 판단과 예외 처리(Edge-cases)가 반드시 고려되어야 합니다.

### 4.1. 손목 게이지: 시선 추적 플리커링(Flickering)과 위치 오프셋
*   **리스크**: HMD(머리)와 손목 각도의 내적(Dot Product) 임계값 경계에서 손이 조금만 떨려도 위젯이 깜빡거리는 현상 발생.
*   **설계 결정**: 단순 `if (Dot > 0.8)` 조건문 대신, `FMath::FInterpTo`를 사용해 투명도를 부드럽게 전환(Hysteresis)하고 서서히 페이드 인/아웃 되도록 처리해야 합니다.

### 4.2. 아이템 툴팁: WidgetComponent 과부하 및 최적화
*   **리스크**: `ADroppedItemBase`마다 `UWidgetComponent`를 개별 부착하면 월드에 아이템이 100개 있을 때 심각한 CPU Tick 오버헤드가 발생합니다.
*   **설계 결정 (아키텍처 변경)**: 아이템에 위젯을 달지 않고, **플레이어 폰(VRPawn)에 단 1개의 툴팁 위젯을 부착(Pooling)**합니다. 플레이어의 손이 아이템과 오버랩될 때 해당 아이템의 데이터를 읽어 단일 위젯에 업데이트하고 위치만 옮기는 방식이 성능상 압도적으로 유리합니다.

### 4.3. 거래 패널과 인벤토리의 상태 분리 (물리 기반 설계)
*   **리스크**: 교환 패널에 올린 아이템과 인벤토리를 소프트웨어적으로 동기화(롤백 등)하려고 하면, 손으로 다시 뺏어갈 때(Grab) 아이템 복사(Duping) 버그가 터집니다.
*   **설계 결정 (즉시 분리 및 물리화)**: 가상의 트랜잭션(롤백) 개념을 완전히 버립니다. 플레이어가 인벤토리에서 아이템을 꺼내는 순간, 해당 아이템은 **즉시 인벤토리에서 완전 제거(`RemoveItem`)되고 독립된 물리 액터(`ADroppedItemBase`)로 취급**됩니다.
    *   **인벤토리 Grab Pull (신규 상호작용)**: 기존의 '바닥에 버리기' 버튼 대신, 인벤토리 슬롯을 플레이어의 손(컨트롤러)으로 잡아 당기면(Grab) 그 즉시 바닥이 아닌 손에 아이템 액터가 스폰되어 쥐어집니다.
    *   **교환 패널의 역할**: 단순히 물리 액터를 올려놓는 '접시(Snap Zone)' 역할만 합니다. 
    *   **취소 시 행동**: [취소]를 눌러도 시스템이 강제로 인벤토리에 다시 넣어주지 않습니다. 올려둔 아이템의 스냅만 풀려 바닥에 떨어지며, 플레이어가 직접 주워야(Grab -> `AddItem`) 다시 인벤토리로 들어갑니다.
    *   **수락 시 행동**: 접시 위에 있는 액터들만 `Destroy` 시키고, NPC 인벤토리에 추가하면 끝입니다.
*   **효과**: "가방에서 손으로 꺼내어 -> 그대로 접시에 올린다"는 VR 특유의 매끄러운 물리적 직관성이 확보되며, 상태 충돌로 인한 복사 버그가 원천 차단됩니다.

### 4.4. AI Thinking: 무한 로딩 (Timeout Fallback)
*   **리스크**: NPC가 생각 중(`[...Thinking...]`)인데 백엔드가 타임아웃 되면, 위젯이 영원히 사라지지 않고 NPC도 마비됨.
*   **설계 결정**: `WebSocketClient` 측이나 행동 트리 상에 최대 대기 시간(예: 30초) 워치독(Watchdog) 타이머를 두어, 응답이 안 오면 위젯을 강제 종료하고 폴백(Fallback) 처리해야 합니다.

### 4.5. 거래 패널 엣지 케이스: 물리적 Grab으로 인한 복사 버그
*   **리스크 (치명적)**: 교환 슬롯에 아이템을 올려둔 상태에서 그 아이템을 다시 "물리적인 손(Grab)"으로 잡은 채 [취소] 버튼을 누르면, 인벤토리로 반환(`AddItem`)되면서 손에도 들려있는 **복사 버그(Duping)**가 발생합니다.
*   **설계 결정 (Grab 차단 및 홀로그램화)**: 아이템이 패널 슬롯에 등록(Snap)되는 즉시, 해당 액터의 물리 연산(`SimulatePhysics`)과 VR 잡기 가능 속성(`bIsGrabbable`)을 **강제 비활성화(Disable)**해야 합니다. 교환 중인 아이템은 건드릴 수 없는 '전시(Hologram) 상태'가 되어야 하며, 오직 버튼(수락/취소)이나 다른 아이템 덮어쓰기만을 통해서 소유권이 바뀌어야 합니다.
