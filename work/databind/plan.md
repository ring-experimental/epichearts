# RmlUi 데이터 바인딩 이전 계획

작성 2026-10-21. 근거는 `survey/`의 조사 셋(a: menu·title·talkctl, b: dialogs·bagbuy·charselect·smithy·map·store, c: 나머지 12개)과 `golden/` 기준선이다. 공통 규칙은 [RULES.md](RULES.md).

- 대상: `src/ui/*.cpp`에서 Screen을 상속한 21개 화면. 지금은 update()에서 RML 문자열을 만들어 `Doc::rml/cls/prop/attr`로 넣는다.
- 목표: 화면마다 `Rml::DataModel` 하나를 두고, 문서는 data-for / data-if / data-class / data-attr / data-style / data-rml로 그리게 한다.
- 원칙: 동작과 dump는 그대로 둔다.
- 범위 밖: hud, talk(Screen이 아님), autoscreen(제외).

## 1. 되돌리기와 기준선

- git 저장소: 이 저장소 루트(2026-10-21에 init. 공개 저장소는 새 기록으로 시작해서 아래 커밋은 공개 기록에 없다). 기준 커밋은 `e933e2d Baseline before RmlUi data-binding migration`(리팩터 직후 상태).
  - `.gitignore`가 빌드 산출물, ghidra_proj, third_party, apk, save, `work/`(`work/databind` 제외)를 뺀다.
  - `core.autocrlf=false`. 커밋은 화면 하나(또는 기반 단계 하나)마다 한 번 한다.
- 기준선: `golden/<화면>/<단계>.ui.txt|.state.txt`.
  - 시나리오는 `scenarios/*.txt`, 실행기는 `run_golden.py`, 비교는 `compare.py`.
  - 정규화하는 것: 클래스 순서, 1px 이하 좌표, `volatile.txt`에 적은 값.
  - 실행 조건은 RULES.md의 "검증 절차"를 따른다.
- 입력 장치: 모든 시나리오는 `ui dev kbm`(키보드·마우스, focus 클래스가 보임)으로 시작한다.
  - 터치 기준선은 둘이다: `options/touch1`, `menu/touch_tab1`(`ui dev touch`).
  - 터치일 때 focus 클래스는 숨는다(screen_util.h `Nav::set`).
  - 실제 DualSense는 `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0`으로 막는다.

## 2. 화면별 판정

난이도: 하 / 중 / 상. "부분"은 일부 요소만 옮기고 나머지는 지금 코드(Doc)에 남긴다는 뜻이다.

| 화면 | 난이도 | 판정 | 옮기지 않는 부분 · 핵심 위험 | 기준선 |
|---|---|---|---|---|
| detail | 하 | 이전 | – · desc는 data-rml. rows→gems→sec→mats 순서는 data-for 넷으로 지킨다. on_dir 스크롤 | detail/ |
| qbox | 하 | 이전 | – · 75ms 릴 갱신 타이머는 update()에서 변수만 바꾼다. rtitle은 data-rml | qbox/ |
| hotslot | 하~중 | 이전 | – · 2×5 중첩 data-for. focus는 `s<sel>`로 다음 프레임에 찾는다 | hotslot/ |
| town | 하~중 | 이전 | NPC 스프라이트 선택 계산(C++)은 남기고 결과(키·크기)만 바인딩 | town/ |
| bagbuy | 하 | 이전 | 먼저 bagbuy.rml로 분리(4절) | bagbuy/ |
| talkctl | 하~중 | 이전 | 로그 스크롤 3프레임 고정, autofill 타이머. log는 want()에서 바뀌므로 dirty는 update()에서 | talkctl/ |
| title | 중(하에 가까움) | 이전 | `#art` 절대 좌표는 C++ 계산을 유지(data-style로 넣을 수 있음). 카드 3장은 정적 RML. 카드 재생성 때 `focus_cards()`가 일으키는 focus 이동을 명시적으로 재현해야 한다 | title/ |
| options | 중 | 이전 | – · 칸 수가 창 크기에 따라 바뀌는 목록(c_scale). 행 안의 화살표 때문에 capture 단계 위임 리스너가 꼭 필요하다. 이 화면이 그 검증용이다 | options/ |
| help | 중 | 이전 | – · 지속 `topics`(게임 스레드에서 채움). pbody는 data-rml. `SetScrollTop(0)`은 data-rml이 반영된 다음 프레임에 한다 | help/ |
| collection | 중 | 이전 | – · title·desc는 data-rml. 같은 칸을 다시 누르면 등록/해제하는 의미를 유지 | collection/ |
| farm | 중 | 이전 | – · 1초 타이머로 행을 다시 그림. 행 안 조건이 많다 | farm/ |
| store | 중 | 이전 | – · 카테고리 칩 4개는 정적. desc는 data-rml | store/ |
| charselect | 중 | 부분 | `#spr0..2` 라이브 스프라이트와 좌표(빈도 집계)는 제외. 카드 3장은 정적 RML, `.nm`/`.ds`는 data-rml | charselect/ |
| skillbook | 중~상 | 이전 | – · 목록과 쪽 팝업이라는 두 뷰. `.off`가 Nav에 영향 | skillbook/ |
| map | 중 | 부분 | **제외**: `#map` 방·통로 생성(map.cpp:102-137, 박스 크기 피드백 좌표), zoom 라이브 텍스처와 크기(139-167). **이전**: title·kick·zlabel·hint·zoom 클래스·범례 4개 | map/ |
| keymap | 상 | 이전(마지막 묶음) | – · 섹션 머리와 행이 형제로 섞인 순서(평평한 data-for로는 불가). update() 밖 캡처 콜백, 기기 전환 때 행 재사용 | keymap/ |
| smithy | 상 | 이전 | mode마다 구조가 다른 `#slots`·`#det`는 처음엔 inner 문자열을 data-rml로 통째로 넣는다 · mode 7가지와 제련 팝업 3단계 | smithy/ |
| dialogs | 상 | 이전 | – · form 다섯 개와 kind/mode 조합. CSS 기본이 `display:none`인 `#sub`/`#rows`. 버튼 역순. `GetChild(sel/last)` focus. `vector<bool>` | dialogs/ |
| menu | 상 | 이전(페이지 단위로 나눔) | – · 생성 영역 17곳, Pending 탭 표시, cash 목록(snapshot 밖), action()에서 토스트를 직접 고침, qdetail 문자열 파싱, 장치 glyph 7곳 | menu/ |
| helpfull | 상 | **제외** | 레이아웃 결과(GetOffset*)에 기대는 좌표, 게임 스레드 텍스처 키, 기기별 분기. 카드 8행만 옮길 수 있지만 남는 기하 코드에 비해 얻는 것이 적다 | helpfull/(회귀 확인용) |
| autoscreen | 불가 | **제외** | 매 프레임 생성되는 레이아웃, 중첩 clip, 실시간 텍스처, 휴리스틱 | autoscreen/(회귀 확인용) |
| (hud, talk) | – | 범위 밖 | Screen이 아님 | – |

