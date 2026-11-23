# 조사 C: options, keymap, help, collection, farm, skillbook, town, detail, hotslot, qbox, helpfull, autoscreen

(Explore 서브에이전트 보고, 2026-10-21. 리팩터 후 코드 기준. file:line은 src/ui/ 기준. 근거를 다시 확인하지 않은 줄도 있으니 고칠 때는 해당 줄을 열어 볼 것.)

12개 화면 모두 data-for로 옮길 수 있다. 다만 공통 기반 세 가지를 먼저 바꾸지 않으면 Nav 동작이 어긋난다: 클릭 리스너, 포커스를 다시 잡는 시점, 모델을 만드는 시점.
autoscreen은 제외, helpfull은 대부분 제외. 이 12개 중 Pending<T>를 쓰는 곳은 없다(Pending은 charselect.cpp:25, menu.cpp:90).

## 0. 공통 사항

**리스너와 data-act**
- `ActionListener`(screens.cpp:45-57)는 `GetCurrentElement()`의 data-act를 `input::ui_action`으로 보낸 뒤 `StopPropagation`(53). 가장 안쪽 요소가 이긴다.
- `bind_actions`(59-63)는 data-act 속성이 있는 요소에만 리스너를 붙인다. `AttachEvent`는 같은 리스너를 중복으로 붙이지 않는다(rmlui EventDispatcher.cpp:40-52).
- data-for 복제본은 템플릿 속성을 복사해 만든 새 요소(rmlui DataViewDefault.cpp:511, 523). 템플릿에는 `data-attr-data-act`만 있으므로 복제본에 리스너가 없다.
- 그래서 문서 단위 위임 리스너가 필요하다. **capture 단계**에서 target부터 위로 올라가며 처음 만나는 data-act를 보내고 멈춘다.
  bubble 단계로 하면 options에서 리스너 없는 화살표(`o_bgm-`)를 클릭할 때 부모 행 `r_bgm`이 `o_bgm`을 먼저 처리한다.
- `ui act`는 클릭을 거치지 않고 `ui_action`을 바로 부른다(input.cpp:935-937). dump의 `[act]`(screens.cpp:528-529)와 Nav(screen_util.h:70)에는 data-act 속성 자체가 필요하다.

**모델과 생성 요소가 생기는 시점**
- 순서: `screens::update()` → `context->Update()`(ui.cpp:305-306). 데이터 모델은 `Context::Update` 안에서 갱신된다(rmlui Context.cpp:196-197).
- update()에서 모델을 바꾼 직후에는 새 data-for 요소가 아직 없다. `nav.set(GetChild(sel))`이나 data-act로 포커스를 찾는 코드는 다음 프레임으로 미뤄야 한다:
  help.cpp:88, collection.cpp:107, farm.cpp:127, skillbook.cpp:165, hotslot.cpp:66, keymap.cpp:72-75.
- data-for는 같은 인덱스의 요소를 재사용한다(DataViewFor::Update 496-543). 개수가 그대로면 포커스 포인터가 유효하다.

**템플릿 요소**: `display:none`으로 DOM에 남고(DataViewDefault.cpp:473) 복제본은 그 앞에 들어간다. dump는 보이지 않는 요소를 건너뛰고(screens.cpp:520) Nav도 IsVisible로 거른다. `GetChild(sel)` 인덱스는 템플릿이 마지막이라 맞는다.

**모델 생성 위치**: 정적 싱글턴이라 생성자 시점에는 context가 없다. 문서는 `init()`에서 불러온다(screens.cpp:381-384). `load_doc` 전에 부르는 가상 함수 훅이 필요하다. on_show 직전에 `d.invalidate()`(screens.cpp:442) — 여기에 DirtyAllVariables.

**Doc 캐시와 데이터 뷰 충돌**: `Doc::prop`/`Doc::attr`은 마지막 값을 캐시한다(screen_util.h:51-60). 같은 속성을 data-style/data-attr와 Doc가 함께 건드리면 서로 덮어쓴다 → 속성마다 주인을 하나로.
`data-if`는 인라인 `display:none`을 쓴다(DataViewDefault.cpp:224-245). 지금 `d.prop(...,"display",...)` 쓰는 곳(help.cpp:81, detail.cpp:154, helpfull.cpp:72-73, options.cpp:78)은 통째로 바꿔야 한다.

