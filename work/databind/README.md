# work/databind — RmlUi 데이터 바인딩 이전 기록

이전은 끝났다(2026-10-22). 21개 화면 가운데 19개가 화면별 `Rml::DataModel`을 쓰고, helpfull·autoscreen은 제외했다. 화면별 모델 이름과 제외 이유는 [progress.md](progress.md) 1절 "이전 완료"에 있다.

| 파일 / 폴더 | 내용 |
|---|---|
| [plan.md](plan.md) | 화면별 난이도와 판정(이전/부분/제외), 배치 순서, dialog.rml 공유 해법, 발견한 기존 문제 |
| [progress.md](progress.md) | 이전 진행 기록: 끝난 화면, 바꾼 패턴, 함정, 템플릿, 남은 기존 문제, 프레임 시간 |
| [RULES.md](RULES.md) | 공통 규칙, 검증 절차, 조사에서 나온 주의점, 시나리오 함정 |
| `survey/` | 조사 보고 셋 (a: menu·title·talkctl, b: dialogs·bagbuy·charselect·smithy·map·store, c: 나머지 12개) |
| `scenarios/` | uishot 회귀 시나리오 15개와 공통 시작부 `_prefix.txt` |
| `run_golden.py` | 시나리오를 실행하고 dump를 `<dest>/<화면>/<단계>`로 정리 |
| `compare.py` | 정규화한 뒤 비교(클래스 순서, 1px 좌표, volatile.txt) |
| `volatile.txt` | 실행할 때마다 달라지는 값의 마스크와 무작위 dump 목록 |
| `golden_b0/` | **지금 쓰는 기준선**(묶음 3부터): 14개 시나리오, Nav focus ObserverPtr 뒤 빌드(57ab990), [diffs/batch0.md](diffs/batch0.md) |
| `golden_menux/` | menu_more 시나리오의 기준선(옮기기 전 menu 코드 51bdbf0로 뜸, [diffs/batch5.md](diffs/batch5.md) 10) |
| `golden/` | 처음 기준선(2026-10-21, build64, 기준 커밋 e933e2d). [INDEX.md](golden/INDEX.md) |
| `golden_helpx/` | help_more 시나리오의 기준선(옛 help 코드로 뜸) |
| `golden_townx/` | town_more 시나리오의 기준선(옛 farm·skillbook·store 코드로 뜸) |
| `diffs/` | 묶음별 기준선 대비 차이와 사유([batch0.md](diffs/batch0.md), [batch2.md](diffs/batch2.md), [batch3.md](diffs/batch3.md), [batch4.md](diffs/batch4.md), [batch5.md](diffs/batch5.md)) |
| `perf/` | 프레임 시간 확인(`run_perf.py <tag> [sys_perf.txt / town_perf.txt / b3_perf.txt / b4_perf.txt / menu_perf.txt]`, EH_UIPERF) |
| `golden_rerun/` | 같은 빌드로 한 번 더 뜬 결과(비결정성 확인용, golden과 차이 0) |
| `out/`, `probe/`, `current*/`, `perf/out_*/` | 실행할 때마다 생기는 임시 파일(.gitignore) |

빠른 사용:

```bash
python work/databind/run_golden.py --dest current menu
```

```bash
python work/databind/compare.py work/databind/golden_b0 work/databind/current menu detail hotslot bagbuy
```

## 데이터 바인딩으로 새 화면을 만들 때

1. **템플릿**: [progress.md](progress.md) 5절의 C++·RML 템플릿에서 시작한다. 가까운 예는 다음과 같다.
   - 목록 + 상세 칸: store.cpp / store.rml
   - 모드마다 구조가 다른 상자: smithy.cpp(`inner` 패턴)
   - 여러 페이지와 탭, 칩 줄, 격자: menu.cpp(`rows`/`refocus`/`chips`/`inner`)
   - 중첩 목록: hotslot.cpp
   - 같은 rml을 두 화면이 쓰는 경우: bagbuy.rml(문서 분리)
2. **규칙** ([RULES.md](RULES.md)):
   - 모델은 `make_model`에서 `load_doc` 전에 만든다. 이름은 화면 고유로 한다.
   - 값은 update()에서만 `put()`한다.
   - 원작 문자열은 `rml_safe` 뒤 data-rml로 넣는다.
   - 인라인 형제는 한 줄로 쓴다.
   - data-for 요소는 닫는 태그를 쓴다.
   - focus는 다음 프레임에 `nav.want_act`로 잡는다.
   - 클릭은 문서의 capture 위임 리스너가 data-act로 받는다. `bind_actions`는 없어졌다.
3. **기준선 먼저**: 기존 시나리오가 지나지 않는 경로는 새 시나리오로 쓴다. 옮기기 전 코드로 기준선을 뜬다(progress.md 함정 9, 예: `golden_menux`).
4. **검증 순서**: 빌드(저장소 루트에서 `cmd /c build64.bat`, PowerShell) → 관련 시나리오 `run_golden.py` → `compare.py golden_b0`(또는 새 기준선) → 차이마다 사유를 `diffs/`에 적음 → 커밋.
   - 서브에이전트 둘을 함께 돌린다. 하나는 비교와 `[rml]` 종류를 보고, 다른 하나는 diff를 검토한다(dump에 안 나오는 동작 차이, 새로 도는 transition).
   - 빌드하는 동안에는 테스트를 돌리지 않는다.
   - 시나리오가 도는 동안에는 rml을 고치지 않는다.
5. **focus 규칙은 옛 코드 기준선과 맞춰 본다**: 한 dump를 고치려고 만든 규칙이 이웃 경로의 focus를 바꿀 수 있다(batch5.md 10b).
