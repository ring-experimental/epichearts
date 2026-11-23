# EpicHearts SDL3 이식판 (원본 정적 리컴파일)

원본 `epichearts.apk`의 게임 본체 `lib/armeabi/libtar-android.so`(ARMv5TE Thumb-1, soft-float)를
**기계어 단위로 C로 정적 리컴파일**하고, 안드로이드 플랫폼 계층(GL ES1, JNI/Java 브리지, libc)을
SDL3로 대체해 Windows에서 실행합니다. 게임 로직·렌더링(소프트웨어 래스터라이저)·리소스 해석은
전부 원본 코드 그대로 실행됩니다.

> [!IMPORTANT]
> **이 저장소에는 원작이 들어 있지 않습니다.** 원본 `epichearts.apk`와 거기서 나온 파생물(추출한 `.so`, 리컴파일 C `src/gen/`,
> 심볼 목록 `syms_dem.txt`, 원작 아이콘으로 만든 앱 아이콘, 글리프 캐시, 필드 출구 그래프, 원본 세이브, 원본 실행 화면·덤프
> `work/databind/golden*/`)은 저장소에도 커밋 기록에도 없습니다(`.gitignore`). 직접 가진 APK를 저장소 루트에 두고
> `python tools/bootstrap.py`를 실행하면 로컬에서 다시 만듭니다. 경로는 모두 저장소 루트 기준 상대 경로이고, 기계마다 다른 것
> (Android SDK·NDK, Visual Studio, 서명 키)은 환경 변수나 표준 설치 위치에서 찾습니다.

브라우저 판(PWA): <https://ring-experimental.github.io/epichearts/>
페이지를 열고 가지고 있는 원본 `epichearts.apk`를 고르면 그 기기 안에서 실행합니다. 파일은 어디로도 보내지 않습니다.

## 웹 빌드 (`web/`, `src/interp.c`)

네이티브 빌드는 원본 코드를 C로 번역(`src/gen`)해 실행 파일에 넣으므로 공개 배포하지 않습니다. 웹 빌드는 번역 C 없이
**원본 .so를 브라우저에서 해석 실행**합니다. 페이지에는 실행 환경만 있고, 게임 코드는 사용자가 고른 APK에서 읽습니다.

| 구성 | 내용 |
|---|---|
| `src/interp.c` | ARMv5TE Thumb 인터프리터(+ 원본의 Thumb-2 패치 구역에 쓰인 명령). `rc.h`의 레지스터 파일, 임포트, 훅을 번역 빌드와 그대로 공유 |
| `src/accel.c` | 원본의 가장 무거운 픽셀 루틴 16개(`alPaletteEf_R5G6B5`, `fnGFFillRect_2b`, 팔레트·알파 행 그리기 등)를 디스어셈블리로부터 다시 쓴 네이티브 판. 각 루틴에 원본 주소를 적었다 |
| `src/hooks.h` | 호스트 래퍼가 붙는 원본 함수 목록. 번역 빌드(`tools/recomp.py`)와 해석 빌드가 같은 목록을 쓴다 |
| `web/` | 페이지(`index.html`, `app.js`), 서비스 워커(`sw.js`: 오프라인 캐시 + 교차 출처 격리 헤더), 매니페스트, 직접 그린 아이콘 |

- 스레드(원본의 게임 스레드와 pthread)는 Emscripten pthreads로 돈다. SharedArrayBuffer에 필요한 COOP/COEP 헤더를
  GitHub Pages가 보내지 않으므로 서비스 워커가 붙이고, 첫 방문은 한 번 새로 고친다.
- APK는 IndexedDB(`epichearts-apk`)에 그대로 두고, 시작할 때 wasm 메모리로 옮겨 `fmemopen`으로 읽는다(`src/main.c`
  `eh_set_apk`). 세이브와 설정은 `/persist`(IDBFS)에 있고 몇 초마다, 페이지를 떠날 때 저장한다.
