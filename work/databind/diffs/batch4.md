# 묶음 4 (smithy, dialogs) — 기준선 대비 차이와 사유

기준선: `golden_b0/`(diffs/batch0.md 0d). 비교: `compare.py golden_b0 <dest> <화면 ...>`.
정규화 후 남은 차이만 적는다. 다른 시나리오의 dump가 대상 화면 폴더에 있어 생기는 MISSING은 세지 않는다.

## 0. 시나리오 보강 (화면 코드를 고치기 전, HEAD 1137335 빌드)

새 시나리오 둘을 지금 코드로 떠서 `golden_b0/`에 같은 이름 규칙으로 더했다(함정 9의 덧붙임: 화면을 고치기 전이라 되돌릴 필요가 없다).

### smithy_more (smithy 28개 + dialogs/enc_nosocket)

- 제련(mode 1, fn2 i1): 제련석 5개(8/0)와 행운 주문서 두 종류 3개씩(8/0x17, 8/0x1a: doScrollAction @001189f8이 넣는 두 종류)을 넣는다. 목록 Down → l0 → add0 → rmsc → add1 → sc1 → add0 → do(팝업 rask) → Left(rno에 focus) → ryes → 진행 → rdone → rok → do → rno → pick0(피커 다시 열기) → 닫기 → 다시 열기.
- 강화(mode 0, fn2 i2): pick0 → Down → l0 → 원작 알림 "소켓이 없거나 이미 강화된 장비입니다"(`dialogs__enc_nosocket`) → b0 → 닫기 → 다시 열기.
  - lv14 저장의 장비(l0, l2, l6, l10, l11, l14, l18을 확인)는 모두 소켓이 없어서 강화석 칸이 열리지 않는다.
  - **장비 칸이 빈 채로 `pick1`을 누르면 게임이 죽는다**: ShowCommonTv @001185d0이 mode 0, 칸 1일 때 칸 0 아이템의 +6(종류)을 읽는다(`*(ushort*)(**(int**)(this+0xb4)+0x14)+6)`, 빈 칸이면 null). 기존 문제로 기록만 하고(아래 "찾은 기존 문제") 시나리오는 pick1을 누르지 않는다. pick1 칸의 마크업은 코드 검토로 대신한다.
- 보석 제거(mode 5): 연금술사(fn3) i2 "강화석 제거"다. 스크립트 211이 op 0x99(cflScript::MakeUIWnd @000e16cc)에 operand 0x50b(id 0xb, mode 5)를 준다(탐색 서브에이전트가 `tools/scpdis.py`로 확인. 마을 기능 버튼 → `FireScript(200+i)`, 203 → 211). 열기(자동 피커, 목록 비어 있음) → pick0 → 닫기 → 다시 열기.
- 다시 열기는 모드마다 fn2/fn3부터 한다(RULES.md 시나리오 함정).
- 제련 결과(원작 판정, DecideRefineResult @000f0054 로컬 경로)는 기준선 두 번 모두 성공("아이템 제련이 성공했습니다.", 방어 30 → 32, HP 10 → 11)이었다.

### dialogs_more (dialogs 9개)

- 링크스톤(0x13): 필드 2/1에서 `ui dev worldmap 1` → `ui key Return`. 원작 cFormWorldMap::OnKeyPress @000fd94c가 키 0x25/0x30에 ShowLinkstoneChoiceWnd @000fd6d8(CreateForm 0x13)을 부른다. 열기 → Down(닫기 버튼) → cancel → Return으로 다시 열기 → Escape 두 번.
- 부활(0x14): `ui dev suicide`. 골드 3,067 ≥ 즉시 부활 값 1,000이라 rgold가 켜지고 focus를 받는다 → Down(off인 i1을 건너뛰고 i2) → rgold(부활, 골드 2,067) → `ui dev gold -2000`(67) → 다시 죽음(rgold off, focus i2) → rgold(거절) → i2(포기).
- 처음 순서(부활 → 포기 → 링크스톤)에서는 포기 뒤 월드맵에서 Return을 누르면 스크립트 말풍선(cTalkBallonWnd::SetBalloonType ← cflScript::MakeTalkBallon)에서 죽었다. 그래서 링크스톤을 먼저 한다. 포기 뒤 상태는 이 묶음의 범위가 아니라 기록만 한다.
- dev 명령은 추가하지 않았다(`worldmap`, `suicide`, `gold`로 충분).

