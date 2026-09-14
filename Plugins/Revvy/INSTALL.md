# Revvy 플러그인 — 다른 프로젝트 설치 가이드

레비(Revvy) = 에디터 내장 + 외부 앱 AI 어시스턴트 (로컬 Claude Code / Kimi Code CLI 연동 — API 키 불필요, Codex/ChatGPT 로컬 로그인도 선택 가능).

## 요구사항
- **UE 5.8** (다른 버전은 빌드 호환성 확인 필요)
- **C++ 프로젝트** (블루프린트-only면 빌드 불가 — 에디터에서 "New C++ Class" 하나 추가하면 C++ 프로젝트로 전환됨)
- PC에 **Claude Code CLI** 로그인 상태 (`claude` 명령 동작) — 또는 **Kimi Code CLI** 로그인 상태 (`kimi` 명령 동작, 설정 ⚙에서 AI 엔진을 Kimi Code로 선택)
- (선택) **Codex CLI의 ChatGPT 로그인** — 채팅 제공자로도 선택 가능하며 이미지/텍스처 생성에도 사용
- 외부 앱/터미널/MCP 연동 시 **Python 3** (`python` 명령 동작)
- 외부 앱 EXE 재빌드 시 **PyInstaller** (`python -m pip install pyinstaller`)

## 설치 (3단계)
1. **복사**: 이 폴더(`Plugins/Revvy`) → 대상 프로젝트 `Plugins/` 에 통째로. (Binaries/Intermediate는 빼고 복사해도 됨 — 첫 빌드 때 생성)
2. **빌드**: `Plugins\Revvy\SetupUnrealDev.bat` **더블클릭** (아래 원클릭 셋업 참고). 또는 수동: .uproject 우클릭 → Generate Visual Studio project files → 빌드.
3. **활성화**: 에디터 → Edit > Plugins → "레비 (Revvy)" 체크 → 재시작.

## 원클릭 환경 셋업 (아트 작업자용)

Visual Studio가 없는 새 PC라도 `Plugins\Revvy\SetupUnrealDev.bat` 더블클릭 한 번이면 끝:

1. `.uproject`와 엔진 설치를 자동 탐색 (EngineAssociation → 레지스트리 → 스캔)
2. 엔진의 `Windows_SDK.json` 기준으로 MSVC 툴체인 검증 (최소/금지 버전 반영)
3. 없으면 **VS Build Tools 자동 설치** — 관리자 승인(UAC) 창 1회, 10~30분 조용히 진행
4. Visual Studio 프로젝트 파일 생성 → 에디터 타깃 빌드까지 완료
5. 끝나면 `.uproject` 더블클릭으로 바로 에디터 실행

