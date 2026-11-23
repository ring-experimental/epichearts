# 묶음 0 나머지 (worldmap 마스크, Nav focus ObserverPtr, golden_b0) — 기준선 대비 차이와 사유

plan.md 묶음 0의 4번(Nav focus 안전화)과 7번(golden_b0)을 묶음 3 첫머리에 했다.

## 0a. autoscreen/worldmap을 NONDET로

diffs/batch2.md (b)는 "기준선끼리(golden, golden_rerun, probe)도 다르다"고 적었다. 확인해 보니 그렇지 않았다.

| 실행 | 코드 | worldmap1.ui.txt md5 |
|---|---|---|
| golden, golden_rerun, probe/g2, current_b0 | 기준 커밋 ~ 묶음 1 | 9d315f3d (같음) |
| current, current_misc2 | 묶음 2 도중 | 9475a70e |
| current2, current_stale_2336 | 묶음 2 도중 | 14b88f26 |
| probe/wm_a, probe/wm_b | HEAD b4ac98c (묶음 2 끝) | 9d315f3d (golden과 같음) |

- worldmap2도 같은 양상이다. worldmap_closed는 모든 실행에서 같다.
- 다른 값은 `div.af-clip` 안 `img.af-s`(미끄러져 들어오는 그림 두 장과 커서) x/y 몇 px뿐이다.
- autoscreen.cpp는 묶음 1·2에서 바뀌지 않았다. 묶음 2 끝 코드로 두 번 돌리면 golden과 같으므로, 코드가 아니라 실행 타이밍(uishot의 벽시계 wait 안에 지난 게임 틱 수)에 따른 차이다. 기준선 여섯 번이 우연히 같은 틱에 걸렸다.
- 그래서 `volatile.txt`에 `@file autoscreen/worldmap[0-9].*`를 넣었다. 세션 지시는 `worldmap*`였지만, 늘 같은 `worldmap_closed`(필드 HUD)는 비교에 남기려고 숫자로 끝나는 둘만 골랐다.
- 같은 두 실행 가운데 wm_a에서 charselect/cls2의 `img#spr2` 크기가 달랐다(선택된 카드의 라이브 애니메이션 프레임). batch2.md (b)의 charselect/right와 같은 종류다.

## 0b·0c. Nav focus를 ObserverPtr로 (57ab990) — 전체 시나리오 비교

같은 build64로 11개 시나리오를 모두 돌렸다(`current_b0n`). 크래시·TIMEOUT 없음.
비교: `golden`(기본 7개), `golden_townx`(town_more), `golden_helpx`(help_more), clicks는 `current_opt`·`current_b0`, keymap_dev는 `current_km2`.

### (a) 기존 버그 흔적이 고쳐진 것 (RULES.md 표)

| dump | golden | 지금 | 사유 |
|---|---|---|---|
| store/buy1_cat1 | `span.c.focus [cat3] *FOCUS` | `span.c.focus [cat0] *FOCUS` | 표의 store 행. buy1_r1부터 focus는 cat0이었다. 옛 store 코드는 칩 줄을 SetInnerRML로 다시 만들어 focus가 해제되고, 그 주소가 새 cat3과 겹쳤다. 지금 store는 묶음 2에서 데이터 바인딩으로 옮겨져 요소를 재사용하므로 cat0에 남는다(batch2.md (a)와 같은 줄) |
| store/count_cancelled | 같음 (cat3) | cat0 | 위 상태에서 수량 창을 열고 닫았을 뿐 |
| dialogs/store_count, store_count_right | 뒤의 store `span.c.focus`가 cat3 | cat0 | 뒤에 깔린 store 문서가 함께 찍힌 것. dialogs의 `*FOCUS`(b0 "확인")는 같다 |
| store/sell_cat2 (town_more, golden_townx) | `span.c.focus [cat3] *FOCUS` | cat0 | buy1_cat1과 같은 원인(판매 모드). 표에는 buy1_cat1만 있었다 |
| smithy/smith_i0_l1 | `div.focus.row [l18] *FOCUS` (화면 밖) | `div.row [l18]`, `*FOCUS` 없음 | 표의 smithy 행. `l1`을 누르면 smithy가 목록을 SetInnerRML로 다시 만들고 focus를 다시 잡지 않는다. 옛 포인터는 해제된 뒤 새 l18 자리와 겹쳤고, 지금은 null이 되어 focus가 없다. smithy는 아직 옮기지 않았다(옮길 때 focus를 다시 잡는다) |

