# ue_mcp — UE5 에디터 MCP 서버

클로드가 UE5 에디터를 직접 조작하기 위한 stdio MCP 서버.
UE5 `RemoteControl` 플러그인의 raw HTTP API(`127.0.0.1:30010`)를 툴 4개로 감싼 것.

## 전제

- **에디터가 켜져 있어야 한다.** RemoteControl 웹서버는 에디터 기동 시 자동 시작
  (`bAutoStartWebServer` 기본 true, 에디터면 무조건 허용 — `WebRemoteControl.cpp:231,361`).
  안 뜨면 에디터 콘솔에 `WebControl.StartServer`.
- 신규 파이썬 의존성 없음 — `.venv` 의 `mcp` + `httpx` 사용.
- localhost 요청은 passphrase 검사 면제. **`UE_RC_URL` 을 외부 IP 로 바꾸지 말 것** —
  RemoteControl 은 인증 없이 임의 UFunction 실행을 허용하므로 노출 시 원격 코드 실행이 된다.

## 등록

`.mcp.json` 의 `ue5` 항목. 클로드 재시작 후 `/mcp` 로 연결 확인.

**`.mcp.json` 은 `.gitignore` 대상**(`.gitignore:30`)이라 저장소에 안 올라간다. 새 클론에서는 직접 추가할 것:

```json
{
  "mcpServers": {
    "ue5": {
      "command": ".venv/Scripts/python.exe",
      "args": ["tools/ue_mcp/server.py"],
      "type": "stdio"
    }
  }
}
```

환경변수: `UE_RC_URL`(기본 `http://127.0.0.1:30010`) · `UE_RC_TIMEOUT`(기본 30초).

## 툴

| 툴 | 라우트 | 용도 |
|---|---|---|
| `ue_call_function` | `PUT /remote/object/call` | 임의 UFunction 호출 |
| `ue_get_property` | `PUT /remote/object/property` (READ) | 프로퍼티 읽기 |
| `ue_set_property` | `PUT /remote/object/property` (WRITE) | 프로퍼티 쓰기 |
| `ue_search_assets` | `PUT /remote/search/assets` | 에셋 경로 찾기 |

## 오브젝트 경로 형식

무엇을 하든 `object_path` 부터 알아내야 한다.

- 에디터 서브시스템 (CDO 경로로 호출): `/Script/UnrealEd.Default__EditorActorSubsystem`
- 에셋: `/Game/Blueprint/NPC/ABP_SmartNPC.ABP_SmartNPC`
- 블루프린트 CDO: `/Game/Blueprint/NPC/BP_SmartNPC.Default__BP_SmartNPC_C`
- 레벨 액터 인스턴스: `/Game/Level/Sample.Sample:PersistentLevel.BP_SmartNPC_C_1`

에셋 경로는 `ue_search_assets` 로, 레벨 액터 경로는 아래 레시피의 `GetAllLevelActors` 로 얻는다.

## 레시피

레벨 액터 전부 나열:
```
ue_call_function("/Script/UnrealEd.Default__EditorActorSubsystem", "GetAllLevelActors")
```

액터 위치 읽기/쓰기 (`RootComponent` 경유):
```
ue_get_property("<액터경로>.DefaultSceneRoot", "RelativeLocation")
ue_set_property("<액터경로>.DefaultSceneRoot", "RelativeLocation", {"X": 0, "Y": 0, "Z": 200})
```

에셋 프로퍼티 수정 후 디스크 저장 (수정은 메모리에만 반영되므로 저장 필수):
```
ue_call_function("/Script/EditorScriptingUtilities.Default__EditorAssetSubsystem",
                 "SaveLoadedAsset", {"AssetToSave": "<에셋경로>"})
```

가구 BP 기본값 확인 (에디터 수작업 검증용):
```
ue_search_assets("BP_Chair", class_names=["/Script/Engine.Blueprint"])
ue_get_property("/Game/.../BP_Chair.Default__BP_Chair_C", "FurnitureType")
```

## 한계

- **함수는 `BlueprintCallable`/`CallInEditor` 여야 한다.** 순수 C++ 내부 함수는 호출 불가.
- 블루프린트 **노드 그래프 편집 불가** — 이건 RemoteControl 범위 밖.
  (필요해지면 `PythonScriptPlugin` 기반 브리지로 확장해야 함 — 초기 검토의 경로 C)
- PIE 중 게임 로직 검증은 여전히 사용자 몫. 이 서버는 에디터 상태 조회·수정용.