## 3. 배치 순서

각 묶음은 다음 순서로 진행한다: 코드 수정 → build64 빌드 → `run_golden.py --dest current <해당 시나리오>` → `compare.py golden current <화면>` → 차이가 있으면 사유 기록 → git 커밋.

### 묶음 0 · 기반 (화면 코드는 건드리지 않음)

1. **모델 생성 훅**: Screen에 `virtual void make_model(Rml::Context *)`를 둔다.
   - `screens::init()`의 `load_doc` **앞에서** 부른다(screens.cpp:381). data-model은 `Element::SetParent`에서 한 번만 연결되기 때문이다(rmlui Element.cpp:2205).
   - Screen에 `Rml::DataModelHandle model`을 둔다.
   - `screens::update()`에서 `on_show()` 바로 뒤에 `model.DirtyAllVariables()`를 부른다. on_show가 `ss = {}`로 시작하므로 그 뒤가 된다.
2. **공용 타입 등록**: 여러 모델이 같이 쓰는 Struct/Array(문자열 배열, ItemInfo 등)는 한 곳에서 한 번만 등록한다. DataTypeRegister는 context 전체가 공유하므로, 두 번 등록하면 "already declared" 오류가 난다.
3. **위임 클릭 리스너**: 문서마다 **capture 단계** 리스너를 하나 둔다.
   - 동작: 이벤트 target에서 위로 올라가며 처음 만나는 data-act를 `input::ui_action`으로 보내고 `StopPropagation`한다.
   - 이유: data-for 복제본에는 bind_actions 리스너가 없다. bubble 단계로 하면 options의 화살표를 눌렀을 때 행 액션이 먼저 실행된다.
   - `bind_actions`는 아직 이전하지 않은 화면을 위해 남겨 둔다.
   - 확인할 것: 같은 요소에서 두 번 실행되지 않는지. capture에서 멈추면 target 단계 리스너는 돌지 않아야 한다(EventDispatcher.cpp:153-157). 실제로 그런지 묶음 1의 options 검증에서 확인한다.
4. **Nav focus 안전화**:
   - `Nav::focus`를 `Rml::ObserverPtr<Rml::Element>`로 바꾼다.
   - "다음 프레임에 data-act로 focus 찾기" 유틸 `nav.want_act(act)`를 만든다. menu `keep_focus`를 일반화한 것이다.
   - 이유: data-for가 만든 요소는 update() 다음의 `Context::Update`에서 생기므로, 같은 프레임에는 찾을 수 없다.
