# 묶음 5 (menu) — 기준선 대비 차이와 사유

기준선: `golden_b0/`(diffs/batch0.md 0d). 비교: `compare.py golden_b0 <dest> <화면 ...>`.
정규화 후 남은 차이만 적는다. 다른 시나리오의 dump가 대상 화면 폴더에 있어 생기는 MISSING은 세지 않는다.
알려진 차이(이전 묶음): bagbuy/open1·open2·bought의 문서 이름 줄(batch3.md 1-1b), smithy/smith_i0_l1 focus(batch4.md 1-1).

menu는 plan.md 3절 묶음 6과 progress.md 5절 "menu 메모"대로 페이지 단위로 나눠 옮겼다. 단계마다 "빌드 → menu·qbox·town_more 시나리오 → golden_b0 비교 → 사유 → 커밋" 순서다.

## 0. 성능 (묶음 처음, perf/menu_perf.txt)

`run_perf.py before_b5 menu_perf.txt`(HEAD 51bdbf0의 build64 exe): 결과는 progress.md 7절.

## 1. 정적 요소 (data model "menu" 시작)

- body에 `data-model="menu"`, `data-class-touch`. 탭 `sel`·페이지 `on`은 int `tab` 하나에 `data-class-sel="tab == N"`/`data-class-on="tab == N"`(Pending 탭 `ss.tab_req` 계산은 그대로, 결과 탭을 put). `#ptitle`·`#gold`·`#c_t`는 `{{ }}`(우리 리터럴·`num()`), `#bumpl`·`#bumpr`·`#foot`(glyph)·`#c_d`(상품 이름은 `rml_safe`)는 data-rml. `#dot0`·`#dot3`·`#confirm`의 `on`, `#sy_save`의 `off`는 data-class(`can_save`는 옛 코드처럼 탭 8일 때만 갱신).
- 탭 전환 프레임의 `ensure()`: 옛 `d.cls`도 IsVisible은 Context::Update에서 바뀌었으므로(함정 5) 새 페이지는 그 프레임에 보이지 않고 focus는 탭 버튼으로 간다. 지금도 같다.

### 비교 (`current_b5s1`: menu, qbox, town_more)

- golden_b0 대비 **menu 차이 0**. 나머지는 알려진 bagbuy 문서 이름 3건.
- qbox 실행 중 `ui act close` 직후 창이 1824x897(dp 1.25)로 바뀌어(`[ui] layout: window 1824x897` 로그, 외부 요인) dialogs/notice1·notice1_closed·notice2·notice1_reopen의 크기 줄만 달랐다. qbox만 다시 돌린 `current_b5s1q`는 차이 0, `[rml]` 0.
- `[rml]`: menu 0(golden_b0의 detail `rows[N]` 3쌍은 이번엔 없음, 타이밍), town_more store `rows` 감소 쌍(기존), qbox 첫 실행의 dialog `btns` 1쌍(창 크기 변경 뒤, 재실행 0).
- 검토: dump에 안 나오는 동작 차이 없음. 새로 도는 transition 없음(`.tab` 전환은 옛 코드도 정적 요소였다). 다시 열 때 옛 코드는 `d.invalidate()`로 bumpl·bumpr·foot·c_d 자식을 다시 만들었고 지금은 같은 문자열이면 그대로 둔다(그 안에 focus 대상·애니메이션 없음).

## 2. p0 캐릭터

- `#cvimg`는 `data-attr-src`, `#cvname`·`#cvtitle`(원작 이름·칭호)은 data-rml + `rml_safe`, `#cvlvt`·`#cvxp`·`#gpleft`·`#st0-2`·`#sp0-2`는 `{{ }}`. xp 막대 `.fill` 폭은 `d.prop(bar->GetChild(0), "width")` 대신 `data-style-width="xpw"`(초기값 `"auto"`: 빈 문자열이면 문서를 불러온 첫 Update에 `width: ;` 구문 경고, "0%"는 첫 표시 때 0.15초 폭 전환이 새로 돈다). `.plus`의 `on`은 `data-class-on="gp_on"`.
- `#sec`: 옛 코드가 만들던 `.cell2` 7칸을 menu.rml에 정적으로 두고(한 줄, 형제 사이 공백 없음) 값만 `{{sec0..6}}`.

### 비교 (`current_b5s2`: menu, qbox, town_more / xpw 초기값을 고친 빌드로 `current_b5s2b`: menu)

- golden_b0 대비 **menu·detail·hotslot 차이 0**, 알려진 bagbuy 3건만.
- `[rml]`: 첫 빌드에서 시나리오마다 `width: ;` 1건(위 xpw) → 고친 빌드에서 없음. menu는 detail `rows[N].rml` 3쌍(golden_b0과 같음), town_more는 store `rows` 감소 쌍(기존).
- 검토: dump에 안 나오는 동작 차이 없음(`.plus` 표시 시점은 옛 `d.cls`와 같은 Context::Update). 새 transition 없음(`.fill` 폭 전환은 옛 코드도 같은 정적 요소였다). 다시 열 때 `#sec` 칸을 다시 만들지 않는다(focus 대상 아님).

