# Embedded C AI Agent Benchmark - Adaptive Cruise Control (ACC)

車載組み込みCプロジェクトをベースにしたAIエージェントのベンチマーク課題です。

## 概要

本プロジェクトは、アダプティブクルーズコントロール（ACC）の制御ロジックをC言語で実装するベンチマーク課題です。
AIエージェントが要求仕様書のみを参照して設計・実装・テストを自律的に行い、その品質を評価します。

## ベンチマーク用途

- **課題**: `docs/requirements_specification.md` を読み、設計・実装・テストを行う
- **模範解答**: `reference_solution/` ディレクトリ（ベンチマーク評価時に比較用）
- **採点基準**: `benchmark/scoring_rubric.md`

## ディレクトリ構成

```
.
├── docs/                       # 要求仕様・設計書
│   ├── requirements_specification.md  # 要求仕様書（AIエージェントへの入力）
│   ├── architecture_design.md         # アーキテクチャ設計書（模範解答）
│   └── software_design.md             # ソフトウェア設計書（模範解答）
├── include/                    # 公開ヘッダ（模範解答）
├── src/                        # 実装ソース（模範解答）
├── test/                       # Google Testテスト（模範解答）
├── benchmark/                  # 採点基準・評価スクリプト
├── html/                       # HTMLドキュメント
└── scripts/                    # ベンチマーク実行スクリプト
```

## ビルドとテスト

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## ベンチマーク実行

```bash
./scripts/run_benchmark.sh <candidate_directory>
```

## ドキュメント

HTMLドキュメントは `html/index.html` で閲覧可能です。

## ライセンス

MIT License
