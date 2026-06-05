# NPC 지식(RAG) 작성 가이드

NPC가 자신의 설정·과거를 회상하게 하는 RAG 지식베이스 작성법.
런타임 데이터인 `app/agents/knowledge/` 는 `.gitignore` 됨 — 이 템플릿(`knowledge_template/`)만 버전관리된다.

## 1. 폴더 구조

NPC 한 명당 `lore / persona / history` 3개 카테고리 서브폴더에 `.md` 를 넣는다:

```
app/agents/knowledge/<npc_id_lowercase>/
├── lore/        # 세계관·지역·세력·사건 등 NPC가 아는 외부 지식
│   └── *.md
├── persona/     # 성격·말투·가치관·관계 등 NPC 자신에 대한 설정
│   └── *.md
└── history/     # NPC가 겪은 과거 이력·일화
    └── *.md
```

- `<npc_id>` 는 소문자. AgentID 와 일치해야 함 (예: `skadi`, `elara`, `james`).
- 폴더명(`lore/persona/history`)이 검색 결과의 `chunk_category` 메타데이터로 태깅된다.
- 그 외 폴더에 둔 `.md` 는 `chunk_category=general` 로 처리됨.

## 2. 작성 방법

1. 이 템플릿 폴더의 구조를 런타임 위치로 복사:
   `knowledge_template/{lore,persona,history}/` → `knowledge/<npc>/{lore,persona,history}/`
2. 예시 `.md` 를 참고해 실제 설정 텍스트로 채운다 (한 파일 = 한 주제 권장).
3. 한 청크는 256~1024자(약 1~3문단) 분량이 검색 품질에 좋다. 자동으로 500자 단위로 다시 쪼개짐(overlap 50).

## 3. 빌드(인덱싱)

`.md` 를 작성/수정한 뒤 FAISS 인덱스를 (재)빌드한다. CognitiveEngine 디렉터리에서:

```bash
python -m app.utils.build_knowledge --list            # NPC별 문서 수 확인
python -m app.utils.build_knowledge --agent skadi      # 특정 NPC
python -m app.utils.build_knowledge --all              # 전체
```

- 런타임에도 첫 retrieve 시 자동 빌드되지만, 편집 직후 즉시 반영하려면 위 명령 사용.
- 임베딩 모델: 로컬 `app/models/embeddings/all-MiniLM-L6-v2` (클라우드 임베딩 사용 금지 — 오프라인 동작).
- 출력: `knowledge/vectorstores/<npc>/` (FAISS index.faiss / index.pkl).

## 4. 검색 동작 (참고)

`rag_utils.retrieve_context(agent_id, query, k=3)` 가 NPC별 스토어에서 코사인 유사 상위 k개를 뽑아
프롬프트에 `[category:source] 내용` 형태로 주입한다. NPC마다 별도 스토어이므로 다른 NPC의 지식은 섞이지 않는다
(= PDF 설계서 §5 의 `npc_id` 메타필터를 스토어 분리로 달성).