## 3. p8 시스템

- `#sysinfo`(플레이 정보·조작 카드, glyph 마크업)는 C++에서 만든 마크업을 data-rml로(매 프레임 put 비교, 탭 8일 때만 갱신은 그대로). 캐릭터 이름과 지역 이름은 `rml_safe`.

### 비교 (`current_b5s3`: menu, qbox, town_more)

- golden_b0 대비 **menu 차이 0**, 알려진 bagbuy 3건만. `[rml]`: menu detail `rows[N].rml` 1쌍, qbox dialog `btns[N].act` 1쌍(b5s1에도 있던 것, 타이밍), town_more 0.
- 검토: 다시 열 때 같은 문자열이면 다시 만들지 않는다(안에 focus 대상·hover·스크롤 없음). 새 transition 없음.
- 기존 위험(옛 코드와 같음, 고치지 않음): `rml_safe`는 `{`만 바꾸므로 원작 이름에 `<`·`&`가 있으면 RML로 해석된다. glyph의 키 이름(SDL_GetKeyName)도 이스케이프하지 않는다.

## 4. p4 퀘스트

- `#qlist`: `.q`의 data-for(struct Quest: act·kind·nm·st·sel·sub·done; `.kind`의 `sub`, `.st`의 `done`은 data-class, 제목은 data-rml + `rml_safe`). 빈 표시 `.empty`는 템플릿 뒤 `data-if="quests.size == 0"`. 옛 코드는 이 목록을 keep_focus 없이 다시 만들었으므로 refocus는 하지 않는다. 행이 바뀌면 `drop_if_gone`(지워질 행의 focus만 버림).
- `#qsum`: 옛 코드가 만들던 마크업을 정적으로 두고 숫자만 `{{q_main}}`·`{{q_sub}}`·`{{q_ready}}`.
- `#qdetail`: 목적지 파싱(`<br/>`로 나누고 `*`로 시작하는 줄)은 C++에 두고 결과 마크업을 data-rml. 제목·본문·목적지·조건은 파싱한 뒤에 `rml_safe`. 상자 자신이 스크롤되므로 옛 코드처럼 스크롤을 리셋하지 않는다.

### 비교 (`current_b5s4`: menu, qbox, town_more; menu 재실행 `current_b5s4m`)

- golden_b0 대비 **menu 차이 0**(tab4·tab4_q1·tab4_reopen 포함, tab4_q1의 `*FOCUS`는 레일 tab4), 알려진 bagbuy 3건만.
- `[rml]`: 첫 실행 menu에 `quests[0].act/.sel` 1쌍(`ui act tab4`로 다시 들어갈 때, 원작 m.tab이 아직 4가 아닌 대기 프레임에 quests가 비어 남은 행이 지워짐: 함정 4와 같은 종류), 재실행에서는 없음. 나머지는 기존 잡음(detail `rows`, store `rows`, dialog `items`).

### dump에 안 나오는 동작 차이 (검토)

- 키보드로 `q<i>`를 누르면 focus가 그 행에 남는다. 옛 코드는 목록을 다시 만들어 focus를 잃었고, 그 뒤 확인 키는 `ensure()`로 첫 행(q0)을 골랐다.
- 탭 4에서 닫고 퀘스트 키로 바로 탭 4로 다시 열면, 첫 update의 `ensure()`가 남아 있는 q0을 잡는다(옛 코드는 `d.invalidate()`로 다시 만들어 focus가 null).
- 새 transition 없음(`.q`·`.kind`·`.st`에 transition 없음).

## 5. p6 상점 + 공용 헬퍼 (keep_focus 대체)

- `#shopgrid`: `.cell.shop`의 data-for(struct Cell: act·src·n·pr·sel·free·hs·bad, 가방·장비 격자도 같은 타입). 수량 `.n`은 `data-if="c.n != ''"`, 가격 `.pr`은 `{{c.pr}}` + `data-class-free`. 빈 표시 "상품을 불러오는 중"은 템플릿 뒤 data-if. `#shophead`의 안내 문구는 menu.rml에 정적으로. `#shopdet`은 C++ 마크업을 data-rml(이름·종류·설명 `rml_safe`).
- **keep_focus 대체** (menu.cpp `rows()`/`refocus()`): 옛 keep_focus는 목록 문자열이 바뀌어 SetInnerRML한 뒤 `ss.focus_act`(마지막 페이지 액션의 data-act)인 요소에 `nav.focus = nullptr; nav.set(e)`를 했다. 그래서 누른 칸으로 focus가 가고(`ui act sh1` → sh1), 이전 focus 요소의 `.focus` 클래스는 지워지지 않았다(RULES.md 표, plan.md 5절 2). 지금은 목록 모델이 바뀔 때 `drop_if_gone`(지워질 행의 focus) → put → focus_act가 새 행에 있으면 그 요소를 `find_act`(재사용 행)로 바로, 없으면 `want_act`로 다음 프레임에 잡는다. `nav.set`이 이전 focus의 클래스를 지운다. 구매 확인(`c_yes`)의 `keep_focus("shopgrid")`는 `find_act`로 바로 잡는다.
- `inner()`: 상세 칸(data-rml 통째로) 안의 focus는 smithy처럼 다음 프레임에 같은 data-act로 다시 잡는다(옛 코드는 잃었다).
- 퀘스트 목록도 `rows(.., false)`로 바꿨다(동작 같음, refocus 없음).

