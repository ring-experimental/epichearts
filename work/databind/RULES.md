# RmlUi 데이터 바인딩 이전 — 공통 규칙

대상: `src/ui/*.cpp`의 Screen 화면들을 `Rml::DataModel`(data-model / data-for / data-if / data-class / data-attr / data-rml / data-event)로 옮기는 작업. 계획은 [plan.md](plan.md), 기준선은 `golden_b0/`(묶음 3부터, diffs/batch0.md 0d). 처음 기준선 `golden/`은 기록으로 남긴다.

## 공통 규칙 (세션 지시 그대로)

- 동작과 레이아웃은 바꾸지 않는다. 바꾼 뒤 dump가 기준선과 같아야 하고, 다르면 하나하나 이유를 적는다. 좌표와 클래스 순서처럼 의미 없는 차이는 정규화해서 비교한다.
- data-act 문자열과 Nav 동작(포커스 위치, 방향키 이동, 스크롤)은 유지한다. 테스트의 `ui act`가 그대로 통해야 한다.
- 원본 함수명·주소를 적은 기존 주석은 남긴다. 주변 코드의 스타일과 주석 밀도를 따른다.
- 데이터 모델은 메인 스레드의 update()에서만 고친다. 게임 스레드 쪽(state::post 람다)에서는 모델을 건드리지 않는다.
- 화면 하나당 모델 하나, 이름은 화면 고유. 문서를 불러오기 전에 만든다.
- on_show의 `ss = {}` 뒤에는 모델 전체를 dirty로 표시한다(DirtyAllVariables).
- 원작 RML 텍스트는 {{ }}가 아니라 data-rml로 넣는다(태그가 이스케이프되면 안 됨).
- 한국어가 들어간 C++은 Edit/Write 도구나 Write로 만든 파이썬 스크립트로만 쓴다. Git Bash heredoc 금지.
- 코드를 고치는 것은 그 세션의 메인 에이전트 하나만 한다. 서브에이전트는 읽기, 테스트 실행, 결과 분석만 한다. 빌드하는 동안에는 테스트 실행을 하지 않는다(exe 잠김).

## 검증 절차

1. 빌드: PowerShell로 저장소 루트에서 `cmd /c build64.bat`.
   - Git Bash의 `cmd //c`로는 아무것도 되지 않는다.
   - build64.bat은 실행 중인 epichearts.exe를 죽인다. 빌드하는 동안에는 테스트를 돌리지 않는다.
2. 실행: `python work/databind/run_golden.py --dest current [시나리오 ...]`.
   - build64, `EH_WINSIZE=2280x1080`, `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0`으로 돈다. 이 PC에 연결된 DualSense가 테스트 창을 움직이지 못하게 하려는 것이다.
   - 시나리오는 한 번에 하나씩 차례로 돈다. exe를 두 개 동시에 띄우지 않는다.
   - 시나리오 이름은 `scenarios/*.txt` 파일 이름이다: menu, qbox, sys, title, town, misc, newgame, 그리고 추가 시나리오 clicks, keymap_dev, help_more, town_more, title_more, smithy_more, dialogs_more, menu_more. 이름 없이 돌리면 15개를 모두 돈다.
   - 화면과 시나리오의 대응은 아래 표와 같다.
3. 비교: `python work/databind/compare.py work/databind/golden_b0 work/databind/current [화면 ...]`. golden_b0에는 14개 시나리오의 dump가 모두 있으므로(smithy_more·dialogs_more는 묶음 4 첫머리에 추가), 일부 시나리오만 돌렸으면 다른 시나리오의 dump가 MISSING으로 나온다(세지 않는다). menu_more(묶음 5)의 기준선은 `golden_menux/`(옮기기 전 menu 코드로 뜸)다: `compare.py work/databind/golden_menux <dest>`.
   - 기본으로 정규화하는 것: 클래스 순서, 1px 이하 좌표 차이, `volatile.txt`의 값.
   - `--geom ignore`는 좌표를 아예 무시한다. 원인을 좁힐 때만 쓴다.
   - `--raw`는 정규화 없이 그대로 비교한다.
