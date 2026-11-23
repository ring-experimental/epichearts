# 조사 B: dialogs, bagbuy, charselect, smithy, map, store

(Explore 서브에이전트 보고, 2026-10-21. 리팩터 후 코드 기준. 경로는 저장소 루트 기준. "probe/g_all"은 시나리오를 고치기 전의 시험 덤프이고, 그때 빠졌던 화면은 golden에 다시 떴다.)

## 0. 공통 구조 (screens.cpp)

- `screens::init()`(screens.cpp:374-388)이 화면마다 `load_doc(sc->d, sc->file())` → `nav.root = d.doc` → z 순서로 `PullToFront`.
- `load_doc`(361-367): `LoadDocument(asset_path(file))` → `Show(None, None)` → `bind_actions(doc)`. 문서는 계속 Show 상태이고 표시/숨김은 body 클래스 `on`(`body{display:none} body.on{display:block}`, 441).
- 매 프레임(419-454): 모든 문서에 `touchin` → `nav.focus->SetClass("focus", !touch)`(427, 매 프레임 포인터 역참조) → want()가 바뀌면 켜질 때 `d.invalidate(); on_show()`, 꺼질 때 `nav.clear(); on_hide()` → 보이면 `update(s)`, modal 중 마지막이 `g_active` → 다시 PullToFront.
- 액션: `ActionListener`(45-57)는 StopPropagation 후 `input::ui_action(data-act)`. `ui act X`(input.cpp:935-937)는 요소를 거치지 않으므로 테스트는 action() 문자열 파싱에만 의존. `screens::action`(456-460)은 registry 순서로 처리하는 첫 화면.
- 패드/키: South `nav.ensure(); activate()`, East/Start `back()`, LB/RB `tab()`, 102/103 `subtab()`(462-471). `pad_dir`(472-478)는 on_dir → nav.move → `Nav::scroll`. Nav(screen_util.h:65-157)는 data-act가 있고 data-nonav·`.off`가 아닌 보이는 요소만(70).
- 호출 순서: ui.cpp:305-306 `screens::update()` → `context->Update()`, 모델 갱신은 Context.cpp:196-197. data-for 요소는 update()가 끝난 뒤 생긴다 → 같은 update()에서 새 자식에 nav.set/bind_actions 불가. `DataModelHandle`에는 공개 Update가 없다.
- 기존 버그(기준선에 이미 나타남): `Doc::rml`의 SetInnerRML로 자식이 즉시 해제(Element.cpp:1169-1179, 1452-1468)되고 풀로 돌아가 재사용(ElementInstancer.cpp:12, 25-27). store와 smithy는 목록을 다시 만든 뒤 nav.focus를 고치지 않아 focus가 재할당된 엉뚱한 요소를 가리킨다(menu.cpp:415는 `nav.focus=nullptr` 후 set으로 피함). 증거: 시험 덤프 smithy m0_l1(`ui act l1` 뒤 focus가 `l18`, y=2054 화면 밖), store buy1_cat1(`cat1` 뒤 focus가 `cat3`). 이전하면 정상 쪽으로 바뀐다 → diff 사유로 미리 적는다.
- 덤프: 문서는 `GetSourceURL()`(590), 활성 화면은 `file()`(578). display:none은 출력 안 함(520) → data-for 템플릿은 안 나옴. 텍스트는 leaf만(533-551).
- data-for(DataViewDefault.cpp:474, 496-543): 템플릿은 display:none으로 컨테이너 마지막 자식에 남고 생성 요소는 그 앞에 들어간다 → `GetNumChildren()`/`GetChild(last)`에 기대는 코드가 깨진다(dialogs, bagbuy, smithy).
- data-if(224-245): 로컬 display를 지우는 방식 → CSS 기본값이 display:none인 요소에 쓰면 영영 안 보인다.
- `data-act`, `data-nonav`, `data-block`은 instancer가 없어 무시된다(Factory.cpp:236-253, 580-587) → `data-attr-data-act="'i'+it_index"` 가능.
- 공백만 있는 텍스트 노드는 버리지만(Factory.cpp:338-341) 내용 옆 공백은 남는다 → 인라인 버튼 사이 공백이 x 좌표를 바꾼다.
- `{{`가 들어간 텍스트는 모델 안에서 식으로 해석된다(Factory.cpp:343-365).
- DataTypeRegister는 context에서 공유(Context.cpp:1059-1063). 같은 Struct/Array를 두 화면에서 등록하면 "already declared"(DataModelHandle.h:150-165) → 등록을 한 곳으로.
- `std::vector<bool>`(Dialog::item_off)은 ArrayDefinition으로 묶을 수 없다.
- 문자열: `g::rml_text`는 RML(색 span, `<br/>`, `&lt;` 등), `g::plain_text`는 `<`/`&`를 이스케이프하지 않음, `element_text`(state.cpp:540-555)는 rml_text. 결론: 지금 `d.rml`로 들어가는 문자열은 출처와 상관없이 data-rml로 옮겨야 덤프가 같다.

