# 조사 A: menu, title, talkctl

(Explore 서브에이전트 보고, 2026-10-21. 리팩터 후 코드 기준. 줄 번호는 src/ui/ 기준. 지금은 src/ui에 DataModel 코드가 하나도 없다.)

## 공통 구조

- 프레임 순서: `input::update()` → `screens::update()` → `context->Update()`(ui.cpp:304-306). 모델 갱신은 Context.cpp:196-197.
- 문서 수명: `init()` → `load_doc`(screens.cpp:381-383) = LoadDocument → Show → `bind_actions(doc 전체)`(361-367). 모델은 LoadDocument 전에 만들어야 하는데 Screen에 그 훅이 없다. 보일 때 `d.invalidate(); on_show()`, 숨길 때 `nav.clear(); on_hide()`(442). 매 프레임 모든 문서에 `touchin`, 현재 `nav.focus`에 `focus`를 다시 설정(424-428).
- 클릭: `ActionListener`(screens.cpp:45-56) → StopPropagation → `input::ui_action`(input.cpp:215-249) → 처리 안 되면 `screens::action`(registry 순서로 처음 true인 화면, 456-460).
- 키/패드: 모던 화면이 활성이면 `screens::pad_button`(input.cpp:319-330, 405-416). SOUTH ensure+activate, EAST/START back, 100/101 tab, 102/103 subtab(screens.cpp:462-471). 방향: on_dir → nav.move → 위아래 실패 시 nav.scroll(472-478). 오른쪽 스틱 scroll_active(input.cpp:474, screens.cpp:479-483).
- Nav: focus는 날 포인터(screen_util.h:66). `set()`은 이전 focus를 역참조해 `SetClass("focus", false)`, 새 focus에 ScrollIntoView(79-86). `valid()`는 포인터 비교만(74-78). collect는 보이고 data-act가 있고 data-nonav·`.off`가 아닌 요소(68-72).
- dump_ui: 보이지 않는 요소 건너뜀(520). 줄 형식 `tag#id.class [data-act] (geom) "text"`. 텍스트는 자식이 모두 #text인 leaf만(533-551) — **숨겨진 자식도 leaf 판정에 들어감**. `*FOCUS`는 `e == g_active->nav.focus`(568, 577).

## 1. menu (menu.cpp + menu_pages.cpp + cashshop.cpp, menu.rml)

데이터: `s.menu` = `state::MenuData`(state.h:66-88), 수집 menu.cpp:14-71, menu_pages.cpp:64-148. 페이지 데이터는 `m.tab`이 그 탭일 때만 채워짐(menu_pages.cpp:68, 83, 106; menu.cpp:34). 상점은 `cash::pull()`(cashshop.cpp:89).