4. 차이가 나면 `diffs/<묶음>.md`에 줄마다 사유를 적는다. 사유를 적지 못하는 차이가 있으면 다음 화면으로 넘어가지 않는다.
5. 커밋: 화면 하나를 끝낼 때마다 git 커밋한다. 메시지에 화면 이름과 비교 결과(차이 수와 사유 문서)를 넣는다.

| 화면 | 시나리오 | 화면 | 시나리오 |
|---|---|---|---|
| menu, detail, hotslot, bagbuy | menu | options, keymap, help | sys (options는 title에도 있음) |
| qbox, dialogs(notice) | qbox | title | title |
| town, dialogs(choice·count), skillbook, store, smithy, farm, map | town | collection, skillbook, helpfull, autoscreen, talkctl(crawl), charselect | misc |
| talkctl(대사), helpfull | newgame | farm(먹이·부화), skillbook(쪽 팝업), store(판매) | town_more |
| title(내보내기·가져오기 선택 모드, dump 폴더 titlex) | title_more | help(더 많은 경로, helpx) · options 클릭(optclick) · keymap 장치(kmdev) | help_more · clicks · keymap_dev |
| smithy(제련 팝업·주문서, 강화 pick0, 보석 제거 mode 5, 다시 열기), dialogs(강화 알림) | smithy_more | dialogs(부활 0x14, 링크스톤 0x13) | dialogs_more |
| menu(생활 스킬 탭, 가방 칩, 단축키·저장 토스트, 등록 뒤 피커, 터치 스킬 페이지; 기준선 golden_menux) | menu_more | | |

- 덤프 이름 규칙: `<화면>__<단계>`. run_golden.py가 `<dest>/<화면>/<단계>.ui.txt` 경로로 나눠 담는다.
- 덤프에 `{scroll top/range}`가 나오는 요소는 스크롤되는 상자다.
- `!OVERLAP`은 클리핑 상자 밖으로 스크롤된 텍스트에서 거짓 양성일 수 있다. 원인을 판단할 때 이 점을 감안한다.

## 조사하며 알게 된 점

자세한 내용은 `survey/a_*.md`, `survey/b_*.md`, `survey/c_*.md`에 있다. 화면 코드를 고칠 때 지켜야 할 것만 여기 모았다.

### 모델과 시점

- **모델은 `Context::LoadDocument` 전에 만든다.**
  - `data-model`은 요소가 부모에 붙을 때(`Element::SetParent`, rmlui Element.cpp:2205) 한 번만 연결된다. 모델이 없으면 그 뒤로 영영 붙지 않는다.
  - 생성 위치는 `screens::init()`의 `load_doc` 앞이다(plan.md 묶음 0).
- **같은 이름의 모델을 두 번 만들 수 없다.** 같은 Struct/Array 타입을 두 번 등록할 수도 없다. 타입 등록은 context 전체가 공유하기 때문이다. 공용 타입은 한 곳에서 한 번만 등록한다.
- **data-for가 만든 요소는 update()가 끝난 뒤에 생긴다.** 순서가 `screens::update()` → `context->Update()`(ui.cpp:305-306)이기 때문이다. 같은 update() 안에서 새 요소를 찾지 않는다. focus는 다음 프레임에 data-act로 찾는다(`nav.want_act`).
- data-for는 같은 인덱스의 행을 **재사용**한다. 행 수가 줄어들 때만 뒤쪽 행이 지워진다.
  - 그래서 "다시 만들어서 생기던 부수 효과"가 사라진다. 예: keep_focus가 focus를 옮기던 것, title이 카드를 다시 만들 때 `focus_cards()`가 불리던 것.
  - 이런 효과가 필요한 곳은 명시적으로 다시 호출한다.
