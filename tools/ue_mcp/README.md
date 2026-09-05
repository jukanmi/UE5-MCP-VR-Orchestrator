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
| `ue_run_python` | `ExecutePythonCommandEx` 경유 | 에디터 안에서 `unreal` 모듈 실행 (에셋 생성 등) |

### `ue_run_python`

위 3개 툴은 **이미 있는** 오브젝트만 건드린다. 에셋을 **새로 만들거나** 컴포넌트를 붙이려면
RemoteControl raw API 로는 안 되고, PythonScriptPlugin 의 `unreal` 모듈이 필요하다.
별도 포트를 열지 않고 `UPythonScriptLibrary::ExecutePythonCommandEx`(BlueprintCallable static)를
기존 `/remote/object/call` 로 태운다.

**선행 설정 2가지** (둘 다 없으면 HTTP 400):

1. **PythonScriptPlugin 활성화** — 엔진 기본 비활성(`PythonScriptPlugin.uplugin:13`). `.uproject` 에 항목 추가 + 에디터 재시작.
2. **RemoteControl 보안 게이트 해제** — `RemoteControlModule.cpp:2531` 이 `bEnableRemotePythonExecution=false` 일 때
   `PythonScriptLibrary` 를 **클래스 이름으로 하드 차단**한다(`Object ... cannot be accessed remotely`).
   Project Settings > Plugins > Remote Control > Security 에서 `Restrict Server Access` 를 먼저 켜야
   (`RemoteControlSettings.h:347` editCondition) `Enable Remote Python Execution` 체크박스가 활성화된다.

> ⚠️ 2번은 **임의 코드 실행 스위치**다. 30010(HTTP)은 루프백 전용이지만 30020(WebSocket)은 기본 `0.0.0.0` 이라
> 같은 LAN 의 기기가 임의 코드를 실행할 수 있게 된다. 함께 `Remote Control Websocket Bind Address` 를
> `127.0.0.1` 로 바꿀 것.
- 반환값은 없다. 알고 싶은 값은 **`print` 로 찍어야** `LogOutput` 으로 회수된다.
- `mode`: `script`(기본, 여러 줄) · `eval`(표현식 1개, 값 반환) · `file`(디스크 .py 실행).
  `script` 는 코드를 `exec(...)` 한 줄로 감싸 보낸다 — UE 의 `ExecuteStatement` 가 `Py_single_input`
  컴파일이라 여러 줄을 그대로 주면 `SyntaxError: multiple statements found` 가 난다.
- Undo 트랜잭션을 걸지 않는다. 필요하면 스크립트 안에서 `unreal.ScopedEditorTransaction` 을 쓸 것.

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

블루프린트 에셋 생성 (부모 C++ 클래스 지정 + 컴파일 + 저장):
```python
ue_run_python('''
import unreal
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)   # 예: unreal.SmartNPCCharacter
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    "BP_Probe", "/Game/Blueprint/Test", None, factory)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
print(bp.get_path_name())
''')
```

컴포넌트 추가 (`SubobjectDataSubsystem` — UE5 에서 컴포넌트 계층을 다루는 유일한 공개 API):
```python
ue_run_python('''
import unreal
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
bp = unreal.load_asset("/Game/Blueprint/Test/BP_Probe")
root = sds.k2_gather_subobject_data_for_blueprint(bp)[0]
handle, fail = sds.add_new_subobject(
    unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.StaticMeshComponent,
                                 blueprint_context=bp))
print("fail:", fail)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
''')
```

CDO 기본값 세팅:
```python
ue_run_python('''
import unreal
cdo = unreal.get_default_object(unreal.load_blueprint_class("/Game/Blueprint/Test/BP_Probe"))
cdo.set_editor_property("FurnitureType", "Chair")
''')
```

검증용 스크린샷 (클로드가 PNG 를 직접 읽어 육안 확인):
```python
ue_run_python('unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, "probe.png")')
```

## 함정

- **`save_asset(path)` 는 저장 안 하고도 `True` 를 반환한다.** 방금 `create_asset` 으로 만든
  패키지처럼 dirty 플래그가 안 선 경우 `LogFileHelpers: 모든 파일이 이미 저장되었습니다` 만 찍고
  디스크에 아무것도 안 쓴다. 반환값을 성공으로 믿으면 에셋이 있다고 착각한 채 진행하게 된다
  (2026-09-05 실측: `IA_Grab` 생성·IMC 매핑·저장이 전부 True 였는데 `.uasset` 파일 자체가 없었음).
  → **에셋 저장은 항상 `save_asset(path, only_if_is_dirty=False)`**, 확인은 반환값이 아니라
  `os.path.exists(<디스크 경로>)` 로.
- **CDO `set_editor_property` 는 클래스에 그 프로퍼티가 없으면 예외 없이 조용히 무시된다.**
  C++ 에 새 `UPROPERTY` 를 추가했다면 **빌드 후 에디터를 재시작한 뒤** 세팅할 것. 세팅 직후
  같은 이름으로 `get_editor_property` 해서 값이 돌아오는지 확인한다.

## 한계

- **함수는 `BlueprintCallable`/`CallInEditor` 여야 한다.** 순수 C++ 내부 함수는 호출 불가
  (`ue_call_function` 한정. `ue_run_python` 은 `unreal` 모듈이 노출하는 전체 API 를 쓴다).
- 블루프린트 **노드 그래프(K2Node) 배선은 여전히 불가.** 파이썬에도 노드 스폰·핀 연결 API 가
  노출돼 있지 않다. 진짜 필요해지면 `BlueprintGraph` 모듈을 링크하는 자체 C++ 에디터 플러그인이
  유일한 길. 이 프로젝트는 로직이 전부 C++(StateTree·NPCActionComponent) 이라 당장은 불필요.
- PIE 중 게임 로직 검증은 여전히 사용자 몫. 이 서버는 에디터 상태 조회·수정용.