- 지원하는 원본은 `lib/armeabi/libtar-android.so`의 SHA-256이 `36eb21b8…`인 판 하나다(훅과 네이티브 루틴이 그 판의 주소를 쓴다).
- 성능(헤드리스 Chrome, 소프트웨어 GL): 필드에서 원작 속도(프레임 간격 37 ms, 약 27 fps)를 유지하며 프레임마다 약 30 ms가 남는다.

빌드와 확인:

```bash
bash tools/deps.sh                 # third_party/: SDL3, FreeType, RmlUi, stb_truetype, dr_mp3 (고정 판, SHA-256 확인)
emcmake cmake -S . -B build_web -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build_web   # Windows: build_web.bat
python tools/web_site.py           # build_web/site: 페이지 + epichearts.js/.wasm/.data
python tools/web_serve.py 8765     # COOP/COEP 헤더를 붙여 로컬에서 연다
```

`main`에 푸시하면 `.github/workflows/pages.yml`이 원본 없는 러너에서 빌드해 Pages에 올린다.
네이티브로 인터프리터를 확인하려면 `build_interp.bat`(32비트, `-DEH_INTERP=ON`). 시험 스위치:
`EH_ACCEL=0`(네이티브 루틴 끔), `-DEH_ACCEL_CHECK` 빌드의 `EH_ACCEL_CHECK=1`(호출마다 원본과 결과 비교),
`-DEH_IPROF` 빌드(함수별 명령 수 프로파일). 웹 페이지에서는 `?env=EH_BOT=1,EH_FPS=1`처럼 같은 스위치를 넘긴다.

## 구조

| 경로 | 내용 |
|---|---|
| `tools/recomp.py` | 리컴파일러. ELF + Ghidra 흐름 정보(`work/flow.txt`) → `src/gen/*.c` |
| `ghidra_scripts/ExportFlow.java` | 명령어 목록·함수 범위·switch 대상 추출 |
| `ghidra_scripts/DumpAll.java` | 전체 디컴파일 덤프(`work/decomp/<클래스>.c`) — 분석용 (덤프는 저장소에 없음, 필요하면 다시 생성) |
| `ghidra_proj/` | 원본 .so의 Ghidra 분석 프로젝트 |
| `tools/bootstrap.py` | 처음부터 재현: APK에서 .so 추출 → `src/gen` 재생성 → 빌드 |
| `src/loader.c` | .so 이미지 매핑, 재배치, 게스트 주소 → 함수 디스패치, 프레임 언와인딩 |
| `src/libc.c` | libc/libm/pthread/`__aeabi_*` 임포트 |
| `src/gl.c` | GL ES1 부분집합(`glTexSubImage2D` + `glDrawTexiOES`) → SDL_Renderer |
| `src/jni.c`, `src/jni.h` | 가짜 JNIEnv/JavaVM 테이블, 객체·문자열·배열·모니터 |
| `src/java*.c` | classes.dex의 Java 클래스 재구현(smali 근거): GofManager, 파일·폰트·오디오·장치 브리지, SimpleBackgroundNet, cAndroid_textView |
| `src/hooks.c` | 리컴파일 함수 래퍼(테스트 모드 전용) |
| `src/main.c` | 액티비티 역할: 초기화 순서, 게임 스레드, 렌더 루프, 입력 |

주소 표기: 함수명 `f_XXXXXX`는 ELF vaddr, 주석의 ghidra 주소 = vaddr + 0x10000.

## 빌드 / 실행

처음 받은 상태(생성 코드·빌드 폴더 없음)에서는 한 번에:

```bash
python tools/bootstrap.py
```

필요한 것: `epichearts.apk`, `third_party/`, Python 패키지 `capstone`·`pyelftools`, Visual Studio(MSVC x86, 내장 CMake/Ninja).
`work/apk/lib/armeabi/libtar-android.so`(APK에서 추출)와 `src/gen/`(리컴파일 결과)은 생성물이라 저장소에 두지 않음.
이후 소스만 고쳤을 때는:

