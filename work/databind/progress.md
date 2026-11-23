# 데이터 바인딩 이전 진행 기록

계획은 [plan.md](plan.md), 공통 규칙은 [RULES.md](RULES.md). 이 문서는 실제로 옮기면서 정한 패턴, 부딪힌 함정, 다음 배치에 그대로 쓸 템플릿을 모은다.

## 1. 진행 상황

| 커밋 | 내용 | 검증 |
|---|---|---|
| 711e50f | 준비물(work/databind) 커밋 | – |
| 6931ba4 | 기반: `Screen::make_model` 훅, `model` 핸들, `put()` | 동작 변경 없음(모델 없는 화면) |
| d7eeb67 | 기반: 문서 단위 capture 클릭 위임, `bind_actions` 무동작, `EH_UIPERF` | 7개 시나리오 golden 대비 타이밍 차이만, clicks 17단계 모두 기대값 |
| 640e1ab | options | sys·title golden 대비 0, clicks 0(이전 빌드 결과 대비), [rml] 0 |
| 7bb5803 | keymap, `Nav::want_act/settle/find_act`, `rml_safe` | sys golden 대비 0, keymap_dev 기대값, [rml] 경고 1(아래 함정 4) |
| 0795830 | help | sys·title golden 대비 0, help_more(새 시나리오) golden_helpx(옛 코드로 뜬 기준선) 대비 바이트 단위로 같음, [rml] 0 |

| ded3f64 | 시나리오 town_more + 옛 코드 기준선 `golden_townx` | 농장 먹이·부화, 스페셜북 쪽 팝업, 상점 판매 |
| ca0ff94 | collection | misc golden 대비 0 |
| 014969c | farm | town golden 대비 0, town_more 0 |
| d9f0d05 | skillbook | misc·town golden 대비 0, town_more 0 |
| 0ee1ffb | store, [diffs/batch2.md](diffs/batch2.md) | town 4건·town_more 1건, 모두 기준선의 해제 focus 별칭(cat3 → cat0) |
| 9ed505f | town | town golden 대비 0(바이트 단위) |
| d8049d7 | detail | menu golden 대비 0 |
| ef44e16 | hotslot | menu golden 대비 0 |
| db3f746 | qbox | qbox golden 대비 0(보상 NONDET 제외) |
| 973b5c9 | 묶음 0a: `@file autoscreen/worldmap[0-9].*` NONDET | 기준선 여섯 번은 같았고 묶음 2 중 실행이 갈렸다(타이밍), [diffs/batch0.md](diffs/batch0.md) |
| 57ab990 | 묶음 0b: `Nav::focus`를 `Rml::ObserverPtr<Rml::Element>`로 | – |
| d3cc14f | 묶음 0c: 전체 시나리오 비교, 마스크(`#autofill` 폭, `img#spr` 좌표) | golden 대비 store cat3→cat0 4건, smithy smith_i0_l1 focus 없음, town_more sell_cat2. 모두 RULES.md 표의 해제 focus 흔적 |
| ff459cc | 묶음 0d: 새 기준선 `golden_b0/`(12개 시나리오, 새 `title_more` 포함) | 같은 빌드 독립 실행 대비 0 |
| 5da2052 | 묶음 0e: `Nav::drop_if_gone` | 호출 없음(동작 변경 없음) |
| f1698fc | dialog.rml `<style>` → dialog.rcss | menu·qbox·town golden_b0 대비 0 |
| 2644ed6 | bagbuy (bagbuy.rml로 분리) | menu: 문서 이름 두 줄만(예상), [diffs/batch3.md](diffs/batch3.md) |
| 72b9dd4, aca00d2 | talkctl, 이름 rml_safe | newgame·misc 0(talkctl dump 바이트 단위로 같음) |
| ea12f76 | title | title·title_more·newgame 0(바이트 단위로 같음) |
| a014c6a | charselect(부분, 스프라이트 제외) | misc 0 |
| 1dfb96f | map(부분, 방·통로·zoom 캔버스 제외) | town 0 |
| d201652 | 묶음 4 0단계: 시나리오 smithy_more·dialogs_more, golden_b0에 추가, 마스크(월드맵 `img.af-s`, 제련 막대) | 같은 빌드 두 번 0, [diffs/batch4.md](diffs/batch4.md) 0 |
| ff05736 | smithy, `Nav::want_act(act, in)` 범위 | town·smithy_more·town_more: smith_i0_l1 focus가 l0에 남음(예상), 마스크 `rfill !OUTSIDE`·`issues N` |
| e02fc01 | dialogs, `Dialog::Item`, `Nav::settle_input` | 14개 전체: dialogs 0, 전체는 알려진 bagbuy 3건 + smith_i0_l1 |
| 51bdbf0 | (다른 세션) smithy mode 0 pick1, 장비 칸이 비면 원작 알림(크래시 고침) | – |
| 09edfb4 | menu 1: 모델 "menu", 정적 요소(탭 sel/페이지 on, ptitle, gold, glyph, touch, dot, confirm, foot, sy_save) | menu·qbox·town_more: menu 0, [diffs/batch5.md](diffs/batch5.md) |
| ab5ef8d | menu 2: p0 캐릭터(xp 막대 data-style-width, 고정 `#sec` 칸) | menu 0 |
| ac03aa4 | menu 3: p8 `#sysinfo` data-rml | menu 0 |
| ee4856d | menu 4: p4 퀘스트(행 data-for, 고정 `#qsum`, `#qdetail` data-rml) | menu 0 |
| 34e75b3 | menu 5: p6 상점, `rows()`/`refocus()`(keep_focus 대체), `inner()` | 레일의 낡은 `.focus`(tab6) 제거만, `*FOCUS` 모두 같음 |
| e855d2e | menu 6: p1 가방, 칩 줄, 열림마다 `.ds` 스크롤, `switch_tab`이 focus_act를 비움 | + tab1 낡은 `.focus`, it30 격자 스크롤(사유 6) |
| 44300a8 | menu 7: p2 장비, `inner()`가 방금 누른 버튼은 다시 잡지 않음 | + tab2 낡은 `.focus` |
| ed0bcfb | menu 8: p3 스킬(스킬·생활 행 data-for) | + tab3 낡은 `.focus`, **tab3_skt1 `*FOCUS` 복구**(skt1 칩) |
| c1f680e | menu 9: 단축키 피커(중첩 data-for) | 8단계 대비 0 |
| 10d57bb | menu 10: 토스트, 시나리오 menu_more + 옛 코드 기준선 `golden_menux` | 9단계 대비 0, menu_more 칩 `*FOCUS` 4건(→ 10b) |
| 1299871 | menu 10b: 칩 focus는 옛 코드가 잃던 경우에만 누른 칩으로 | menu_more golden_menux 대비 0 |
| e966cec | 정리: menu `keep_focus`·`subhint`, 무동작 `bind_actions`(HUD의 마지막 호출 포함), 멤버를 가리던 지역 이름 | 15개 전체(아래 "이전 완료") |

plan.md의 순서(묶음 1 = detail → qbox → hotslot → options)와 달리, 사용자가 시범으로 options → keymap → help를 골랐다.
묶음 2(둘째 세션)는 collection, farm, skillbook, store, town, detail, hotslot, qbox를 옮겼다. 화면 cpp를 앞서 고쳐 두는 바람에 빌드는 여러 화면을 한 번에 했고, 검증·커밋은 화면별로 나눴다.
묶음 3(셋째 세션)은 먼저 plan.md 묶음 0의 남은 4번(Nav focus ObserverPtr)과 7번(golden_b0)을 하고, bagbuy, talkctl, title, charselect(부분), map(부분)을 화면 하나씩 "빌드 → 시나리오 → golden_b0 비교 → 사유 → 커밋" 순서로 옮겼다. 이후 비교 기준은 `golden_b0/`이다(golden, golden_townx, golden_helpx의 역할을 모두 대신한다).
묶음 4(넷째 세션)는 먼저 smithy·dialogs가 지나는데 기준선에 없던 경로를 시나리오 둘(smithy_more, dialogs_more)로 떠서 golden_b0에 더하고, smithy → dialogs를 같은 순서로 옮겼다. dialogs 뒤에 14개 시나리오를 모두 돌렸다.
묶음 5(다섯째 세션)는 menu를 페이지 단위 10단계(+10b)로 옮겼다. 단계마다 "빌드 → menu·qbox·town_more → golden_b0 비교 → 사유 → 커밋"이다. menu 시나리오가 지나지 않는 경로는 새 시나리오 `menu_more`로 덮었고, 기준선은 옮기기 전 코드로 떴다(`golden_menux`). 끝에 쓰지 않게 된 코드를 정리하고 15개 시나리오를 모두 돌렸다.