### 같은 빌드로 두 번 (probe/b4s1, probe/b4s2)

| dump | 차이 | 처리 |
|---|---|---|
| dialogs/ls_reopen | 뒤에 깔린 월드맵(autoscreen)의 `img.af-s` 몇 px | 0a의 worldmap과 같은 원인(미끄러져 들어오는 그림을 벽시계 시점에 잡음). `img.af-s`는 월드맵에만 있으므로(golden_b0의 다른 dump에는 없다) 정규식 `(?<=img\.af-s) …`로 좌표를 가린다 |
| smithy/ref_prog | `div.rfill` x 837 ↔ 834 | 진행 막대의 CSS animation(`slide 0.9s infinite`). 정규식 `(?<=div\.rfill) …` |

- 마스크를 넣은 뒤 **차이 0**. (1-1에서 rfill 마스크를 `!OUTSIDE`까지 넓히고 `issues N` 합계 줄도 가렸다.) 두 실행 모두 크래시·TIMEOUT 없음, `[rml]` 0.

### 성능 (묶음 처음, perf/b4_perf.txt)

`run_perf.py before_b4 b4_perf.txt`: 결과는 progress.md 7절.

## 1-1. smithy (data model "smithy")

- 고정 요소: `#kicker`·`#gold`는 `{{ }}`(우리 리터럴, `num()`), `#title`(원작 RML)·`#hint`(glyph)는 data-rml. body 클래스 noslots / refining / rdone / rask는 data-class(rdone·rask는 옛 코드처럼 팝업이 떠 있을 때만 바꾼다).
- `#list`: 행은 `.row`의 data-for(`data-attr-data-act` l<i>, `data-class-sel`, `data-attr-src`, `.nm`·`.ty`는 data-rml, 원작 이름은 `rml_safe`). 빈 목록 표시 둘("대상 아이템이 없습니다", "다른 장비 선택" 버튼)은 템플릿 뒤 `data-if="list_mode == 1/2"`.
- `#slots`·`#det`·`#rbody`: mode마다 구조가 달라 C++에서 안쪽 마크업을 만들어 data-rml로 통째로 넣는다(plan.md 2절). 원작 문자열(장비·주문서·결과 행 이름, 결과 제목, hint)은 넣기 전에 `rml_safe`.
- focus:
  - 단계(fkey: mode / picker / 행 수 / 팝업 단계)가 바뀌면 옛 코드처럼 다시 정한다. 팝업이면 ryes / rok(옛 코드 그대로, `popup_focus()` 유지). 행이 있으면 `l<sel>`: 재사용될 행이 있으면 바로 `find_act`, 없으면 `want_act(l<sel>, #list)`. 행이 없으면 다음 update 맨 앞에서 `ensure()`(`ss.ensure_next`): 옛 코드가 `.none`에 잠깐 둔 뒤 ensure로 잡던 첫 항목(목록의 "다른 장비 선택", 슬롯 pick0, `#det`의 doall/do)이 data-if·data-rml이 반영된 뒤 잡힌다.
  - `#slots`·`#det` 마크업이 바뀔 때 focus가 그 안에 있으면, 그 data-act를 그 상자 범위로 `want_act`한다. `pick0`이 `#list`(빈 목록 버튼)와 `#slots`(장비 칸)에 둘 다 있어서 `Nav::want_act`에 범위 인자(`want_in`)를 더했다(screen_util.h).
  - `opened_for`(원작 폼 하나에 피커 한 번)는 그대로다.
- `bind_actions` 호출과 `GetChild(sel)`(smithy.cpp:305)는 지웠다.