- data-for 템플릿은 `display:none`으로 **원래 자리**에 남고, 복제본은 그 바로 앞에 들어간다(DataViewFor::Update `InsertBefore`). 템플릿 뒤에 형제를 두면 마지막 자식이 아니다(progress.md 함정 2).
  - `GetChild(i)`/`GetNumChildren()`/`GetChild(last)`로 행을 고르는 코드는 data-act로 찾는 방식으로 바꾼다. dialogs·bagbuy·smithy·title·menu는 옮기며 바꿨다(남은 곳 없음).
  - dump와 Nav는 보이지 않는 요소를 건너뛰므로 템플릿이 남아 있어도 dump에는 나오지 않는다.
- 시나리오가 도는 동안 `data/ui/*.rml`을 고치지 않는다. rml은 실행할 때 읽힌다(progress.md 함정 1).
- 게임 스레드나 입력 콜백에서 바뀌는 값은 `ss`나 멤버에만 남기고, 모델 반영은 update()에서 한다. 대상:
  - talkctl `log`: want()에서 바뀐다.
  - keymap `begin_capture`
  - title `saveio::poll` 콜백
  - menu toast: action()에서 문서를 직접 고쳤다 → `ss.toast`만 남기고 update()에서 put(묶음 5).

### 클릭과 Nav

- 클릭은 **문서 단위 capture 단계 위임 리스너**로 받는다. target에서 위로 올라가며 처음 만나는 data-act 하나만 실행한다.
  - data-for 복제본에는 요소별 리스너가 없다(옛 `bind_actions`는 묶음 5 정리에서 지웠다).
  - bubble 단계로 하면 options의 화살표(`o_bgm-`) 클릭이 부모 행(`o_bgm`) 액션이 된다.
- **한 요소에 data-act와 data-event-click을 같이 두지 않는다.** 액션이 두 번 실행된다.
- data-act는 `data-attr-data-act="'it' + it_index"`처럼 데이터로 만든다. RmlUi는 `data-act`, `data-nonav`, `data-block`을 바인딩으로 해석하지 않으므로 그냥 속성으로 남는다.
- `focus` 클래스는 Nav와 screens.cpp:427만 붙인다. data-class로 묶지 않는다.
- `off` 클래스는 Nav 후보에서 빠진다(screen_util.h:70). data-class-off로 옮겨도 의미는 그대로다.
- Nav focus는 `Rml::ObserverPtr`다(57ab990). 목록이 SetInnerRML로 다시 만들어지면 null이 된다(예전에는 해제된 주소가 새 요소와 겹쳤다). 아직 SetInnerRML을 쓰는 화면을 옮길 때 focus를 다시 잡는 곳을 확인한다. data-for 행이 바뀔 때는 `nav.drop_if_gone`(progress.md 2.5).

### 텍스트

- **지금 `Doc::rml`로 들어가는 문자열은 출처와 상관없이 data-rml로 옮긴다.**
  - `g::plain_text`는 `<`와 `&`를 이스케이프하지 않는다. 그래서 지금은 RML로 해석되고 있다. `{{ }}`로 바꾸면 화면 표시와 dump가 달라진다.
  - 원작 RML(`rml_text`/`element_text`, 색 span과 `<br/>`)은 당연히 data-rml로 넣는다.
- `{{ }}`는 C++에서 만든 숫자나 순수 텍스트, 그리고 우리가 정한 리터럴에만 쓴다.
  - `num()`(쉼표), `%02d`, `%d / %d`, `%.2f×`, `h:mm:ss` 같은 포맷 문자열은 C++에서 미리 만들어 넘긴다.
  - lucide 엔티티(`&#xe13d;` 등)는 `{{ }}` 안에서 디코드되지 않는다. 템플릿에 리터럴로 둔다.