### 이전 완료 (2026-10-22)

| 화면 | 모델 이름 | 판정 |
|---|---|---|
| options, keymap, help | `options`, `keymap`, `help` | 이전 |
| collection, farm, skillbook, store, town | `collection`, `farm`, `skillbook`, `store`, `town` | 이전(skillbook `pprev/pnext/pinv`의 `off`는 같은 프레임 판정이라 Doc) |
| detail, hotslot, qbox | `detail`, `hotslot`, `qbox` | 이전 |
| bagbuy, talkctl, title | `bagbuy`, `talkctl`, `title` | 이전(title `#art` 좌표·그림은 C++ 계산을 Doc으로) |
| charselect | `charsel` | 부분: `#spr0..2` 라이브 스프라이트와 좌표는 Doc |
| map | `map` | 부분: `#map` 방·통로 생성(박스 크기 피드백)과 zoom 라이브 텍스처는 Doc |
| smithy, dialogs | `smithy`, `dialog` | 이전 |
| menu | `menu` | 이전(10단계) |
| helpfull | – | **제외**: 레이아웃 결과(GetOffset*)에 기대는 좌표, 게임 스레드 텍스처 키, 기기별 분기. 카드 8행만 옮길 수 있는데 남는 기하 코드에 비해 얻는 것이 적다(plan.md 2절) |
| autoscreen | – | **제외**: 매 프레임 생성되는 레이아웃, 중첩 clip, 실시간 텍스처, 휴리스틱 |
| (hud, talk) | – | 범위 밖(Screen이 아님). `Doc::rml/cls/prop/attr`는 이들과 제외·부분 화면이 쓰므로 남긴다 |

- 최종 검증(정리 뒤 빌드, `current_final`, 15개 시나리오)은 diffs/batch5.md 11에 있다. golden_b0 대비 남은 차이는 모두 사유가 있다: bagbuy 문서 이름 3, smith_i0_l1, 그리고 menu의 낡은 `.focus` 제거·tab3_skt1 `*FOCUS`·it30 격자 스크롤. menu_more는 golden_menux 대비 0.

## 2. 기반 (screen_util.h, screens.cpp)

### 2.1 모델 훅과 dirty

```cpp
struct Screen {
    Doc d;
    Rml::DataModelHandle model;               // make_model에서 채운다
    virtual void make_model(Rml::Context *ctx) {}  // screens::init이 load_doc 전에 부른다
    template <class T, class V> bool put(T &var, V &&v, const char *name); // 값이 바뀌었을 때만 DirtyVariable
};
```

- screens::update가 on_show 바로 뒤에 `model.DirtyAllVariables()`를 부른다.
- `put()`은 모델이 없으면 dirty를 부르지 않는다(빈 핸들의 DirtyVariable은 null 역참조).
- 화면 값은 Session(`ss`) 밖의 `struct View {...} m;`에 둔다. 모델은 열 때마다 새로 만들지 않고, 열려 있지 않을 때도 값과 요소가 남는다. 열 때 DirtyAllVariables로 다시 그린다.

### 2.2 클릭 위임 (d7eeb67)

- `load_doc`이 문서마다 capture 단계 `ActionListener`를 하나 단다. target에서 문서까지 올라가며 처음 만나는 data-act 하나를 보내고 `StopPropagation`.
- capture에서 멈추면 target/bubble 리스너와 default action이 모두 돌지 않는다(EventDispatcher::DispatchEvent).
- `bind_actions()`는 아무것도 하지 않게 됐고, 화면을 옮기며 호출을 지웠다. 함수 자체는 묶음 5 정리(e966cec)에서 지웠다(마지막 호출은 HUD의 GP/SP 칩, HUD 문서도 같은 리스너를 받는다).
- 검토 결과: 이전에 바인딩되지 않은 data-act 요소는 없었다(모든 생성 문자열 뒤에 bind_actions가 있었다). 그래서 동작이 바뀌는 곳이 없다.
- 이후 규칙: data-act 요소나 그 자손에 data-event-click이나 다른 Click 리스너를 두지 않는다. capture에서 멈추므로 실행되지 않는다.

### 2.3 Nav 보조 (7bb5803)

- `nav.find_act(act)`: root 아래에서 data-act가 같은 요소(보이는지 여부와 상관없이, DOM 순서 첫 번째).
- `nav.want_act(act)`: 다음 프레임에 data-act로 focus를 잡는다. screens::update가 그 화면의 update() 직전에 `nav.settle()`을 부른다. 숨길 때 want를 지운다.
  - update() 안에서 요청해야 한다. action()(입력 처리)에서 요청하면 모델이 바뀌기 전에 settle될 수 있다.
  - settle은 못 찾으면 focus를 건드리지 않는다. 행이 바뀌는 쪽에서 먼저 `nav.clear()`를 한다(해제될 요소를 가리키지 않게).
- `nav.want_act(act, in)`(묶음 4): `in` 아래에서만 찾는다. 같은 data-act가 화면에 둘 있을 때 쓴다(smithy `pick0`: 빈 목록 버튼과 장비 칸). `in`은 지워지지 않는 정적 상자여야 한다(포인터를 다음 프레임까지 들고 있다).
- `nav.settle_input()`(묶음 4): `pad_button`·`pad_dir`(키보드도 이 경로) 맨 앞에서 부른다. want가 남아 있고 그 요소가 이미 있으면(입력은 요소를 만든 Context::Update 뒤에 처리된다) 먼저 잡는다. 이게 없으면 focus가 비는 한 프레임에 확인 키가 `ensure()` + `activate()`로 **첫 항목을 실행**했다(부활 창에서 아이템 대신 골드 부활 등, diffs/batch4.md 1-2).
- `find_act`가 보이는지 보지 않는 것은 일부러다. help는 이전 코드(`GetChild(sel)` 후 `ensure()`)와 똑같이 동작하게 하려고 이렇게 했다.

### 2.4 data-for와 focus 포인터

- data-for는 같은 인덱스의 행 요소를 재사용한다. 행 수가 그대로면 focus 포인터는 계속 유효하고 같은 data-act를 가리킨다(행 순서가 같을 때).
- 행 수가 줄면 뒤쪽 행이 Context::Update에서 지워진다. 그 전에(같은 update() 안에서) `nav.clear()`를 하고 `want_act`로 다시 찾는다(keymap 장치 전환).
- 이전 코드의 "SetInnerRML 뒤 해제된 focus" 문제는 데이터 바인딩으로 옮긴 화면에서는 생기지 않는다.

### 2.5 focus는 ObserverPtr (57ab990), drop_if_gone (5da2052)