### 비교 (`current_b4sm`: town, smithy_more, town_more)

| dump | golden_b0 | 지금 | 사유 |
|---|---|---|---|
| smithy/smith_i0_l1 | `div.row [l0]`, `*FOCUS` 없음 | `div.row.focus [l0] *FOCUS` | 옛 코드는 `l1`을 누르면(선택 sel 클래스가 바뀌어) 목록 문자열이 달라져 `#list`를 SetInnerRML로 다시 만들었고, focus는 해제됐다(ObserverPtr 뒤 null, 그 전에는 해제된 주소가 화면 밖 l18과 겹침: batch0.md 0b). 지금은 행을 재사용하므로 focus가 있던 l0에 남는다. 세션 지시의 "l1로 돌아온다"와 다른 것은 `ui act`가 focus를 옮기지 않기 때문이다(input.cpp `act`는 ui_action만 부른다). 키보드로 l1에 가서 누르면 focus는 l1에 있다 |

- smith_i2_l1(mode 0, 피커 닫힘)은 golden_b0과 같다(`[pick0]` "다른 장비 선택"에 focus, l1 행이 없어 아무 일도 없음).
- 나머지 smithy 45개, dialogs·town·store·skillbook·farm·map은 **차이 0**. `--raw`로 보면 smithy는 위 한 건과 ref_prog의 진행 막대만 다르다.
- smithy/ref_prog: 첫 실행에서 `div.rfill` (637,536) `!OUTSIDE`(golden_b0 837). 같은 exe로 다시 돌린 `current_b4sm2`는 839였다. CSS animation `slide 0.9s infinite`의 위상이 벽시계 시점에 따라 달라지고, 주기의 약 57%에서는 막대 밖으로 나가 `!OUTSIDE`가 붙는다. 그래서 0의 마스크를 `(?<=div\.rfill) … (?: !OUTSIDE)?`로 넓히고, dump 끝의 `issues N` 합계 줄을 가렸다(`^issues \d+$`). 모든 issue는 자기 요소 줄의 `!` 표시나 `!OVERLAP` 줄로도 찍히므로(screens.cpp dump_ui 573·605행) 합계 줄을 가려도 잃는 것은 없다. `current_b3all`(묶음 3 끝)을 새 마스크로 다시 비교해도 bagbuy 문서 이름 3건만 남는다.

### 로그 `[rml]`

| 시나리오 | 쌍 | 출처 |
|---|---|---|
| town | 4 (smithy) + 25 (store `cat1`) | smithy: smith_i0_l1 뒤 close → fn2 → i1(다시 열 때 지난 열림의 수리 목록 19행 → 제련 목록) |
| smithy_more | 20 | `l0`(피커가 닫혀 19행 → 0행) 10쌍, ref_reopen 5쌍, enc_reopen 5쌍 |
| town_more | 73 | store `cat2`(판매 21 → 4행, 실행마다 다름: golden_b0 0, current_b3all 10) |

- smithy 쌍은 모두 `Data array index out of bounds` + `Could not get value from data variable 'rows[N].act/.sel'`, progress.md 함정 4(행이 줄 때 형제 행의 data-attr·data-class가 data-for보다 먼저 평가됨)와 같은 종류다. 그 행은 같은 Context::Update에서 지워진다. 새 종류는 없다.

### dump에 안 나오는 동작 차이 (검토 서브에이전트)

