# 묶음 3 (bagbuy, talkctl, title, charselect 부분, map 부분) — 기준선 대비 차이와 사유

기준선: `golden_b0/`(diffs/batch0.md 0d). 비교: `compare.py golden_b0 <dest> <화면 ...>`.
정규화 후 남은 차이만 적는다. 다른 시나리오의 dump가 대상 화면 폴더에 있어 생기는 MISSING(예: menu/title_confirm은 title 시나리오, store/sell*는 town_more)은 세지 않는다.

## 1-1a. dialog.rml의 `<style>` → dialog.rcss

- `data/ui/dialog.rcss`에 스타일을 옮기고 dialog.rml은 theme.rcss 다음에 `<link>`로 불러온다(순서가 같으므로 cascade도 같다).
- menu·qbox·town(`current_b3`): bagbuy, dialogs, detail, hotslot, menu, qbox, town, store, smithy 모두 **차이 0**.
- 같은 실행에서 menu/open2의 `span.v "378 / 410"` → `"383 / 410"`이 나왔다. 메뉴 캐릭터 페이지의 HP 칸이 기다리는 동안 회복된 것이다(rcss와 무관). 기존 마스크 `hp \d+/\d+`는 이 형식(공백 있는 "N / M")을 못 잡아서 `volatile.txt`에 `(?<=span\.v) … "\d+ / \d+"`를 더했다(HP·MP 칸). 기준선끼리(golden_b0, current_b0n)는 우연히 같은 값이었다.
- `[rml]`: town 28쌍(store `cat1`, golden_b0의 75쌍과 같은 종류, 실행마다 다름). 새 종류 없음.

## 1-1b. bagbuy (data model "bagbuy", bagbuy.rml)

- bagbuy는 dialog.rml 대신 자기 문서 `bagbuy.rml`을 쓴다(plan.md 4절 안 A). 같은 dialog.rcss를 링크하고 scrim / wrap / box(kicker, title, body, btns, hint)만 둔다.
- menu 시나리오(`current_bb2`): 차이는 예상한 문서 이름 두 종류뿐이다.

| dump | golden_b0 | 지금 | 사유 |
|---|---|---|---|
| bagbuy/open1, open2, bought | `active screen dialog.rml`, `== document …/dialog.rml (active)` | `bagbuy.rml` | 문서 분리(plan.md 4절 4항). `--raw`로도 이 두 줄 말고는 같다 |

- `*FOCUS`: open1·open2 `bb_buy`, bought `bb_close`. golden_b0과 같다.
- `[rml]` 0.

### dump에 안 나오는 동작 차이