- `Nav::focus`는 `Rml::ObserverPtr<Rml::Element>`다. 요소가 지워지면(SetInnerRML은 자식을 바로 지운다) null로 읽힌다. 비교는 `focus.get() == e`(C++17이라 `!=`가 없다), `if (focus)`, `focus->`는 그대로 쓴다.
- 아직 SetInnerRML을 쓰는 화면(smithy, dialogs, menu)에서는 목록을 다시 만든 뒤 focus가 사라진다(옛 코드는 해제된 주소가 새 요소와 겹쳐 엉뚱한 곳에 붙었다). smith_i0_l1이 그 예다. 그 화면을 옮길 때 focus를 명시적으로 다시 잡는다.
- `nav.drop_if_gone(list, acts)`: focus가 `list` 안에 있고 그 data-act가 새 행 목록(acts)에 없으면 `clear()`. data-for 행이 바뀌기 직전(같은 update())에 부른다. 묶음 3에서는 title 카드에서 쓴다. store·farm의 기존 가드는 그대로다.

## 3. 화면별로 바꾼 패턴

### options
- 슬라이더: 화살표 두 개와 val은 정적, 칸은 `span.seg`의 data-for. data-act는 C++에서 `"o_bgm=3"` 문자열로 만들어 `data-attr-data-act`로 넣는다.
- 토글·선택지: 정적 마크업 + `data-class-on="loot"`, `data-class-on="!direct"`.
- `r_full`: Android에서만 숨기던 `d.prop(display)`를 `data-if="full_row"`로(데스크톱은 항상 true).
- `#foot`: glyph 마크업이라 data-rml. 매 프레임 `put`으로 비교하므로 입력 장치가 바뀌면 바로 반영된다.
- `bind_once`와 `ss.bound`는 지웠다(위임 리스너).

### keymap
- 섹션 머리와 행이 형제로 섞인 순서: 그룹이 두 개(0 다음 1)로 고정이라 **정적 섹션 머리 둘 + 그룹마다 data-for 하나**로 풀었다. 래퍼 요소가 없으므로 dump 깊이도 같다.
  - 그룹이 비거나 섞이면 이 방식이 깨진다(bindings.cpp 표가 바뀌면 다시 볼 것).
- 행: `data-class-wait`, `data-attr-data-act`, `{{r.label}}`, glyph는 `data-rml="r.v"`.
- 탭: 정적 span 둘. 패드 탭은 `data-if="has_pad"`(CSS가 inline-block이라 data-if 가능), 이름은 `{{pad_name}} 컨트롤러`.
- 캡처 콜백(begin_capture)은 계속 `ss`만 바꾸고 모델은 update()에서 반영한다.
- 장치 페이지가 바뀌면(`m.dev`와 다르면) focus의 data-act를 `want_act`로 넘기고 `nav.clear()`. `m.dev`는 모델 값과 같이 열림 사이에 남으므로, 같은 장치로 다시 열 때는 focus가 비지 않는다.

### help
- 목록: `div.tp`의 data-for. 안쪽(번호 span + 제목)은 C++에서 만들어 `data-rml="t.rml"`. 제목은 `rml_safe()`를 거친다.
- "불러오는 중…"은 `data-if="list.size == 0"`인 정적 div(템플릿 뒤).
- 본문·제목: data-rml(원작 RML, `rml_safe`).
- `#rescue`: CSS 기본값이 `display:none`이라 data-if가 영영 못 보이게 된다 → CSS를 `display:block`(이전 코드가 보일 때 주던 값)으로 바꾸고 data-if.
- body의 `page`/`confirm` 클래스: data-model이 붙은 body 자신에 `data-class-page` 등.
- `SetScrollTop(0)`은 그대로 update()에서. 본문은 다음 Context::Update에 바뀌고 위치 0은 유지된다.
- 목록 focus: 요소가 있으면 바로 `nav.set(find_act("t<sel>"))`, 없으면(주제가 막 들어옴) `want_act`.
  - 리뷰에서 나온 차이: 주제가 처음 들어오는 프레임에는 focus가 한 프레임 늦게 잡힌다(이전 코드는 SetInnerRML로 같은 프레임에 행이 생겼다). 그 사이 방향키는 `move()`가 첫 항목을 잡으므로 결과가 같다.

### 묶음 2 공통 (collection, farm, skillbook, store)
- **목록 + 상세 칸**: 목록은 data-for, 상세 칸(`#det`)의 자식은 래퍼 없이 하나하나 `data-if="has"`. 선택이 없을 때의 `.none`은 `data-if="!has"`로 맨 뒤에. 래퍼를 넣으면 dump 깊이가 바뀐다.
- **상세 칸 스크롤**: 옛 코드는 `#det`를 통째로 다시 만들어 `.ds`가 맨 위에서 시작했다(열 때마다 `d.invalidate()` 때문에 같은 내용이어도 다시 만들었다). 상세 칸 변수의 `put()` 결과를 `|`로 모아(`det |= put(...)`) 바뀌었거나 이번 열림의 첫 update(`ss.fresh`)이면 `.ds`에 `SetScrollTop(0)`. 요소 자체에 data-rml을 넣던 곳(detail `#desc`, skillbook `#ptext`)은 옛 코드도 스크롤을 리셋하지 않았으므로 그대로 둔다.
- **선택이 바뀔 때 focus**: `nav.set(nullptr)` → 행이 있으면 `find_act`로 set, 없으면 `want_act` → want가 없을 때만 `ensure()`. 선택이 없으면 첫 행을 `want_act`(옛 `ensure()`가 잡던 것). want가 걸린 프레임에 ensure를 부르면 아직 data-if가 평가되지 않은 버튼 같은 엉뚱한 요소를 잡는다.
- **매 프레임 ensure하는 화면(store)**: 열림의 첫 update에서는 ensure를 하지 않는다(`ss.fresh`). 첫 열림에는 목록이 그 프레임의 Context::Update에서 생긴다.
- **행 수가 줄 때(store, farm)**: 이번 Context::Update에 지워질 행(`r<i>`, i ≥ 새 크기)에 focus가 있으면 `nav.clear()`, 그 프레임의 ensure도 건너뛴다(지워질 행을 다시 잡지 않게). 길이가 고정인 목록(collection, skillbook, hotslot)은 필요 없다.
- **텍스트 칸 안의 선택적 자식**: farm `.st`(먹이 아이콘 + 상태 + 타이머)처럼 자식이 텍스트뿐일 때 dump에 텍스트가 나오는 요소는, 선택적 자식을 data-if로 두면 텍스트가 dump에서 사라진다. 안쪽 RML을 C++에서 만들어 data-rml로 넣는다. detail 행·gem 행도 같은 이유로 data-rml.
- **같은 프레임에 판정에 쓰이는 클래스는 Doc에 남긴다**: skillbook `pprev/pnext/pinv`의 `off`는 같은 update()의 `nav.set` → `ensure()`가 바로 봐야 한다. data-class는 Context::Update에 반영되므로 `d.cls`로 둔다(그 클래스의 주인은 Doc 하나).
- **버튼 하나로 두 동작**: collection 등록/해제는 span 하나에 `data-attr-data-act="reg ? 'reg' : 'unreg'"`, `data-class-primary`, `data-rml="reg ? l_reg : l_unreg"`. 등록한 뒤에도 focus가 같은 버튼에 남는다(span 둘을 data-if로 바꾸면 focus가 숨은 요소에 남는다).

### town
- 카드 6장은 data-for, 생성 id는 `data-attr-id="c.fn"`. NPC 스프라이트 선택은 C++에 두고 결과를 `data-attr-src`, `data-style-width/height`로(찾기 전에는 `"auto"`. 빈 문자열은 "Syntax error parsing inline property" 경고).
- 옛 코드는 열 때마다 카드를 다시 만들었다. 그래서 열 때마다 카드 값을 그림 없는 상태로 되돌리고 focus를 fn0에 둔다.

### detail, hotslot, qbox
- detail `#rows`: data-for 셋(행, gem, 재료)과 그 사이의 `data-if="mats.size > 0"` 머리. 템플릿은 각자 자리에 숨어 있어 순서가 지켜진다.
- hotslot: 중첩 data-for(`p : sets` 안에 `c : p.slots`). 등록 순서는 Slot → vector<Slot> → Set → vector<Set>. 옛 마크업 key(슬롯, 선택, glyph)를 그대로 비교해서 바뀌면 focus를 s<sel>로 되돌린다(장치가 바뀔 때도).
- qbox: 고정 요소 r0..r2를 `reels[0].spin` 같은 색인 식으로 묶는다. 배열은 처음부터 3개로 만들어 둔다(빈 배열이면 out of bounds 경고).

