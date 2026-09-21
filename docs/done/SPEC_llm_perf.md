# SPEC: llm_perf — 12B 선제 웜업 + 메모리 요약 idle 디퍼

> 상태: **스펙 확정, 구현 대기** (2026-08-19 인터뷰 · 동일자 Ollama 실측). `/clear` 후 새 세션에서 이 파일 읽고 시작.
> 브랜치: `feature/llm-perf` — 베이스 **`a9936e4`**(당시 `feature/finetune` 팁). 아래 §5.1 라인 앵커가 이 커밋 기준이다.
> 원본 시도 `77a53d2` 는 **폐기됨**(2026-08-19). 태그 `archive/llm-perf` 로만 보존 — 브랜치 삭제. 코드 재사용 금지, 이유는 §7.
> 선행 지식: 없음. 단 **`SPEC_reflex_table` 과 파일 경합 있음** — §4 참조.

## 1. 목표 / Why

### 1.1 실측 (2026-08-19, `localhost:11434`, VRAM 16GB, `/api/generate` 직접 호출)

| 측정 | 값 |
|---|---|
| 12B 최초 콜드 로드(디스크 페이지캐시도 cold) | **36.9s** |
| 12B VRAM 축출 후 재로드(페이지캐시 warm) — **`keep_alive=30s` 만료 시 실제 겪는 값** | **11.0s** |
| 12B 상주 중, 빈 프롬프트 로드콜(`{"model":..,"keep_alive":".."}`) | **342ms** |
| 12B 상주 중, `num_predict=1` 생성(= `77a53d2` 웜업 방식) | **3.7s** (prompt_eval 3.4s) |
| Stage1 e4b 실측 1턴(prompt 1042tok · gen 60tok, 상주 상태) | **2.6–3.1s** |
| 12B VRAM 점유 | 8.36GB / 16GB |
| e4b(`gemma4-e4b-dialogue-v2`) 크기 | 5.3GB |

### 1.2 문제

`llm_factory` 는 12B core 만 `keep_alive="30s"`(다른 모델 5m) — idle squat 방지. 대가로 **replan 마다 11초 콜드 재로드**가 붙는다. replan 은 전투 진입·계획 무효화 등 지연이 가장 아픈 순간에 온다.

동시에 `memory_manager.add_entry` 는 토큰 예산 초과 시 그 자리에서 e2b 요약 LLM 을 돌린다 — 대화 저장 직후, 즉 다음 턴 Stage1·TTS 와 GPU 가 겹치는 최악 타이밍.

### 1.3 목표

1. **replan 콜드스타트 부분 완화** — Stage1 실행 창(3s)과 12B 로드를 병렬화.
2. **웜업 자체 비용 최소화** — 로드콜(342ms)로. 생성콜(3.7s) 아님.
3. **요약 GPU 경합 제거** — 대화가 조용해진 뒤로 요약 이동.

### 1.4 기대 효과의 정직한 상한 — 원안 주석은 틀렸다

`77a53d2` 주석은 *"Stage1 과 병렬로 미리 로드하면 cold-start 가 통째로 숨는다"* 라고 썼다. **실측상 거짓**: 숨길 창 3s < 로드 11s. replan 체감은 11s → 약 8s, **삭감분 3s(27%)**가 상한이다.

근본 해법은 웜업이 아니라 `keep_alive` 정책이다(12B 상주 = 콜드 0). 다만 12B 8.36 + e4b 5.3 = **13.66GB** 에 TTS GPU 가 얹히면 16GB 를 넘어 — 이는 DoList "VRAM 16GB OOM 대책 결정" 미결 항목에 걸려 있다. **이 스펙은 그 결정을 기다리지 않고 지금 얻을 수 있는 3s 를 가져가는 저위험 조치**로 한정한다. 결정이 "12B 상주" 로 나면 §3.1 은 통째로 불필요해진다 — 그때 폐기할 것.

## 2. 결정 사항 (2026-08-19 인터뷰 확정)

