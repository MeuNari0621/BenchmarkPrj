# Embedded C AI Agent Benchmark (Automotive)

このリポジトリは、AIエージェントのベンチマーク用途向けに作成した車載組み込みCプロジェクトです。

## 含まれる成果物
- 要求仕様書: `docs/requirements_specification.md`
- アーキテクチャ設計書: `docs/architecture_design.md`
- ソフトウェア設計書: `docs/software_design.md`
- 実装コード: `include/auto_headlamp_controller.h`, `src/auto_headlamp_controller.c`
- Google Testコード: `test/test_auto_headlamp_controller.cpp`
- 採点基準: `benchmark/scoring_rubric.md`

## ビルドとテスト
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