### 비교 (`current_b5s5`: menu, qbox, town_more)

- golden_b0 대비 bagbuy 3건 외 **19건, 모두 레일의 낡은 `.focus` 클래스가 사라진 한 줄**: `div#tab6.focus.tab` → `div#tab6.tab`(detail open1·open2·reopen, dialogs/qconfirm1, hotslot nav1·open1·open2·s3, menu open2·picker·picker_closed·qbox_item·tab1_reopen·tab4_reopen·tab8, qbox/spin1), `div#tab6.focus.sel.tab` → `div#tab6.sel.tab`(menu tab6_confirm·tab6_no), menu/touch_tab1의 `div#tab6.focus.tab`. tab6에 focus가 있을 때 `ui act sh1`의 keep_focus가 남긴 것이다(qbox 시나리오는 sh6 구매). 다른 탭(tab1·tab2·tab3)의 낡은 클래스는 아직 남아 있다(가방·장비·스킬 격자는 다음 단계).
- **모든 dump의 `*FOCUS`(요소와 act)가 golden_b0과 같다.** 상점 격자·머리·상세 내용도 같다.
- `[rml]`: 새 종류 없음(detail `rows`, store `rows`, dialog `items`의 기존 잡음).

### dump에 안 나오는 동작 차이 (검토)

- 상품을 두 번 눌러 구매: 결과 같음. 첫 번째 누름 뒤 refocus는 focus가 이미 그 칸이라 아무것도 하지 않는다(옛 코드는 새 요소에 ScrollIntoView를 다시 불렀다).
- 키보드 focus가 `sh_buy`에 있는 동안 골드가 모자라게 되면, 다음 프레임에 `.off` 버튼에 focus가 돌아온다(find_act는 off를 보지 않음). 옛 코드는 focus를 잃었고, 확인 키 결과(ensure → 첫 항목)는 같다. 메뉴가 열린 채 골드가 바뀌는 경로는 없다.
- **다시 열 때 `.ds` 스크롤**: 옛 코드는 열 때마다(`d.invalidate()`) 상세 칸을 다시 만들어 설명이 맨 위에서 시작했다. 지금은 같은 문자열이면 다시 만들지 않으므로 스크롤이 남는다. 6단계에서 "열림마다 페이지의 첫 그리기에 `.ds`를 맨 위로"를 넣었다.
- 탭 전환·대기 탭 처리(`switch_tab`, `tab_req`)는 바꾸지 않았다.

### 새로 도는 transition

- `.cell`(`border-color background-color 0.1s`): 재사용 칸에서 `sel`이 옮겨 가면 잃는 칸과 얻는 칸 모두 0.1초 전환(옛 코드는 다시 만들어 즉시). 키보드 focus가 있던 칸을 두고 다른 칸을 누르면 그 칸의 `.focus`가 0.1초에 걸쳐 빠진다(옛 코드는 그 요소가 사라졌다). box-shadow는 전환 대상이 아니다.
- 없음: `.pr`, `.n`, `#shopdet .btn`(통째로 새로 만들어짐), 레일 `.tab.focus`(box-shadow).

## 6. p1 가방

- `#invcats`: 칩 다섯(cat0-4, 전체·장비·소비·재료·기타)을 menu.rml에 정적으로 두고 `data-class-on="inv_cat == i"`. 양 끝 키 힌트 `.sb`는 `data-if="gl != ''"` + data-rml(스킬 페이지와 같은 `gl`/`gr`), `.cap`은 `{{inv_used}} / {{inv_cap}}` + `data-class-full` + 정적 `bag_expand` 버튼. 한 줄(인라인 형제 사이 공백 없음).
- `chips()`(**10b에서 바뀜**, 아래 10b가 최종 규칙): 칩 줄 선택이 바뀌면 누른 칩(`ss.focus_act`, 예: `cat2`, Z/C 키의 `subtab()` → `action("cat%d")`도 같은 경로)이 focus를 받는다. 옛 코드는 이 줄을 keep_focus 없이 다시 만들어 그 안의 focus를 잃었다(칩 줄에는 keep_focus 규칙이 빠져 있었다). 스킬 탭 칩(8단계)이 같은 헬퍼를 쓴다(tab3_skt1).
- `#invgrid`: `.cell`의 data-for(Cell, `.n`·`.hs`는 data-if), `rows(.., true)`. `#invdet`: data-rml + `inner()`(이름·종류·설명 `rml_safe`).
- 열림마다 페이지를 처음 그릴 때 그 페이지의 `.ds`를 맨 위로(`ss.drawn`): 옛 코드는 열 때 `d.invalidate()`로 상자를 다시 만들어 설명이 맨 위에서 시작했다. 5단계의 상점 `.ds`도 여기서 돌아온다.
- `switch_tab()`에서 `ss.focus_act`를 비운다(검토에서 나옴): Q/E 탭 키는 page_action의 clear를 거치지 않는다. 원작이 페이지를 아직 바꾸지 않은 대기 프레임에는 가방 값이 기본값(`inv_cat` 0, 빈 목록)이라, 돌아올 때 카테고리가 2→0→2로 바뀌며 예전 칩으로 focus가 튀었다. 레일 클릭(`ui act tabN`)은 이미 page_action에서 비워지므로 dump는 같다(고친 빌드 `current_b5s6b` menu·qbox가 `current_b5s6`과 같음).

