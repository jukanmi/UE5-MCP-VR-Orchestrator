# 파인튜닝 M2 (Stage1 e4b) 실행 결과 — 2026-07-21, 스위치 2026-07-23

## 결론: 파이프라인 성공 + 광역 eval 통과 + **MODELS 스위치 완료**. 실가구 타겟팅만 PIE 검증 대기.

## 스위치 후 광역 eval (2026-07-23, `eval/broad_result.json`)
- **gold 액션 재현**: 8개 카테고리(Sleep/Attack/GiveItem/Move/Comfort/Trade/Investigate/Follow) 구모델 3/8→**신모델 8/8**. 구모델 오답 전부 해소(Move→Wait, Comfort/Trade 누락, Investigate→Move).
- **held-out(학습에 없는 신규 발화) 6개**: 구모델 0/6 무동작. 신모델 2/6 의도인식(Sing·간접Sleep). speech 매번 다름 — overfit 아닌 실제 능력 개선.
- ⚠️ grammar 없는 raw 호출에선 간접Sleep 2건 포맷 깨짐(`"actions":"Sleep"` bare string) — **프로덕션 grammar 강제 하에선 미재현 확인**(`/api/debug/prompt` 스모크로 `ActionType:"Sleep"` 정상 출력).
- **target="Player" 오귀속**: debug 엔드포인트가 `nearby_furniture` 안 실어주는 구조적 한계(모델 문제 아님, 이전 세션 진단). 실가구 컨텍스트는 PIE 에서만 검증 가능.

## 다음
1. ✅ MODELS 스위치 완료, 서버 재시작 완료
2. **PIE 필요**: "누워"→Sleep+실제 Bed 타겟팅, 일반 대화 다양성(DoList 2-5)
3. 저품질/회귀 발견 시 롤백: `MODELS["gemma4_slm"]="gemma4:e4b"` (기존 태그 불변)

## 산출물
- **LoRA adapter**: `finetune/train/outputs/stage1_e4b/adapter/` (adapter_model.safetensors 293MB)
- **GGUF**: `finetune/deploy/out_gguf/gemma-4-e4b-it.Q4_K_M.gguf` (5.3GB)
- **Ollama 태그**: `gemma4-e4b-dialogue-v1` (등록 완료, `ollama list` 확인)

## 환경 (재현용)
- **venv**: `C:\github\chatbot\venv` 재사용 (torch2.11+cu128 sm_120, unsloth **2026.7.3** — 4.4는 gemma-4 미지원이라 git 최신 업뎃). WSL 불필요(Windows venv). 롤백: `finetune/unsloth_rollback.txt`
- **베이스**: `unsloth/gemma-4-E4B-it` (서빙 gemma4:e4b = arch gemma4 8B 와 정합)
- 학습: QLoRA r32/alpha32, seq **1024**(Blackwell>1024 크래시), lr2e-4, 3ep, 721행, 543스텝 ~1h

## Blackwell(RTX 5070 Ti SM120) 크래시 회피 (train_stage1_e4b.py)
1. `TORCHDYNAMO_DISABLE=1` — AOT autograd backward memory_format coerce 의 cudaErrorUnknown 회피(이거 없으면 ~175스텝서 크래시)
2. `CUDA_LAUNCH_BLOCKING=1` + `expandable_segments` + grad_and_value/autograd.backward synchronize 패치(chatbot/train.py 이식)
3. `_save` 몽키패치 — unsloth 가 SFTConfig 재정의해 torch.save pickle 크래시 → adapter+tokenizer만 저장
4. gemma-4 chat 마커 = `<|turn>user\n` / `<|turn>model\n` (gemma-3 `<start_of_turn>` 아님)

## eval (finetune/eval/exact_eval.json)
학습 프롬프트("침대에서 자"+Bed_02): 신모델 → `{"type":"Sleep","target":"Bed_02"}` **gold 완벽 재현**. speech 까지 복제 = overfit 경향(721행 소량).
⚠️ 축약 프롬프트(smoke_result.json)에선 한글 "누워" — 학습 분포 밖이라 왜곡. 프로덕션 DIALOGUE_STRUCTURED_PROMPT 에서만 정확.

## 다음 (사용자 복귀 후)
1. **MODELS 스위치**: `app/utils/llm_factory.py` MODELS["gemma4_slm"] = `"gemma4-e4b-dialogue-v1"` (기존 `gemma4:e4b` 불변 = 즉시 롤백)
2. 서버 재시작 → **PIE 로 "누워"→Sleep + 일반 대화 품질** 확인 (overfit 로 대화 다양성 저하 여부 주시)
3. 통과 시 스위치 확정 / 저품질이면 데이터 증량(721→SPEC 3000+) 재학습
4. Stage2 12B plan 학습(M3)은 별도

