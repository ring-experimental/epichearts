# 묶음 2 (collection, farm, skillbook, store, town, detail, hotslot, qbox) — 기준선 대비 차이와 사유

비교: `compare.py golden current` (misc, town, menu, qbox 시나리오), `compare.py golden_townx current_townx` (town_more).
정규화 후 남은 차이만 적는다. 클래스 순서(`.sel.focus` ↔ `.focus.sel`)는 정규화 대상이라 세지 않는다.

## (a) 기존 버그 흔적이 고쳐진 것

| dump | golden | 지금 | 사유 |
|---|---|---|---|
| store/buy1_cat1 | `span.c.focus [cat3] *FOCUS` | `span.c.focus [cat0] *FOCUS` | focus는 buy1_r1부터 cat0에 있었다. `ui act cat1`은 focus를 옮기지 않는다. 옛 코드는 칩 줄을 SetInnerRML로 다시 만들어 focus가 해제된 포인터가 되었고, 그 주소가 새 cat3 요소로 재사용되어 cat3에 붙었다(RULES.md 표, plan.md 5절 1번). 데이터 바인딩은 요소를 재사용하므로 focus가 cat0에 남는다 |
| store/count_cancelled | 같음 (cat3) | cat0 | 위와 같은 상태에서 수량 창을 열고 닫았을 뿐이다 |
| dialogs/store_count, store_count_right | 뒤에 깔린 store의 `span.c.focus` 가 cat3 | cat0 | store 문서의 같은 차이가 dump에 함께 찍힌 것(dialogs 쪽 *FOCUS는 같다) |

## (b) 실행마다 달라지는 것 (이전과 무관)

| dump | 차이 | 사유 |
|---|---|---|
| autoscreen/worldmap1, worldmap2 | 미끄러져 들어오는 `img.af-s`와 커서의 x/y가 몇 px | autoscreen은 원작이 기록한 draw를 프레임마다 옮긴다. uishot의 wait는 벽시계 기준이라 열고 2.5초 안에 지난 프레임 수에 따라 값이 바뀐다. 기준선끼리(golden, golden_rerun, probe)도 다르다. 이번 묶음은 autoscreen을 고치지 않았다 |
| charselect/right | `img#spr1` 크기 (첫 실행만) | 캐릭터 애니메이션 프레임. 다시 돌리면 같다 |
| qbox/done1, done2, closed1, dialogs/qconfirm2 | 보상과 그에 따른 가방 선택·개수 | volatile.txt의 `@file` NONDET(QBox 보상은 무작위) |

## 로그

- `[rml] Data array index out of bounds` + `Could not get value from data variable 'rows[N]...'` 쌍:
  - detail 다시 열기(행 5 → 2)에서 3쌍, store `cat1`(행 33 → 4)에서 11~15쌍(실행마다 다르다. 어느 뷰가 data-for보다 먼저 평가되는지가 정렬에 따라 바뀐다).
  - progress.md 함정 4와 같다. 지워질 행의 data-attr / data-class 뷰가 같은 깊이의 data-for보다 먼저 평가되는 경우다. 그 행은 같은 Context::Update에서 지워지므로 화면과 dump에는 영향이 없다. 행이 많이 줄수록 쌍이 늘어난다.
- `[ui] unhandled action close`(menu): 기준선에도 같은 줄이 있다(hotslot s3 뒤 close, RULES.md 시나리오 함정).

## dump에 나오지 않는 동작 차이

- 행·칸 요소를 다시 만들지 않으므로 CSS transition이 실제로 돈다(progress.md 함정 6): farm·store `.row`의 배경·테두리(0.1s, 선택이 바뀔 때), town `.fn`(0.14s), `.bar .fill`의 폭(0.15s, farm 포획 막대). dump는 wait 뒤에 떠서 잡히지 않는다.
- 첫 열림에서 focus가 한 프레임 늦게 잡힌다(collection, farm, skillbook, hotslot, town은 want_act, store는 첫 update의 ensure를 건너뜀). 행이 그 프레임의 Context::Update에서 생기기 때문이다. help와 같은 종류다(progress.md 3절).
- 옛 코드에서 목록을 다시 만들 때마다 해제되던 focus가 이제 그 요소에 남는다: 부화 페이지의 1초 갱신, 상점·컬렉션에서 행을 고른 뒤, 등록/해제 뒤.

## town_more (새 시나리오, 옛 코드 기준선 golden_townx)

- 기준선은 progress.md 함정 9의 방법으로 떴다: farm·skillbook·store의 cpp/rml만 HEAD로 되돌려 빌드하고 town_more를 돌렸다.
- 시나리오: 농장 먹이 주기(i1)·부화(i2) 페이지, 스페셜북 b12의 쪽 팝업(open → pnext → pclose), 상점 판매 모드(i1)와 카테고리 변경.
- 결과: 12개 dump 가운데 11개가 같다(sell_cat2만 다름).

| dump | golden_townx | 지금 | 사유 |
|---|---|---|---|
| store/sell_cat2 | `span.c.focus [cat3] *FOCUS` | `span.c.focus [cat0] *FOCUS` | buy1_cat1과 같은 (a). sell_r1까지 focus는 cat0이었다 |

- 함께 확인한 것: 쪽 팝업 focus는 pinv(page1, page_next), 닫으면 그리드 b12로 돌아온다(page_closed). 부화 페이지는 1초마다 다시 그려져도 Down으로 옮긴 r1 focus가 유지된다(hatch_nav, 옛 코드도 이 dump에서는 같았다).
- 로그: store `cat2`(판매 행 21 → 4)에서 out of bounds 36쌍(이번 실행). 위 "로그"와 같은 성격이다. 기준선 로그에는 `[rml]` 줄이 없다(옛 코드에는 data-for가 없다).