5. **glyph 변수 공용화**: glyph/glyph_ref 문자열은 장치 상태(`g_dev`, `g_kb_typed`)에 따라 바뀐다. 화면마다 매 프레임 이전 값과 비교해서 바뀌었을 때만 dirty로 표시하는 헬퍼를 둔다.
6. **dialog.rml 분리**(4절).
7. **기존 버그로 생긴 기준선 차이 처리**:
   - 4번을 넣으면 **지금 기준선에 들어 있는 잘못된 focus**가 바뀐다. 이것은 해제된 포인터와 풀 재사용으로 생긴 결과다(survey b 0절, a 위험 1·2). 바뀌는 곳:
     - store `buy1_cat1`: focus가 cat3
     - smithy `smith_i0_l1`: focus가 l18(화면 밖)
     - menu `tab3_skt1`: `*FOCUS` 없음
     - menu 탭들에 `.focus`가 오래 남음(keep_focus가 이전 focus의 클래스를 지우지 않음, menu.cpp:415)
   - 그래서 묶음 0이 끝나면 **차이를 하나씩 이유와 함께 `diffs/batch0.md`에 적고**, 새 기준선 `golden_b0/`을 뜬다. 이후 묶음은 `golden_b0`과 비교한다.
   - 차이가 여기에 적은 곳 말고도 생기면 멈추고 원인을 찾는다.

### 묶음 1 · 시범 (작고 고정된 화면, 기반 검증)

detail → qbox → hotslot → options

- 검증하는 것: data-rml(원작 RML), 타이머 변수, 중첩 data-for, 다음 프레임 focus, capture 위임 리스너, on_dir.

### 묶음 2 · 단순 목록

town → bagbuy → talkctl → title → help

### 묶음 3 · 시설 목록

collection → farm → store → charselect(부분) → skillbook

### 묶음 4 · 특수 구조

map(부분) → keymap

### 묶음 5 · 큰 화면

smithy → dialogs

- dialogs는 다른 시설 화면의 흐름에 모두 끼므로, dialogs를 고친 뒤에는 **전체 시나리오**를 다시 돌린다.

### 묶음 6 · menu (페이지별로 커밋)

**끝남(묶음 5 세션, 2026-10-22)**: 아래 10단계 + 10b(칩 focus 규칙)를 단계마다 커밋했다. 차이와 사유는 [diffs/batch5.md](diffs/batch5.md), 패턴은 progress.md 3절 "묶음 5". 이것으로 이전이 끝났다(helpfull·autoscreen 제외).

1. 정적 id 요소: 탭 sel / 페이지 on, ptitle, gold, dot, confirm, foot, sy_save. glyph는 data-rml.
2. p0 캐릭터
3. p8 sysinfo
4. p4 퀘스트: qdetail 목적지 파싱은 C++에 두고 결과만 넣는다.
5. p6 상점: shop_sel은 지속 멤버로 둔다.
6. p1 가방
7. p2 장비
8. p3 스킬: 두 모드와 rank pip 중첩.
9. picker
10. toast: action()에서 문서를 직접 고치는 대신 ss에 남기고 update()에서 반영한다.

- Pending 탭 표시(`ss.tab_req`)는 모델 값을 계산할 때 그대로 적용한다.
- menu를 마친 뒤 **전체 시나리오**를 다시 돌린다.

## 4. dialog.rml 공유 문제

**현황**
- dialogs.cpp와 bagbuy.cpp가 둘 다 `file()`로 `"dialog.rml"`을 돌려준다.
- `screens::init`이 화면마다 따로 불러오므로 ElementDocument는 두 개다. 각자 Doc 캐시와 Nav를 갖고, z는 50과 52다.

**문제**
- 두 문서에 같은 `data-model="dialog"`가 있으면 **같은 모델**에 붙는다(DataModel의 루트는 set이다). 그러면 bagbuy가 dialog 내용을 그대로 비춘다. 가방이 가득 찬 상태에서 알림이 뜨는 것처럼 둘이 동시에 보이는 경우 내용이 섞인다.
- 파일 안에서는 모델 이름을 문서마다 다르게 줄 방법이 없다.

**해법: 안 A, bagbuy.rml 분리**
1. dialog.rml의 `<style>`을 `data/ui/dialog.rcss`로 옮기고, dialog.rml이 `<link>`로 불러오게 한다. 이것만 하고 dump가 같은지 먼저 확인한다.
2. `data/ui/bagbuy.rml`을 새로 만든다. 같은 rcss를 링크하고, scrim / wrap / box(kicker, title, body, btns, hint)만 둔다. bagbuy가 쓰지 않는 `#sub/#rows/#items/#count`는 원래 숨겨져 있어 dump에 나오지 않으므로 빼도 된다.
3. bagbuy.cpp의 `file()`을 `"bagbuy.rml"`로 바꾸고, `#count/#items`를 숨기는 코드(bagbuy.cpp:42-43)는 지운다.
4. 기대 차이: bagbuy dump의 `== document …/dialog.rml` 줄과 `active screen dialog.rml` 줄이 `bagbuy.rml`로 바뀐다. 이 두 종류의 줄만 달라야 한다. 사유를 `diffs/batch0.md`에 적는다.
5. 그다음 dialogs는 `data-model="dialog"`, bagbuy는 `data-model="bagbuy"`로 각자 옮긴다.