### 비교 (`current_b5s6`: menu, qbox, town_more; 고친 빌드 `current_b5s6b`: menu, qbox)

- **모든 dump의 `*FOCUS`가 golden_b0과 같다.** bagbuy 3건 외 차이:
  - 레일의 낡은 `.focus` 제거: 5단계의 tab6 19줄에 더해 `div#tab1…focus`가 사라졌다(menu tab1_it3·tab1_right·tab2·tab2_eqs1·tab3·tab3_sk1·tab3_skt1·tab4·tab4_q1·tab6·tab6_confirm·tab6_no·tab8·picker·picker_closed·open2·qbox_item, dialogs/qconfirm1, qbox/spin1). `ui act it3`·`it0`·`it30`의 keep_focus가 남긴 것이다.
  - **menu/qbox_item, dialogs/qconfirm1, qbox/spin1: `div#invgrid {scroll 0/84}` → `{scroll 65/84}`**(칸 좌표가 모두 y -65). `ui act it30`으로 focus가 it30에 갈 때, 옛 keep_focus는 SetInnerRML로 막 만든(레이아웃 전, 크기 0) 요소에 `ScrollIntoView`를 불러 실제로는 스크롤되지 않았고 it30(y 908, 높이 109)은 격자 아래(952)에서 잘려 있었다. 지금은 재사용된(레이아웃이 끝난) 칸에 `nav.set`이 불려 focus 칸이 보이게 스크롤된다. batch4.md 1-1의 smithy "스크롤" 항목과 같은 원인이고, focus 커서가 원래 의도한 동작이다.
- `[rml]`: menu에 `items[N].act/.sel` 쌍이 새로 나온다(한 실행 50쌍 남짓, 탭 1에 다시 들어갈 때마다). 원작이 아직 탭 1로 바뀌지 않은 대기 프레임에는 가방 목록이 비어 들어와(`capture_pages`는 m.tab == 1일 때만 채운다) 행이 지워졌다가 다시 생긴다. 옛 코드도 그 1~2프레임 동안 "비어 있습니다"로 다시 만들었으므로 화면은 같고, 경고는 함정 4(행 감소)와 같은 종류다. 나머지는 기존 잡음.

### dump에 안 나오는 동작 차이 (검토)

- Z/C 키나 칩 클릭 뒤 focus가 그 칩에 간다(옛 코드는 잃고, 다음 방향키가 첫 항목 cat0을 잡았다). 칩 focus는 아이템 사용·가방 확장·입력 장치 전환(gl "" ↔ glyph)으로 칩 줄이 바뀌어도 남는다(옛 코드는 줄을 다시 만들어 잃었다).
- **'사용'(`inv_use`) 뒤 focus가 그 버튼에 남는다**(`inner()`, smithy와 같은 규칙). 옛 코드는 상세 칸을 다시 만들어 잃었다. 그래서 다시 누르면 한 개 더 쓰고, 그 아이템이 다 떨어지면 선택이 옮겨 간 다음 소비 아이템의 '사용'에 focus가 간다(상세 칸에 보이는 아이템이 쓰인다). 키보드 Enter의 자동 반복은 메뉴 경로에서 걸러지지 않으므로(input.cpp `in_modern_menu` 분기, 기존) 누르고 있으면 연달아 쓴다. 캐릭터 페이지의 GP `+`(정적 버튼)도 옛 코드부터 같은 성질이다. progress.md 6절에 기존 문제로 적었다.
- 피커(`inv_hot`) 경로는 같다. 격자가 줄면 지워지는 칸의 focus만 `drop_if_gone`.

### 새로 도는 transition

- `.cell`: 5단계와 같다(재사용 칸의 `sel` 이동, 카테고리가 바뀌어 같은 인덱스 칸이 다른 아이템이 될 때 테두리·배경 0.1초. `.cell.sel`의 box-shadow는 즉시).
- 없음: `.chips .c.on`(decorator, 전환 없음), `.hs`, `.cap.full`.

## 7. p2 장비 + `inner()` 규칙 보완