| id / 생성 영역 | 방식 · 출처 | 반복 · data-act · 행 클래스 | Nav · keep_focus | 원작 RML | 바인딩에 맞지 않는 부분 |
|---|---|---|---|---|---|
| body | `doc->SetClass("touch", touch_input())`(170). screens가 `on`(441), `touchin`(426) | – | – | – | 입력 장치 상태 |
| tab0,1,2,3,4,6,8 | cls sel ← `ss.shop ? 6 : m.tab`, Pending `ss.tab_req.want` 우선(153-160) | 정적 7개 `tab%d` | 탭이 바뀌면 `nav.root=p<tab>; set(nullptr); ensure();` 실패 시 탭 요소로(161-164). 새 페이지가 레이아웃 전이라 보통 탭 요소로 감(golden menu/tab1: `#tab1 *FOCUS`) | – | **Pending<int> 표시 덮어쓰기**(90, 128, 154). `ss.cur_tab=-1` 강제 재구성(123-125). nav.root 변경은 모델로 표현 불가 |
| p0..p8 | cls on(159) | 정적 | nav.root | – | – |
| ptitle | rml TAB_NAMES[tab](166) | | | | |
| psub, sy_save_s | 갱신 안 함 | | | | |
| gold | rml `num(s.gold)`(167) | | | | num() 콤마(screens.cpp:19-26) |
| bumpl / bumpr | rml glyph tabl/tabr(168-169) | | | glyph | 장치 의존(input.cpp:126-170) |
| dot0 / dot3 | cls on ← s.gp>0 / s.sp>0(171-172) | | | | |
| mtoast | rml + cls on, 타이머 ss.toast_until(138-142, 173) | | | | **update 밖(action)에서 문서를 직접 고침**: 510, 629, 649. 결과 토스트 182-183, 저장 결과 on_frame 715-720. `shop_seen` 영속(96) |
| confirm | cls on ← ss.confirm(187) | | sh_buy: root=confirm, set(c_yes)(630). sys_title: set(c_no)(668). c_no/c_yes가 root 복원(669-677) | | |
| c_t | rml 고정(192, 199) | | | | |
| c_d | rml: confirm==2면 img + p.name + price_label + num(g)(194-195), ==1이면 고정 문구(200) | | | p.name plain, 미이스케이프 | `state::get().gold` 직접(193) |
| foot | rml glyph advance/back(202-203) | | | glyph | |
| cvimg | attr src ← sprite_src(s.face)(522) | | | | sprite 키 ""는 아직 렌더 전(sprites.h:6) |
| cvname / cvtitle | rml s.name / m.title(523-524) | | | plain | |
| cvlvt / cvxp | rml "Lv "+level, num(exp)+" / "+num(expmax)(525-526) | | | | 포맷 |
| cvbar > .fill | prop width ← pct(exp,expmax), **GetChild(0)**(527-528) | | | | data-style-width로 |
| gpleft, st0-2, sp0-2 | rml(529, 531-532) | | | | |
| pl0-2 | cls on ← s.gp>0(533), `gp%d` | | `.plus`가 꺼지면 display:none → Nav에서 빠짐(golden menu/nav1 `#pl1 *FOCUS`) | | |
| sec | rml cell2 7개(535-540) | 고정, data-act 없음 | | | |
| **invcats** | rml(257-262). m.inv_cat, inv_used, inv_cap | subhint glyph + 5칩 `cat%d`(c/on), `span.cap.full` 안 `bag_expand`(nonav) | **bind_actions만, keep_focus 없음**(262) | glyph(697-700) | `&#xe13d;` 엔티티 |
| **invgrid** | rml(263-272). m.items[i](menu_pages.cpp:68-80), m.inv_sel | `<div class="cell[ sel]" data-act="it%d">` + img, `span.n`(count>1), `span.hs`. 비면 `div.empty`(271) | **keep_focus("invgrid")**(272). golden menu/tab1_it3 `[it3] *FOCUS` | it.name plain | sprite(menu_pages.cpp:24) |
| **invdet** | rml(273-289) | `inv_open/inv_use/inv_hot/inv_piece/inv_drop`(조건 280-286) | bind만(289) | **it.desc = rml_text**(277; menu_pages.cpp:32) | item_props 조립(210-219) |
| **eqslots** | rml(297-303). m.equipped[k], m.eq_slot | 6개 `div.eqs[ sel]` `eqs%d`, 아이콘 없으면 `div.ph` lucide, `.inm[ vacant]` | **keep_focus**(303) | it.name plain | sprite |
| eqhead | rml + `style="color:#ffb547"` span(304-305) | | | | |
| **eqcands** | rml(306-314). m.cands, m.cand_sel | `div.cell[ sel][ bad]` `eqc%d`, 비면 `.empty` | **keep_focus**(314) | | `bad`에 s.job, s.level(309) |
| **eqdet** | rml(315-338) | `eq_equip` / `eq_off` | keep_focus 없음 | **cur->desc rml_text**(332) | delta 블록 `style="top: 200dp;"`(325), ▲/▼(326-330) |
| **sktabs** | rml(344-348). m.sk_tab, s.sp | subhint + 3칩 `skt%d` + `span.cap` "SP n" | keep_focus 없음(golden menu/tab3_skt1에 *FOCUS 없음) | glyph | |
| **sklist** | rml(349-382), 두 모드: (A) sk_tab≤1 m.skills(menu_pages.cpp:116-138) (B) 생활 스킬 m.life[3](140-146) | (A) `div.sk[ sel][ locked]` `sk%d`, **중첩** rank pip `<span class="on"/>`/`<span/>`(355), `span.req` 안 `span.up`. (B) data-act 없는 `div.sk` 3개. 비면 `.empty` | **keep_focus**(382). (B)는 Nav 대상 없음 | r.name, lf.name plain | (B) `style="width: 300dp"`, `width: %.0fdp`(377-378). 모드에 따라 DOM이 다름 |
| **skdet** | rml(359-372, 380, 383) | `sk_up`(+off), `sk_hot` | 없음 | | `.off`는 Nav에서 빠짐 |
| **qlist** | rml(566-575). s.menu.quests(menu.cpp:34-70), **ss.qsel** | `div.q[ sel]` `q%d`, `div.kind[ sub]`, `span.st[ done]`. 비면 `.empty` | 없음(575). q%d는 action()(647), focus_act는 page_action 끝(515)에서 지움 | q.title plain(46) | 행 수 클램프(565) |
| qsum | rml 고정 3칸(577-582) | | | | |
| **qdetail** | rml(584-613), qs[ss.qsel] | q.conds[c] div, `class="ok"`(609) | | **q.desc rml_text**(47)를 `<br/>`로 나눔, `*`로 시작하는 줄은 태그를 떼어 목적지 카드로(587-604, 606). conds는 plain + 포맷(59, 64) | **C++ 문자열 파싱**(태그만 떼고 엔티티는 남김), 타이머 `%02d:%02d:%02d`(610) |
| shophead | rml 고정(226) | | | | |
| **shopgrid** | rml(227-236). cash::pull(), **shop_sel 영속**(95: 마지막에 고른 상품으로 열림) | `div.cell.shop[ sel]` `sh%d`, `span.n`, `span.pr[ free]`. 비면 `.empty` | **keep_focus**(236). c_yes 뒤 `focus_act="sh<sel>"; set(nullptr); keep_focus`(674-676) | p.name plain | cash는 게임 스레드가 mutex로 따로 공개 |
| **shopdet** | rml(237-251) | `sh_buy`(+off), `span.why` | 없음 | **p.desc rml_text**(246; cashshop.cpp:79) | gold 비교(240) |
| sy_save | cls off ← !s.can_save(185), `sys_save` | | `.off` Nav 제외 | | |
| sysinfo | rml(543-560) | 카드 2개 + 키 행 | | s.name, s.field_name plain + **glyph_ref**(551-557) | pad 판정이 `glyph_ref("stick")` 문자열 검색(551) |
| picker | cls on ← ss.picker(387), `pk_close` | | inv_hot/sk_hot: root=picker, set(nullptr), ensure()(448, 494) — picker가 아직 숨김이라 focus null(golden menu/picker에 *FOCUS 없음) | | |
| pk_d | rml 고정(390) | | | | |
| **pk_rows** | rml(391-404). m.hot[10](menu_pages.cpp:40-62) | 2행 × 5칸 `div.cell[ vacant]` `pk%d`, 아이콘 없으면 `span.ph`(빈 img는 흰 사각형, 396), `span.k` glyph(slot%d) | 변경 시 bind_actions(pk_rows)+bind_actions(picker)(404) | glyph | |

