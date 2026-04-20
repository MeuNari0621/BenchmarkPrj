# アーキテクチャ設計書: アダプティブクルーズコントロール (ACC)

## 1. 概要

本文書は、ACC制御ソフトウェアのアーキテクチャを定義する。

## 2. ソフトウェア構成

### 2.1 モジュール構成

```
+--------------------------------------------------+
|                ACC_Application                    |
+--------------------------------------------------+
|  +------------+  +------------+  +------------+  |
|  | StateMgr   |  | SpeedCtrl  |  | GapCtrl    |  |
|  +------------+  +------------+  +------------+  |
|  +------------+  +------------+  +------------+  |
|  | Emergency  |  | Override   |  | FaultMgr   |  |
|  +------------+  +------------+  +------------+  |
+--------------------------------------------------+
|               ACC_Core (統合制御)                 |
+--------------------------------------------------+
|              Input/Output Abstraction             |
+--------------------------------------------------+
```

### 2.2 モジュール責務

| モジュール | 責務 |
|-----------|------|
| ACC_Core | 全体統括、周期処理起点、出力調停 |
| StateMgr | 状態遷移管理（STANDBY/ACTIVE/OVERRIDE/FAULT） |
| SpeedCtrl | 設定車速制御、車速維持の加速度計算 |
| GapCtrl | 車間距離制御、先行車追従の加速度計算 |
| Emergency | 緊急制動判定、TTC計算 |
| Override | ドライバー介入検出、復帰タイマー管理 |
| FaultMgr | センサ診断、フォルトコード管理、復帰判定 |

## 3. データフロー

### 3.1 入力処理
1. センサ値を入力構造体 `ACC_Input` で受け取る
2. `FaultMgr` でセンサ値の妥当性を検証
3. 異常時は `StateMgr` にフォルト通知

### 3.2 状態遷移
1. `StateMgr` が現在状態と入力条件から次状態を決定
2. 状態遷移時にコールバック処理（タイマーリセット等）

### 3.3 加速度計算
1. `SpeedCtrl` が車速維持用の目標加速度を計算
2. `GapCtrl` が車間維持用の目標加速度を計算
3. `Emergency` が緊急制動の要否を判定
4. `ACC_Core` が最終的な加速度要求を選択（最小値選択）

### 3.4 出力処理
1. 加速度要求、状態、警報を出力構造体 `ACC_Output` に設定
2. 上位層へ返却

## 4. 状態遷移図

```
        [STANDBY]
           |
           | ACC_ON && speed>=30 && shift==D && !fault
           v
        [ACTIVE]<-------------------+
           |                        |
           | brake || accel_over    | release && timer>=2s && speed>=25
           v                        |
        [OVERRIDE]------------------+
           |
           | speed < 25
           v
        [STANDBY]

    Any State ---(sensor_fault)---> [FAULT]
        [FAULT] ---(fault_reset)---> [STANDBY]
```

## 5. インターフェース仕様

### 5.1 公開API

```c
void ACC_Init(ACC_Controller* ctrl);
void ACC_Update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms);
ACC_Output ACC_GetOutput(const ACC_Controller* ctrl);
```

### 5.2 入力構造体

```c
typedef struct {
    float   ego_speed_kph;           // 自車速度 [km/h]
    float   target_distance_m;       // 先行車との距離 [m]
    float   target_rel_speed_mps;    // 先行車との相対速度 [m/s]
    bool    target_detected;         // 先行車検出フラグ
    bool    acc_on_request;          // ACC ONスイッチ
    bool    acc_off_request;         // ACC OFFスイッチ
    bool    set_plus_request;        // SET+スイッチ
    bool    set_minus_request;       // SET-スイッチ
    bool    resume_request;          // RESUMEスイッチ
    bool    gap_adjust_request;      // 車間調整スイッチ
    bool    brake_pressed;           // ブレーキペダル踏込
    float   brake_force_n;           // ブレーキ踏力 [N]
    float   accel_pedal_pct;         // アクセル開度 [%]
    uint8_t shift_position;          // シフト位置 (0:P,1:R,2:N,3:D)
    float   steering_angle_deg;      // ステアリング角 [deg]
    float   longitudinal_accel_mps2; // 前後加速度 [m/s²]
    float   lateral_accel_mps2;      // 横加速度 [m/s²]
} ACC_Input;
```

### 5.3 出力構造体

```c
typedef struct {
    float       accel_request_mps2;  // 加減速要求 [m/s²]
    ACC_State   state;               // ACC状態
    float       set_speed_kph;       // 設定車速 [km/h]
    uint8_t     gap_setting;         // 車間設定 (0-3)
    bool        warning_visual;      // 視覚警告
    bool        warning_audio;       // 聴覚警告
    uint16_t    fault_code;          // フォルトコード
} ACC_Output;
```

## 6. 設計方針

### 6.1 安全性
- フォルト検出時は即座に制御を停止（Fail-safe）
- 加速度要求は常に安全側（減速優先）で調停

### 6.2 保守性
- 各制御機能をモジュール分離
- 状態遷移をテーブル駆動で管理

### 6.3 テスタビリティ
- 全モジュールを単体テスト可能に設計
- 時間依存処理は経過時間をパラメータ化

## 7. 制約事項

- 動的メモリ割当禁止
- 外部ライブラリ依存禁止（C標準ライブラリのみ）
- グローバル変数禁止（コントローラ構造体で状態管理）