- **focus가 없는 한 프레임**: (a) 단계가 바뀌고 `l<sel>` 행이 아직 없을 때(처음 열기, 행이 적던 목록 뒤), (b) 행이 없는 단계(피커 닫힘, rok·rno 뒤, mode 2-4의 빈 목록, 열림 첫 프레임), (c) `#slots`·`#det` 마크업이 바뀔 때. 옛 코드는 같은 update()에서 SetInnerRML로 요소를 만들어 바로 잡았다. 최종 대상은 옛 코드와 같다. 그 프레임에 방향키를 누르면 `move()`가 첫 항목을 잡은 뒤 settle이 덮어쓴다(help와 같은 패턴).
- **`#slots`·`#det` 안 focus를 다시 잡는다**: golden_b0 코드는 SetInnerRML 뒤 focus를 잃었다(키보드로 add0을 누르면 주문서 칸이 바뀌며 focus가 사라졌다). 지금은 같은 data-act의 새 요소로 돌아간다. 주문서를 다 써서 `add0`이 `off`가 되면 흐린 버튼에 focus 링이 남고(find_act는 off를 보지 않음), 다음 방향키는 첫 항목으로 간다(valid()가 false). 옛 코드는 focus가 없어서 결과는 같고 링만 새로 보인다.
- **행 내용만 바뀌면 focus가 그 행에 남는다**: smith_i0_l1, 수리 뒤 내구 문구 변경 등. 옛 코드는 잃었다.
- **스크롤**: 옛 코드는 레이아웃 전(크기 0)의 새 행에 ScrollIntoView를 불렀다. 지금은 재사용 행이나 settle(레이아웃 뒤)에서 불러 `l<sel>`이 실제로 보이게 스크롤된다. 시나리오의 sel은 0이라 dump에는 영향이 없다.
- **다시 열기**: 행과 `#slots`·`#det`·`#rbody` 자식이 열림 사이에 남는다(DataViewRml은 같은 문자열이면 SetInnerRML을 건너뛴다, DataViewDefault.cpp:212). 열 때 fkey가 비므로 focus 대상은 같다.

### 새로 도는 transition (theme.rcss `.btn`: background-color border-color transform 0.12s)

- 목록의 "다른 장비 선택" `span.btn[pick0]`: 이제 문서를 불러올 때부터 있는 정적 요소라, 한 프레임 늦게 focus를 받을 때 테두리가 0.12초에 걸쳐 금색이 된다(mode 0/1/5 열기, 피커 선택·닫힘 뒤, rok·rno 뒤, 다시 열기). 옛 코드는 만들고 같은 update에서 focus를 줘서 전환이 없었다.
- `#det`의 `do`/`doall`: 한 프레임 늦게 focus를 받거나(mode 2-4 빈 목록, smith_i3) 다시 열 때 재사용되면 전환이 돈다.
- `#slots`의 add0/add1/rmsc: 마크업이 바뀐 뒤 새 요소에 focus가 돌아올 때 테두리가 페이드인한다.
- 돌지 않는 것: `.row`, `.slot`, `.sc`, `.none`(transition 없음), body 클래스(display·visibility·left만 바뀜), `#racts` 버튼(옛 코드도 정적), `#rbox popin`·`body.on fadein`(정적 요소), `.rfill slide`(`#rbody`가 단계마다 문자열이 달라 매번 새로 만들어짐, 옛 코드와 같다).
- dump는 300ms 이상 기다린 뒤 뜨므로 0.12초 전환은 dump에 나오지 않는다.

## 1-2. dialogs (data model "dialog", dialog.rml)