- 난이도 **상**: 생성 영역 17곳(keep_focus 의존 5, 비의존 8), 중첩 루프, 모드에 따라 바뀌는 sklist, qdetail 문자열 파싱, 원작 RML 4곳, Pending 탭, snapshot 밖 데이터(cash), update 밖 문서 수정(toast), 장치 glyph 7곳, struct 배열 ItemInfo/SkillRow/HotRef/QuestInfo/Product.
- 권장 순서: (1) 정적 id 요소(rail, ptitle, gold, dot, confirm, foot, sy_save), glyph는 변수 하나에 data-rml (2) p0 캐릭터 (3) p8 sysinfo (4) p4 퀘스트 (5) p6 상점 (6) p1 가방 (7) p2 장비 (8) p3 스킬 (9) picker (10) toast(ss로 옮겨 update에서 반영).
- 위험:
  1. **오래 남는 `.focus` 클래스가 이미 기준선에 있다.** keep_focus가 `nav.focus = nullptr; nav.set(e)`로 이전 focus의 클래스 해제를 건너뛴다(menu.cpp:415) → 탭 요소에 `.focus`가 남는다(menu/tab3_skt1에서 tab1·tab2·tab3 모두 `.focus`, menu/open2에 `.focus` 5개). keep_focus 의미를 바꾸면 이 줄들이 달라진다.
  2. **끊어진 focus 포인터**: SetInnerRML로 즉시 해제(Element.cpp:1174-1175, 1452-1500), 풀 재사용(ElementInstancer.cpp:19-28). keep_focus가 없는 invcats, invdet, eqdet, sktabs, skdet, qlist, pk_rows와 focus_act가 맞지 않는 경우(skt1 → sklist). golden menu/tab3_skt1에 *FOCUS가 없는 것이 그 결과. data-for는 행을 재사용하므로 이전 뒤에는 *FOCUS가 새로 생긴다.
  3. `ui act it3/eqs1/sk1`의 focus 이동은 재생성 때 keep_focus가 해서 생긴다. data-for에서는 focus_act로 명시적으로 다시 잡아야 기준선과 같다.
  4. data-if로 숨긴 자식도 leaf 판정에 들어가 텍스트 있는 요소 안에 두면 그 줄의 텍스트가 dump에서 사라진다.
  5. data-for 행은 템플릿 앞에 들어간다(DataViewDefault.cpp:523) → `.empty` 같은 형제 위치에 따라 순서가 바뀜.
  6. plain_text 미이스케이프(state.cpp:62-75). 엔티티(`&#xe13d;`)는 `{{ }}` 안에서 디코드되지 않으니 템플릿에 리터럴로.
  7. num(), CreateString 포맷은 C++에서 미리.
  8. 키보드 `tab()` 전환은 `ss.focus_act`를 지우지 않는다(708-714).