- `#eqslots`: 여섯 칸의 data-for(struct Slot: act·src·ph·sl·nm·sel·has). 아이템 그림 img와 빈 칸 표시 `.ph`(lucide 엔티티, data-rml)는 data-if로 고른다. `.sl`은 `{{ }}`(우리 낱말), `.inm`은 data-rml(원작 이름 `rml_safe`, 빈 칸은 "비어 있음") + `data-class-vacant`. `rows(.., true)`.
- `#eqhead`(인라인 style이 든 마크업)는 data-rml. `#eqcands`: Cell의 data-for(`bad`는 data-class), 빈 표시 "교체할 장비가 없습니다"는 data-if. `#eqdet`: data-rml + `inner()`.
- **`inner()` 보완(검토에서 나옴)**: 상세 칸 안의 focus가 **방금 누른 버튼**(`ss.focus_act`와 같은 act: 사용·장착·해제·레벨 업·버리기 …)이면 다시 잡지 않고 버린다. 그 버튼의 동작이 상세 칸을 바꾼 것이고, 새 상세 칸의 같은 버튼은 다른 대상을 가리킬 수 있다: 장착하면 원작 `RefreshEquipInven(form, 1)`이 후보 0을 고르므로(menu_lists_spec 2.2) `eq_equip`을 다시 잡으면 Enter 연타·자동 반복으로 보지 않은 후보 0이 이어서 장착됐다. 옛 코드도 다시 만들며 focus를 잃었으므로 이 경우는 옛 동작 그대로다. 다른 이유로 상세 칸이 바뀔 때(골드·장치 변화 등)는 그대로 다시 잡는다. 6단계에 적은 '사용' 연속 사용도 이것으로 옛 동작으로 돌아간다(상점 `sh_buy`는 확인 창으로 focus가 옮겨 가므로 해당 없음).

### 비교 (`current_b5s7`: menu, qbox, town_more; `inner()`를 고친 빌드 `current_b5s7b`: menu, qbox)

- golden_b0 대비 **OTHER 0**. 6단계까지의 차이(bagbuy 3, 레일 tab1·tab6의 낡은 `.focus`, it30 격자 스크롤)에 더해 `div#tab2.focus.tab` → `div#tab2.tab`(`ui act eqs1`의 keep_focus가 남긴 것, 23개 dump: detail·hotslot 전부, menu open2·picker·picker_closed·tab1_reopen·tab3·tab3_sk1·tab3_skt1·tab4·tab4_q1·tab4_reopen·tab6·tab6_confirm·tab6_no·tab8·touch_tab1, tab2_eqs1은 `.sel`만 남음).
- **모든 `*FOCUS`가 golden_b0과 같다.** tab2·tab2_eqs1의 슬롯·후보(`bad` 위치)·머리·상세가 같다(이 저장에는 장착한 장비가 없어 img 분기는 dump로 확인되지 않았다).
- `current_b5s7b`는 `current_b5s7`과 같다(NONDET 제외).
- `[rml]`: menu `cands[N].act/.sel` 1쌍(eqs1로 후보 20 → 0, 함정 4), 가방 대기 프레임 `items[N]`(6단계), 기존 잡음.
- 참고(NONDET): qbox/closed1에서 보상이 기존 묶음(it0)에 합쳐지면 원작 선택(`sel`)은 it0, focus는 it30에 남는다. 행을 재사용하므로 focus가 누른 칸에 그대로 있는 것이고(6단계부터), golden_b0에는 이 dump의 `*FOCUS`가 없었다(옛 코드는 격자를 다시 만들며 잃었다).

### dump에 안 나오는 동작 차이 (검토)

- 해제(`eq_off`) 뒤 focus는 옛 코드처럼 없다(원작이 후보 0을 고르므로 상세 칸에 '장착'이 나온다).
- 키보드 focus가 후보 칸 eqcK에 있을 때 마우스로 '장착'을 누르면, 개수가 K보다 많이 남으면 focus가 같은 인덱스의 (다른 장비가 된) 칸에 남는다(옛 코드는 잃었다).
- 대기 프레임(원작이 아직 탭 2가 아님)에 빈 슬롯·"가방의 장비 0"이 한두 프레임 보이는 것은 옛 코드와 같다.
- 원작은 장착한 뒤 슬롯 버튼으로 focus를 옮긴다(menu_lists_spec 2.2). 옛 코드부터 따르지 않았다(focus 없음). 기존 문제로 progress.md 6절에 적었다.

### 새로 도는 transition

- `.cell`(후보 칸): 5·6단계와 같다(선택 이동, 장착·해제 뒤 후보 0 선택, 슬롯이 바뀌어 같은 인덱스 칸의 내용이 바뀔 때).
- `.btn`(0.12s): 상세 칸에서 다시 잡히는 버튼(누른 버튼이 아닌 경우)은 다음 프레임에 `.focus`가 붙어 테두리가 페이드인한다(smithy와 같음).
- 없음: `.eqs`, `.inm.vacant`, `.ph`.

## 8. p3 스킬