**기존 버그: 해제된 포커스 포인터**: `SetInnerRML`은 자식을 바로 파괴한다(rmlui Element.cpp:1169-1179). collection, farm, skillbook은 목록을 다시 만들 때 선택이 바뀌지 않으면 `nav.focus`를 다시 잡지 않는다 → 포커스가 파괴된 요소를 가리키고, 매 프레임 screens.cpp:427(`SetClass("focus")`)과 screen_util.h:81에서 쓴다(UAF). farm 부화 페이지는 타이머 때문에 1초마다 목록을 다시 만든다(farm.cpp:113, 121). data-for로 옮기면 사라지지만 dump의 `*FOCUS`가 기준선과 달라질 수 있다.

**텍스트 처리**
- `rml_text`는 07 색 코드를 `<span style="color:…">`로, 0A를 `<br/>`로 바꾸고 `< > &`를 이스케이프한다(state.cpp:31-60).
- `plain_text`는 이스케이프하지 않는다(state.cpp:62-75). 지금은 SetInnerRML에 들어가므로 `&`, `<`가 RML로 해석된다. `{{ }}`로 바꾸면 글자로 나오고 dump에는 `EncodeRml`이 적용된다(ElementText.cpp:445-447) → 이름에 `&`가 있으면 차이.
- `element_text()`는 `rml_text`를 돌려준다(state.cpp:555) → 반드시 data-rml.

**dump와 비교 도구**
- dump는 자식이 텍스트뿐인 요소의 텍스트만 출력한다(screens.cpp:533-538). 텍스트와 span이 섞인 요소(help `.tp` 등)의 텍스트는 비교되지 않는다.
- inline-block 형제 사이의 공백은 비교에 걸린다. 지금 C++가 만드는 마크업에는 형제 사이 공백이 없다(.seg/.arrow/.choice, keymap .c, .tp, .cl/.bk, .sl, .fn, .reel). .rml 템플릿을 줄바꿈·들여쓰기로 쓰면 공백 텍스트 노드가 생겨 좌표가 바뀐다 → 한 줄로 붙여 쓴다.

**테스트가 쓰는 data-act**: options o_bgm-/o_keys/o_close, keymap km_kb/km_attack/km_close, help t2/rescue/c_no/hclose, collection c1/close, skillbook b1/b2/close, town fn0..fn5/tw_map/tw_close, farm r1/close, hotslot s3/close, detail·qbox ok, helpfull next.

**glyph**: `glyph()`는 키보드를 친 적이 없거나 터치일 때 ""를, `glyph_ref()`는 항상 키 이름을 돌려준다(input.cpp:126-149, 958-959). 결과가 `<span class="glyph …">` RML이라 data-rml.

## 1. options (options.cpp / options.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| c_bgm, c_fx | rml `segs()` (67-68, 30-36). 값은 메인 스레드에서 게임 메모리 직접 읽기: bgm()=M8(sys+6), fx()=M8(sys+7) (22-24) | 화살표 `o_bgm-`, `o_bgm=1..10`, `o_bgm+`. class `seg`/`seg on`, 모두 data-nonav. 뒤에 `span.val` "N%" | 포커스는 행에만 | 없음 | 고정 10칸, data-for 가능. 화살표와 붙여 쓰기 |
| c_speed | rml segs (69). speed()=(M8(sys+0xb8)-11)/2 (25). 라벨 `%.2f×` | `o_speed-`, `=1..10`, `+` | 같음 | 없음 | 표시 문자열은 C++에서 포맷 |
| c_scale | rml segs (75-76). `settings::get().ui_scale`, `ui_max_scale()` | 칸 수 = (mx-80)/10+1, **창 크기에 따라 바뀜**. `o_scale=%d` | 같음 | 없음 | 칸 수가 바뀌는 목록 |
| c_vibs | rml segs (80). ps.vib_strength | `o_vibs-`, `=1..10`, `+` | 같음 | 없음 | – |
| c_loot, c_vib, c_full | rml `toggle()` (37, 70-71, 73). M32(sys+8) 비트(26-27), ps.fullscreen | `toggle`/`toggle on` + `span.k` | 행(r_loot 등)에 data-act | 없음 | data-class-on |
| c_ctrl, c_sharp | rml `choice()` (38-41, 72, 74). direct()=M32(sys+8)&8 (28), ps.sharp | `o_ctrl=1`/`=0`, `o_sharp=1`/`=0`. `choice`/`choice on`, nonav | – | 없음 | data-class |
| r_full | prop display none (Android만, 77-79) | – | – | – | 컴파일 시점 분기 |
| r_vibs | cls `dim` (81) = !vib() | – | – | – | data-class |
| foot | rml, glyph("advance"/"back") (83) | – | – | – | data-rml |
| (ss.bound) | `bind_once` (86-91) | – | – | – | 위임 리스너로 없어짐 |