## 1. dialogs (dialogs.cpp, dialog.rml)

| id | 설정 방식 / 출처 | 반복 목록 | Nav·keep_focus | 원작 RML | 바인딩에 안 맞는 부분 |
|---|---|---|---|---|---|
| `#kicker` | rml 매 프레임. 리터럴 표 `KICK`, `RK[kind]`(346-350) | – | – | 없음 | id/kind 분기 |
| `#title` | rml + prop display. `s.dialog.title`, 0x14에서 비면 "지금 쓰러졌습니다"(351-354) | – | – | **예.** id1 `element_text(+0x50)`, id3/0xf `children_text`, 0x13 `element_text(+0x54)`(226, 256, 271, 292) | data-if 가능 |
| `#sub` | rml + prop display. `dg.icon`+`dg.subtitle` → `<img src=sprite:…/><span>…</span>`(358-362) | img, span | – | subtitle은 plain_text(pool, GetTitle, 스킬명 + "  Lv %d / %d"; 55, 73, 78, 91) → data-rml | **CSS `#sub{display:none}`**(dialog.rml:15) → data-if 쓰면 영영 숨김. data-style-display 또는 CSS 수정 |
| `#body` | rml + prop display. `dg.body`(355-356) | – | Nav 스크롤 대상(screen_util.h:139-146) | **예**(아래) | data-if 가능 |
| `#rows` | rml + prop display. `dg.rows_title` + `dg.rows[]{icon,text}`(363-369) | `div.rh`, `div.rw > img + span`, data-act 없음 | 스크롤(max-height 260dp) | rows_title은 "보상" 또는 `lang(0x1df)`(rml_text). row.text plain(`reward_row` 26-37) | **CSS `#rows{display:none}`**(dialog.rml:17) |
| `#items` | rml, **ss.key가 바뀔 때만**(371-410) + prop display + bind_actions. id3/0x13 `dg.items`, `dg.item_off`. 0x14는 리터럴 4행 + `dg.count`, `s.gold`, `dg.extra` | id3/0x13: `div.it[.off] data-act="i%d"` = `<span class="n">i+1</span>` + 원작 텍스트(375). 0x14: `rgold`(.off 골드 부족, `span.s` "N G"), `i1`(.off 0개, "보유 N"), `i2`, `i3` | fresh일 때 `nav.root=#box`, `set(nullptr)` 후 `GetChild(sel)`(sel = dg.selected를 자식 수로 자름). 0x14는 extra>0이면 1, 살 수 있으면 0, 아니면 2(418-430) | **예.** id3 `rml_text(entry+4)`(259), 0x13 셀의 첫 element_text(302-311) | 섞인 내용 → 행은 data-for, 안쪽은 data-rml. vector<bool>. GetChild/GetNumChildren이 템플릿에 걸림. ss.key에 항목 텍스트·selected가 빠져 있음(340-343) |
| `#count` | cls on(id==0xf) + rml. 값이 바뀌면 bind_actions. `dg.count`, `dg.count_max`(411-417) | 고정 5칸: `span.b` c-10, c-1(nonav), `span.v` %d `<span class="m"> / %d</span>`, c+1, c+10(nonav) | on_dir 좌우 → adjust(±1)(434-437) | 없음 | 정적 RML + data-rml로 쉬움 |
| `#btns` | rml + bind. id1 `dg.buttons`(mode별), 기본 확인/취소/아니오/예/퀘스트 포기(387-403) | id1: i=2→0 **역순** `span.btn[.primary] data-act="b%d"`. mode0 b0, mode1 b1·b0, mode2 b2·b0(392). 0xf: b2(취소), b0(확인 primary). 3/0x13: `cancel`(닫기) | 항목이 없으면 **마지막 자식(primary)**에 focus(427-428) | 캡션은 element_text(btn) → RML(250) | GetChild(last)가 템플릿에 걸림. 역순 생성 |
| `#hint` | rml 매 프레임, glyph(advance/back)(431) | – | – | glyph | 장치 의존 |
| `#box` | nav.root(419) | | | | |