- `#sktabs`: 칩 셋(skt0-2, 액티브·패시브·생활)을 정적으로 두고 `chips()`(6단계와 같은 헬퍼, 키 힌트 `gl`/`gr` 공유), `SP {{sp}}`.
- `#sklist`: 스킬 행 data-for(struct Skill: act·src·nm·rk·req·sel·locked; 이름은 `rml_safe`, 랭크 pip `.rk`와 "Lv N · SP N" + "배울 수 있음" `.req`는 C++ 마크업을 data-rml)와 생활 스킬 행 data-for(struct Life: src·nm·w·req, data-act 없음, `.rk` 300dp 트랙 안 `.on` 폭은 data-style-width). 빈 표시 "스킬이 없습니다"는 `data-if="sk_empty"`(스킬 탭이고 행이 없을 때, C++에서 계산). 스킬 행은 `rows(.., true)`, 생활 행은 put.
- `#skdet`: data-rml + `inner()`(레벨 업 뒤에는 누른 버튼이라 focus를 버림, 옛 코드와 같음).

### 비교 (`current_b5s8`: menu, qbox, town_more)

- golden_b0 대비 7단계까지의 차이에 더해:
  - 레일 `div#tab3.focus.tab` → `div#tab3.tab`(`ui act sk1`의 keep_focus가 남긴 것, 20개 dump, tab3_sk1은 `.sel`만 남음).
  - **menu/tab3_skt1: `span.c.on [skt1] "패시브"` → `span.c.focus.on [skt1] "패시브" *FOCUS`.** golden_b0에는 `*FOCUS`가 없었다. 옛 코드는 sk1 행에 focus를 둔 채(`ui act sk1`의 keep_focus) `ui act skt1`에서 칩 줄과 목록을 keep_focus 없이 다시 만들어 focus를 잃었다(RULES.md 표 "tab3_skt1 `*FOCUS` 없음"). 지금은 sk1 행이 지워지며 `drop_if_gone`, 칩 줄 선택이 바뀌어 누른 칩 skt1이 focus를 받는다. 세션 지시의 "tab3_skt1 `*FOCUS` 복구"다.
- 나머지 `*FOCUS`는 golden_b0과 같다. p3 내용(칩, 행 pip·req·`.up`, 스크롤 0/645·0/403, 상세)은 golden_b0과 같다.
- 생활 스킬 탭(skt2)과 빈 표시는 기존 시나리오에 없다. 묶음 끝의 `menu_more` 시나리오(옛 코드로 뜬 기준선)로 확인한다(아래 11).
- `[rml]`: 새 종류 없음(가방 대기 프레임 `items[N]`, `cands[N]`, store `rows[N]`).

### dump에 안 나오는 동작 차이 (검토)

- 칩을 바꾸면 focus가 칩에 간다(가방과 같음). 이미 켜진 칩을 다시 눌러 목록이 바뀌지 않으면 목록 행의 focus는 그대로다.
- 대기 프레임에 "스킬이 없습니다"·"스킬을 선택하세요"가 한두 프레임 보이는 것은 옛 코드와 같다.
- 생활 행에는 data-act가 없어 Nav 후보가 아니다(템플릿에도 없음).
- 새 transition 없음(`.sk`, `.rk span`, `.chips .c`에 transition 없음, 생활 막대는 `.bar .fill`이 아니다).

## 9. 단축키 피커

- `#picker`의 `on`은 `data-class-on="picker"`(열릴 때마다 `ss = {}`라 첫 update에서 닫힌 값을 put: "다시 열 때 피커가 남던" 옛 버그가 다시 생기지 않는다). `#pk_d` 안내 문구는 menu.rml에 정적으로. `#pk_rows`는 중첩 data-for(struct PkRow {pg, cells}, PkCell {act, src, k, has}; img·`.ph`는 data-if, 키 glyph `.k`는 data-rml). 행은 옛 코드처럼 피커가 열려 있을 때만 다시 계산한다. 여는 경로(`inv_hot`/`sk_hot`: `nav.root = picker; nav.set(nullptr); nav.ensure()`)와 닫는 경로는 그대로다.

### 비교 (`current_b5s9`: menu, qbox, town_more)

- `current_b5s8` 대비 **차이 0**. menu/picker·picker_closed는 golden_b0과 레일 `.focus` 줄만 다르고(`*FOCUS` 없음 그대로: 피커를 여는 프레임의 `ensure()`는 아직 클래스가 붙지 않은 `#picker`를 보지 못한다), closed1 → open2에서 피커는 숨어 있다.
- `[rml]`: 새 종류 없음.

### dump에 안 나오는 동작 차이 (검토)

- 피커가 열린 채 입력 장치가 바뀌면 `.k` glyph만 바뀌고 칸이 남아 focus를 잃지 않는다(옛 코드는 `#pk_rows`를 다시 만들어 잃었다).
- 등록한 뒤 다시 열면 그 칸의 `.vacant`가 빠지며 배경이 0.1초 전환된다(알파 4 → 8, 눈에 띄지 않음). 키보드로 등록해 focus가 숨은 칸에 남은 채 마우스로 다시 열면 그 칸의 `.focus` 테두리가 0.1초에 걸쳐 빠지는 것이 잠깐 보인다(옛 코드는 내용이 바뀌어 칸을 새로 만들었다).

## 10. 토스트

