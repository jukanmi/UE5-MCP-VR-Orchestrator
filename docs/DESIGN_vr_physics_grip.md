# VR 물리 그립·NPC 상호작용 설계 논의 (2026-09-21, 코드 미구현 — 기술 스파이크 전 단계)

착수 시 `/feature-spec` 으로 서브시스템별 SPEC 을 쪼갤 것. 원래 `docs/Memo.md` Handoff T 절.

- **결론**: 논의한 항목 전부 기술적으로 가능(NPC 던지기·수플렉스급 연출 포함). 단 엔진 내장 원클릭 기능은 하나도 없음 — 전부 기존 Chaos physics/Control Rig 부품 조합으로 직접 구현. 참고 사례: Quinn Kuslich YouTube "Advanced VR Hand Physics for in Unreal Engine 5"(유료 Gumroad 애셋, 물리 손+breakable constraint+per-finger collision 구조가 아래 설계와 동일 — 전제조건으로 "VR Procedural Grip Poses" 튜토리얼 필요).
- **손가락 wrap 그립(플레이어)**: Control Rig 내장 솔버 없음, 직접 구현 필요. 실시간 절차적 wrap 아니고 "그립 지점별 고정 포즈 저작+블렌드" 방식이 실전적(Meta Interaction SDK HandGrabPose 도 결국 이 방식).
- **핸드트래킹 전환**: `OpenXRHandTracking` 플러그인 이미 켜져 있음. 단 **Quest 는 VIVE 가 쓰는 `XR_EXT_hand_interaction`(Pinch/Grasp Value 자동 제공) 미지원** — Meta 는 FB 프리픽스 확장(`XR_FB_hand_tracking_aim` 등)과 Interaction SDK(= OculusXR/Meta XR 플러그인, 현재 프로젝트에서 `Enabled: false`)로만 제공. 순정 OpenXR 유지 시 핀치 제스처 판정(손가락 거리 계산) 직접 구현 필요, 지름길 없음.
- **물체 밀기 저항/못 뚫음**: "ghost hand" 기법 — 실제 트래킹 위치를 쫓아가는 물리 시뮬 손(구체/캡슐)을 `PhysicsConstraintComponent` Linear Drive(Max Force 제한)로 구동. 무거운 물체는 Chaos 충돌이 못 밀어서 손도 표면에 막힘 — raw 트래킹은 계속 가는데 보이는 손만 멈추는 시각 트릭(햅틱 대체).
- **큰 물체/조각상 양손 그립 안정성**: `PhysicsConstraint` breakable + 무게중심 토크는 엔진이 자동 계산(형상 무관) — 모서리로 잡으면 토크 커져서 자동으로 놓침, 양손이 무게중심 양쪽 브라케팅하면 안정. 복잡 형상은 Convex Decomposition 필요(애셋당 Hull 10~20개 예산, 안 그러면 동적 상태일 때 물리비용 튐).
- **NPC 물건 쥐기**: 핸드트래킹 불필요(NPC 는 하드웨어 없음) — 기존 FBIK/Control Rig 로 충분, 단 **아이템 카탈로그가 유한할 때만**(정해진 무기·도구). NPC 가 임의 복잡 형상(조각상 등)을 쥐어야 하면 플레이어와 같은 절차적 문제로 재귀함.
- **NPC 가구에 걸려 넘어짐**: NavMesh 가 가구를 Nav Modifier 로 인식해 자동 회피 베이크하므로 **기본 설계로는 NPC 가 가구에 물리적으로 안 닿음**. 걸려 넘어지게 하려면 (a) 해당 가구만 Nav 영역에서 제외해 경로가 관통하게 하거나 (b) 내러티브 연출 순간에 MoveTo 대신 강제 직선 이동 트리거 — 후자가 훨씬 쌈.
- **NPC가 플레이어 잡기**: 시각 그립은 쌈(플레이어가 고정된 단일 리그라 그립 지점별 고정 포즈 몇 개 저작으로 끝, `GetHandLocation()` API 재사용). 물리적 구속은 `AVRPawn::SyncCapsuleToHMD`(매틱 HMD 위치로 캡슐 강제 동기화) 때문에 애니메이션만으론 안 먹힘 — 가구 착석(`SeatedFurniture`) 패턴처럼 별도 이동잠금 상태 필요.
- **NPC 던지기(수플렉스급 연출)**: 순수 관절 토크 계산으론 안 나옴. 그랩 순간 NPC 전신 래그돌 전환(`NPCRagdollComponent::EnterRagdoll` 재사용) + 그립 지점 강한 PD constraint(puppet 방식, breakable 아니고 뻣뻣하게) + 릴리즈 시 손 속도 임펄스(`InventoryComponent`/`VRPawn::ThrowVelocityScale` 이미 있는 패턴 재사용) — Blade & Sorcery 류가 실제로 쓰는 방식, 실사용자 팔 리치가 궤적 품질을 좌우(엔진 문제 아니고 물리적 제약).
- **NPC 안고 들기**: "겨드랑이 끼워 들기"(다리는 래그돌로 바닥에 끌림, 상체만 지지)가 princess-carry 보다 훨씬 쌈 — 기존 넉다운/기상 흐름(`Knockdown()`)과 자연스럽게 이어짐(의식 없는 NPC 끌고 가는 그림). 훅 판정은 겨드랑이 트리거 존 + 진입 각도(노멀 내적) 체크.
- **겨드랑이 "낀" 반응(공간이 생기며 파고듦)**: `Flinch()` 가 이미 쓰는 부분 물리 블렌드 패턴(`SetAllBodiesBelowSimulatePhysics(팔본, true)`) 재사용 — 평소 NPC 본은 QueryOnly(이미 기본값, 항상 분리된 콜리전)만 있다가, 플레이어 손이 근접할 때만 그 팔 하나만 SimulatePhysics 토글. PD 복귀력이 침입 콜라이더에 막혀 자연스럽게 "그 위에 걸침" 이 나옴 — 스크립트로 안 만들고 물리 결과로 나옴. 상시 켜두면 NPC 수만큼 물리비용 누적이라 반드시 근접 게이팅.
- **attach 없이 순수 접촉으로 들어올림(가장 리얼함, 가장 리스크 큼)**: 양팔 다 훅(무게중심 브라케팅 안 하면 회전하며 미끄러짐) + 그 순간 NPC 전신을 동적 물리 바디로 전환해야 함(팔만 블렌드로는 몸통이 안 들림). 안정성은 계산으로 확답 불가 — **기술 스파이크(먼저 프로토타입으로 되는지 확인) 필요**, "연구"라 부르기엔 새 지식 생산이 아니라 기존 부품(PD 블렌드+접촉 해석+2점 브라케팅) 재조합이라 정확한 표현 아님.
- **NPC 옷(Cloth)**: Chaos Cloth 엔진 내장(플러그인 아님). 밀기는 공짜(물리 손과 자동 충돌). 쥐고 당기기는 파티클 핀 방식 별도 구현 필요(Cloth 자체엔 런타임 핀 기능 없음). **진짜 성능 병목은 여기** — Quest 3S 90Hz 프레임 예산 ~11ms 에서 NPC 동시 Cloth 시뮬 개수 제한(LOD 로 근접만) 필수, 안 그러면 바로 병목.
- **총 작업 규모**: 물리 손(ghost hand), breakable constraint, Convex Decomposition, Cloth 파티클 핀, NavMesh 가구 예외 처리, 부분 물리 블렌드(팔 훅), 래그돌 puppet+release 임펄스 — 최소 5~6개 서브시스템 신설. 스프린트 하나 분량 아님, 우선순위 정해서 `/spec` 쪼개야 함.