---

# 운영 주의점 (2026-09-13 Memo.md Handoff 에서 이관)

## Stage2 12B 는 unsloth 로 불가능 (2026-07-31)
설정·데이터를 아무리 고쳐도 모델 로드에서 `ValueError` 로 죽는다. 같은 진단을 반복하지 말 것.
- 12B 는 google·unsloth 양쪽 다 `model_type=gemma4_unified`(`Gemma4UnifiedForConditionalGeneration`). `gemma4` 아키텍처 12B 는 존재하지 않음. Stage1 이 된 건 e4b 가 구 아키텍처 `gemma4` 이기 때문.
- transformers 5.5.0 엔 `gemma4_unified` 모듈 없음(v5.6.0→v5.10.0 사이 추가). unsloth 는 2026.7.6 까지도 `transformers<=5.5.0` 상한이라 올릴 수 없고, unsloth main `loader.py` 에 `gemma4_unified` 0회.
- 재개 조건: unsloth 지원 추가 확인(`loader.py` grep + 핀 상한) 또는 unsloth 없이 순정 HF PEFT + transformers>=5.10 + bitsandbytes 로 **별도 venv**. 기존 `C:\github\chatbot\venv` 는 Stage1 배포 경로(e4b merge/GGUF)가 의존하므로 건드리지 말 것.
- 2026-09-08 이후 서빙 Stage2 플래너는 qwen3:8b(`MODELS["mid"]`) — 12B 파인튜닝 필요성 자체가 낮아졌다.

## VRAM 스필오버는 OOM 이 아니라 조용한 감속 (RTX 5070 Ti 16GB)
- 차면 WDDM 이 시스템 RAM 으로 페이징 → step 시간만 오른다. `nvidia-smi` free 는 PyTorch 캐싱 할로케이터 때문에 항상 0 근처라 판정 근거가 못 된다. **구간 실측(경과시간 차분/step 차분)으로만 판정**, tqdm 누적 평균에 속지 말 것.
- 실측: batch2×accum2 → 6→12→18→25→39s/it(07-24 런은 3863s/it 로 정지). batch1×accum4 → ~22s/it 정상상태 수렴 후 완주. 열·전력 스로틀 무관.
- **수렴과 발산을 구분할 것** — 감속을 발산으로 오판해 멀쩡한 런을 죽일 뻔했다.
- 학습 전 시퀀스 길이부터 재라. 페르소나 풀 300장 도입 후 p50 943·p90 981 로 1024 캡에 붙어 batch 2 가 감당 불가. 줄일 땐 batch 를 내리고 accum 을 같은 배수로 올려 유효배치 보존.
- `save_only_model=True` 라 체크포인트에 옵티마이저 상태 없음 → resume 시 Adam 모멘트 초기화(재웜업).

## 배포 경로
- `convert_to_gguf.sh` 3번째 인자 = 상속할 베이스 태그. Stage2 는 반드시 `gemma4-12b` — 기본값 `gemma4:e4b` 로 두면 12B 가 e4b 의 TEMPLATE/PARAMETER 를 물려받아 조용히 어긋난다.
- `merge_and_export.py` 는 `--out` 을 스테이지별로 분리하지 않으면 e4b GGUF 를 덮어쓴다. unsloth 는 출력 경로에 `_gguf` 를 덧붙인다(`out_v2` → `out_v2_gguf`).
- `.gitignore` 의 `finetune/*` 블랭킷은 추적 중 파일의 `git add` 도 막는다 → `git add -u` 또는 `-f`. 아티팩트(train/outputs 2.2G, deploy/out* 36G)는 계속 무시.

## 시드 데이터 페르소나 규칙 (2026-08-04)
고정 루프 템플릿의 동일 대사 반복·전역 유사도 과적용으로 NPC별 정당한 반응 삭제·Moca 연막탄 획일화가 있었다.
1. `docs/CORE_NPC_LOREBOOK.md` 가 코어 5인(Elara·Skadi·Moca·James·Guard)의 페르소나 기준.
2. 같은 상황이라도 NPC 가 다르면 독자 바리에이션으로 인정, 100% 보존.
3. Moca 는 Strict Non-Combatant(`Attack` 금지). 4단계 비전투 응대: `Dodge/Flee` → `Sing/Comfort` → `UseItem(Bandage)/HandObject(HerbTea)` → 고립 시 `UseItem(SmokeBomb)`.
4. `gold.speech_hint` 에 설명용 메타 텍스트 금지 — 인물이 직접 말하는 인게임 대사만.