```bash
cmd //c build.bat
```

실행: `build\epichearts.exe`를 더블클릭하거나 APK 경로를 인자로 준다.

```bash
build/epichearts.exe epichearts.apk
```

- 배포용은 64비트 빌드(`cmd //c build64.bat` → `build64\epichearts.exe`, 게스트 주소 공간 `src/gas.h`). 32비트(x86, `build.bat`)는 게스트 포인터 = 호스트 포인터인 개발·비교용 기준 빌드로만 둔다.
- **외부 공유용 빌드** (`dist/`):
  - PC: `cmd //c build64.bat` 후 `python tools/package_win.py` → `dist/EpicHearts.exe` 한 파일(원작 APK·UI 데이터 포함). 첫 실행 때 `%LOCALAPPDATA%\EpicHearts`에 풀고, 세이브·설정은 `문서\EpicHearts`.
  - 안드로이드: `python tools/build_android.py --release` → `dist/EpicHearts.apk`. 배포 키 `~/.epichearts-signing/epichearts-release.jks`로 서명(비밀번호는 실행 중 입력, 또는 환경 변수 `EH_KS_PASS`), 버전 번호는 빌드 시각으로 자동 증가. 키 파일과 비밀번호를 잃으면 이미 설치된 앱을 업데이트할 수 없으니 백업.
- 앱 아이콘은 원작 APK의 `res/drawable-*/icon.png`: `python tools/make_icons.py`가 `android/res/mipmap-*`와 Windows용 `src/app.ico`(실행 파일·창 아이콘, `src/app.rc`)를 만든다.
- UI 아이콘 폰트(lucide)는 쓰는 아이콘만 남긴 부분집합: 새 아이콘을 쓰면 `python tools/subset_fonts.py`. 글자는 원작 폰트(`res/raw/fttcustom`)를 그대로 쓴다.
- 안드로이드: `python tools/build_android.py` → arm64-v8a만 담은 APK. 오래된 32비트 기기·x86 에뮬레이터가 필요하면 `--abi arm64-v8a,armeabi-v7a,x86_64`.
- APK는 그대로 읽습니다(에셋·.so·mp3가 모두 STORED). 세이브는 APK가 있는 폴더(인자로 줄 때는 작업 폴더)의 `save/`. 비교 테스트용 Lv14 세이브는 `tests/saves/lv14/`(슬롯 2 = 메인 퀘스트 13).
- MP3는 Windows Media Foundation으로 디코딩, 폰트는 원본 `res/raw/fttcustom`(OTF)을 stb_truetype로 렌더링.
- 네트워크는 없음: 소켓은 실패, `SimpleBackgroundNet` 요청은 원본 예외 경로(결과 2)로 처리.

## 테스트/검증용 환경 변수