- 첫 초안은 닫기 버튼 하나에 `data-class-primary="!ask"`를 두었다. 검토에서 `.btn`의 transition(theme.rcss: background-color, border-color, transform 0.12s) 때문에 **ask 상태로 열 때 '취소'가 0.12초 주황으로 보였다가 어두워진다**는 것이 나왔다(모델 값은 열림 사이에 남으므로, 직전이 확인/닫기 상태였거나 처음 여는 경우). 그래서 `#btns`를 "취소(data-if ask) / 확인·닫기 primary(data-if !ask) / 확장하기(data-if ask)" 세 요소로 나눴다. 상태가 바뀌어도 클래스는 바뀌지 않고 display만 바뀌므로 전환이 돌지 않는다. focus는 `#btns`의 보이게 될 마지막 버튼(자식 1 또는 2)이다. 세 요소는 정적이라(data-for가 아님) 자식 위치로 고른다.
- 남은 transition: 열려 있는 동안 key(afford, bag_cap)가 바뀌면 focus를 `nav.set(nullptr)` 뒤 마지막 버튼에 다시 둔다. 키보드로 '취소'에 focus를 둔 상태였다면 그 테두리(`.focus`, #ffd166)가 0.12초에 걸쳐 사라진다. 옛 코드는 버튼을 새로 만들어 즉시 사라졌다.
- 열린 첫 프레임에 focus가 아직 display:none인 `bb_buy`에 걸린다(data-if가 같은 프레임의 Context::Update에 반영). 그 사이에 입력이 처리되지 않으므로(ui.cpp: input → screens::update → Context::Update) 그려지는 첫 프레임은 같다.
- 지운 `#sub/#rows/#items/#count`는 원래 숨겨져 있었다. `bind_actions` 호출은 지웠다(무동작).

## 1-2. talkctl (data model "talkctl")

- 버튼 상태(active/off), body 클래스(log/crawl/skip)는 data-class, 키 glyph는 data-rml, AUTO 게이지 폭은 data-style-width, 대사 기록은 `.entry`의 data-for(이름 span은 data-if, 본문은 data-rml)와 `#logempty`의 data-if.
- log는 want()에서 바뀌므로(게임 스냅숏 쪽, 숨어 있을 때도) 모델 반영은 update()에서 `log_ver`를 보고 한다. 로그 스크롤 3프레임과 autofill 타이머는 그대로다.
- newgame·misc(`current_tc`): **차이 0**(정규화). talkctl dump 8개는 `--raw`로도 바이트 단위로 같다. `[rml]` 0.
- newgame 로그의 `[talkctl]` 줄: 첫 실행에서 스킵 중 `line 2:` 한 줄이 더 찍혔다(0.09초 스킵 동안 두 번째 페이지를 잡았는지에 따름, want()는 바뀌지 않았다). 다시 돌린 `current_bb2`에서는 golden_b0과 같은 4줄이었다. (b)로 본다.

### dump에 안 나오는 동작 차이

- 새로 도는 transition·animation은 없다. `.entry`/`.who`/`.say`에는 transition이 없고, `.tbtn`의 transition(background-color, transform)은 active(decorator·color·border)·off(opacity)와 겹치지 않는다. 커튼 `curin`, 로그 창 `login`은 클래스가 실제로 바뀔 때만 시작한다(DataViewClass가 IsClassSet과 비교).
- `.entry` 요소를 재사용하므로 줄이 늘 때 앞 줄에서 `now`가 빠지고 새 줄에 붙는다(옛 코드는 목록 전체를 다시 만들었다). 화면은 같다.
- 이름이 없는 줄(`cont` 또는 이름 없음)에도 숨은 `.who` span이 있다. 옛 코드는 span을 만들지 않았다. 숨어 있어 레이아웃과 dump에 영향이 없다.
- 본문과 이름은 `rml_safe`(`{` → `&#123;`)를 거친다(이름은 검토에서 빠진 것이 나와 따로 고쳤다. misc·newgame `current_cs`에서 talkctl 차이 0). 옛 문서에는 모델이 없어 `{{`가 글자로 나왔고, 지금도 같은 글자로 나온다.
- 검토 결과, data-for가 만든 행의 레이아웃은 SetInnerRML과 같은 프레임(Context::Update 안)에 반영된다. 스크롤 3프레임 동작은 신구 코드가 같다.

## 1-3. title (data model "title")

- 슬롯 카드는 처음부터 3장인 배열의 data-for(안쪽 pf / plus / del / go는 data-if, nm·meta·plus·go 아이콘은 data-rml). 메뉴·슬롯 패널·확인 창·알림의 클래스는 data-class, 텍스트는 `{{ }}`(우리 리터럴·숫자) 또는 data-rml(마크업, 원작 직업명, 알림). `#art` 좌표는 C++ 계산 그대로.
- 카드를 다시 만들 때 `focus_cards()`가 일으키던 focus 이동은 명시적으로 재현했다: 카드 key가 바뀌면 `nav.drop_if_gone(#cards, 새 act 목록)` 뒤, 슬롯 패널이 열려 있고 확인 창이 없으면 `focus_cards()`. `focus_cards()`는 아직 반영되지 않은 `off` 클래스 대신 `m.cards[i].off`를 읽는다.
- saveio 콜백은 계속 ss / busy / nav만 바꾸고(update() 맨 앞의 `saveio::poll()`에서 돈다), 모델은 update()에서 반영한다.
- title·title_more·newgame(`current_ti`): **차이 0**. title·titlex dump 46개는 golden_b0과 바이트 단위로 같다(`*FOCUS`도 모두 같음). `[rml]` 0.

### dump에 안 나오는 동작 차이

- `.card`의 transition(title.rml `background-color border-color transform 0.14s`)이 새로 돈다. 옛 코드는 key가 바뀔 때마다 카드를 새로 만들어 전환 없이 최종 모습이 나왔다.
  - focus 이동: 내보내기/가져오기 토글, 선택 모드에서 Esc, 내보내기·가져오기 시작(pick → 0), 삭제·가져오기 뒤 저장 목록이 바뀔 때 `focus_cards()`가 첫 카드로 옮기면, 새 카드는 translateX(-4dp)로 미끄러져 들어가고 이전 카드는 0.14초에 걸쳐 제자리로 돌아간다.
  - `#slots`의 pick-x/pick-i 토글: focus·hover된 카드의 테두리 색이 0.14초에 걸쳐 바뀐다.
  - hover 중인 빈 카드에 `off`가 붙고 떨어질 때 배경·테두리·transform이 전환된다(opacity는 즉시).
  - 삭제·가져오기로 `empty`가 바뀔 때 배경·테두리가 전환된다.
  - 화면이 숨어 있는 동안 끝나는 전환(생성자의 빈 카드 → 실제 값, 게임에서 돌아올 때)은 보이지 않는다.
- 빈 카드에도 숨은 `.pf img`, 숨은 `.del`/`.go`가 있다. 선택 모드에서 숨은 `.del`은 `data-act="del%d"`를 그대로 갖지만 보이지 않아 Nav와 클릭 대상이 아니다.
- `#c_yes`는 처음부터 `btn`이다(옛 마크업은 `btn danger`로 시작해 첫 update에서 뗐다, 숨어 있을 때). 보이는 모습은 같다.
- 카드 img를 매번 새로 만들지 않으므로 얼굴 텍스처를 src가 바뀔 때만 다시 읽는다.

## 1-4. charselect 부분 (data model "charsel")

- 카드 3장은 charsel.rml의 정적 마크업(id cc0..2, data-act cls0..2, img#spr0..2). 이름·설명은 data-rml(`rml_safe`), `.sel`·`#b_start .off`는 data-class, `#b_cancel`은 data-if(옛 `d.prop(display)`), 힌트는 data-rml.
- **제외**: `#spr0..2` 스프라이트(src, 크기, 위치, 빈도 집계)는 C++(Doc) 그대로. 옛 코드는 열 때마다 카드를 새로 만들어 img가 빈 상태로 시작했으므로, 열림의 첫 update에서 src를 비우고 width/height/left/top 인라인 속성을 지운다(그다음 같은 update의 스프라이트 루프가 다시 넣는다).
- misc(`current_cs`): **차이 0**. `--raw`로 보면 cls2의 `img#spr2` 크기 한 줄만 다르다(선택된 카드의 라이브 애니메이션, 0c에서 마스크한 값). open1·right·cls2의 `*FOCUS`(cc0, cc1, cc2)와 SELECTED 배지 위치가 같다. `[rml]` 0.
- 한 번만 열 수 있다는 시나리오 함정(RULES.md)은 그대로다. 다시 여는 경로는 시나리오가 없어 검토로 확인했다(스프라이트 초기화가 없으면 이전 열림의 스프라이트가 남는다).

### dump에 안 나오는 동작 차이

- `.cc`의 transition(charsel.rml `transform border-color background-color 0.18s`)이 새로 돈다. 카드가 문서를 불러올 때부터 있어 스타일 정의가 있기 때문이다(옛 카드는 첫 Context::Update 전에 클래스가 붙어 전환이 없었다).
  - 열 때마다 focus를 받는 카드(키보드·패드)의 테두리가 0.18초에 걸쳐 금색이 된다(옛 코드는 첫 프레임부터 금색).
  - 한 실행 안에서 다시 열 때, 이전 열림에서 선택했던 카드의 `.sel`이 빠지면서 배경·테두리가 0.18초에 걸쳐 돌아간다(원작은 열 때 선택을 -1로 시작한다, `InitCreateCharElements` @000fcca4). 배지와 box-shadow는 즉시 바뀐다.
  - 두 전환 모두 body의 `fadein`(0.25초) 안에서 일어나 거의 보이지 않고, 다시 여는 경로는 한 게임에 한 번 있는 전직뿐이라 고치지 않았다.
- `.sel`·`.off`·`#b_cancel`이 update() 안이 아니라 같은 프레임의 Context::Update에서 반영된다. 같은 update()에서 이 클래스를 읽는 코드는 없다.

## 1-5. map 부분 (data model "map")

- 이전: `#kick`·`#zlabel`은 `{{ }}`(우리 리터럴), `#title`(원작 방·필드 이름, `g::plain_text`)과 `#hint`(glyph)는 data-rml(`#title`은 `rml_safe`), `#zoom .on`은 data-class, `#legend`와 `#lg_p/#lg_q/#lg_s`는 data-if(옛 `d.prop(display)`; div는 block, span은 inline으로 돌아가 옛 값과 같다).
- **제외**: `#map`의 방·통로 생성(박스 크기를 읽는 좌표, SetInnerRML)과 zoom 캔버스(`#zimg` 텍스처, `#zbox` 크기)는 C++ 그대로. `bind_actions(box)` 호출만 지웠다(무동작, 방 클릭은 문서 capture 리스너).
- town(`current_map`, 필드 2/1에서 HUD `map`): **차이 0**, map dump 5개(open1, zoom, unzoom, closed, open2)는 `--raw`로도 같다. `[rml]`은 store의 알려진 경고 11쌍뿐이다.
- 범례·zoom 상자는 모두 absolute라 data-if가 `#map`의 박스(방 좌표 계산에 쓰는 GetClientWidth/Height)에 영향을 주지 않는다.

### dump에 안 나오는 동작 차이

- 새로 도는 transition·animation은 없다(옮긴 요소와 자식에 transition이 없다).
- 다시 열 때 `#title`·`#kick`·`#hint`·`#zlabel` 내용이 같으면 다시 만들지 않는다(옛 코드는 열 때마다 `d.invalidate()`로 SetInnerRML을 다시 했다). 보이는 차이는 없다.

## 마무리: HEAD(1dfb96f) 빌드로 12개 시나리오 전체 (`current_b3all`)

- golden_b0 대비 차이는 bagbuy/open1·open2·bought의 문서 이름 줄 3건뿐이다(1-1b의 예상 차이). 나머지는 NONDET(QBox 보상, worldmap).
- `[rml]`(모두 `Data array index out of bounds` + `Could not get value from data variable` 쌍, 화면별):

| 시나리오 | 쌍 | 출처 |
|---|---|---|
| town | 10 | store `cat1`(행 33 → 4) |
| town_more | 10 | store `cat2`(판매 21 → 4) |
| keymap_dev | 12 | keymap `km_pad`(키보드 → 패드 행 감소, `g0[14..].act/wait`). 이번에 건드리지 않은 화면이고 current_km2에도 있던 종류(progress.md 함정 4). 쌍 수는 실행마다 다르다(golden_b0 0, current_km2 1) |
| 나머지 9개 | 0 | |

- 이번 묶음 화면(bagbuy, talkctl, title, charselect, map)에서는 `[rml]` 경고가 한 줄도 나오지 않았다. 새 종류도 없다.