## 2. title (title.cpp, title.rml; saveio.cpp는 문서를 직접 건드리지 않음)

saveio는 콜백만(saveio.cpp:145-160). title.cpp의 `saveio::poll()`이 update 맨 앞(86)에서 돌고 그 콜백이 ss, busy, nav를 바꾼다(170-180, 189-199, 209-213).

| id | 방식 · 출처 | 반복 | Nav | 원작 RML | 맞지 않는 부분 |
|---|---|---|---|---|---|
| art | attr src ← s.keyart(state.cpp:524) + prop width/height/left/top px(48-60) | | | | **C++ 절대 좌표**(win_w/h, sprite 크기). RCSS drift 애니메이션(title.rml:8-9) |
| m_start_d | rml ← s.saves[] 집계(88-92) | | | | |
| slots | cls on/pick-x/pick-i/busy ← ss.slots_open, ss.pick, **busy 영속**(38)(93-97) | | nav.root(157) | | |
| menu | cls dim(94). `t_start`/`t_options`/`t_quit` | | back()에서 set(m_start)(277) | | |
| x_export / x_import | cls active(98-99) | | | | |
| s_hint | rml, `<span class="gold/cyan">`(100-103) | | | | |
| **cards** | rml, `ss.cards_key`(105-111)가 바뀔 때만(110-131). s.saves[i](state.cpp:511-523) | 항상 3장. 있으면 `div.card` `save%d` + `div.pf>img`(data-bleed) + no/nm/meta + (pick==0이면 `div.del` `del%d`, 아니면 `div.go`). 없으면 `div.card.empty[ off]` `save%d` + `div.plus` | 재생성 뒤 slots_open && !modal이면 **focus_cards()**(131; 77-83): GetChild(i) 순서로 `.off`가 아닌 첫 카드 | v.job_name plain(state.cpp:521) | sprite 얼굴, lucide 엔티티, 존재/빈 슬롯 구조 분기 |
| confirm | cls on ← modal_up()(133), c_no/c_yes | | del/import 시 root=confirm, set(c_no)(198, 259) | | |
| c_t / c_d / c_yes | rml(135-147). import 본문은 ss.import_data(139-146) | | | | c_yes 내용·클래스 danger/primary 토글(149-150) |
| n_text / n_icon / notice | rml ss.note, cls on/err(152-156) | | | note에 **파일 basename**(179)과 saveio 오류(saveio.cpp:98-110)가 RML로 | **SDL_GetTicks 타이머**(70, 152) |
| hints | rml glyph(158-160) | | | glyph | |
| (nav) | 매 프레임 nav.root 재지정(157) | | | | |