| 변수 | 동작 |
|---|---|
| `EH_CTRL=<파일>` | 실행 중 명령 파일 폴링: `tap x y`, `down/up/move x y`, `shot 파일.bmp`, `quit` |
| `EH_SCRIPT="<프레임> tap x y; ..."` | 고정 스크립트 입력 |
| `EH_MONKEY=<seed>` | 무작위 터치 스트레스 테스트 |
| `EH_SHOT=<n>` | n프레임마다 스크린샷 |
| `EH_JTRACE=1` | 모든 JNI 호출 로그 |
| `EH_TESTBUFF=1` | **검증 봇 전용** 데미지 하한: 일반 몬스터 1~2타, 보스는 페이즈(HP 구간)당 2~3타(페이즈 없으면 5타), 플레이어 공격은 빗나가지 않음(원본의 명중 무시 플래그). 기본 꺼짐(원작 그대로). `=2`는 타격 로그 |
| `EH_TESTGOD=1` | **봇 전용**: 플레이어 HP가 1 아래로 내려가지 않고 즉사 메시지(1/9)도 무시. 기본 꺼짐 |
| `EH_WARPTO=g/s[/fg/fs]` | **보스 검증용**: 원본 `PendMoveField`로 필드 g/s에 이동(fg/fs를 주면 그 필드의 출구로 진입). 걸을 수 없는 위치면 보스 옆으로 옮김 |
| `EH_WARPGAUGE=1` | `EH_WARPTO`와 함께: 스크립트가 보스전을 시작하지 않으면 원본 `SetBossGauge(1, instId, 0)`로 보스 게이지 등록 |
| `EH_INDUN=g/i` / `EH_INDUNSCAN=1` | 원본 `cDungeonManager::Enter`로 인던 진입 / 모든 인던의 스테이지·보스 스테이지 목록 로그(`[indun]`) |
| `EH_WARPSCAN=g/s,...` | 필드 목록을 차례로 이동하며 보스 몬스터 배치를 로그(`[warpscan]`) |
| `EH_BOT=1` | 자동 진행 봇(`src/bot.c`): 게임 메모리를 원본 오프셋대로 읽어 터치를 넣음. `EH_TESTBUFF=1`과 함께 사용 |
| `EH_TURBO=1` | **테스트 전용**: 프레임 대기(appLock.wait) 생략 — 게임 로직을 CPU 한계 속도로 진행 |
| `EH_BOTDBG=1` | 봇 디버그: 플레이어/스크롤/오브젝트 목록 주기 덤프 |
| `EH_FPS=1` | FPS와 원본 프레임 간격(cNixSystem+0x14) 로그 |
| `EH_WAVDUMP=<파일>` | 믹서 출력(float32 stereo 44.1kHz) 덤프 |

로그: 실행 폴더의 `epichearts.log`.

### 자동 진행 봇

`work/bot_spec.md`(디컴파일 근거의 상태 포인터 체인)를 따릅니다. 게임 스레드의 `GofManager.procTouch`
시점에 실행되어 경합 없이 상태를 읽습니다.

- 메뉴(state 1): `cFormMainMenu`(폼 50) 페이지(+0x184)로 판단 — 0: Game Start, 1: 안내 팝업 확인 → 슬롯 1
- 필드(state 3): 대화 대기(`mwm+4`, flag 4)면 탭, 스크립트 실행 중이면 대기, 가까운 몬스터(목표가 있으면 90px,
  없으면 260px 이내)는 탭 공격, 그다음 힌트 → 퀘스트 NPC(퀘스트 아이콘의 주인; 직선이 열려 있으면 탭으로 원본의
  자동 이동/대화) → 스크립트 트리거 → 맵 출구·액션 오브젝트(점프/사다리 등) 탐색
- 경로: 타일 충돌(`cField+0xE4`, flag 0x40) + 오브젝트 점유 상자(`cGObj::GetPossArea`) 위 BFS, 시야 내 최원점 추종
- UI 폼(state 4): 폼 ID별 처리(1 메시지, 3 리스트 팝업, 6 GP 투자, 39 캐릭터 선택), 그 외 뒤로가기/닫기
- 약 4분(게임 프레임 기준)마다 메뉴로 저장(가방 → 시스템 → 저장하기 → 확인)
- 미션 팝업(`gm+0x1fc`, `cShowPendingMission`)은 터치로 닫음 — 열려 있는 동안 원본은 스크립트를 하나도 실행하지 않음
- 퀘스트: 메인 퀘스트 목표(`cQuest+0x1C`)로 목표 필드/대상을 정하고, 목표 필드에서 대상이 없으면(아이템·처치) 필드의 몬스터를 모두 사냥,
  액션 오브젝트(스크립트 이벤트)는 충돌 상자 바깥 접근점까지 가서 탭
- 회오리(`cActionStuff` 타입 0x20): 짝 회오리 반대쪽에서 진입해 그 방향으로 날아감(원본 `cWhirlwindProcess`) — 벽으로 막힌 구역 이동.
  퀘스트 방향 출구가 막혀 있으면 다른 출구 대신 이것부터 시도. 떠다니는 구름(타입 0x167/0x168)은 무시