Nav: on_show에서 `nav.root=#body`, `nav.set(r_bgm)` (62). 행(r_*)은 정적. on_dir: 좌우면 포커스 행의 data-act로 `adjust(row,±1)` (93-108). 스크롤: `#body` overflow-y:auto (options.rml:16). 설정은 state::post로 보냄(43-52, 100, 123) — 적용 전 한 프레임은 옛 값(지금도 같음).

## 2. keymap (keymap.cpp / keymap.rml, bindings.cpp)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| tabs | rml (47-49, 68). ss.dev, `pad_connected()`(23-28), `input::pad_is_ps()` | `km_kb`, 패드 연결 시 `km_pad`. `c`/`c on`, nonav, `span.i` | 탭은 Nav 대상 아님 | 없음 | data-if. 공백 주의 |
| rows | rml (50-62, 69-77). `bind::actions(dev)` 정적 표(bindings.cpp:10-63, 67), `bind::get` (74-78), ss.waiting | 그룹이 바뀔 때 `div.sec`(53-56)이 **행과 형제로 섞여** 나옴. `row`/`row wait`, `km_<id>`(61), `span.l`, `span.v`(glyph 또는 대기 문구) | **keep_focus**(67-75). on_show `nav.set(nullptr)` (41) | 없음 | 섹션·행 혼합 → 평평한 data-for 하나로 안 됨. 래퍼를 두면 트리 깊이가 바뀜. glyph data-rml |
| ktoast | rml (63). ss.toast, ss.toast_until (2.5초, 102) | – | – | `&#8594;`와 glyph RML (101) | 시간 비교는 update()에서 |
| note | rml (79). 기기별 고정 문구 | – | – | – | data-if 둘 |

영구 멤버 `open`, `open_dev`(20-21). `begin_capture`가 update() 밖(입력 처리 중)에서 ss를 바꿈(91-104) → 모델은 update()에서 ss를 보고 반영. KB↔PAD 전환 시 행 수·내용이 바뀌고 data-for 재사용 때문에 같은 인덱스에 포커스가 남음 → 다음 프레임에 data-act로 다시 찾기.

## 3. help (help.cpp / help.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| body 클래스 page / confirm | cls d.doc (71-72). ss.page, ss.confirm | – | nav.root = cbox / pagewrap / listwrap (85) | – | body에 data-model, 문서 수준 data-class |
| list | rml (73-77). **영구 멤버 `topics`**("원작 자원에서 한 번 읽어 유지", 25-29). 게임 스레드에서 채우고 mutex·atomic으로 넘김(43-62, 67) | `div.tp`/`tp sel`, `t%d`, `<span class="n">%02d</span>`+제목. 비면 `div.none` "불러오는 중…" (76) | 키(page/confirm/size)가 바뀔 때만 다시 그림(68-70). `GetChild(ss.sel)`(88), 없으면 ensure(89) | 제목은 plain_text (53) | `%02d` 미리 포맷 |
| ptitle | rml (79) | – | – | plain_text | & 차이 주의 |
| pbody | rml (80) + `SetScrollTop(0)` (82-83) | – | **on_dir** 위아래 80dp 스크롤(118-125) | **rml_text(body, true)** (54) | data-rml. 스크롤 초기화가 data-rml 반영(다음 Context::Update)보다 먼저 일어남 |
| rescue | prop display (81) | – | 페이지 Nav 후보 | – | data-if |
| c_no / c_yes / hback / hclose | 정적 | – | confirm이면 c_no 포커스(87) | – | – |