- data-rml로 넣은 내용도 모델에 연결된다. 원작 문자열에 `{{`가 있으면 데이터 식으로 해석된다.
  - rml_text는 `{`를 이스케이프하지 않는다.
  - data-rml로 넣는 원작 문자열은 `{`를 `&#123;`로 바꾸는 헬퍼를 거친다.
  - 헬퍼를 넣을 때 dump가 같은지 확인한다. dump는 GetInnerRML을 plain으로 바꿔 쓴다.
- glyph/glyph_ref는 `<span class="glyph …">` 마크업이므로 data-rml로 넣는다. 장치 상태가 바뀌면 값도 바뀌므로 매 프레임 비교한 뒤 dirty로 표시한다.

### 레이아웃과 dump

- **템플릿은 한 줄로 붙여 쓴다.** 인라인 형제 사이에 공백이나 줄바꿈이 있으면 공백 텍스트가 생겨 x 좌표가 바뀐다. 지금 C++가 만드는 마크업에는 형제 사이 공백이 없다.
  - 해당 요소: `.seg/.arrow/.choice`, keymap `.c`, `.tp`, `.cl/.bk`, `.sl`, `.fn`, `.reel`, `span.btn`, `span.c`.
- **`data-if`는 인라인 `display`를 지우는 방식이다.** CSS 기본값이 `display:none`인 요소에 쓰면 영영 보이지 않는다. 대상: dialog.rml의 `#sub`, `#rows`. 이런 요소는 data-style-display를 쓰거나 CSS를 먼저 정리한다.
- 속성(prop/attr)마다 주인을 하나로 정한다. `Doc::prop/attr` 캐시와 data-style/data-attr이 같은 속성을 같이 쓰면 서로 덮어쓴다.
  - 지금 `d.prop(..,"display",..)`를 쓰는 곳은 data-if로 통째로 바꾼다. 해당 위치: help.cpp:81, detail.cpp:154, helpfull.cpp:72-73, options.cpp:78, dialogs #title/#sub/#body/#rows, charselect #b_cancel, map 범례.
- dump는 **자식이 텍스트뿐인 요소**만 텍스트를 찍는다. data-if로 숨긴 자식도 이 판정에 들어간다. 그래서 텍스트가 있는 요소 안에 data-if 자식을 두면 그 줄의 텍스트가 dump에서 사라진다.
- 빈 목록 표시(`.empty`, `.none`, `#logempty`)는 지금과 같은 DOM 위치에 둔다. data-for 행은 템플릿 앞에 들어간다. 이 표시를 형제로 둘 때 순서가 바뀌지 않는지 확인한다.
- 생성 id(`fn%d`, `npc%d`, `cc%d`, `spr%d`, `r0..r2`, `r0img..`)와 dialogs 버튼의 역순 생성(b2, b1, b0)은 지금과 같게 유지한다.

### 기준선에 이미 들어 있는 기존 버그 흔적

이전하면 이 줄들은 **정상 쪽으로 바뀐다.** golden_b0(Nav focus ObserverPtr 뒤)에서는 store 줄이 이미 고쳐졌고 smithy 줄은 "focus 없음"으로 바뀌었고, 묶음 4에서 smithy를 옮긴 뒤 focus가 l0에 남는다(diffs/batch4.md 1-1). menu 두 줄은 golden_b0에도 그대로 있고, 묶음 5에서 정상 쪽으로 바뀌었다(diffs/batch5.md 5-8: 낡은 `.focus` 제거, tab3_skt1 `*FOCUS`가 skt1 칩). 바뀌면 `diffs/`에 사유를 적는다. 근거는 plan.md 5절에 있다.

| 기준선 | 지금 모습 | 원인 |
|---|---|---|
| store/buy1_cat1 | `cat1`을 누른 뒤 focus가 cat3에 있다 | 목록을 다시 만든 뒤 해제된 focus가 풀에서 재할당된 요소와 일치 |
| smithy/smith_i0_l1 | `l1`을 누른 뒤 focus가 화면 밖 `l18` | 같음 |
| menu/tab3_skt1 | `*FOCUS` 없음 | keep_focus가 없는 영역을 다시 만듦 |
| menu 여러 dump | 탭 줄에 `.focus`가 여러 개 남는다 | keep_focus의 `nav.focus = nullptr; nav.set(e)`(menu.cpp:415) |
| collection, farm(부화 페이지), skillbook | 선택을 그대로 둔 채 다시 만들면 focus가 사라진다 | 해제된 포인터 |