- 난이도 **중(하에 가까움)**: 카드는 늘 3장 → title.rml에 고정 3장 + data-if/data-class. 부담은 art 좌표와 타이머.
- 순서: 정적 텍스트·클래스 → art(style 문자열을 C++에서) → cards.
- 위험: pick 토글만으로 cards_key가 바뀌어 카드가 다시 만들어지고 focus_cards()가 첫 카드로 focus를 옮긴다(105, 131) → 정적 카드로 바꾸면 명시적으로 불러야 같다(golden title/slots1, title/del_no의 `div.card.focus [save0]`). 잠재 버그: 카드에 focus가 있을 때 begin_export가 `ss.pick=0`(166)으로 재생성 → focus_cards → Nav::set이 해제된 카드 역참조(screen_util.h:81). 파일 경로의 `<`/`&`는 지금 RML로 파싱됨.

## 3. talkctl (talkctl.cpp, talkctl.rml)

| id | 방식 · 출처 | 반복 | Nav | 원작 RML | 맞지 않는 부분 |
|---|---|---|---|---|---|
| body | cls log/crawl/skip(350-354) ← ss.log_open, **crawl 영속(want에서 매 프레임)**, ss.skipping && !ss.skip_paused | | | | 상태 계산이 want()에 있음(232-282) |
| b_skip / b_ffwd / b_auto / b_log | cls active/off(355-362) ← s.fast_on, fast_locked, fast_button, crawl_fast_on, **autoplay 영속** | 정적, 모두 nonav | Nav 대상 아님 | | |
| k_log / k_auto / k_ffwd / k_skip | rml glyph(365-368) | | | glyph | |
| autofill | prop width "%.1f%%"(390) | | | | **update()의 타이머가 매 프레임**(372-389) |
| **loglist** | rml, `log_ver != ss.log_drawn`일 때(393-406). **영속 `log`**(155; 스크립트가 끝날 때까지 유지, 153-154), want()에서 append(239-246)/지움(248-254) | `div.entry[ cont][ now]`, `span.who`는 esc(name)(400), `span.say`. 비면 **`div#logempty`**(403). data-act 없음 | on_dir이 loglist를 직접 스크롤하고 항상 true(340-346) | **log[i].text = s.talk.text rml_text**(state.cpp:372, 398) | **스크롤**: ss.scroll_frames(3프레임) 동안 SetScrollTop(GetScrollHeight())(405-411) |
| adv / logclose / scrim | 정적 | | nav.root = d.doc(287). SOUTH → `#adv` Click → "advance"(input.cpp:234). toggle_log에서 nav.clear()(181) | | |
| (keydown listener) | context 루트(290), slot이 늘 -1이라 동작 없음(302-303) | | | | 문서 밖 |

- 난이도 **하~중**. 순서: body·버튼 클래스·k_* glyph → autofill(data-style-width) → loglist(data-for, say data-rml, who는 `{{ }}`).
- 위험: `#logempty` id·위치, log를 want()에서 바꾸므로 dirty 표시는 update()에서.

## 화면 사이 공유