| 항목 | 결정 |
|------|------|
| 원본 브랜치 | **폐기** — `archive/llm-perf` 태그만 보존, `feature/llm-perf` 삭제. 코드 재사용 없이 재작성(§7) |
| 웜업 트리거 | **replan 턴 1곳만** — `_build_prompt_state` 의 `requires_replan` 확정 직후 |
| emergency 웜업 | **이 스펙 밖** — `SPEC_reflex_table` 로 이관(§4). 같은 함수를 두 스펙이 고치는 상황 회피 |
| 웜업 호출 형태 | **빈 프롬프트 로드콜**(`prompt` 키 없음). `num_predict=1` 생성 아님 — 실측 3.7s vs 342ms |
| 요약 디퍼 방식 | **`memory_manager` 자체 `threading.Timer`** — 호출부 무변경, 모듈 안에서 닫힘 |
| 디바운스 시간 | 10.0s (`SUMMARIZE_DEBOUNCE_S`) |

## 3. 설계

### 3.1 12B 선제 웜업

`main.py` 에 모듈 스코프 상태 + 코루틴 추가:

```python
_last_core_prewarm: float = 0.0
_CORE_PREWARM_THROTTLE_S = 30.0   # keep_alive(30s)와 동일 — 윈도 내 중복 웜업 무의미

async def _prewarm_core_llm() -> None:
    """Stage2 12B 를 빈 프롬프트 로드콜로 메모리에 올린다. 실패는 무해(콜드 폴백)."""
```

- **모델 해석**: `llm_factory.MODELS[llm_factory.STAGE2_MODEL]`.
  `DEFAULT_MODEL` 은 **존재하지 않는다** — `77a53d2` 의 치명적 stale 심볼(§7).
- **요청 바디**: `{"model": model_id, "keep_alive": "30s"}` — `prompt`·`options` 없음.
  Ollama 는 prompt 없는 `/api/generate` 를 "모델 로드만" 으로 처리한다. `keep_alive` 는 `llm_factory` core 분기 값과 **반드시 같은 문자열** — 다르면 웜업이 squat 정책을 조용히 뒤엎는다.
- **HTTP**: `_get_ollama_client()` 커넥션 풀 재사용. 단 이 클라이언트 timeout 은 **20.0s** 인데 최초 콜드는 36.9s — 최초 1회는 타임아웃으로 죽는다. **무해**(웜업 실패 = 기존 콜드 경로 폴백)이므로 timeout 을 늘리지 말 것. 늘리면 죽은 웜업 태스크가 커넥션을 오래 문다.
- **스로틀**: `time.monotonic()` 기준 30s. wall clock 금지.
- **발사**: `spawn_background(_prewarm_core_llm(), label="core-prewarm")` — `async_tasks` 공용 헬퍼(`main.py:37` 이미 import). `asyncio.create_task` 직접 호출 금지(R4 `607cb99` 에서 단일화한 규약 — 참조 유지·예외 로그 포함).
- **훅 지점**: `_build_prompt_state` 의 강제 재계획 폴백이 끝나 `requires_replan` 이 최종 확정된 직후, `async with _world_state_lock` **직전**. 폴백보다 앞에 두면 `replan=False→True` 승격 케이스를 놓친다.

### 3.2 메모리 요약 idle 디퍼

`memory_manager.py`:

- 상수 `SUMMARIZE_DEBOUNCE_S = 10.0` 추가.
- `ConversationMemory.__init__` 에 `self._summarize_timer: Optional[threading.Timer] = None`.
- `add_entry`: `self._check_and_summarize()` 인라인 호출 제거. 대신 `_save_to_file()` 후 예산 초과 시 `_schedule_summarize_locked()`.
- `_schedule_summarize_locked()`: 기존 타이머 `cancel()` → 새 `Timer(SUMMARIZE_DEBOUNCE_S, ...)`, `daemon=True`, `start()`. **대화가 이어지는 동안 계속 밀린다.**
- `_run_deferred_summarize()`: 타이머 스레드 콜백. `with self.lock:` 안에서 타이머 참조 해제 → `_check_and_summarize()` → entries 수가 변했을 때만 `_save_to_file()`.
- `_check_and_summarize`·`_summarize_oldest_entries` 본문은 **무변경**.

### 3.3 함정 (구현 시 필수 준수)