- `toast()`는 `ss.toast`/`ss.toast_until`만 남기고, update()가 페이지·`cash::result` 토스트 뒤에 `v.toast`(`{{toast}}`, 모두 우리 한글 상수)와 `v.toast_on`(`elapsed <= toast_until`, `data-class-on`)을 put한다. 옛 코드는 action()에서 `#mtoast`를 바로 고쳤다(RULES.md "입력 콜백에서 바뀌는 값은 ss에만"). 입력 → screens::update → Context::Update 순서라 보이는 프레임은 같다. 이미 떠 있을 때 다시 띄우면 글만 바뀌고 `toastup` 애니메이션은 다시 돌지 않는 것도 같다.

### 새 시나리오 `menu_more`와 옛 코드 기준선 `golden_menux`

- menu 시나리오가 지나지 않는 경로: 스킬 생활 탭(skt2)과 돌아오기(skt0), 가방 카테고리 칩(cat2, cat3), 단축키 등록 토스트(pk7), 등록한 칸이 찬 채 다시 연 피커, 저장 토스트, 터치 장치로 다시 연 스킬 페이지(sk2). dump 8개(`menu/sk_life, sk_back, cat2, cat3, pk_toast, picker2, save_toast, touch_sk2`).
- 기준선은 함정 9대로 menu.cpp·menu.rml만 51bdbf0(옮기기 전)으로 되돌려 빌드해 떴다(`golden_menux/`, `[rml]` 0).

### 비교 (`current_b5s10`: menu, qbox, town_more, menu_more; menu 재실행 `current_b5s10r`)

- `current_b5s9` 대비 menu·qbox·town_more 차이 0. 첫 실행의 menu/open1에 `span#bumpl`/`#bumpr` 아래 키보드 glyph(Q/E)가 생긴 1건은 실행 중 실제 키 입력이 창에 들어가 `g_kb_typed`가 켜진 것이다(open1은 `ui key` 전 dump). menu만 다시 돌린 `current_b5s10r`은 차이 0.
- `golden_menux` 대비: pk_toast·picker2·save_toast·touch_sk2 **차이 0**(토스트 줄 `div#mtoast.glass-strong.on "단축키에 등록했습니다"`/`"저장했습니다"`, 생활 스킬 행 세 개의 막대 폭·`Lv` span·`.req`, pk7 칸의 img, 터치에서 `.focus` 없음과 `*FOCUS` sk2). sk_life·sk_back·cat2·cat3은 `*FOCUS`가 레일 탭에서 누른 칩으로 옮겨 갔다: 6·8단계 `chips()`가 선택이 바뀔 때마다 칩에 focus를 줬기 때문이다. 옛 코드는 focus가 칩 줄·목록 밖(레일)에 있으면 그대로 두었다. 다음 10b에서 옛 동작에 맞춘다.
- `[rml]`: menu_more에 `skills[N].act/.sel/.locked`(skt2로 스킬 행 11 → 0, 함정 4와 같은 종류)가 새로 나온다. 나머지는 기존 잡음.

## 10b. 칩 focus 규칙을 옛 동작에 맞춤

- 10단계의 `menu_more`가 보여 준 것: 옛 코드는 focus가 레일 탭에 있을 때 `ui act skt2`·`cat2`를 해도 focus를 탭에 두었다(sk_life·sk_back·cat2·cat3). 6·8단계 `chips()`는 선택이 바뀔 때마다 칩으로 옮겼다.
- 최종 규칙(menu.cpp `chips()`가 선택이 바뀐 순간 focus의 위치를 돌려주고, 해당 상자의 변화가 정해진 뒤 `chip_focus()`): 옛 코드는 선택이 바뀌면 칩 줄을 다시 만들고, 목록·상세 칸은 마크업이 바뀌었을 때 다시 만들었다(목록의 keep_focus는 칩 act인 `ss.focus_act`를 목록 행에서 찾으므로 못 찾음). 그래서
  - focus가 **칩 줄**에 있었으면 → 누른 칩(옛 코드는 잃었다)
  - **목록**에 있었고 목록이 바뀌었으면 → 누른 칩(옛 코드는 잃었다). 목록이 그대로면 그대로(옛 코드도 그대로).
  - **상세 칸**에 있었고 상세 칸이 바뀌었으면 → 누른 칩(옛 코드는 잃었다. `inner()`가 같은 act를 다시 잡기 전에 칩이 받는다)
  - 그 밖(레일 탭 등) → 그대로(옛 코드와 같다)
- 중간 시도(목록 행이 지워질 때만 칩)는 검토에서 틀린 것이 드러났다: skt1 목록은 9행이라 sk1 행이 재사용되어 지워지지 않는다(tab3_skt1에서 focus가 다른 스킬이 된 sk1에 남았다).
- tab3_skt1: focus가 목록의 sk1에 있었고 목록이 바뀌었으므로 skt1 칩이 받는다(8단계와 같은 결과).

### 비교 (`current_b5s10c`: menu, qbox, town_more, menu_more; 최종 규칙 빌드 `current_b5s10e`: menu, menu_more)

