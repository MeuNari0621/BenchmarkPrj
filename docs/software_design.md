# ソフトウェア設計書

## 1. モジュールI/F
- `AHC_Init(AHC_Controller*)`: 初期化
- `AHC_Update(AHC_Controller*, const AHC_Input*, uint32_t elapsed_ms)`: 1周期分更新
- `AHC_GetOutput(const AHC_Controller*) -> AHC_Output`: 出力取得

## 2. 内部状態
- `lamp_on`: 現在ランプ状態
- `active_mode`: AUTO/FORCE_ON/FORCE_OFF
- `override_remaining_ms`: 手動モード残時間
- `dark_count / bright_count`: デバウンスカウンタ
- `fault_sensor_range`: センサ異常フラグ
- `has_valid_input`: 有効入力受領済みフラグ

## 3. 判定順序（重要）
1. 手動オーバーライド要求/タイムアウト更新
2. センサ値妥当性チェック
3. FORCE_ON/FORCE_OFF優先
4. トンネル即時ON
5. 低速時OFF
6. 照度デバウンス＋ヒステリシス

## 4. テスト観点対応
- FR-001/002: 連続回数と閾値境界をテスト
- FR-003/004: 車速ゲートとトンネル優先をテスト
- FR-005/006: 手動モード優先と120秒復帰をテスト
- FR-007/008: 異常時保持と起動時OFFをテスト