- Session `ss.key`(324). 지속 멤버 `ptr`, `id`는 snapshot 거울("a new one refocuses", 326-327), `fresh = dg.ptr != ptr`. Pending 없음.
- #body 출처(전부 data-rml): id1 kind 0/9 `rml_text(+0xD8)`(kind 9는 쪽 배열, 228-233; 유료상점·네트워크 문장 치환 237-244), kind 2/3 `labels_in(+0x94)`(element_text를 `<br/><br/>`로, 42-50, 67, 76), kind 4/5 `rml_text(pool)`(kind 4는 lang 0x1de/0x119 붙음, 79-80), kind 6 `rml_text(+0xD8)`(92), kind 8 비움(105), id3 `children_text(+0x54)`(257), 0xf `element_text(+0x58)`(272).
- kind/mode별 요소:
  - id 1 cFormNotice: kind 0/9 kicker "알림", title, body, btns(mode 0/1/2), hint · kind 1 퀘스트 완료 / kind 2 상세: sub, rows("보상"), kind 2는 body=라벨, b2 기본 캡션 "퀘스트 포기" · kind 3 스킬: sub(icon + "Lv a / b"), body · kind 4 축복: sub, body + 레벨 문장 · kind 5: sub, body, rows(재료, lang 0x1df) · kind 6 칭호: sub, body · kind 8 던전 보상: rows, body 없음
  - id 3 cFormChoice: kicker "선택", title, body, items `i%d`, btns `cancel`
  - id 0xf 수량 팝업(kind 3/4/5/7/10): kicker "수량", title, body, count, btns b2/b0, 좌우 키로 수량
  - id 0x14 부활: kicker "전투 불능", title, items rgold/i1/i2/i3, 버튼·취소 없음(492)
  - id 0x13 링크스톤: kicker "링크스톤", title, items `i%d`, btns `cancel`
- 난이도 **상**: 다섯 form × kind/mode, 섞인 내용, child 인덱스 focus, CSS 기본 none 두 곳, vector<bool>, 키 기반 재생성, bagbuy와 파일 공유. store·smithy·qbox·inn 흐름에서 가장 자주 쓰여 깨지면 영향이 크다.
- 하위 순서: (1) kicker/title/body/hint, #sub·#rows는 CSS display:none 먼저 정리 (2) #count 정적 RML (3) #rows data-for (4) #btns·#items: 행 껍데기 data-for + data-class-off/primary + data-attr-data-act, 안쪽 data-rml (5) focus 복원을 "다음 프레임에 data-act로 찾기"로, 0x14 sel 규칙 유지.
- 위험: 버튼 역순, 인라인 `span.btn` 사이 공백, `.it` 안 텍스트를 span으로 감싸면 leaf 줄이 생김, focus 다음 프레임 지연. data-act `i%d`, `b%d`, `rgold`, `cancel`, `c±N` 유지(테스트가 `ui act i0`, `b0`, `cancel` 사용).

## 2. bagbuy (bagbuy.cpp, dialog.rml을 함께 씀)

- `file()`이 두 화면 모두 `"dialog.rml"`(dialogs.cpp:331, bagbuy.cpp:29). `screens::init`이 화면마다 `load_doc`을 부르므로 **별개의 ElementDocument 두 개**(RmlUi는 문서를 캐시하지 않음, Context.cpp:270-311; 캐시는 스타일시트뿐). 각자 Doc 캐시와 Nav. z: dialog 50, bagbuy 52. 둘 다 modal → 동시에 보이면 bagbuy가 g_active. 덤프에서 둘 다 `== document …/dialog.rml`, 활성 화면도 `dialog.rml` → 순서로만 구별.