- 고정 요소: `#kicker`는 `{{ }}`(우리 리터럴). `#title`·`#body`·`#sub`는 data-rml + `data-if="x != ''"`(옛 `d.prop(display)`). `#hint`는 data-rml(glyph).
- `#sub`·`#rows`는 CSS 기본이 `display:none`이었으므로 dialog.rcss에서 그 선언을 지우고 data-if를 쓴다(RULES.md). dialog.rcss를 함께 쓰는 bagbuy.rml에는 두 요소가 없다.
- `#rows`: `.rh`(data-rml `rows_title`) + `.rw`의 data-for(img `data-attr-src`, span data-rml). `#items`: `.it`의 data-for(`data-attr-data-act`, `data-class-off`, 안쪽 번호 span·글·부활 값 span은 C++에서 만들어 data-rml).
- `#btns`: 옛 코드의 역순 생성(b2, b1, b0)을 지키려고 일반 버튼(b2 / b1 / cancel)의 data-for 뒤에 primary b0을 정적 `data-if="primary"` 요소로 둔다. primary는 늘 마지막 버튼이라 순서가 같다. 버튼 하나가 primary ↔ 일반으로 바뀌지 않으므로 `.btn` 색 전환이 없다(batch3.md bagbuy와 같은 이유).
- `#count`: 정적 마크업(`{{count}}`, `{{count_max}}`)과 `data-class-on="is_count"`.
- `state::Dialog`의 `items` + `item_off`(`vector<bool>`)를 `Item{text, off}` 하나로 합쳤다(state.h, capture, input.cpp dump_state 출력 형식은 같다).
- `rebuild`(key 문자열)·`fresh`(ptr) 판정은 그대로다. items·버튼 모델은 rebuild 때만 바꾸고, fresh일 때만 focus를 다시 잡는다. 대상은 옛 코드와 같다: 목록이 있으면 선택 행(부활은 아이템 > 즉시 부활 > 포기 순), 없으면 b0, 그것도 없으면 마지막 버튼. 그 행이 이번 Context::Update에서 바뀌면 `want_act`, 아니면 바로 `find_act`(b0은 정적이라 늘 바로).
- 원작 문자열(제목, 본문, 부제, 보상 행 제목과 글, 항목 글, 버튼 이름)은 `rml_safe`.
- `bind_actions`, `GetChild(sel)`/`GetChild(last)`, `d.prop(display)`는 지웠다.

### 비교 (`current_b4dlg`: 14개 시나리오 전체)

- golden_b0 대비 정규화 차이 4건: bagbuy/open1·open2·bought의 문서 이름 줄(batch3.md 1-1b)과 smithy/smith_i0_l1(1-1). **dialogs는 0건**이다. dialogs `--raw`의 5건은 모두 가려진 값(QBox 보상 골드, HP 회복)이다.
- dialogs dump 24개의 `*FOCUS`가 모두 golden_b0과 같다(i0: 선택지 7개와 링크스톤, b0: 알림·수량 8개, cancel: ls_nav, rgold: res_gold, i2: res_nav·res_nogold·res_nogold_rgold).
- 크래시·TIMEOUT 없음.

### 로그 `[rml]` (current_b4dlg, 쌍 = `Data array index out of bounds` + `Could not get value from data variable`)

| 시나리오 | 쌍 | 출처 |
|---|---|---|
| town | dialog 12, store·smithy `rows` 57 | dialog: 선택지 → 수량 창 0행(`deal`, 9쌍), 선택지 6·4행 → 3행(3쌍). store `cat1`, smithy 다시 열기(1-1) |
| smithy_more | dialog 13 | 선택지(6행) 뒤 강화 알림(0행·버튼 1 → 0) |
| dialogs_more | dialog 2 | 링크스톤(닫기 버튼) 뒤 부활 창(버튼 0개) |
| town_more | 4 | store `cat2` |
| keymap_dev | 10 | keymap `km_pad`(묶음 3과 같음) |
| menu | 3 | detail(golden_b0과 같음) |
| 나머지 8개 | 0 | |

- dialog 모델의 `items[n].act/.off/.rml`, `btns[0].act/.cap`가 새로 나온다. 모두 행 수가 줄 때(progress.md 함정 4) 나는 같은 종류이고, 그 행은 같은 Context::Update에서 지워진다. 화면에는 영향이 없다.

### dump에 안 나오는 동작 차이 (검토 서브에이전트)

