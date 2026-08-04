# ProjectG 협업 가이드

## 핵심 규칙

1. 모든 작업은 기능별 `feature` 브랜치에서 진행합니다.
2. 기능 완성 전에는 절대 `main`에 반영하지 않습니다.
3. 완성된 기능은 직접 Merge하지 않고 반드시 Pull Request를 생성합니다.
4. `main`에는 직접 Commit, Push, Force Push하지 않습니다.
5. Blueprint와 Map은 수정 전에 담당자와 Lock 상태를 확인합니다.

## 1. 최초 설정

```bash
git lfs install
git clone https://github.com/NBcampUnrealTrack/8th-Team10-CH4-Project.git
cd 8th-Team10-CH4-Project
git lfs pull
```

## 2. 브랜치 작업

개인 장기 브랜치는 만들지 않고 기능별 브랜치를 사용합니다.

```text
feature/combo-system
feature/lock-on
feature/weapon-pickup
feature/player-hud
fix/input-binding
```

작업 시작:

```bash
git switch main
git pull --ff-only origin main
git switch -c feature/기능명
```

작업 저장:

```bash
git add <변경 파일>
git commit -m "[FEAT] 기능 설명"
git push -u origin feature/기능명
```

기능 하나당 브랜치 하나만 사용하며 C++, Blueprint, Animation, DataAsset을 함께 관리합니다.

## 3. Unreal 에셋 규칙

### 작업 영역

```text
Content/Developers/[이름] : 개인 테스트와 임시 에셋
Content/Main              : 게임에 적용되는 공용 에셋
Content/Level             : 공용 맵
```

- 다른 팀원의 Developers 폴더를 수정하지 않습니다.
- `Content/Main`의 에셋이 Developers 폴더의 에셋을 참조하면 안 됩니다.
- 에셋 이동은 Unreal Content Browser에서 수행합니다.
- 이동 후 `Fix Up Redirectors in Folder`를 실행합니다.
- 공용 Map은 한 번에 한 명만 수정합니다.

### Lock

`.uasset`과 `.umap`은 병합할 수 없으므로 수정 전에 Lock합니다.

```bash
git lfs locks
git lfs lock "Content/Main/경로/파일.uasset"
```

다른 팀원이 Lock한 파일은 수정하지 않습니다. PR Merge 후 Lock을 해제합니다.

```bash
git lfs unlock "Content/Main/경로/파일.uasset"
```

## 4. Commit 규칙

```text
[ADD] 신규 파일 또는 에셋 추가
[FEAT] 새로운 기능 구현
[FIX] 버그 수정
[UPD] 기존 기능 또는 수치 변경
[REFACTOR] 구조 개선
[DOC] 문서 변경
```

예시:

```text
[FEAT] 콤보 입력 버퍼 구현
[FIX] 공격 Montage Section 전환 오류 수정
```

Commit 하나에는 하나의 목적만 포함합니다. C++ 변경은 Commit 전에 반드시 로컬 컴파일을 확인합니다.

## 5. Pull Request

기능 완성 후 Feature 브랜치에서 `main`으로 PR을 생성합니다.

PR 전 확인:

- 로컬 컴파일 성공
- 핵심 기능 테스트 완료
- 임시 로그와 테스트 코드 제거
- 관련 없는 파일 제외
- Blueprint 및 Map 충돌 확인
- 구현 내용과 테스트 방법 작성

PR 작성자는 직접 Merge하지 않습니다. 리뷰 담당자가 확인 후 Merge합니다.

```text
최신 main 받기
→ feature 브랜치 생성
→ 기능 구현 및 테스트
→ Feature 브랜치 Push
→ PR 생성
→ 리뷰 후 main Merge
```