### 시나리오 함정 (기준선을 뜨며 알게 된 것)

- **hotslot**: `s3`처럼 슬롯을 고르면 팝업이 저절로 닫힌다. 그 뒤에 `close`를 보내면 menu가 닫힌다.
- **bagbuy**: menu 안의 `bag_expand`는 menu가 보일 때만 처리된다. 기준선은 HUD의 `ui act bagbuy`로 열었다.
- **map**: 마을 집 안(필드 32/1)에서는 열리지 않는다. `ui dev warp 2 1`로 필드로 간 뒤 HUD `map`으로 연다.
- **smithy**: 화면을 닫으면 선택지가 아니라 마을로 돌아간다. 모드마다 `fn2`부터 다시 연다.
- **store**: 이미 선택된 행을 다시 누르면(`r%d`) 바로 거래가 시작된다. `deal`을 함께 보내면 수량 창이 두 개 뜬다.
- **QBox**: 용비늘 상자(7/0)는 menu 상점 sh6(용비늘)을 사야 열린다. PvP 상자(7/4)는 "예"를 눌러도 아무 일도 없다.
- **charselect**: dev 경로(`uiwnd 27`)에서 `cs_cancel`을 누르면 타이틀로 간다. 타이틀에서 `uiwnd 27`을 보내면 게임이 죽는다. 그래서 한 번만 열고 닫지 않는다. 새 게임 초반에는 나오지 않는다.
- **newgame**: tskip을 누르면 튜토리얼 그림(0x33)에서 멈춘다. `next`는 그 그림을 닫고, `advance`는 대사를 넘긴다.
- **smithy 강화(mode 0)**: lv14 저장의 장비에는 소켓이 없어 `l0`을 고르면 "소켓이 없거나 이미 강화된 장비" 알림이 뜨고 장비 칸은 빈 채로 남는다. 장비 칸이 빈 채로 `pick1`(강화석)을 누르면 원작 ShowCommonTv @001185d0가 칸 0의 아이템을 읽다가 **게임이 죽는다**(c0000005). 시나리오는 pick1을 누르지 않는다.
- **smithy 보석 제거(mode 5)**: 연금술사(fn3)의 i2 "강화석 제거"(스크립트 211, op 0x99 operand 0x50b)에서 열린다.
- **smithy 제련 결과**: 원작의 판정이다. 기준선 두 번 모두 성공이었다. 달라지면 volatile.txt에 @file로 넣는다.
- **링크스톤(0x13)**: 필드에서 `ui dev worldmap 1` 뒤 `ui key Return`(cFormWorldMap::OnKeyPress @000fd94c, 0x30 → ShowLinkstoneChoiceWnd). 부활 창에서 i2(포기)를 고른 뒤에 열면 스크립트 말풍선(cTalkBallonWnd::SetBalloonType)에서 게임이 죽는다. 그래서 dialogs_more는 링크스톤을 먼저 한다.
- **부활(0x14)**: 마을에서 죽으면 스크립트가 밖으로 데려가므로 필드(`ui dev warp 2 1`)에서 `ui dev suicide`. 즉시 부활 값(1,000 G)보다 골드를 적게 하려면 `ui dev gold -2000`(AddGold @0012facc는 음수를 받는다).
- **`ui act`는 focus를 옮기지 않는다.** 행을 재사용하는 화면에서는 시나리오의 focus가 처음 자리에 남는다(progress.md 함정 15).
- 메뉴가 가끔 안 열리면(타이밍) `ui act menu` 앞에 `wait 1500`을 둔다. 시나리오는 이미 그렇게 되어 있다.