| id | 설정 방식 / 출처 | 반복 | Nav | 원작 텍스트 | 안 맞는 부분 |
|---|---|---|---|---|---|
| `#kicker` | rml "가방 확장" | – | – | 없음 | – |
| `#title` | rml + prop display block. ss.done, s.bag_cap, SLOTS, maxed(`s.bag_cap >= s.bag_max`) 세 갈래(45-61) | – | – | 없음 | 3분기 |
| `#body` | rml. `num(price)`(cash::harz_to_gold(30)), bag_cap, bag_max, gold + 포트의 `<span style="color:…">`(55-57) | – | – | 마크업 → data-rml | – |
| `#count`, `#items` | prop display none(42-43) | – | – | – | 문서가 따로라 사실상 불필요 |
| `#sub`, `#rows` | 건드리지 않음(CSS 기본 none) | | | | |
| `#btns` | rml + bind. 확인 `bb_close`, 확장 `bb_close`(취소) + `bb_buy`(primary, 못 사면 .off)(48, 52, 58-59) | 1~2개 | `nav.root=#box`, set(nullptr) 후 **마지막 자식**(66-69) | 없음 | GetChild(last) |
| `#hint` | rml "" | | | | |

- 다시 그리는 조건 `ss.drawn` = "%d/%d/%d/%d"(done, cap, afford, maxed)(38-40). `g_open`은 파일 static(열기 요청이 화면이 보이기 전에 오기 때문, 25), on_hide/back에서 false. `ss.done==2`는 설정되는 곳이 없다. done=1 직후 snapshot bag_cap이 옛 값일 수 있음(Pending 없음).
- 난이도 **하**. 문제는 파일 공유 하나.
- (시험 덤프 당시 `ui act bag_expand`가 unhandled였다 — menu가 보일 때만 처리, menu.cpp:444. golden은 HUD의 `ui act bagbuy`로 뜬다.)

## 3. charselect (charselect.cpp, charsel.rml)

| id | 설정 방식 / 출처 | 반복 | Nav | 원작 텍스트 | 안 맞는 부분 |
|---|---|---|---|---|---|
| `#cards` | rml **열 때마다 한 번**(ss.built) + bind(48-59). `s.charsel.name[i]`, `desc[i]` | `for i<3`: `div.cc#cc%d data-act="cls%d"` > `.stage > img#spr%d`, `.floor`, `.badge` "SELECTED", `.nm`, `.ds` | 처음 만들 때 `nav.root=d.doc`, cc(sel 또는 0) | name `plain_text(gCharNpcName)`, desc `rml_text(lang 0x2d9+i)`(dialogs.cpp:213-214) → data-rml | 3장 고정 → 정적 RML이 맞음 |
| `#cc0`~`#cc2` | cls sel 매 프레임, `s.charsel.selected`(65) | – | focus가 옮겨가면 `ask_class(i)`(62-64), 좌우는 nav.move | – | 표시는 원작 값. `ss.cls_req`(Pending)는 중복 요청 방지용 |
| `#spr0`~`#spr2` | attr src + prop width/height/left/top(dp)(91-104). `recorded()`에서 버튼 rect 안 가장 큰 스프라이트, 선택 안 된 카드는 `ss.seen` 최빈 자세 | – | – | – | **라이브 애니메이션 스프라이트, C++ 좌표** → 제외 |
| `#hint` | rml glyph(lr, advance, back) + `c.can_cancel`(66-68) | – | – | glyph | – |
| `#b_start` | cls off(sel<0), 정적 `cs_start` | – | nav 대상 | – | – |
| `#b_cancel` | prop display(can_cancel), 정적 `cs_cancel` | – | nav 대상 | – | data-if |

- Session: built, seen[3], deselected[3], last_sel, `Pending<int> cls_req`(30프레임, 39). on_hide에서 `cls_req.clear()`. ptr, sel은 snapshot 거울. action(117-128): `cls%d` 같은 카드면 press(0x68), 아니면 nav.set + ask. back()은 can_cancel일 때만.
- 난이도 **중**. 카드 3장 정적화, `.nm`/`.ds` data-rml, sel data-class, hint data-rml, b_cancel data-if. `#spr%d`는 Doc::attr/prop 유지.

## 4. smithy (smithy.cpp, smithy.rml)