- 막힘: 길이 없는 몬스터는 대상에서 제외, 같은 목표로 3번 우회해도 안 되면 그 목표를 30초간 피함
- 출구 판정: 필드 목적지(`+0x3C`)가 있는 스크립트 트리거(예: 32번 필드 문지기 타입 2 → 2/1)는 출구로 취급, 벽 안의 스크립트 트리거는 건너뜀
- 필드 전환 직후 20틱은 대기(오브젝트 목록이 이전 필드 것), 비둘기(`cBird`)는 장애물로 보지 않고 누름 지점은 오브젝트에서 떨어뜨림
- 진행 확인(새 게임 → 퀘스트 7 완료): 프롤로그 → 세이프우드 → 회오리로 동쪽 → 몬스터 오일 획득 → 제페타 보고 → 마우스트대공 방문
- 필드 경로: `data/field_exits.txt`(원본 필드 데이터에서 뽑은 출구 그래프, 필드 1192개 출구)를 읽어 퀘스트 목표 필드까지 최단 경로로 이동.
  `EH_WARPSCAN`으로 모든 필드를 원본 `PendMoveField`로 불러 `cTrigger` 목적지(+0x3C/+0x3E)를 기록해 만든 것(경로: `EH_FIELDGRAPH`로 변경)
- 채집: 메인 퀘스트 아이템 목표가 남아 있으면 필드의 채집물(오브젝트 종류 9, `cCollectStuff`)에 붙어서 탭(채집 중엔 대기)
- 막힌 칸: 바로 옆(48px) 링크 오브젝트는 경로가 없어도 탭(회오리로 들어온 작은 구역)
- 가방 초과: 원본은 가방 아이템 수가 용량을 넘으면 이동을 거부함(`cIdleKeyProcess::MoveKeyProcess` →
  `cGameWorld::WarnIfPlayerCannotMoveCauseBackpackIsFull` @0012c898, "백팩이 가득차서…" 메시지). 퀘스트 보상으로 넘치면 원본도 그대로 멈춤
  (에뮬레이터에서 같은 세이브로 확인). 봇은 상점 판매와 같은 순서(`cFormStore::doCompleteSelling` @0011e12c)로 싼 물건부터 팔아 해소
- 발동하지 않는 스토리 트리거(퀘스트 조건 미충족) 위에 서면 90초간 제외
- 디버그: `EH_BOTDBG`, `EH_BOTMAP`, `EH_OBJDUMP`(필드 진입 후 오브젝트 목록), `EH_PLDUMP`(플레이어/월드 덤프),
  `EH_MOVETEST=<틱>`(8방향 탭 이동 시험), `EH_MOVETRACE`(이동/터치 판정 함수 로그)
- 조작 모드는 원본 `cFormOption::OnReflectTouchType`과 같은 방식으로 Direct로 전환

```bash
EH_BOT=1 EH_TESTBUFF=1 EH_TESTGOD=1 EH_TURBO=1 EH_SHOT=540 build/epichearts.exe epichearts.apk
```

### 보스 검증 (EH_TESTBUFF)

보스 배치(`EH_WARPSCAN` 조사, 원본 필드 데이터): HynWarrior 75/1·75/2·211/3, Dragon 150/1·410/1, Eamoru 150/2,
Salamander 159/1, Embk 135/2, HynWarriorEvent 380/2 (37/5·37/6은 연출용 개체).