- glyph / glyph_ref(input.cpp:958-959 → glyph_for 126-170): g_dev, g_kb_typed, 키 바인딩에 의존(snapshot 밖), 마크업 → data-rml, update()에서 매 프레임 비교해 dirty.
- num / pct(screens.cpp:19-32), sprite_src(screen_util.h:208). 빈 `<img>`는 흰 사각형(menu.cpp:396).
- `talkctl_shown()`(talkctl.cpp:56)을 HUD(screens.cpp:198)와 입력(input.cpp:322, 410)이 읽는다.
- `confirm`/`c_t`/`c_d`/`c_no`/`c_yes` id가 menu.rml과 title.rml에 중복(문서가 따로라 지금은 무해).
- theme.rcss `.glyph*`(64-142), `.focus`(115), `body.touchin`.
- rml_text는 `{`를 이스케이프하지 않는다 → 모델에 묶인 요소 안에서 원작 문자열의 `{{`가 식으로 해석된다(Factory.cpp:343-365, 390-392).

## RmlUi 6.3 질문

**(a) data-for 재사용**: 인덱스 기준으로 재사용한다. `DataViewFor::Update`(DataViewDefault.cpp:507)는 `i < max(size, 기존 행 수)`; `i >= 기존 행 수`일 때만 새 행(템플릿 앞 InsertBefore + `rmlui-inner-rml` SetInnerRML, 509-528), `i >= size`일 때만 `RemoveChild(...).reset()`(531-536). 남은 행은 같은 Element*. dirty는 최상위 변수 이름 단위(545-549; DataView.cpp:90-95), 뷰는 DOM 깊이 순(DataView.cpp:104), 새 뷰도 같은 Update 안에서 최대 10회 반복(70).
- Nav: 목록이 줄지 않는 한 nav.focus는 유효(인덱스 기반 data-act면 지금 keep_focus와 같은 결과). 줄어서 focus 행이 지워지면 포인터가 끊어진다 → `Element::GetObserverPtr()`로 감싸면 안전. 새 행은 context->Update(ui.cpp:306) 안에서 생기므로 같은 update()에서 찾으면 없다. 템플릿은 display:none으로 남는다(474); `GetChild(i)` 순회 코드(title.cpp:79-80, menu.cpp:528)는 템플릿을 만난다.

**(b) data-event-click과 bind_actions 리스너**: 둘 다 실행된다. DataControllerEvent는 일반 리스너(DataControllerDefault.cpp:96), target/phase 확인 안 함(101-113). 같은 요소 리스너는 붙인 순서대로(EventDispatcher.cpp:45-49, 138). `StopPropagation`은 단계가 바뀔 때만 멈춤(153-157), 같은 요소에서 멈추는 건 `StopImmediatePropagation`(169; Event.cpp:73-98) → 한 요소에 둘 다 두면 action이 두 번. 자식의 ActionListener가 StopPropagation하면 조상의 data-event-click은 실행 안 됨. `data-act`, `data-nonav`, `data-block`은 뷰가 없어 무시(ElementUtilities.cpp:416-432). `data-attr-data-act`는 attr 뷰(418-426), `'it' + it_index` 연결 가능(DataExpression.cpp:998-1004).

**(c) data-rml의 잘못된 마크업**: `DataViewRml::Update`는 이전 문자열과 다를 때만 SetInnerRML(DataViewDefault.cpp:211-216, Doc::rml과 같은 경로). 자식을 먼저 지우고(Element.cpp:1174-1175) `Factory::InstanceElementText`(Factory.cpp:330-382): `{{` 괄호 오류면 경고 후 빈 요소(352-357); `<`가 있으면 `<body>`로 감싸 XML 파서로(368-381); 닫는 태그 불일치는 LT_ERROR 후 계속(XMLParser.cpp:169-181), 깊이 0의 예상치 못한 닫는 태그는 경고 후 중단(BaseXMLParser.cpp:270-274 → 156), 미닫힘은 끝에서 경고(169-172). 예외 없음, 부분 DOM이 남음. `previous_rml`은 실패해도 갱신되어 같은 문자열로 재시도 안 함. 삽입된 내용도 모델에 연결된다(Element.cpp:2150-2166) → 안의 `{{ }}`나 `data-*`가 바인딩으로 해석된다.