### 묶음 3 (bagbuy, talkctl, title, charselect, map)
- **공유 문서 분리(bagbuy)**: 두 화면이 같은 rml을 쓰면 같은 data-model 이름에 붙는다. 스타일을 `dialog.rcss`로 빼고(theme.rcss 다음 `<link>`, cascade 순서 유지) 화면마다 rml을 따로 둔다. 문서 이름 줄(`active screen …`, `== document …`)만 dump에서 바뀐다.
- **모양이 다른 버튼은 요소를 나눈다(bagbuy)**: 한 요소에 `data-class-primary="!ask"`를 두면, 모델 값이 열림 사이에 남아 있어 다른 상태로 열 때 `.btn` transition(0.12s)으로 색이 잠깐 비친다. "취소 / 확인·닫기(primary) / 확장하기"를 data-if 세 요소로 나누면 display만 바뀐다. 정적 요소라 focus는 자식 위치로 고른다.
- **게임 쪽에서 바뀌는 목록(talkctl log)**: want()가 `log`와 `log_ver`를 바꾸고, update()는 `log_ver`가 바뀌었을 때만 모델 배열을 다시 만든다. 이름 span은 data-if, 본문·이름은 data-rml(`rml_safe`). 빈 표시 `#logempty`는 템플릿 뒤 `data-if="log.size == 0"`.
- **"다시 만들 때 일어나던 focus 이동" 재현(title)**: 옛 key(내용 문자열)를 그대로 두고, key가 바뀌면 `drop_if_gone` 뒤 옛 조건 그대로 `focus_cards()`. 같은 프레임에 판정할 값(`off`)은 클래스가 아니라 모델(`m.cards[i].off`)에서 읽는다. 카드 배열은 생성자에서 3개로 만들어 문서의 첫 Context::Update부터 요소가 있게 한다.
- **정적 카드 + C++ 스프라이트(charselect)**: 카드를 rml에 정적으로 두고, 옛 코드가 열 때마다 새 img로 시작하던 것을 첫 update에서 `src=""`와 인라인 width/height/left/top 제거로 재현한다. Doc의 prop 캐시는 열 때 `invalidate()`되므로 같은 update에서 다시 들어간다.
- **부분 이전(map)**: 박스 크기를 읽어 만드는 `#map` 내용과 라이브 텍스처는 Doc에 남기고, 머리·범례·zoom 클래스만 옮긴다. data-model 안에서 SetInnerRML로 만든 요소도 문제없다(`data-act`는 바인딩이 아니고, 생성 마크업에 `{`가 없어야 한다).
- **CSS transition 점검**: 정적 요소나 재사용 요소의 클래스가 바뀌면 transition이 돈다. 문서를 불러올 때부터 있던 요소는 스타일 정의가 있어서 처음 클래스가 붙을 때도 돈다(charselect `.cc` 0.18s, title `.card` 0.14s). 화면마다 검토 서브에이전트에 "새로 도는 transition 목록"을 받아 batch 문서에 적는다.

### 묶음 4 (smithy, dialogs)
- **구조가 모드마다 다른 상자는 안쪽 마크업을 data-rml로 통째로(smithy `#slots`·`#det`·`#rbody`)**: 값이 바뀌면 자식이 새로 만들어지므로 그 안의 focus는 ObserverPtr로 null이 된다. put하기 전에 focus가 그 상자 안이면 data-act를 기억해 `want_act(act, 상자)`로 다음 프레임에 다시 잡는다(smithy.cpp `inner` 람다). 원작 문자열은 마크업에 넣기 전에 하나씩 `rml_safe`.
- **"행이 없으면 첫 항목"인 focus는 다음 프레임 `ensure()`(`ss.ensure_next`)**: 옛 코드는 같은 update()에서 SetInnerRML한 새 요소(visible이 처음부터 true)를 ensure로 잡았다. 지금은 지워질 옛 행이 아직 보이므로 그 프레임의 ensure는 엉뚱한 요소를 잡는다. update() 맨 앞(settle 뒤)에서 한다.
- **단계 판정(fkey, key 문자열, ptr)은 옛 코드 그대로 두고, 그 안에서 "재사용될 요소가 있으면 바로 `find_act`, 이번 Context::Update에서 생기거나 바뀌면 `want_act`"**: dialogs는 items·버튼을 `put`한 결과(바뀌었는지)로 고른다. data-for 행의 act가 인덱스마다 같은 이름(`i<k>`, `l<k>`)이면 재사용 행이 곧 새 행이다.
- **모양이 다른 버튼은 요소를 나눈다(dialogs `#btns`)**: 역순 생성(b2, b1, b0)에서 primary b0은 늘 마지막이라, 일반 버튼의 data-for 뒤에 정적 `data-if="primary"` b0을 둔다. 한 요소가 primary ↔ 일반으로 바뀌지 않으므로 `.btn` 색 전환이 없다(bagbuy와 같은 이유).
- **CSS 기본 `display:none` 요소(dialogs `#sub`·`#rows`)**: rcss의 `display:none`을 지우고 data-if. 같은 rcss를 쓰는 다른 문서(bagbuy.rml)에 그 요소가 있는지 먼저 본다.
- **`vector<bool>`은 행 구조체로**: `state::Dialog`의 `items`+`item_off`를 `Item{text, off}`로 합쳤다(바인딩할 수 없고 인덱스 둘을 맞출 필요도 없어진다).
- **정적으로 둬도 되는 반복 마크업(dialogs `#count`)**: 옛 코드가 열 때마다 만들던 고정 구조는 rml에 두고 `{{ }}`와 data-class만 바꾼다(숨어 있을 때도 DOM에 있지만 data-nonav·display:none이라 Nav·클릭에 걸리지 않는다).

### 묶음 5 (menu)
- **여러 페이지를 가진 화면 하나 = 모델 하나**: 탭 sel·페이지 `on`은 int `tab` 하나로 `data-class-sel="tab == N"`. 대기 탭(`ss.tab_req`) 계산은 그대로 두고 결과만 put한다. 탭 전환 프레임의 `ensure()`는 옛 코드처럼 새 페이지를 보지 못하고(IsVisible은 Context::Update에서 바뀜) 탭 버튼으로 간다.
- **keep_focus 대체 (`rows()`/`refocus()`)**: 옛 keep_focus는 목록을 다시 만든 뒤 `ss.focus_act`(마지막 페이지 액션)인 요소를 잡았다. 그래서 `ui act it3`이나 클릭은 그 칸으로 focus를 옮겼고, 이전 focus의 `.focus`를 지우지 않았다(`nav.focus = nullptr; nav.set(e)`). 지금은 목록 모델이 바뀌면 `drop_if_gone` → put → focus_act가 새 행에 있으면 `find_act`(재사용 행)로 바로, 없으면 `want_act`. 옛 코드가 keep_focus 없이 다시 만들던 목록(퀘스트)은 `rows(.., false)`.
- **칩 줄 (`chips()`/`chip_focus()`)**: 옛 코드가 매번 만들던 칩 줄은 정적 칩 + `data-class-on`으로 둔다. 선택이 바뀌면 옛 코드는 칩 줄(과 바뀐 목록·상세 칸)을 다시 만들어 그 안의 focus를 잃었다. 그 경우에만 누른 칩이 focus를 받고, 밖(레일)의 focus는 그대로다(10b, 옛 코드 기준선으로 확인).
- **상세 칸 (`inner()`)**: smithy 패턴(data-rml 통째로, 안쪽 focus를 다음 프레임에 같은 act로). 단 **방금 누른 버튼**(`ss.focus_act`)은 다시 잡지 않는다: 장착하면 원작이 후보 0을 고르므로 같은 '장착' 버튼이 다른 장비를 가리킨다(Enter 연타·자동 반복으로 이어서 장착됐다).
- **열림 사이에 남는 요소**: 옛 코드는 열 때 `d.invalidate()`로 모든 상자를 다시 만들었다. 이제 요소가 남으므로 그 부수 효과가 필요한 것만 되살린다: 열림마다 페이지를 처음 그릴 때 `.ds` 스크롤을 맨 위로(`ss.drawn`). 피커는 `ss = {}` 뒤 첫 update에서 닫힌 값을 put.
- **페이지 액션은 페이지를 넘지 않는다**: `switch_tab()`이 `ss.focus_act`를 비운다(Q/E 탭 키는 page_action의 clear를 거치지 않고, 원작이 페이지 값을 채우기 전 대기 프레임에 목록이 비었다 다시 차며 옛 칩·칸으로 focus가 튀었다).
- **고정 구조는 정적 마크업**: `#sec` 7칸, `#qsum`, 칩, 안내 문구는 rml에 두고 값만 `{{ }}`. 모양이 바뀌는 상세 칸·`#sysinfo`·`#qdetail`·`#eqhead`는 data-rml 통째로.
- **중첩 data-for(피커)**: hotslot과 같은 등록 순서(Cell → vector → Row → vector). 구조체 타입은 화면 클래스 안에 중첩(hotslot의 `Slot`과 menu의 `Slot`은 다른 타입).
- **입력 시점의 문서 변경은 ss로**: 토스트는 `ss.toast`/`toast_until`만 남기고 update()가 put한다.
- **data-if 식에 `&&`를 쓰지 않는다**: RML 속성 안의 `&amp;&amp;` 해석을 믿지 않고 C++에서 bool 하나로 계산해 넘긴다(`sk_empty`).