| 보스 | 페이즈 근거 | 결과 |
|---|---|---|
| Eamoru (150/2, 컷신 스크립트 19131이 보스전 시작) | 페이즈 없음 | 5타 (120600 × 5 = maxHp 603000) → 스크립트 19133/19134(승리) |
| Salamander (159/1, `EH_WARPGAUGE`) | hp < 30%에서 state 4 | 3타로 30% 아래(28797), 이어서 2타 |
| HynWarrior 0xa3 (퀘스트 33, 73→75 진입, 이벤트 186) | 70% | 2타로 70% 아래(111998), 이어서 3타 → 퀘스트 33 완료 |
| Embk (퀘스트 79, 310 진입 → 스크립트가 135/2로) | 막대마다 | 무적 동안 하수인(2타씩) 처치 후 막대 268800을 3타 → 퀘스트 79 완료 |
| Dragon (퀘스트 94, 링크스톤 36의 도착 스크립트 19163) | HP 20%마다 페이즈(페이즈 스크립트 0x4ade, 컷신 37/6) | 페이즈마다 2타 × 5 = 10타, 매 페이즈 사이 원본 컷신 → 쓰러뜨린 뒤 엔딩 시퀀스 |

보스 셋업(테스트 전용): `EH_QUESTJUMP=<id>`(앞 퀘스트 완료 처리 후 보스 퀘스트 설정, 원본 `SetDoneEvent`/`SetCompleteQuset`/`AddQuest`),
`EH_SETEVENTS=a,b`(보스 필드 스크립트가 확인하는 이벤트, `EH_CONDTRACE`로 찾음), `EH_WARPTO=g/s` + `EH_WARPENTRY=1`(필드 자신의 입구로 도착)
드래곤의 시작 조건은 정적 분석으로 찾음: 링크스톤 정보(res+0x288, 0x14바이트) 36번 레코드 +4의 상위 16비트가 스크립트 19163
(최종전: 37/4 컷신 → 410/2 → 410/1, 보스 게이지 inst 50). 퀘스트 93의 현자 스크립트 19153이 링크스톤 36 활성화·이벤트 358/353·퀘스트 94를 설정.
`EH_SETLINK=36`(원본 `SetLinkStoneFlag`), `EH_FIRESCRIPT=19163`(원본 `cScriptEngine::PendingScript`).
Eamoru는 `EH_WARPTO=150/2`로는 시작되지 않는다(도착하면 출구 스크립트 19183이 446/1로 보냄). 보스전은 컷신 스크립트
19131이 시작한다: 150/1 → 150/2로 MoveMap, 개체 0x14 활성화, `SetUnblockField`, `SetBossGauge(0x14)`. 끝나면 19133이
19134(승리: `SetDoneEvent` 0x37f, 링크스톤 0x25)를 부른다. 같은 컷신은 `ui dev script 19131`로도 실행된다.
```
EH_BOT=1 EH_TESTBUFF=2 EH_TESTGOD=1 EH_TURBO=1 EH_QUESTJUMP=94 EH_SETEVENTS=358,353 EH_SETLINK=36 EH_FIRESCRIPT=19163 build/epichearts.exe epichearts.apk
EH_BOT=1 EH_TESTBUFF=2 EH_TESTGOD=1 EH_TURBO=1 EH_QUESTJUMP=33 EH_SETEVENTS=186 EH_WARPTO=73/1 EH_WARPENTRY=1 build/epichearts.exe epichearts.apk
EH_BOT=1 EH_TESTBUFF=2 EH_TESTGOD=1 EH_TURBO=1 EH_QUESTJUMP=79 EH_WARPTO=310/1 EH_WARPENTRY=1 build/epichearts.exe epichearts.apk
EH_BOT=1 EH_TESTBUFF=1 EH_TESTGOD=1 EH_TURBO=1 EH_QUESTJUMP=1 EH_FIRESCRIPT=19131 build/epichearts.exe epichearts.apk
```

### 스크립트 디스어셈블러 (`tools/scpdis.py`)

`assets/epic50.png`의 원본 스크립트를 풀어서 보여줌(LZMA 청크, 명령 = 헤더 u32: opcode 0~9비트, 피연산자 수 10~12비트).
opcode 이름은 `tools/scpops.txt`(인터프리터 switch에서 자동 추출). 런타임 추적(`EH_SCPTRACE`, `cflScriptBase::NextCommand` 훅)과 오프셋까지 일치.
```
python tools/scpdis.py <APK에서 꺼낸 assets/epic50.png> dump 19163     # 스크립트 하나
python tools/scpdis.py <APK에서 꺼낸 assets/epic50.png> find c4        # SetBossGauge를 쓰는 스크립트
```