- 바뀌지 않은 표의 줄: menu/tab3_skt1(`*FOCUS` 없음)과 menu 탭 줄의 `.focus` 여러 개. 둘 다 menu keep_focus(menu.cpp:415의 `nav.focus = nullptr; nav.set(e)`)에서 생기고, 세션 지시대로 그 줄은 고치지 않았다. tab3_skt1은 옛 코드에서도 해제된 포인터가 다른 요소와 겹치지 않아 `*FOCUS`가 없었고, 지금은 null이라 같은 모습이다.
- collection, farm(부화 페이지), skillbook: 묶음 2에서 이미 요소를 재사용하게 되어 차이가 없다.

### (b) 실행마다 달라지는 것 → volatile.txt 마스크 추가

| dump | 차이 | 사유와 처리 |
|---|---|---|
| talkctl/auto_on | `div#autofill` 폭 44 → 32 | AUTO 게이지. 폭은 `(t - auto_start) / 읽기 지연`, 곧 벽시계 경과 비율이다(talkctl.cpp update). golden 44, golden_rerun 43(1px 허용 안), current_b0 32, current_b0n 32. 정규식 `(?<=div#autofill) …`로 좌표를 가린다 |
| charselect/cls2 (current_b0 대비) | `img#spr2` 크기 248x300 ↔ 186x282 | 선택된 카드는 원작의 라이브 애니메이션 프레임을 보여 준다. 0a의 wm_a 실행과 batch2.md (b)의 charselect/right도 같은 종류. charselect를 부분 이전할 때 스프라이트는 제외하므로 정규식 `(?<=img#spr\d) …`로 좌표를 가린다 |

### 로그

- `[rml]`: town_more만 30줄(`Data array index out of bounds` 15 + `Could not get value from data variable 'rows[N]…'` 15). store `cat2`(판매 21 → 4행)에서 나며 batch2.md의 같은 경고다(그때 36쌍, 실행마다 다름). 나머지 시나리오는 0.
- 새 종류의 `[rml]` 경고는 없다.

## 0d. 새 기준선 golden_b0

- 0c와 같은 build64(57ab990 코드)로 12개 시나리오를 모두 떴다: 기존 11개와 새 `title_more`.
  - `title_more`: 타이틀 슬롯의 내보내기·가져오기 선택 모드(카드가 대상 표시로 바뀌고, 빈 슬롯이 off가 되고, 카드가 바뀔 때마다 focus가 첫 카드로 감). 묶음 3에서 title을 옮기기 전 코드로 뜬 것이라 따로 옛 코드 빌드를 만들 필요가 없었다.
- 확인: 같은 빌드의 독립 실행 `current_b0n`(11개 + title_more 재실행)과 비교해 정규화 후 **차이 0**(NONDET는 QBox 보상과 worldmap).
- 이후 비교 기준은 `golden_b0/`이다. town_more·help_more·clicks·keymap_dev의 dump도 여기에 들어 있으므로 golden_townx·golden_helpx·current_opt·current_km2를 따로 볼 필요가 없다.
- 기준선 로그의 `[rml]`: menu 6줄(detail 다시 열기 행 5 → 2, 3쌍), town 150줄(store `cat1` 행 33 → 4, 75쌍). 모두 `Data array index out of bounds` + `Could not get value from data variable` 쌍이다. 쌍 수는 실행마다 다르다(batch2.md는 11~15쌍, current_b0n의 town은 0줄, town_more는 15쌍).