.NET 별도 설치는 필요 없음(엔진 번들 dotnet 사용). 옵션: `-CheckOnly`(진단만),
`-SkipInstall`, `-SkipBuild`, `-NoPrompt`. 로그: `%LOCALAPPDATA%\Revvy\SetupLogs\`.

에디터 안에서는 레비에게 "환경 체크해줘"라고 하면 `DevEnvironmentService`로 진단하고,
필요 시 이 셋업 콘솔을 대신 띄워줍니다.

## 첫 실행 (전부 자동)
- 하단 상태바 **[레비]** 버튼 → 채팅 패널
- MCP 토큰/.mcp.json/프록시 — 자동 프로비저닝 (설정 불필요)
- Claude/Kimi/Codex CLI 경로 — 자동 탐색 (설정 ⚙에서 수동 지정 가능). 설정에서 **Codex GPT**를 선택하면 현재 ChatGPT 로그인을 그대로 사용하며 API 키를 저장하지 않음

## 외부 Revvy 앱

명령 창 없이 `Plugins\Revvy\RevvyApp.vbs`를 실행합니다. 패키징된 앱은 `Binaries\RevvyApp\Revvy.exe`를 열고, 없으면 Python GUI로 실행됩니다.

EXE를 다시 빌드하려면:

```powershell
Plugins\Revvy\BuildRevvyApp.ps1
```

앱의 채팅 세션과 설정은 `%APPDATA%\Revvy`에 남고, 대화 본문은 프로젝트별 `chat-history.json`에 원자적으로 저장됩니다. 앱이나 Unreal Editor를 다시 실행해도 기존 대화가 자동 복원되며, **새 대화**를 누르면 현재 프로젝트 기록만 지웁니다. 독립 프록시는 Unreal Editor 재시작 중에도 유지됩니다.

앱의 **권한**은 기본적으로 `Safe · 읽기 전용`입니다. `Full access · 전체 권한`을 선택하면 Codex/Claude가 승인 창 없이 파일 쓰기, 셸 명령, 네트워크와 사용자 로컬 MCP 설정을 사용할 수 있으며, 권한 변경 시 기존 세션 대신 새 세션을 시작합니다. Codex에서는 `%USERPROFILE%\.codex\config.toml`도 로드하므로 전역 MCP 구성을 사용할 수 있습니다. 외부 MCP 토큰은 프로젝트나 Revvy 설정에 넣지 말고 폐기·재발급한 값을 PC 환경변수에만 저장하세요.

외부 앱도 내장 Revvy와 같은 밝고 다정한 한국어 해요체를 사용합니다. 채팅 화면의 **작업 진행** 패널은 명령과 도구 단계를 누적해 `진행 중 → 완료/실패/중지`로 갱신하고 완료 단계 수를 표시합니다. Unreal 영역의 **캐시 정리 + 프로젝트 파일 재생성**은 Unreal과 IDE가 종료된 경우에만 프로젝트 루트의 `.vs`, `Binaries`, `Intermediate`, `DerivedDataCache`를 정리하며, `Saved`, `Content`, `Config`, `Source`, `Plugins`는 보존합니다.

외부 터미널/IDE에서도 사용할 프로젝트는 루트에서 한 번 실행:

```powershell
Plugins\Revvy\revvy.cmd install --project .
Plugins\Revvy\revvy.cmd status --json
```

프록시는 에디터와 독립적으로 유지되므로 에디터 재시작 중에도 도구/스킬 조회가 가능하고, 재연결 후 같은 외부 세션을 계속 사용합니다.

설치기는 프로젝트별 포트를 할당하고 `.mcp.json`에 읽기 전용 엔드포인트(`http://127.0.0.1:<proxy_port>/mcp/safe`)를 씁니다. 실제 포트는 `<project>/.revvy/revvy-proxy.json`에서 확인하세요. 본인 클라이언트에서 변경 도구까지 쓰려면 Full 권한 베어러가 필요합니다 — [README.ko.md의 "외부 클라이언트 권한"](README.ko.md#-외부-클라이언트-권한--도구가-읽기-전용밖에-없어요) 참조.

## 내장 자산 (프로젝트 무관 — 플러그인에 포함)
- `/Revvy/VFX/_Library/NS_EmitterPalette` — 이미터 블록 8종 (이펙트 조립용)
- `/Revvy/VFX/Textures/` — VFX 텍스처 (생성 6종 + 추출 15종)
- `Content/Python/` — levy_vfx 원샷 빌더(+프리셋 6종), 팔레트/마스터 구축 스크립트
- `Content/Skills/` — 엔진별 디렉터리(`unreal/` 언리얼 전용, `shared/` 엔진 공통)로 나뉜 도메인 지식과 워크플로 (실제 목록은 `manage_skills(action="list")`로 조회)
- `/Revvy/VFX/Materials/` — 마스터 머티리얼 3종 + MI 6종 (내장 — 바로 사용; 손상 시 `build_vfx_assets.py`로 재생성)

## 주의
- 내부 모듈명은 `Revvy` 유지 (코드 식별자 — 변경 시 빌드/MCP 와이어링 깨짐). 플러그인 표시명만 Revvy.
- 지식 문서의 절대 경로(`C:/Unreal/8th-Team8-CH3-Project/...`)는 levy_vfx sys.path 예시에 있음 — 다른 프로젝트에선 `Plugins/Revvy/Content/Python` 상대 경로로 잡히도록 레비가 프로젝트 루트 기준으로 사용.