## 4. collection (collection.cpp / collection.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| title | rml (76). `pull_collection().title = element_text(M32(f+0x58))` (33, 55, 70) | – | – | **rml_text** | data-rml |
| prog, pfill | rml `"%d / %d"` (79), prop width % (80) | – | – | – | data-style-width |
| grid | rml (81-89). c.e[] {reg, owned}, c.sel, c.icon | `div.cl` + ` sel`/` reg`/` own`, `c%d`. `img` + `span.tag`/`span.tag new` | 선택이 바뀔 때만(ss.last) `GetChild(sel)`(102-109). **같은 선택으로 다시 만들면 해제 포인터** | – | data-if 둘 |
| det | rml (90-101) + bind_actions. name(plain, 43), desc(rml_text, 45), l_reg/l_unreg(plain, 35-36), complete | 조건부 `unreg`(btn)/`reg`(btn primary) (97-98) | 해제 포인터 위험 | **e.desc rml_text** | data-rml, data-if |
| hint | rml glyph (110) | – | – | – | data-rml |

이미 선택된 칸에 `c%d`를 다시 보내면 reg/unreg(114-118) — 의미 유지.

## 5. farm (farm.cpp / farm.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| kicker, title | rml (97-100). fm.mode, fm.title = lang(0x26c+mode) (24, 34) | – | – | plain | – |
| list | rml (101-121). `pull_farm()` rows{name, icon, status, food_icon, captured, need, fed, food, timer, egg} (19-73, 89) | `div.row`/` sel`/` full`, `r%d`. `.ic img`, `.nm`, `.cap .bar .fill style="width:%d%%"`, `span.v "%d / %d"`, `.st`[img.fi + status + ` <span class="tm">h:mm:ss</span>`], 조건부 `span.go`. 비면 `div.none` + fm.empty | 선택이나 ptr이 바뀔 때만(122-129). **부화 페이지는 1초마다 다시 만들어 해제 포인터**(113) | status는 C++ 조립(58-65) | data-style-width, data-if 여러 개, `.st` 혼합 텍스트, 타이머 미리 포맷 |
| hint | rml glyph (130) | – | – | – | data-rml |

## 6. skillbook (skillbook.cpp / skillbook.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| body 클래스 page | cls d.doc (116-117). sb.popup | – | root = pbox 또는 main (162) | – | 문서 data-class |
| grid | rml (118-126). `pull_skillbook()` books{owned, mastered, current}, sel, icon | `div.bk`/` sel`/` no`, `b%d`, img, `span.tag done`/`span.tag cur` | key = page ? 1000+page : sel. 바뀔 때만 GetChild(sel) (159-167) | – | data-if |
| det | rml (127-137). name(plain, 50), lang(0x296-0x298) 정적 캐시(40-43), pages | `.bigic`, `.dn`, `.st s-no/s-read/s-done`, `.pc "%d장"`, `.ds`, owned면 `open`. 없으면 `div.none` | – | **k.summary rml_text** (51, 134) | data-rml |
| ptitle | rml (141-143). 이름 + `<span class="pn">%d / %d</span>` | – | – | – | 혼합 |
| ptext | rml (144) | – | – | **rml_text** (69) | data-rml |
| porb | rml (145-154) | 정적 분기 | – | – | data-if, data-style-width |
| pprev, pnext, pinv | cls `off` (155-157) | – | **`off`는 Nav에서 빠짐**(screen_util.h:70). page면 pnext/pinv 포커스(164). on_dir 좌우(201-204) | – | data-class-off |
| hint | rml glyph (168) | – | – | – | data-rml |

## 7. town (town.cpp / town.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| tname | rml (34). s.field_name (plain) | – | – | plain | – |
| grid (fn0..5, npc0..5) | rml **세션당 한 번**(ss.built, 35-46). 정적 표 FN_T/FN_D/FN_I (13-15) | `div.fn` id·data-act `fn%d`, `img.npc#npc%d`, `span.ic`, `.t`, `.d` | root=d.doc, fn0 포커스(44-45) | – | 6개 고정 → 정적 RML로 충분 |
| npc%d src/width/height | attr src, prop width/height dp (64-69). `recorded()` 프레임에서 셀 안 가장 큰 세로형 스프라이트(47-63) | – | – | – | 계산은 C++에 두고 결과만 바인딩 |
| side, hint | 정적 / glyph | – | – | – | – |