- `golden_menux` 대비 menu_more **차이 0**(sk_life·sk_back·cat2·cat3의 `*FOCUS`가 레일 탭으로 돌아옴).
- menu/tab3_skt1은 `span.c.focus.on [skt1] "패시브" *FOCUS`(8단계와 같음). 나머지 menu·qbox·town_more는 `current_b5s10r`/`current_b5s10`과 같다.
- `current_b5s10e`(목록·상세 칸 조건까지 넣은 최종 빌드)는 `current_b5s10c`와 차이 0(차이가 나는 경우 — 카테고리가 바뀌어도 목록이 같을 때, 상세 칸에 focus — 는 시나리오에 없다. 검토로 확인).
- `[rml]`: 기존 잡음만.

## 11. 정리와 최종 검증

### 정리 (코드)

- menu.cpp: `keep_focus()`와 `subhint()`를 지웠다(호출 없음; 칩 줄 키 힌트는 menu.rml의 `.sb` data-if + `gl`/`gr`). 멤버 `v`(View)를 가리던 지역 이름을 바꿨다(`val`, `dv`, `pv`).
- screens.cpp·screen_util.h: 무동작 `bind_actions()`를 지웠다. 마지막 호출은 HUD의 GP/SP 칩(`#points`)이었는데, HUD 문서도 `load_doc`에서 문서 단위 capture 클릭 리스너를 받으므로 클릭은 그대로다.
- 남긴 것: `Doc::rml/cls/prop/attr/invalidate`(hud·talk, 제외 화면 helpfull·autoscreen, 부분 이전 화면 charselect 스프라이트·map 방·title art, skillbook의 같은 프레임 `off`), Nav 보조 함수 전부, `ss.focus_act`(`rows()`/`chip_focus()`가 씀).

### 최종 비교 (정리 뒤 빌드, `current_final`: 15개 시나리오 전체)

- 15개 모두 exit 0, 크래시·TIMEOUT 없음, 창 2280x1080, 실제 키 입력 흔적 없음.
- golden_b0 대비 차이 dump 33개, **OTHER 0**. 줄 단위 분류:

| 종류 | dump | 사유 |
|---|---|---|
| bagbuy 문서 이름 줄 | bagbuy/open1·open2·bought | batch3.md 1-1b |
| smithy focus가 l0 | smithy/smith_i0_l1 | batch4.md 1-1 |
| 레일의 낡은 `.focus` 제거(tab1/2/3/6, 41쌍) | menu 21개, detail 3, hotslot 4, qbox/spin1, dialogs/qconfirm1 | 5-8 (keep_focus의 `nav.focus = nullptr; nav.set(e)`) |
| tab3_skt1 `*FOCUS`가 skt1 칩 | menu/tab3_skt1 | 8, 10b |
| `#invgrid {scroll 0/84}` → `{scroll 65/84}`(칸 y -65) | menu/qbox_item, dialogs/qconfirm1, qbox/spin1 | 6 |

- golden_menux 대비 menu_more 8개 dump **차이 0**.
- 정리 직전 빌드(`current_b5s10c`: menu, qbox, town_more, menu_more) 대비 **차이 0**(정리는 동작을 바꾸지 않았다).
- `[rml]`: 새 종류 없음. 모두 data-for 배열이 줄 때 나는 `Data array index out of bounds` + `Could not get value …[N]` 쌍(함정 4)이다: menu `cands`·`items`, menu_more `skills`, smithy_more·town·town_more `rows`, dialog `items`·`btns`. 개수는 실행마다 크게 다르다(같은 시나리오가 s10c에서 menu 362, 이번에 54). 나머지 8개 시나리오는 0.

### 옛 버그 넷 다시 확인

| 옛 버그 | 확인 | 결과 |
|---|---|---|
| 다시 열 때 빈 페이지(대기 탭 초기화) | 탭 40번 무작위 전환(seed 5, 마지막 tab2) → 닫기 → `burst 40` + 다시 열기 | 블러가 처음 보인 프레임(f6) 다음 프레임(f7)에 페이지 내용(제목·페이지 상관 0.995/0.946), f8 완성. 레일·패널이 그려진 채 페이지만 빈 프레임 없음. f6은 `body.on`의 `menuin` 페이드 첫 프레임이라 패널이 아직 투명하다(옛 코드부터). 다시 열면 원작 값대로 탭 0 |
| 상점 → 다른 탭에서 옛 탭이 잠깐 선택됨 | tab1 → 상점 → tab2 / tab8 / tab1, 각각 `burst 30`, 레일 탭마다 배경 R−B와 #ffd166 글자 픽셀 수 | 매 프레임 선택 집합은 {6} → {대상}뿐. 상점 직전 탭 tab1은 tab2·tab8 시나리오 30프레임 내내 꺼진 값(R−B −13, 노란 픽셀 0). 두 탭이 함께 반쯤 켜진 프레임 없음(tab6은 f6에 거의 꺼지고 대상은 f6부터 0.12초 전환) |
| 터치에서 focus 링이 남음 | `ui dev touch` dump(menu/touch_tab1, menu_more touch_sk2) | `.focus` 클래스 없음, `*FOCUS`(it3, sk2)만 있음 |
| 피커가 다시 열 때 남음 | menu 시나리오 picker 연 채 close → open2 | `#picker` 숨김(9) |