1. **`add_entry` 는 이벤트 루프가 없는 워커 스레드에서 돈다** — 호출부가 전부 `to_thread`(`dialogue.py:326`) / `spawn_background(to_thread(...))`(`main.py:458`). 여기서 `asyncio.*` 호출 금지. 이것이 §2 에서 `threading.Timer` 를 고른 이유.
2. **타이머 콜백은 `self.lock` 을 잡는다** — `add_entry` 도 같은 lock 을 잡는다. `_schedule_summarize_locked` 는 이름대로 **lock 보유 중에만** 호출할 것. 콜백 안에서 `_schedule_summarize_locked` 를 재호출하지 말 것(자기 재진입 → 요약 영구 연기).
3. **요약 유실 허용** — `daemon=True` 라 프로세스 종료 시 미발화 타이머는 사라진다. 대화 entries 자체는 `add_entry` 가 이미 파일 저장했으므로 **손실은 "요약이 다음 기회로 밀림" 뿐**. 이 트레이드오프를 바꾸려 non-daemon 으로 돌리지 말 것 — 서버 종료가 10초 매달린다.
4. **NPC 별 타이머** — `ConversationMemory` 는 NPC 당 1 인스턴스(`get_memory`). NPC 5명 동시 대화 시 타이머 5개가 거의 동시에 만료해 e2b 요약 5건이 겹칠 수 있다. 실측 부하 확인 전엔 방치하되, 문제 시 전역 세마포어(동시 1건)를 §8-①.
5. **웜업 실패는 로그만** — `except Exception` 으로 삼키고 `logger.warning`. 웜업이 요청 경로를 절대 블로킹·실패시키지 않는다.
6. **웜업은 `raise_for_status()` 하지 말 것** — 4xx/5xx 도 무해 폴백. `_ollama_raw_generate` 와 규율이 다르다.

## 4. Out of Scope / 경합

- **emergency_report 훅** — `SPEC_reflex_table` §3.5 가 `_handle_emergency_report` 를 통보 전용으로 재작성 중이다. 웜업 훅을 여기 넣으면 두 스펙이 같은 줄에서 충돌한다. **`SPEC_reflex_table` §3.5 에 "통보 전용화하면서 웜업 발사 한 줄 동반" 을 명시**했고, 이 스펙은 손대지 않는다.
  - 근거 보강: reflex_table 이 SLM 반사(e4b)를 걷어내면 전투 진입 순간 GPU 가 비므로 그 자리가 웜업에 **더 좋아진다**. 또 전투 진입은 replan 보다 리드타임이 길어 §1.4 의 3s 상한을 넘길 여지가 있는 유일한 지점이다.
- **`keep_alive` 정책 변경 / 12B 상주** — DoList "VRAM 16GB OOM 대책 결정" 소관. 여기서 결정하지 않는다.
- **TTS GPU/CPU 전환**, `num_ctx` 축소 — 동 항목.
- 요약 모델 교체(e2b→더 작은 것), 요약 알고리즘 변경.

## 5. 범위 (변경 파일)

- Python: `app/main.py`(웜업 코루틴·스로틀·훅 1곳), `app/utils/memory_manager.py`(디퍼 3개 멤버).
- 테스트: `tests/` 신규 — **기존 memory/warmup 관련 pytest 0건**(2026-08-19 확인, `tests/` 9개 파일 grep 무매치).
- 문서: `docs/SPEC_reflex_table.md`(§4 이관 명시 — 반영 완료), `docs/Memo.md`, `docs/DoList.md`.
- C++ 변경 **없음**. Envelope·스키마 변경 **없음**(CLAUDE.md §5 해당 없음).

### 5.1 코드 라인 앵커 (2026-08-19, `a9936e4` 기준 실측)

- `app/main.py`: `from .utils.async_tasks import spawn_background:37` · `_get_ollama_client:122`(timeout 20.0s) · `_ollama_raw_generate:132` · `_handle_emergency_report:463`(**손대지 말 것** — §4) · `victory-memory spawn:458`(디퍼 함정 §3.3.1 근거) · `_build_prompt_state:537` · `requires_replan` 확정 `:560-571` · **웜업 삽입점 `:573` 직전**(`async with _world_state_lock`)
- `app/utils/memory_manager.py`: `MAX_TOKENS_PER_NPC:28` · `SUMMARIZE_THRESHOLD:29` · `ENTRIES_TO_SUMMARIZE:31` · `self.lock:74` · `_save_to_file:96` · `add_entry:114`(인라인 요약 호출 `:129`) · `_check_and_summarize:133` · `_summarize_oldest_entries:146` · `get_memory:226` · `add_conversation:234`
- `app/utils/llm_factory.py`(HEAD 기준): `MODELS:33` · `STAGE2_MODEL:60` · `STAGE1_MODEL:62` · `OLLAMA_BASE_URL:64` · **core keep_alive 분기 `:102`**
  ⚠️ 이 파일은 2026-08-19 현재 **워킹트리에 미커밋 변경 +55/-11**(클라우드 모델 정리)이 있어 실제 줄번호가 약 +35 밀려 있다. 구현 시 줄번호 말고 심볼로 찾을 것.