## 8. detail (detail.cpp / detail.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| icon | attr src (141). dt.icon | – | – | – | 폼 ptr마다 한 번(ss.drawn, 139-140) |
| name | rml (142) | – | – | plain | – |
| sub | rml (143-146). type + " · grade" + `<span class="bad/ok">cls</span>` | – | – | cls는 wordimg | 혼합, C++ 조립 |
| rows | rml (147-152). rows{k, v, bad}, gems, mats | `.pr`, `.gem`, `.sec "재료"`, mats `.gem`. data-act 없음 | – | – | data-for 넷 + data-if 하나로 순서 유지 |
| desc | rml (153) + prop display (154) | – | **on_dir** 위아래 ±60 **px**(160-163) | **rml_text** (40, 96, 104) | data-rml, data-if |
| ok, scrim | 정적. scrim은 data-act ok, nonav | – | root=box, ok 포커스(155-156) | – | – |

## 9. hotslot (hotslot.cpp / hotslot.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| kicker, title, body, btn | rml (40-43, 69). kind/mode는 update()에서 **폼 메모리 직접 읽기** M32(f+0x168/0x1b4) (34-39) | – | – | – | 모델에 복사 |
| slots | rml (45-61). s.menu.hot[i]{kind, icon}, sel = M16(il+0x32) | `div.pg` 둘 + `span.pl "SET A/B"`, `div.sl`/` sel` `s%d`(p*5+c), img, `span.k` glyph(`slot%d`) | key = g+sel 바뀌면 다시 만들고 `s<sel>` 찾아 포커스(58-67) | – | 중첩 data-for(2×5). 포커스 찾기는 다음 프레임 |
| cur | rml (62) | – | – | plain | – |

## 10. qbox (qbox.cpp / qbox.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| bname, bicon, bcost | rml, attr, rml (36-40). s.qbox.box, box_icon, cost/gold | – | – | plain | – |
| r0..r2 / r0img..r2img | attr src (45), cls spin/stop/win/lose (46-49) | 정적 셋 (qbox.rml:43) | – | – | **75ms 타이머**(ss.flip_at/flip, 35). spin[(flip*7+i*13)%n] (43). `stop`이 붙는 순간 land 애니메이션(qbox.rml:25) |
| body 클래스 done | cls d.doc (54) | – | done이면 root=box, ok 포커스(68-70) | – | `body:not(.done) #ok` (qbox.rml:35) |
| rtitle, rsub | rml (55-66) | – | – | **q.msg = element_text → RML** (dialogs.cpp:136). `<span class="got">획득</span>`+이름 | data-rml |

## 11. helpfull (helpfull.cpp / helpfull.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| frame / card | prop display (72-73). `show_card()` = M32(ptr+0x5c)==0 && !touch (47) | – | root=d.doc, next 포커스(33) | – | 기기에 따라 카드/그림 |
| card | rml (48-66), ss.card_key 캐시. **glyph_ref** 여러 개, 패드 판정은 stick glyph의 "bumper"(52, input.cpp:134) | 고정 8행 `.cr > .ck + .cw` | – | – | data-rml 가능 |
| card / next 위치 | prop left/top px (76-82). win_w/h, dp, **GetOffsetHeight/Width(레이아웃 결과)** | – | – | – | 레이아웃 뒤 계산 좌표 |
| pic, frame | attr src, prop width/height/left/top (95-117). 게임 스레드가 만든 키 `sprites::grid(0x4a, ...)`를 static g_key+mutex로(45-46, 92-94). 정수 배율 k (102-103), 오프셋 (108-111) | – | – | 원작 그림 | 창 크기 기하 |

## 12. autoscreen (autoscreen.cpp + autoform.cpp / auto.rml)