## 4. 함정

1. **data/ui/*.rml은 실행할 때 읽힌다.** 시나리오가 도는 동안 rml을 고치면 테스트가 "옛 exe + 새 rml"로 돈다(옛 exe는 `[rml] Could not locate data model`을 남긴다). 테스트 중에는 rml 초안을 scratchpad에 두고 빌드 직전에 옮긴다. cpp는 빌드 때만 읽히므로 고쳐도 된다.
2. **data-for 템플릿은 마지막 자식이 아니라 원래 자리에 남는다.** 복제본은 템플릿 바로 앞에 들어간다(DataViewFor::Update, `InsertBefore(.., element)`). RULES.md의 "마지막 자식"은 "data-for 구간의 끝"으로 읽는다. 템플릿에는 인라인 `display:none`이 붙고 data-attr 등 다른 뷰가 만들어지지 않는다(그래서 data-act도 없다).
3. **data-for 요소는 `<x ...></x>`로 닫아 쓴다.** `<x .../>`이면 `rmlui-inner-rml`이 없어서 복제본마다 "Missing or invalid RML contents" 경고가 난다.
4. **행 수가 줄 때 `[rml] Data array index out of bounds` 경고가 날 수 있다.** 같은 깊이의 뷰는 정렬 순서가 같아서(DataView sort_order = 깊이) 형제 행의 data-attr 뷰가 data-for보다 먼저 평가될 수 있다. 그 행은 바로 지워지므로 화면에는 영향이 없다(keymap 키보드 → 패드 전환에서 1건). 지워지는 행 수 × 그 행의 뷰 수만큼 늘어난다: store 카테고리 변경(33 → 4행) 15쌍, 판매 21 → 4행 36쌍, detail 5 → 2행 3쌍.
5. **IsVisible은 클래스를 바꾼 같은 프레임에는 옛 값이다.** 이전 코드도 이것 때문에 focus를 잃는 경로가 있었다(help: 페이지에서 닫고 다시 열기, 확인 창의 c_no, 페이지에서 목록으로 돌아가기). 데이터 바인딩에서도 data-class는 Context::Update에 반영되므로 같은 시점 문제가 그대로다. 이전 동작을 유지하려고 고치지 않았다(아래 6절).
6. **값이 바뀔 때 요소를 다시 만들지 않으므로 CSS transition이 실제로 돈다.** options 토글 노브(`.toggle .k`의 `left 0.15s`)가 이제 미끄러진다. 이전에는 매번 새 요소라 전환이 보이지 않았다. dump를 전환 중(150ms 안)에 뜨면 좌표가 다르다.
7. **python으로 C++을 고칠 때**: 이 저장소 파일은 CRLF와 LF가 섞여 있다. `open(..., newline='')`로 읽고 쓴다. 또 Bash heredoc 안의 `'\\x..'`가 전달 과정에서 바뀌어 매칭이 실패했다. `\x` 이스케이프가 든 줄은 Edit 도구로 고친다.
8. **시나리오가 어느 장치 페이지로 여는지 확인한다.** keymap은 키를 한 번도 치지 않았으면 패드 페이지로 연다(`pad_connected() && (pad_input() || !keyboard_last())`). keymap_dev는 `ui act km_kb`로 시작한다.
10. **시나리오가 도는 동안 rml을 고치면 결과를 믿을 수 없다(다시 겪음).** 묶음 2에서 collection 검증 도중 collection.rml을 고쳤다. 다행히 dump를 뜬 뒤였고 같은 exe로 다시 돌려 확인했다. 검증 서브에이전트가 돌고 있을 때는 rml 초안을 scratchpad에만 둔다.
11. **기준선을 뜨려고 파일을 되돌릴 때** 고친 파일을 scratchpad에 복사하고 `git show HEAD:<경로> > <경로>`로 되돌린 뒤 `touch`. 끝나면 복사본을 돌려놓고 md5를 확인한다. 같은 시간에 diff를 읽는 서브에이전트가 있으면 디스크 파일 대신 `git show`로 읽으라고 알린다.
9. **추가 시나리오의 기준선은 옛 코드로 뜬다.** 화면 하나의 cpp/rml만 HEAD로 되돌려 증분 빌드(1분 남짓)하고 새 시나리오를 돌리면 그 화면의 기준선이 생긴다(help_more → `golden_helpx/`). 되돌릴 때 `git checkout HEAD -- 파일` 뒤 `touch`(ninja가 mtime으로 판단).
   - 그 화면을 고치기 전이라면 지금 빌드로 바로 뜨면 된다. 묶음 3의 `title_more`는 golden_b0을 뜰 때 함께 떴다.
12. **cpp 초안을 src에 미리 넣어 두면 다른 화면을 빌드할 때 같이 빌드된다.** 묶음 3에서 title.cpp 초안이 bagbuy 재빌드에 섞여 "새 cpp + 옛 rml"인 exe가 됐다(테스트 전에 알아채 되돌림). 다음 화면의 cpp를 미리 넣었으면, 그 화면 rml을 넣기 전에 빌드할 일이 생길 때 cpp를 HEAD로 되돌려 둔다. 빌드 로그의 `Building CXX object …` 목록으로 확인한다.
13. **Git Bash heredoc에 `'\r\n'` 같은 이스케이프를 쓰지 않는다.** 파이썬 스크립트를 heredoc으로 고치다 `\r\n`이 실제 줄바꿈으로 바뀌어 스크립트가 깨졌다(함정 7과 같은 원인). 스크립트는 Write 도구로 만든다.
15. **`ui act`는 focus를 옮기지 않는다**(input.cpp `act` → `ui_action`). 그래서 행을 재사용하게 되면 시나리오에서는 focus가 처음 자리(l0)에 남는다(smith_i0_l1). 세션 지시의 "l1로 돌아온다"는 키보드 사용을 가정한 것이었다. 기대 dump를 적을 때 이 점을 본다.
16. **CSS animation 위상은 실행마다 다르다.** smithy 제련 막대 `.rfill`(`slide 0.9s infinite`)은 기준선 세 번이 우연히 같은 위상(x 834~839)이었다가 한 번 637이 나왔다. 주기의 절반 넘게 막대 밖이라 `!OUTSIDE`도 붙는다. 마스크에 `(?: !OUTSIDE)?`를 넣고 dump 끝의 `issues N` 합계 줄을 가렸다(모든 issue는 자기 줄에도 찍히므로 잃는 것이 없다).
17. **dev 경로가 원작을 죽이는 경우가 있다(시나리오에서 피함)**: smithy 강화(mode 0)에서 장비 칸이 빈 채 `pick1`을 누르면 원작 ShowCommonTv @001185d0이 칸 0 아이템을 읽다가 죽는다. 부활 창에서 포기(i2)한 뒤 월드맵에서 Return을 누르면 스크립트 말풍선에서 죽는다. RULES.md 시나리오 함정과 6절에 적었다.
18. **Git Bash `sed -i`는 CRLF 파일을 LF로 바꾼다.** menu.cpp(CRLF)에 `sed -i`를 한 번 쓰자 파일 전체가 바뀐 diff가 됐다. CRLF 파일은 Edit 도구나 `newline=''`로 여는 파이썬으로 고치고, 고친 뒤 `\r\n` 수와 `\n` 수를 비교한다(함정 7·13과 같은 계열).
19. **한 dump를 고치려고 만든 focus 규칙은 이웃 경로를 바꿀 수 있다.** 칩 줄 refocus(tab3_skt1 복구용)를 "선택이 바뀌면 언제나"로 만들었더니 레일에 있던 focus까지 칩으로 옮겼다. 기존 시나리오에는 그 경로가 없어 몰랐고, 옛 코드로 뜬 `menu_more` 기준선에서 드러났다. focus 규칙을 새로 만들면 옛 코드 기준선이 있는 시나리오로 이웃 경로를 확인한다.
20. **규칙의 전제(행 수)를 dump로 확인한다.** "skt1이면 스킬 목록이 비어 sk1이 지워진다"는 틀렸다(패시브 9행, sk1 재사용). 검토 서브에이전트가 golden dump의 행 수로 찾았다.
21. **data-style 변수의 초기값을 빈 문자열로 두지 않는다.** 문서를 불러온 첫 Update에 `width: ;` 구문 경고가 난다. `"auto"`(옛 "인라인 없음"과 같은 계산값)로 둔다. `"0%"`는 첫 표시 때 폭 전환이 새로 돈다.
22. **대기 탭 프레임에는 페이지 값이 기본값이다.** 원작은 자기 탭일 때만 페이지 값을 채운다(`capture_pages`). 우리 탭이 먼저 바뀌는 1~2프레임 동안 목록이 비었다가 다시 차서 `[rml]` 행 감소 쌍(`items[N]` 등)이 한 번에 수십 개 나온다. 옛 코드도 그 프레임에 빈 표시를 그렸으므로 화면은 같다.
23. **재사용 요소에서는 `ScrollIntoView`가 실제로 듣는다.** 옛 keep_focus는 레이아웃 전 새 요소에 불러 스크롤되지 않았다(it30이 격자 밖에 잘림). 이제 focus 칸이 보이게 스크롤된다(menu/qbox_item 격자 scroll 0 → 65).
24. **테스트 창에 실제 키 입력이 들어가면 glyph dump가 바뀐다.** `ui key` 전 dump(menu/open1)에 키보드 glyph가 생겼다. 그 시나리오만 다시 돌려 확인한다.
14. **실행마다 달라지는 값을 새로 만나면 기준선끼리 먼저 비교한다.** 묶음 3에서 추가한 마스크: worldmap(@file), `#autofill` 폭(AUTO 게이지, 벽시계 비율), `img#spr` 좌표(전직 화면 라이브 애니메이션), menu 캐릭터 페이지의 `span.v "N / M"`(HP·MP 회복). 1px 허용 안에서 우연히 같았던 값이 다음 실행에서 드러난 경우가 많다.

## 5. 다음 배치에 그대로 쓸 템플릿

### C++

```cpp
class FooScreen : public Screen {
    struct Session { ... } ss;                 // 열 때마다 새로
    // data model "foo" (foo.rml), set in update() only
    struct Row {
        std::string act, text;                 // data-act, 표시 문자열(포맷은 C++에서)
        bool on = false;
        bool operator==(const Row &o) const { return act == o.act && text == o.text && on == o.on; }
    };
    struct View {
        std::vector<Row> rows;
        std::string title;                     // 원작 RML이면 rml_safe() 후 data-rml
        bool busy = false;
    } m;
public:
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("foo");   // 화면 고유 이름
        if (!c) return;
        if (auto h = c.RegisterStruct<Row>()) { h.RegisterMember("act", &Row::act); h.RegisterMember("text", &Row::text); h.RegisterMember("on", &Row::on); }
        c.RegisterArray<std::vector<Row>>();
        c.Bind("rows", &m.rows); c.Bind("title", &m.title); c.Bind("busy", &m.busy);
        model = c.GetModelHandle();
    }
    void update(const state::Snapshot &s) override {
        std::vector<Row> r;
        for (...) r.push_back({Rml::CreateString("r%d", i), text, i == ss.sel});
        put(m.rows, std::move(r), "rows");
        put(m.title, rml_safe(title), "title");
        // 새로 생길 행에 focus: 있으면 바로, 없으면 다음 프레임
        if (Rml::Element *e = nav.find_act(a)) nav.set(e); else nav.want_act(a);
    }
};
```

- 구조체 타입은 화면 클래스 안에 중첩해서 둔다. 다른 화면과 같은 C++ 타입을 등록하지 않는다(타입 등록은 context 전체 공유).
- bool, int, std::string은 등록하지 않아도 된다.
- 게임 스레드 람다(state::post)에서는 모델 값을 건드리지 않는다.
- data-rml로 통째로 넣는 상자 안의 focus(묶음 4, smithy):

```cpp
auto inner = [&](std::string &var, std::string &&val, const char *name) {
    if (var == val) return;
    Rml::Element *box = d.el(name), *f = nav.focus.get(), *p = f;
    while (p && p != box) p = p->GetParentNode();
    if (p && box) nav.want_act(f->GetAttribute<Rml::String>("data-act", ""), box); // 새 마크업의 같은 act
    put(var, std::move(val), name);
};
```

### RML

```html
<body data-block="1" data-model="foo" data-class-busy="busy">
<div id="list"><div class="row" data-for="r : rows" data-class-on="r.on" data-attr-data-act="r.act">{{r.text}}</div><div class="none" data-if="rows.size == 0">없음</div></div>
<div id="title" data-rml="title"></div>
```

- 인라인 형제는 한 줄에 붙여 쓴다(공백 텍스트 노드).
- data-for 요소는 닫는 태그를 쓴다.
- CSS 기본이 `display:none`인 요소에 data-if를 쓰려면 CSS를 먼저 보이는 값으로 바꾼다.
- lucide 엔티티(`&#xe06e;`)는 템플릿에 리터럴로.

### 검증 순서 (화면 하나)

1. cpp는 바로 고치고, rml 초안은 scratchpad에 둔다(테스트 중이면). 다음 화면 cpp를 미리 넣었으면 함정 12.
2. 시나리오에 없는 경로가 있으면 새 시나리오를 쓰고, 그 화면만 HEAD로 되돌린 빌드로 기준선을 뜬다(함정 9).
3. rml을 옮기고 build64. 빌드 로그에서 이번 화면 cpp만 컴파일됐는지 본다.
4. 서브에이전트 1: `run_golden.py` → `compare.py golden_b0 <dest> <화면>`(필요하면 `--raw`), 로그의 `[rml]` 종류와 개수.
5. 서브에이전트 2(동시에): `git diff HEAD -- <화면 파일>` 검토. dump에 안 나오는 동작 차이와 **새로 도는 CSS transition 목록**을 받는다.
6. 사유를 `diffs/<묶음>.md`에 적고 화면 파일과 함께 커밋. 메시지에 비교 결과와 알려진 차이.
7. 성능은 묶음 처음과 끝에 `perf/run_perf.py`로 한 번씩(화면마다 돌리지 않아도 된다).

### menu 메모 (plan.md 3절 묶음 6의 10단계, 묶음 4에서 배운 것으로 바뀌는 점) — 묶음 5에서 끝남

실제로 한 것과 다른 점: 9번 피커는 `ensure_next`를 쓰지 않았다(옛 코드도 피커를 여는 프레임에는 focus가 없었다). 10번의 변수 이름은 `ss.toast`. 격자가 비면 첫 항목 focus(`ensure_next`)는 필요 없었다(탭 전환 focus는 탭 버튼). 나머지는 3절 "묶음 5"와 diffs/batch5.md.


1. 정적 id 요소(탭 sel/페이지 on, ptitle, gold, dot, confirm, foot, sy_save, bumpl/bumpr glyph): 그대로. `c_d`(구매 확인)는 원작 이름이 들어가므로 rml_safe 뒤 data-rml.
2. p0 캐릭터: `#xpbar`의 `GetChild(0)` 폭(`d.prop(bar->GetChild(0), "width")`, menu.cpp:528)은 data-style-width로. `span.v "N / M"`은 이미 마스크가 있다.
3. p8 sysinfo, 4. p4 퀘스트(qdetail 파싱은 C++, `qsum`·`qdetail`은 smithy `#det`처럼 data-rml 통째로).
5. p6 상점 / 6. p1 가방 / 7. p2 장비 / 8. p3 스킬: 격자(`shopgrid`, `invgrid`, `eqslots`, `eqcands`, `sklist`)는 data-for. 상세 칸(`shopdet`, `invdet`, `eqdet`, `skdet`)은 처음엔 smithy `inner` 패턴(data-rml 통째로 + 안쪽 focus를 `want_act(act, 상자)`로 다시 잡기)으로 옮기면 안전하다.
   - **keep_focus(menu.cpp:408-416)는 지운다**: 격자를 data-for로 바꾸면 행이 재사용되어 focus가 그대로 남는다(smith_i0_l1과 같은 효과). 행 수가 줄어 focus 행이 사라질 때만 `drop_if_gone` 뒤 `want_act`. 기준선의 "탭 줄에 `.focus`가 여러 개"와 tab3_skt1 `*FOCUS` 없음이 정상 쪽으로 바뀔 것이다(RULES.md 표). 사유를 적는다.
   - 같은 data-act가 페이지 둘에 있으면(예: 피커와 격자) `want_act`에 범위를 준다.
   - 격자가 비면 첫 항목 focus는 `ss.ensure_next`(다음 update 맨 앞 ensure).
9. picker(`pk_rows`): data-for. 피커를 열 때 `nav.root = picker; ensure()`(menu.cpp:448)는 행이 이번 Context::Update에 생기면 `ensure_next`로.
10. toast: `action()`의 `toast()`(menu.cpp:138, 510, 629, 649)는 `ss.toast_text`/`toast_until`만 남기고 update()에서 put. `mtoast .on`은 data-class(같은 프레임 판정 없음).
- 공통: 탭 전환으로 페이지 상자가 data-if/data-class로 바뀌면 focus가 숨은 페이지에 남지 않도록 탭 전환 때 `nav.set(nullptr)` 후 want/ensure_next. `[rml]` 행 감소 쌍(함정 4)이 격자마다 나올 것이다. 확인 키 틈은 `settle_input`이 막는다.
- 시나리오: menu 시나리오(74 dump)와 qbox(상점 sh6 구매), town_more 일부가 menu를 지난다. 옮긴 뒤 14개 전체를 돌린다(dialogs가 menu 상점 확인 창에도 낀다).

## 6. 이번에 찾은 기존 문제 (고치지 않음)

- help: 페이지에서 닫고 다시 열면 목록에 focus가 없다. 확인 창을 열어도 c_no에 focus가 없고, 페이지에서 목록으로 돌아가도 focus가 없다. 원인은 같은 프레임의 IsVisible이 옛 값이라 `ensure()`가 focus를 지우는 것이다(함정 5). 키를 누르면 첫 항목부터 잡히므로 큰 문제는 아니다.
- keymap: 열려 있을 때 `open_keymap()`이 다시 불리면 `ss = {}`만 하고 `input::begin_capture(nullptr)`는 하지 않는다. 대기 중이던 캡처가 남아 이전 행에 키가 들어간다.
- keymap: 바뀐 상대 행의 라벨을 찾지 못해도 `toast_until`은 갱신되어, 이전 토스트 문구가 다시 보인다(keymap.cpp 캡처 콜백).

- ~~묶음 4: smithy 강화(mode 0)에서 장비 칸이 빈 채 강화석 칸(`pick1`)을 누르면 게임이 죽는다~~ → **51bdbf0에서 고침**(장비 칸이 비면 원작 알림을 띄운다).
- 묶음 4: 부활 창에서 포기(i2)를 고른 뒤 필드 2/1에서 월드맵을 열고 Return(링크스톤)을 누르면 cTalkBallonWnd::SetBalloonType(← cflScript::MakeTalkBallon)에서 죽었다. dev 경로(suicide, warp, worldmap)를 이어 쓴 결과라 제품 경로인지는 모른다.
- 묶음 4: lv14 저장에는 소켓 있는 장비가 없어 강화석 칸 경로(mode 0 pick1, 강화 실행)는 시나리오가 없다.
- 묶음 2 리뷰에서 나온 남은 위험(실제로 들어가는 경로는 찾지 못함, 고치지 않음):
  - collection은 `ss.last`(선택)만 보고 폼 포인터를 보지 않는다. 화면이 보이는 채로 크기가 다른 컬렉션으로 바뀌면 focus가 지워진 칸을 가리킬 수 있다(farm은 `last_ptr`도 본다).
  - 행이 줄 때의 가드는 `r<i>` 행만 본다. store 카테고리 칩(4 → 0, 일반 ↔ 튜토리얼 상점)은 가드가 없다. 두 상점이 보이는 채로 바뀌는 경로는 없다.
  - 일반화한다면 "focus의 data-act가 새 모델에 없으면 clear"를 Nav나 Screen의 헬퍼로 두는 것이 좋다.
- 묶음 5 (menu, 고치지 않음):
  - **메뉴 화면에서 Enter 자동 반복이 걸러지지 않는다**(input.cpp `in_modern_menu` 분기는 `k.repeat`를 보지 않는다). focus가 남는 버튼(캐릭터 GP `+`, 상세 칸의 다른 이유로 다시 잡힌 버튼)은 누르고 있으면 연달아 실행된다. 옛 코드부터 있던 성질이다.
  - **장착한 뒤 원작은 슬롯 버튼으로 focus를 옮긴다**(menu_lists_spec 2.2). 옛 코드부터 따르지 않았다(focus 없음).
  - **피커를 열 때 focus가 없다.** 여는 프레임의 `ensure()`가 아직 `on`이 붙지 않은 `#picker`를 보지 못한다. 첫 방향키·확인 키에 pk0을 잡으므로 큰 문제는 아니다. 옛 코드와 같다.
  - `rml_safe`는 `{`만 바꾼다. 원작 이름·지역 이름에 `<`·`&`가 있거나, 키 이름(SDL_GetKeyName, glyph)이 `<`·`&`이면 RML로 해석된다. 옛 코드와 같다.
  - 메뉴를 다시 열면 늘 탭 0(캐릭터)으로 열린다. 원작 메뉴 폼의 탭 값(`mdi+0x90`)을 그대로 따른 것이다. 원작 동작인지는 확인하지 못했다(UNVERIFIED).
  - 다시 여는 첫 프레임에는 `#main`의 블러만 보이고 패널이 아직 투명하다(`body.on`의 `menuin` 0.18초 페이드). 다음 프레임에 내용이 보인다(burst 확인, diffs/batch5.md 11).

## 7. 프레임 시간 (EH_UIPERF, 300프레임 평균, ms: screens::update / context Update)

| 화면 | 이전(d7eeb67) | options 후 | keymap 후 | help 후 |
|---|---|---|---|---|
| field | 0.041/0.065 | 0.039/0.075 | 0.037/0.072 | 0.038/0.072 |
| options | 0.080/0.088 | 0.073/0.088 | 0.073/0.090 | 0.072/0.098 |
| keymap | 0.111/0.075 | 0.081/0.076 | 0.081/0.083 | 0.078/0.080 |
| help 목록 | 0.069/0.085 | 0.070/0.082 | 0.067/0.084 | 0.067/0.087 |
| help 페이지 | 0.069/0.086 | 0.070/0.086 | 0.069/0.084 | 0.068/0.084 |

약 25fps(300프레임 ≈ 12초)라 화면마다 창 하나씩이다. 차이는 측정 잡음 범위다. 기준선 비교 결과는 성능과 상관없다.

### 묶음 2 (perf/town_perf.txt, `run_perf.py <tag> town_perf.txt`)

같은 시나리오(필드 → 마을 → 상점 구매 목록 → 농장 부화 → 컬렉션 → 아이템 상세, 각 12초)를 묶음 2 직전 코드(75c708b의 8개 화면, out_before_b2)와 지금(out_after_b2)으로 돌렸다. 300프레임 창 10개를 순서대로 놓았다. 창과 화면의 경계가 딱 맞지는 않는다.

| 창 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| 이전 | 0.036/0.060 | 0.055/0.072 | 0.067/0.074 | 0.059/0.070 | 0.061/0.078 | 0.095/0.076 | 0.060/0.095 | 0.061/0.094 | 0.060/0.084 | 0.062/0.090 |
| 지금 | 0.036/0.066 | 0.059/0.080 | 0.067/0.080 | 0.061/0.081 | 0.058/0.087 | 0.082/0.082 | 0.053/0.103 | 0.056/0.108 | 0.054/0.098 | 0.061/0.096 |

screens::update는 같거나 조금 줄었고, context Update(데이터 뷰 평가 포함)는 0.006~0.014ms 늘었다. 프레임(약 40ms)에 비하면 무시할 크기다.

### 묶음 3 (perf/b3_perf.txt, `run_perf.py <tag> b3_perf.txt`)

타이틀 → 슬롯 → (불러오기) 필드 → bagbuy → 필드 2/1 → 지역 지도 → 프롤로그 crawl → 전직 화면, 각 12초. 묶음 3 직전 코드(57ab990 빌드, out_before_b3)와 묶음 3 끝(out_after_b3). 창 9개, 로그의 `[title]`·warp·crawl skip·uiwnd 줄로 창과 화면을 맞췄다.

| 창 | 1 타이틀 | 2 슬롯 | 3 필드(불러온 뒤) | 4 bagbuy | 5 필드 2/1 | 6 지도 | 7 지도 | 8 crawl | 9 전직 |
|---|---|---|---|---|---|---|---|---|---|
| 이전 | 0.037/0.081 | 0.052/0.085 | 0.056/0.088 | 0.066/0.086 | 0.063/0.083 | 0.097/0.087 | 0.100/0.087 | 0.053/0.085 | 0.063/0.129 |
| 지금 | 0.040/0.087 | 0.051/0.097 | 0.061/0.098 | 0.066/0.088 | 0.065/0.093 | 0.100/0.090 | 0.096/0.092 | 0.052/0.085 | 0.063/0.124 |

screens::update는 같고(±0.005ms), context Update는 0.000~0.012ms 늘었다(슬롯·필드 창이 가장 크다, 창 하나씩이라 잡음과 구분되지 않는다). 묶음 2와 같은 크기로, 프레임에 비하면 무시할 수 있다.

### 묶음 4 (perf/b4_perf.txt, `run_perf.py <tag> b4_perf.txt`)

lv14 불러오기 → 마을 대장장이 선택지(dialogs choice) → 제련 목록(smithy) → 제련 팝업(rask) → 연금술사 녹이기 목록 → 필드 2/1 → 월드맵 위 링크스톤(dialogs 0x13) → 부활 창(dialogs 0x14), 각 12초. 묶음 4 직전 코드(1137335 빌드, out_before_b4)와 끝(e02fc01 빌드, out_after_b4). 창 11개, 로그의 action·key·dev 줄로 맞췄다.

| 창 | 1 불러오기 | 2 필드 | 3 선택지 | 4 제련 목록 | 5 제련 팝업 | 6 팝업·녹이기 | 7 녹이기 목록 | 8 필드 2/1 | 9 월드맵·링크스톤 | 10 월드맵 | 11 부활 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 이전 | 0.042/0.096 | 0.059/0.091 | 0.066/0.102 | 0.055/0.094 | 0.080/0.114 | 0.058/0.090 | 0.071/0.110 | 0.069/0.113 | 0.233/0.183 | 0.545/0.400 | 0.103/0.154 |
| 지금 | 0.039/0.080 | 0.061/0.091 | 0.065/0.102 | 0.049/0.091 | 0.069/0.098 | 0.057/0.078 | 0.057/0.091 | 0.060/0.103 | 0.224/0.154 | 0.505/0.350 | 0.094/0.125 |

smithy 창(4·5·7)은 screens::update가 0.006~0.014ms 줄었다(매 프레임 Doc 문자열 비교·SetInnerRML 대신 put 비교). context Update는 같거나 줄었다. 9·10 창이 큰 것은 월드맵(autoscreen, 옮기지 않음) 때문이다. 창 하나씩이라 잡음과 구분되지 않는 크기이고, 프레임(약 40ms)에 비하면 무시할 수 있다.

### 묶음 5 (perf/menu_perf.txt, `run_perf.py <tag> menu_perf.txt`)

lv14 불러오기 → 필드 → 메뉴 캐릭터 → 가방 → 장비 → 스킬 → 퀘스트 → 상점 → 시스템 → 가방 단축키 피커, 각 12초. 묶음 5 직전 코드(51bdbf0의 build64 exe, out_before_b5)와 끝(정리 뒤 빌드, e966cec, out_after_b5). 창 12개, 로그의 `[ui] action` 줄로 창과 화면을 맞췄다(11·12는 피커 앞뒤로 조금 어긋난다).

| 창 | 1 불러오기 | 2 필드 | 3 필드 | 4 캐릭터 | 5 가방 | 6 장비 | 7 스킬 | 8 퀘스트 | 9 상점 | 10 시스템 | 11 가방·피커 | 12 피커 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 이전 | 0.038/0.084 | 0.060/0.091 | 0.068/0.091 | 0.063/0.101 | 0.067/0.089 | 0.090/0.089 | 0.070/0.100 | 0.082/0.098 | 0.064/0.109 | 0.086/0.120 | 0.067/0.106 | 0.082/0.112 |
| 지금 | 0.034/0.072 | 0.055/0.086 | 0.064/0.085 | 0.052/0.086 | 0.057/0.087 | 0.059/0.091 | 0.060/0.096 | 0.056/0.101 | 0.055/0.100 | 0.063/0.106 | 0.059/0.116 | 0.067/0.110 |

menu 창(4~12)의 screens::update는 0.008~0.031ms 줄었다. 매 프레임 페이지 마크업 문자열을 만들어 Doc 캐시와 비교하고 SetInnerRML하던 것이 put 비교로 바뀌었기 때문이다. 장비(창 6)가 가장 크게 줄었다. context Update는 −0.015~+0.010ms로 같은 범위다(데이터 뷰 평가가 더해졌지만 다시 만드는 요소가 줄었다). 창 하나씩이라 잡음과 구분되지 않는 크기이고, 프레임(약 40ms)에 비하면 무시할 수 있다.