- `app/agents/subgraphs/dialogue.py`: `to_thread(add_conversation, ...):326`

## 6. 검증

1. `pytest` 전체 통과 (기존 회귀 없음).
2. 신규 테스트:
   - 웜업 스로틀 — 30s 안에 2회 호출 시 HTTP 1회만 (httpx mock).
   - 웜업 실패 삼킴 — mock 이 예외/500 을 던져도 `_build_prompt_state` 정상 반환.
   - 모델 ID 해석 — `MODELS[STAGE2_MODEL]` 결과가 요청 바디에 실림 (`DEFAULT_MODEL` 회귀 방지).
   - 요청 바디에 `prompt` 키 부재 (로드콜 형태 회귀 방지, §7).
   - 디바운스 리셋 — `add_entry` 연속 호출 시 요약 1회만, 마지막 호출 후 실행 (`SUMMARIZE_DEBOUNCE_S` 를 테스트에서 0.1s 로 monkeypatch).
   - 예산 미달 시 타이머 미예약.
3. 스모크(Ollama 실기동): replan 프롬프트 1회 → 로그에 `[Prewarm]` 1줄 + `/api/ps` 에 12B 상주 확인.
4. **효과 측정** — 웜업 전후 replan 응답 wall time 비교. §1.4 예측대로 **약 3s 삭감**이면 정상. 그 이상이 나오면 측정 오류를 의심할 것(11s 가 전부 사라지지 않는다).
5. 서버 재시작 필요 — 반영 확인.

## 7. `77a53d2` 를 재사용하지 않는 이유

| 문제 | 내용 |
|---|---|
| **stale 심볼 (치명적)** | `llm_factory.DEFAULT_MODEL` 참조. 현 코드베이스에 **0건**(`STAGE2_MODEL`/`STAGE1_MODEL` 로 분리됨). 머지 시 `AttributeError` → 자체 `except` 가 삼킴 → 로그만 남고 **웜업 영구 무효**. 조용히 죽는 형태라 최악 |
| 웜업 호출 형태 | `num_predict=1` 생성 = 상주 상태에서도 3.7s(prompt_eval 3.4s 순낭비). 로드콜 342ms 로 대체 |
| 잘못된 전제 | 주석 "cold-start 가 통째로 숨는다" — 실측 반증(§1.4) |
| 헬퍼 미사용 | 자체 `_background_tasks` set + `create_task`. 이후 R4(`607cb99`)가 `async_tasks.spawn_background` 로 단일화 — 규약 위반 |
| 베이스 노후 | 베이스 `dd48ac1`(2026-07-11), 이후 `main.py` 15+ 커밋. cherry-pick 충돌 처리 비용 > 재작성 비용(순 추가 2파일) |

`memory_manager` 쪽 디퍼 로직은 `dd48ac1..a9936e4` 동안 그 파일을 건드린 커밋이 0건이라 **그대로 유효** — `archive/llm-perf` 의 해당 diff 를 참고 구현으로 써도 된다(`git show archive/llm-perf -- OmniAgent_VR_System/CognitiveEngine/app/utils/memory_manager.py`).

## 8. 미결

- ① NPC 다수 동시 요약 폭주(§3.3.4) — 전역 세마포어 필요 여부. 실측 후 판단.
- ② `_CORE_PREWARM_THROTTLE_S` 를 `llm_factory` core `keep_alive` 문자열에서 파싱해 자동 동기화할지, 상수 중복으로 둘지. 지금은 중복 + 주석 경고.
- ③ 12B 상주 결정(DoList VRAM 항목)이 나면 §3.1 폐기 — 그 시점에 재확인.
