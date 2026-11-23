# EpicHearts 원작 재현 — 작업 규칙

목표: 원본 `epichearts.apk`(게임 본체 `libtar-android.so`)를 분석해서 원작을 그대로 재현한다.

## 근거로 쓸 수 있는 것 (원본만)

- `epichearts.apk` 안의 `lib/armeabi/libtar-android.so`, `classes.dex`, `assets/`, `res/`
- Ghidra 프로젝트 `ghidra_proj/`, 스크립트 `ghidra_scripts/`, 디컴파일·문자열·xref 결과 `ghidra_*.txt` / `ghidra_*.log`
- 원본 앱을 실행해서 관찰한 것: Frida(`frida_*.py`, `hook.js`, gadget APK), 기기·에뮬레이터 화면

## 규칙

- 다른 이식판이나 이전 작업 결과물을 명세로 쓰지 않는다. 동작과 포맷은 원본에서 확인한다.
- 동작이나 상수를 구현할 때는 근거가 된 원본 함수명과 주소를 주석에 남긴다.
  확인하지 못한 것은 `// UNVERIFIED:`로 표시한다.
- 검증도 원본과 비교한다: Ghidra 디컴파일, Frida 트레이스, 원본 실행 화면.
- 저장소 옆 폴더 `../_sealed_epichearts`는 보관용 기록이라 읽지 않는다(권한 설정으로 막혀 있음).

## 보유 자원

- `third_party/`: SDL 3.4.18, 7-Zip LZMA SDK 26.04(LzmaDec), stb_truetype v1.26 (모두 공식 원본)
- 빌드 도구: Visual Studio 2026 (MSVC). CMake와 Ninja는 VS 내장(`vcvars64.bat` 실행 후 사용)