| id | 설정 방식 / 출처 | 반복 | Nav | 원작 텍스트 | 안 맞는 부분 |
|---|---|---|---|---|---|
| `#kicker` | rml `KICK[m.mode]`, mode2/sub1이면 "RESTORE"(208-209) | – | – | 없음 | – |
| `#title` | rml `m.title` | – | – | **element_text(+0x58)**(82) | – |
| `#gold` | rml `num(m.gold)`, capture `inv+0x48`(87) | – | – | – | – |
| `#list` | rml + bind, 바뀔 때마다(214-223). `m.list[]`, `m.list_sel` | `div.row[.sel] data-act="l%d"` > img, `.nm`, `.ty`(타입 · 미식별 / +N / 내구 a/b). 비면 `.none` "대상 아이템이 없습니다". 피커가 닫혀 있으면 `.none > span.btn data-act="pick0"` "다른 장비 선택" | fk가 바뀔 때만 `GetChild(list_sel)`(304-306). **list_sel만 바뀌어 다시 만들면 남은 포인터** | 이름·타입 plain(56-57) | 세 형태, GetChild 인덱스 |
| `#slots` | rml + bind, **mode마다 구조가 다름**(226-255), body 클래스 `noslots`(254) | mode0: `slot_html(slots[0],"장비","pick0")` + `span.plus` + `slot_html(slots[1],"강화석","pick1")`. mode1: `pick0` + `.scrolls > div.sc[.empty][.sel] data-act="sc%d"`(4칸) + `.addsc > span.btn.small[.off] add0/add1`(img, 이름, `span.n`) + `rmsc`. mode5: 대상 + `div.sc data-act="gem%d"`(≤4). mode 2/3/4: 비어 있음 | nav 대상 | slot 이름 plain, 스크롤 이름 plain | `.slot`은 sel·empty, `.sc`는 empty·sel 순(반대) |
| `#det` | rml + bind(257-277). mode별 cur, m.hint, 비용, 제련석, 용비늘 | `.bigic img`, `.dn`, `.dt`, `.hn`, `.props > div(k, v 또는 bad)`. mode1 제련석·단계, mode5와 2/1 용비늘, 그 밖 비용. `.acts`: mode2/0이면 `doall`, 늘 `do`(DO[mode]) | nav 대상 | **m.hint = element_text**(85). dn/dt plain | 줄 수가 mode에 따라 다름 |
| body 클래스 `refining`, `rdone`, `rask` | `d.cls(d.doc, …)`(280-283), m.rpop/rdone/rask | – | – | – | body data-class |
| `#rbody` | rml(284-293) | rask `.rq`. 진행 `.rt` "제련 중…" + `.rbar > .rfill`(CSS 무한 애니메이션). 결과 `.rt.{ok,bad,warn}` + `div.rr`(k, o, ar, n) × rrows | – | rtitle element_text(+0x58)(134, 137), 행 이름 wordimg::word | 3단계 |
| `#ryes`, `#rno`, `#rok` | 정적, CSS 클래스로 표시(smithy.rml:83) | – | `popup_focus()`가 매 프레임 ryes/rok 유지(rno 허용)(313-317) | – | – |
| `#hint` | rml glyph | | | glyph | |

- Session: `shown_frames`(4프레임 뒤 피커 자동 열기 타이머, 206), `fkey`(296-308). 지속 멤버 `opened_for`(피커는 원작 form마다 한 번만, dialog_over로 다시 보일 때 또 열지 않음, 169). 매 프레임 `m = pull_smithy()`(195). fk = mode/picker/list.size/rpop rdone rask가 바뀌면 root를 #main/#rbox, set(nullptr), 팝업이면 ryes/rok 아니면 `list.GetChild(list_sel)`, ensure. `#tr` close는 nonav. Pending 없음.
- 난이도 **상**: mode 7가지(0, 1, 2/0, 2/1, 3, 4, 5), 팝업 3단계.
- 하위 순서: (1) kicker/title/gold/hint/body 클래스 (2) #rbody data-rml 하나 (3) #list data-for(none/pick은 data-if 형제) (4) #det, #slots는 처음엔 inner 문자열을 data-rml로 통째로.

## 5. map (map.cpp, map.rml)

