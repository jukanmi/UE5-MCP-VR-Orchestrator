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