**버린 안**
- B: 로드할 때 문자열을 치환하고 LoadDocumentFromMemory로 불러오기. 꼼수이고, 두 모델이 같은 변수를 모두 bind해야 한다.
- C: C++에서 래퍼 요소를 끼워 넣기. dump 들여쓰기가 바뀐다.
- D: RmlUi template. 부품이 늘어난다.
- E: 모델 하나를 공유하기. 두 문서가 동시에 보일 수 없게 된다.

## 5. 이 세션이 찾은 기존 문제 (이전과 별개, 제품 코드는 고치지 않음)

1. **해제된 Nav focus 포인터(UAF)**: 목록을 SetInnerRML로 다시 만든 뒤 `nav.focus`를 다시 잡지 않는다. 해당 화면: collection, farm(부화 페이지는 1초마다 다시 그림), skillbook, store, smithy, menu의 keep_focus가 없는 목록, title의 begin_export 경로.
   - 매 프레임 screens.cpp:427과 `Nav::set`/`clear`가 이 포인터를 역참조한다.
   - 기준선에도 그 흔적(엉뚱한 focus)이 남아 있다.
   - 묶음 0의 4번(ObserverPtr)으로 고친다.
2. **menu keep_focus가 `.focus` 클래스를 남긴다**: `nav.focus = nullptr; nav.set(e)`(menu.cpp:415) 때문에 이전 focus의 클래스가 지워지지 않는다. 기준선의 탭 줄에 그대로 보인다.
3. **`ui dev uiwnd 27 0`을 타이틀에서 부르면 게임이 죽는다**: c0000005. 테스트 명령을 잘못된 맥락에서 쓴 경우라 제품 경로는 아니다. 시나리오는 이 순서를 피한다.
4. **전직(uiwnd 27)에서 `cs_cancel`을 누르면 타이틀로 돌아간다**: 원작 동작인지 확인하지 못했다(UNVERIFIED).
5. **QBox PvP 선물상자(item 7/4)**: 확인 notice에서 "예"를 눌러도 아무 일도 일어나지 않는다. 용비늘 상자(7/0)는 열린다. 네트워크 판정 때문인지는 확인하지 못했다(UNVERIFIED).
6. **bagbuy.cpp의 `ss.done == 2`(실패 상태)는 설정하는 곳이 없다.**
7. **menu `bag_expand`**: menu가 보일 때만 처리된다(menu.cpp:444). HUD의 `bagbuy`는 언제나 열린다.

## 6. 기준선 상태와 빠진 것

**기준선**
- `golden/`에 dump 144개(ui + state)가 있다. 목록은 [golden/INDEX.md](golden/INDEX.md)에 있다.
- 같은 build64로 한 번 더 뜬 `golden_rerun/`과 비교하면 정규화 후 차이가 0개다.
- 무작위인 것은 QBox 보상 하나뿐이다. 이 보상에 따라 달라지는 dump 7개는 `volatile.txt`의 `@file`에 적어 두었고, NONDET로 표시만 하고 차이로 세지 않는다.
- HP 회복 값과 골드 값은 `volatile.txt`의 정규식으로 가린다.
- 시험 단계에서 처음 발견한 문제(시나리오 실수, 화면이 안 열림)는 모두 고친 뒤 기준선을 떴다. 고친 내용은 RULES.md의 "시나리오 함정"에 있다.

**빠진 것**
- **charselect를 닫았다가 다시 여는 경로**: 위 5절 3·4번 때문에 dev 명령으로는 한 번만 열 수 있다. 새 게임 초반에도 나오지 않는다. 그래서 open1 → Right → cls2만 기준선에 있다.
- ~~dialogs의 부활(0x14)과 링크스톤(0x13)~~: 묶음 4에서 `dialogs_more`로 채웠다(부활은 `ui dev suicide`, 링크스톤은 월드맵에서 Return).
- smithy: 묶음 4에서 `smithy_more`로 제련 팝업 3단계·주문서, 보석 제거(mode 5, 연금술사 i2), 강화 pick0을 채웠다. **남은 것**: 강화석 칸(mode 0 pick1)과 강화 실행. lv14 저장에 소켓 있는 장비가 없고, 장비 칸이 빈 채 pick1을 누르면 원작 ShowCommonTv가 죽는다(progress.md 6절).