| id | 설정 방식 / 출처 | 반복 | Nav | 원작 텍스트 | 안 맞는 부분 |
|---|---|---|---|---|---|
| `#map` | rml(ss.last 비교) + bind_actions(box)(102-137). m.rooms, m.aisles, m.sel, m.quest_group, m.cur_group/sub(매 프레임 `pull_map()`, 87) | 통로 `div.ai[.shut/.red]`(data-act 없음), 방 `div.rm[.sp][.cur][.sel] data-act="room%d"` + `span.ic`(0x40), `span.qm`, `span.me`. inline `style="left/top/width/height:%.1fpx"` | **modal()=false**(85), Nav 안 씀, 키는 원작 canvas로. room은 클릭 전용 | 없음 | **순수 좌표 계산**: 범위(94-101), `#map` GetClientWidth/Height 피드백으로 배율 k(106), ox/oy, `rect()`. **일반 데이터**: 클래스, data-act, 아이콘 span 여부, 방문 필터(0x20) |
| `#zoom` | cls on = m.zoomed | – | – | – | – |
| `#zimg`, `#zbox` | attr src "live:mapzoom" + `present::live_update`(ss.seq), prop width/height/margin(139-167) | – | – | – | **라이브 텍스처 + 좌표** → 제외 |
| `#lg_q`, `#lg_s`, `#lg_p`, `#legend` | prop display inline/none, block/none(170-177) | – | – | – | CSS 기본(span inline, div block)으로 돌아가므로 data-if 가능 |
| `#title` | rml m.sel_name 또는 s.field_name | – | – | sel_name = plain_text(element_text(+0x68)) → **RML**(61) | – |
| `#kick`, `#zlabel` | rml 리터럴(zoomed) | | | | |
| `#hint` | rml glyph(move, map, back) | | | glyph | |

- Session: last, seq. 정적 data-act `zoom`, `close`(map.rml:57). action `room%d`: 같은 방이면 0x2b(확대), 아니면 form+0xA8 + FocusTheSelectedMap.
- 자르는 위치: `#map` 생성(102-137)과 zoom 이미지·상자 크기(141-167)는 **제외**, 나머지(title, kick, zlabel, hint, zoom 클래스, 범례 4개)만 이전.

## 6. store (store.cpp, store.rml)

| id | 설정 방식 / 출처 | 반복 | Nav | 원작 텍스트 | 안 맞는 부분 |
|---|---|---|---|---|---|
| `#title` | rml. 튜토리얼이면 `st.title`(plain_text(lang 0x243)), 아니면 "구매"/"판매" | – | – | plain → data-rml 권장 | – |
| `#kicker` | rml, st.tutorial/st.bought/buy(`st.mode != 10`)로 리터럴 | | | | |
| `#gold` | rml `num(s.gold)`(snapshot) | | | | |
| `#cats` | rml + bind(107-113) | `span.sb`(glyph tabl), `span.c[.on] data-act="cat%d"` × 4(CB/CS), `span.sb`(glyph tabr). 튜토리얼이면 비어 있음 | `tab(±1)`이 `cat%d`(170) | glyph | cat을 바꾸면 다시 만들어 focus 별칭(시험 덤프에서 cat1 뒤 cat3) |
| `#list` | rml + bind(114-126). st.rows[], st.sel, s.job, s.level | `div.row[.sel][.bad(구매만)] data-act="r%d"` > img, `.nm`, `.ty`(타입 · Lv N), `.pr`(가격 `<span class="g"> G</span>`), 판매이고 count>1이면 `span.cnt` "xN". 비면 `.none` "항목이 없습니다" | 매 프레임 `nav.ensure()`만(139), 다시 만들면 남은 포인터 | name, type_name plain(53-54, 60, 65) | 같은 행 두 번이면 deal |
| `#det` | rml + bind(127-137) | `.bigic img`, `.dn`, `.dt`, `.props`(가격/판매가, 필요 레벨, 보유 N개), `.ds`, `.acts > span.btn.primary data-act="deal"`. 선택 없으면 `.none` | nav 대상 | **desc = rml_text**(73) | – |
| `#hint` | rml glyph | | | glyph | |