- **focus 없는 한 프레임과 그 사이의 확인 키 (고침)**: 항목이 바뀌는 rebuild(처음 열기, 내용이 다른 선택지·링크스톤, off가 바뀐 부활 창)에서는 focus를 다음 프레임에 잡는다. 그 사이에 키가 들어오면(입력 → settle → update 순서) 옛 동작과 달랐다. 방향키는 `move()`가 첫 항목을 잡은 뒤 settle이 덮어써 한 번이 사라지고, **확인 키는 `ensure()` + `activate()`가 첫 행을 실행했다.** 예를 들어 부활 창에서 아이템이 있으면 대상은 `i1`인데 `rgold`가 실행되어 골드가 빠질 수 있었다. help·smithy의 같은 틈도 같다.
  - 그래서 `Nav::settle_input()`을 더했다. `pad_button`·`pad_dir`(키보드도 이 경로) 맨 앞에서, 요청된 요소가 이미 있으면 먼저 잡는다. 입력은 그 요소를 만든 Context::Update 뒤에 처리되므로 있다. 못 찾으면 요청은 다음 settle까지 남는다. smithy의 `ensure_next`(첫 항목) 경우는 입력 시점에 이미 data-if·data-rml이 반영되어 `ensure()`가 올바른 첫 항목을 잡는다.
  - 확인: 키를 쓰는 7개 시나리오(dialogs_more, qbox, smithy_more, town, keymap_dev, help_more, sys)를 이 빌드로 다시 돌렸다(아래 1-3).
- **숨은 b0에 focus**: 직전 대화상자에 primary가 없었으면(선택지 → 알림) fresh 프레임의 b0은 아직 data-if로 숨어 있다. 다음 Context::Update에서 보이고 그 사이 입력은 없다.
- **같은 행 수로 다시 열면**(링크스톤 다시 열기, QBox 알림 다시 열기, 수량 창) 행을 재사용해 바로 잡는다. 옛 코드는 열 때마다(`invalidate`) 다시 만들었다.
- **수량 창**: 수량이 바뀌면 `{{count}}` 글만 바뀐다. 옛 코드는 `#count`를 통째로 다시 만들었다. `.b`의 hover 상태가 유지된다.
- **행 재사용**: 다른 대화상자가 이어지면 인덱스로 행을 재사용한다(예: 선택지 `i0` 행이 부활 창 `rgold` 행이 됨). fresh에서 focus를 먼저 지우므로 바뀐 act의 행에 focus가 남지 않는다.
- 기존 그대로인 것: key에는 항목 수만 있고 글·off는 없다(창이 열린 채 ptr만 바뀌고 key가 같으면 항목이 갱신되지 않음). `ui act c+1`은 form 종류를 보지 않는다.

### 새로 도는 transition

- `.it`(dialog.rcss `background-color border-color 0.1s`): 처음 focus를 받을 때 전환이 돈다(다음 프레임에 `.focus`가 붙거나 재사용 행에 붙음). 옛 코드는 만든 프레임에 `.focus`를 붙여 전환이 없었다.
- 일반 `.btn`(cancel, b1, b2): 항목 없는 선택지·링크스톤에서 첫 focus가 다음 프레임일 때만 테두리가 0.12초 페이드된다(드묾).
- b0(`.btn.primary`): `.focus`가 primary의 border-color를 이기지 못해 box-shadow만 바뀌고, box-shadow는 transition 목록에 없다. 보이는 전환이 없다.
- 없음: `#count .b`, `.v`, `.rw`, `.it.off`(opacity는 transition 밖), `#box popin`(정적 요소).

## 1-3. `Nav::settle_input` 뒤 다시 돌리기 (`current_b4si`)

- 키(`ui key`)를 쓰는 7개 시나리오(dialogs_more, qbox, smithy_more, town, keymap_dev, help_more, sys): golden_b0 대비 정규화 차이는 smithy/smith_i0_l1(1-1) 1건뿐이다. 나머지는 이 실행에 없는 시나리오의 MISSING과 QBox NONDET다. 크래시 0.
- 시나리오의 키는 모두 dump 사이 300ms 이상 뒤에 들어가 want가 이미 settle된 뒤라 dump는 같아야 한다. 결과도 그렇다. 틈 안에 키를 넣는 결정적 시나리오는 만들 수 없어(벽시계 프레임) 코드 검토로 확인했다.
- `[rml]`: dialog `items`/`btns`, store·smithy `rows`의 행 감소 쌍만 있다(쌍 수는 실행마다 다름). 새 종류 없음.