| 요소 id | 설정 방식 · 출처 | 반복 목록 | Nav | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| root | rml 하나로 통째(216-219, ss.last 비교). on_hide에서 비움(36). 출처 `recorded()` autoform::Frame (autoform.h:13-48), 게임 스레드 `end_frame`(autoform.cpp:315-381) | 폼마다 `af-dim`, `af-form/af-panel/af-btn/af-cell` + on/off/sel/af-focus (105-113), `af-clip`(143-144) 안 img `af-s`(182-183), `af-w`(123-124), `af-t`(187-188), `af-n`(196-197), `af-filter`(137), `af-ic`(209-213). data-act 없음 | modal()=false (28), Nav 없음, pointer-events:none (auto.rml:6) | **c.text = rml_text(s, false)** (autoform.cpp:213) | 모든 left/top/width/height px가 C++ 계산(X()/Y(), clip 상대 좌표, 글자 크기, 숫자 너비, 스프라이트 오프셋). 색은 픽셀 평균(53-79). **실시간 텍스처** `live:cv%d` (97-101). 휴리스틱(155-179). EH_SPRDUMP (181) |

## 판정

| 화면 | 난이도 | 근거 | 판정 |
|---|---|---|---|
| detail | 하 | 폼마다 한 번 그림, 행에 액션 없음, desc만 data-rml | 이전 (시범 1) |
| qbox | 하 | 고정 3릴, 75ms src 갱신은 변수 하나, rtitle data-rml | 이전 (시범 2) |
| hotslot | 하~중 | 고정 2×5 중첩, glyph data-rml, 포커스 찾기 다음 프레임 | 이전 |
| town | 하~중 | 카드 6개 정적화, NPC 계산은 C++ | 이전 |
| options | 중 | 칸 수가 바뀌는 목록, 화살표 중첩 → capture 위임 리스너 필수, on_dir | 이전 (위임 리스너 검증용) |
| help | 중 | 영구 topics·스레드 경계, 문서 클래스 둘, root 전환, 스크롤 초기화 순서 | 이전 |
| collection | 중 | title·desc RML, 조건부 버튼, 같은 칸 재클릭 의미 | 이전 |
| farm | 중 | 행 내부 조건 많음, 혼합 텍스트, 1초 타이머, UAF 해소로 FOCUS 변화 가능 | 이전 |
| skillbook | 중~상 | 목록·팝업 두 뷰, `off`가 Nav에 영향, key 기반 포커스 | 이전 |
| keymap | 상 | 섹션·행 혼합 순서, update() 밖 ss 변경, 기기 전환 재사용, 시간 토스트 | 이전 (마지막) |
| helpfull | 상 | GetOffset* 기하, 게임 스레드 텍스처, 기기 분기 | 제외 (카드 8행만 부분 이전 가능하나 이득 적음) |
| autoscreen | 불가 | 매 프레임 생성 레이아웃, 중첩 clip, 실시간 텍스처 | 제외 확정 |

## dump를 바꿀 수 있는 위험

1. 공백: inline-block 형제 사이 공백 → 템플릿은 한 줄로.
2. 요소 순서: keymap sec·행 혼합(keymap.cpp:53-61)은 평평한 data-for로 재현 불가. detail rows→gems→sec→mats(148-151)는 data-for 넷으로 유지. options 화살표·칸·화살표·val(31-34)은 유지됨.
3. id·data-act: 생성 id `fn%d`·`npc%d`(town.cpp:38), `r0..r2`/`r0img..`(qbox.rml:43) 유지. data-act는 `data-attr-data-act`로.
4. 클래스 순서는 compare.py가 정규화. `focus`는 Nav·screens.cpp:427이 직접 넣으므로 data-class로 묶지 않는다.
5. 텍스트: `{{ }}`는 plain_text를 이스케이프(`&`). `%02d`, `%d / %d`, `%.2f×`, `h:mm:ss`, num()의 쉼표는 C++에서 만든 문자열을 바인딩.
6. 시점: 생성 요소는 다음 Context::Update에 생김 → 바뀐 직후 dump에는 `*FOCUS`가 없을 수 있음(시나리오는 300ms 이상 대기). help `SetScrollTop(0)`(83)은 data-rml 적용 뒤에.
7. 포커스 동작: collection/farm/skillbook은 지금 해제 포인터로 포커스가 사라지지만 이전 후 유지 → 사유 기록. keymap/hotslot은 재사용 때문에 같은 인덱스에 남을 수 있으니 data-act로 다시 찾기.
8. 리스너: bubble 위임이면 options 화살표 클릭이 행 액션이 됨 → capture 단계.