- ss는 비어 있음(선택·카테고리·모드는 원작이 가진 값, 89). on_show `nav.root = d.doc`, set(nullptr). 정적 `close`는 nonav. Pending·타이머 없음. want는 dialog_over일 때도 보임.
- 난이도 **중**. 순서: 헤더 → #cats(정적 4칸 + data-class-on + 라벨, 튜토리얼 data-if, glyph data-rml) → #list(data-for, sel/bad, data-attr-data-act, 안쪽 data-rml) → #det(none 분기 data-if).
- 위험: buy1_cat1 focus가 cat3(별칭)에서 정상으로, `span.c` 사이 공백, 클래스 순서, `.none`이 목록 컨테이너 안에 있어 DOM 위치 맞추기.

## 7. dialog.rml 공유 해법

- RmlUi 6.3이 data-model을 붙이는 시점: `Element::SetParent`(Element.cpp:2178-2223) 안에서 **한 번만** `attributes.find("data-model")`(2205) → `context->GetDataModelPtr(name)`, `AttachModelRootElement`, `SetDataModel`(2214-2217) → 자식까지 `ApplyDataViewsControllers`(Element.cpp:2147-2168, ElementUtilities.cpp:384-489). 모델이 없으면 `LT_ERROR "Could not locate data model"`(2220)로 끝, 나중에 다시 묶지 않는다(로드 뒤 SetAttribute도 효과 없음). body에 단 경우 XMLNodeHandlerBody(20-33)가 문서에 속성을 쓰고 `Context::LoadDocument`의 `root->AppendChild`(Context.cpp:293)에서 묶인 뒤 `data_models Update(false)`(305-306)가 초기값을 그린다. body가 아닌 요소면 파싱 중에 묶인다(Factory.cpp:448).
- 결론: `Context::CreateDataModel`은 `LoadDocument`보다 먼저. 같은 이름 두 번은 오류(Context.cpp:1057-1077).
- 둘 다 같은 이름 data-model이면 두 문서가 같은 DataModel에 붙는다(attached_elements는 set, DataModel.cpp:355-358) → 서로 비춤, 동시 표시(가방 가득 + 알림)에 겹침. 파일 안에서는 이름을 다르게 할 방법이 없다. 모델 생성 훅(`virtual void make_model(Rml::Context*)`)을 screens.cpp:381-384 루프의 load_doc 앞에 넣어야 한다(context는 ui.cpp:247에서 생성, ui.cpp:254 screens::init).

| 안 | 내용 | 덤프 영향 | 평가 |
|---|---|---|---|
| **A. bagbuy.rml 분리(권장)** | dialog.rml의 `<style>`을 dialog.rcss로 빼서 두 파일이 link. bagbuy.rml에는 scrim, wrap, box(kicker, title, body, btns, hint)만. bagbuy는 처음엔 data-model 없이, 나중에 `data-model="bagbuy"` | bagbuy 문서 URL과 활성 화면 이름만 바뀜. 숨겨진 sub/rows/items/count는 원래 덤프에 안 나옴 | 가장 단순, 덤프에서 두 문서 구별 가능 |
| B. 같은 파일, 로드할 때 이름 치환 | 파일 문자열의 `data-model="dialog"`를 "bagbuy"로 바꿔 `LoadDocumentFromMemory(text, asset_path("dialog.rml"))`(Context.cpp:313-324) | 완전히 같음 | 두 모델이 같은 변수를 전부 bind해야 함(또는 allow_missing_variables). 문자열 치환은 꼼수 |
| C. C++에서 래퍼 삽입 | data-model div를 만들어 #wrap을 그 아래로 | DOM 깊이 +1 → 들여쓰기 바뀜 | 비권장 |
| D. RmlUi template | `<body template="…" data-model="…">`(XMLNodeHandlerBody.cpp:32-39) | 구조가 달라질 수 있음 | 부품이 많음 |
| E. 모델 하나 공유 | – | – | 동시 표시 불가, 기각 |

## 8. 권장 순서(이 묶음)

1. dialog.rml 분리 2. store 3. charselect(스프라이트 제외) 4. map(헤더·범례만) 5. smithy(헤더, 팝업, 목록 순; slots·det는 data-rml 통째로) 6. dialogs(정적 요소, count, rows, btns·items 순)

공통 준비: 문서 위임형 ActionListener(또는 다음 프레임 bind_actions), data-act로 다음 프레임 focus 복원 유틸(menu keep_focus 일반화). GetChild 인덱스 focus(dialogs.cpp:423-428, bagbuy.cpp:68-69, smithy.cpp:305)는 data-act 검색으로.