### 원본 대조 테스트 (`tools/compare_run.py`)

같은 세이브와 같은 입력 시나리오(`tests/*.txt`, 512x320 논리 좌표: `wait`/`tap`/`back`/`snap`)를 에뮬레이터의 원본과 이식판에
돌리고, 각 `snap` 지점에서 두 쪽의 원본 소프트웨어 프레임버퍼(512x320 RGB565)를 비교. 포트를 먼저 돌리고, 에뮬레이터는 느리므로
`snap`마다 최대 15초 동안 포트 화면과 가장 가까운 프레임을 고름(동기화 지점).
```
python tools/compare_run.py tests/ui_menu.txt tests/saves/lv14 <출력 폴더>
```
결과(Lv14 세이브): 타이틀(글로우 애니메이션 1.8%), 세이브 목록 0%, 캐릭터·인벤토리·장비·스킬·퀘스트 메뉴 0.14~0.18%(HP 회복 수치와
깜박이는 아이콘만 다름), 마을 메뉴·여관·상점·구매 목록 0%. 필드 화면은 스프라이트 애니메이션 때문에 2~3%.
`tests/ui_title.txt`: 타이틀 옵션 0%, 새 게임 0%, 캐릭터 선택 0.08~0.31%(선택된 캐릭터 애니메이션), 도움말 팝업은 양쪽 동일
(차이 1.8%는 Game Start 글로우). 필드 진입 직후 화면의 차이는 지역 이름 배너(진입 후 일정 시간만 표시)와 가방 아이콘 깜박임의
캡처 시점 차이이며 배치·타일·HUD는 일치.
`tests/ui_town2.txt`: 대장간 메뉴·장비 강화·연금술 메뉴·제조도안(잠김 안내)·몬스터 목장 메뉴 모두 0%, 지역 미니맵(Map)
1.66%(배경 스프라이트 애니메이션 프레임만 다름).

스토리 후반 지점 비교(`tools/mksave.py` + `tests/story_field.txt`): 포트에서 원본 퀘스트 함수로 메인 퀘스트를 옮기고
(`EH_QUESTJUMP`) 그 퀘스트의 목표 필드로 들어간 뒤(`EH_WARPTO`, `EH_SETUPONLY`는 이동 후 봇을 멈춤) 원본 메뉴의 저장하기로
세이브를 만들고, 같은 세이브를 양쪽에 불러 미션 팝업·필드·캐릭터 화면을 비교. Lv14 캐릭터 그대로라 일부 필드에서는 불러온 직후
양쪽 모두 쓰러져 부활 메뉴가 뜸(그 화면도 일치).

| 퀘스트 | 필드 | 미션 팝업 | 필드 | 캐릭터/부활 |
|---|---|---|---|---|
| 20 | 77/1 리노바치오에 | 0.73% | 0.15% | 0.25% |
| 26 | 58/1 (쓰러짐) | 1.04% | 1.19% | 1.19% |
| 32 | 122/1 | 0.15% | 0.45% | 0.13% |
| 44 | 95/1 (96/1 진입 스크립트가 95/1로 옮김, 원본 동작) | 1.49% | 1.28% | 0.13% |
| 56 | 200/1 (쓰러짐) | 1.23% | 0.27% | 0.27% |
| 68 | 251/1 (쓰러짐) | 0.57% | 10.51% | 10.51% |
| 80 | 311/1 | 0.91% | 0.79% | 0.15% |
| 86 | 431/1 | 0.29% | 0.24% | 0% |

차이는 NPC·몬스터 애니메이션과 가방 아이콘 깜박임. 퀘스트 68의 10.5%는 쓰러진 뒤에도 돌아다니는 몬스터들의 위치와 떠다니는
구름(지형·메뉴는 일치). 에뮬레이터가 가끔 첫 탭을 놓쳐서, `compare_run.py`는 탭 직후 스냅샷이 20% 넘게 다르면 그 탭을 한 번
다시 보냄.

`tests/ui_items.txt`(Lv14 세이브의 장비로 대장간·연금술사 아이템 화면): 장비 수리 0.18%, 아이템 선택 후 상세 0.26%, 장비 제련 0%,
장비 복구 0%, 장비 녹이기 0.18%, 장비 스캔 0.18%(선택 테두리 깜박임만 다름).
`tests/play_town.txt`(마을 필드에서 터치 이동 2회, NPC에게 걸어가 대화, 다시 이동): 1.5~2.0%, 대화 장면 0.52%.
카메라 스크롤·NPC 이름 배너·말풍선은 일치하고, 차이는 걷는 중 캡처 순간의 캐릭터 위치(몇 픽셀)와 깜박이는 아이콘.

전투는 몬스터 이동과 피해 굴림이 무작위라 화면 비교가 맞지 않음(에뮬레이터가 느려 같은 스냅샷 시점에 원본 쪽 전투 시간이 몇 배
길어짐). 대신 수치로 비교: `tools/combat_cmp.py <세이브> <출력> [초]`가 같은 세이브(퀘스트 13 목표 필드 34/1 깊은 홀의 동굴, 거미
몬스터; `mksave.py`는 저장 전에 `cCreature::AddHp` @ghidra 000c424c로 HP를 채움)를 양쪽에 불러 40초 동안 캐릭터가 받는 피해를
기록. 포트는 `EH_HPLOG`(cCreature::SubHp @ghidra 000c421c 호출마다), 원본은 에뮬레이터의 ARM 번역 실행 때문에 Frida 대신
프로세스 메모리에서 캐릭터 hp(+0x94)를 폴링해 감소량을 기록.
결과: 포트 16회 {28 20 27 27 28 27 28 27 20 26 20 20 40 21 27 27}, 원본 17회 {27 21 21 27 21 21 27 20 28 28 27 21 21 28 21 27 4}.
두 피해 묶음(20~21, 26~28)이 같음. 포트의 40은 한 번의 SubHp 호출(27의 1.5배 근처, UNVERIFIED: 치명타로 추정), 원본의 4는 폴링
샘플 사이에 자연 회복이 겹친 값.
`--attack`: 양쪽 모두 1.5초마다 가장 가까운 살아 있는 적대 몬스터(목록 1, flags&2, !(flags&8), hp>0, faction 1)의 터치 중심을
터치(Direct 모드 = 공격). 포트는 봇의 `EH_ATTACKNEAREST=<ms>`, 원본은 같은 규칙을 프로세스 메모리로 계산해 adb로 터치하고, 몬스터
hp는 에뮬레이터 안 셸 루프로 폴링. 결과(40초): 캐릭터가 준 피해 포트 152~171(18회, 그중 280 한 번), 원본 164~167(4회, 그중 302
한 번; 호스트 쪽 메모리 읽기와 adb 터치가 느려 공격 시도가 적음). 받은 피해는 양쪽 20~21과 26~28. 280/302는 일반 피해의 약
1.7~1.8배(UNVERIFIED: 치명타로 추정).

### 스모크 테스트 (`tools/smoke.sh`)

진행 봇은 현재 수준(새 게임 → 메인 퀘스트 13)에서 고정. 진행 감시(`EH_WATCHDOG`: 15초 제자리, 한 필드 90초 무진전,
메인 퀘스트 5분 불변이면 이유를 남기고 종료)와 함께 돌려 어디까지 갔는지만 기록하며, 스토리별 상황을 위해 더 확장하지 않음.

```
EH_BOT=1 EH_TESTBUFF=2 EH_TESTGOD=1 EH_TURBO=1 EH_WARPGAUGE=1 EH_WARPTO=159/1 build/epichearts.exe epichearts.apk
```
