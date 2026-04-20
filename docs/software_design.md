# ソフトウェア設計書: アダプティブクルーズコントロール (ACC)

## 1. モジュール詳細設計

### 1.1 ACC_Core

#### 1.1.1 主要関数

```c
void ACC_Init(ACC_Controller* ctrl);
```
- 全内部状態を初期化
- デフォルト値: 設定車速=0, 車間設定=1(1.5秒), 状態=STANDBY

```c
void ACC_Update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms);
```
- 20ms周期で呼び出される主処理
- 処理順序:
  1. センサ診断 → フォルト判定
  2. 状態遷移処理
  3. 各制御モジュール実行
  4. 加速度要求の調停

```c
ACC_Output ACC_GetOutput(const ACC_Controller* ctrl);
```
- 現在の出力値を構造体で返却

### 1.2 StateMgr (状態管理)

#### 1.2.1 状態定義

```c
typedef enum {
    ACC_STATE_STANDBY  = 0,
    ACC_STATE_ACTIVE   = 1,
    ACC_STATE_OVERRIDE = 2,
    ACC_STATE_FAULT    = 3
} ACC_State;
```

#### 1.2.2 遷移条件表

| 現在状態 | イベント | 条件 | 次状態 |
|---------|---------|------|--------|
| STANDBY | ACC_ON | speed>=30 && shift==D && !fault | ACTIVE |
| ACTIVE | ACC_OFF | - | STANDBY |
| ACTIVE | SHIFT_CHANGE | shift!=D | STANDBY |
| ACTIVE | BRAKE | brake_force>10N | OVERRIDE |
| ACTIVE | ACCEL_OVER | accel要求 > set_speed+5 | OVERRIDE |
| OVERRIDE | RELEASE | !brake && !accel_over && timer>=2s && speed>=25 | ACTIVE |
| OVERRIDE | SPEED_LOW | speed<25 | STANDBY |
| ANY | FAULT | sensor_fault | FAULT |
| FAULT | RESET | all_ok_1s && ign_cycle | STANDBY |

#### 1.2.3 タイマー管理

- `override_timer_ms`: オーバーライド解除待ちタイマー（2000ms）
- `fault_ok_timer_ms`: フォルト正常継続タイマー（1000ms）
- `sensor_timeout_ms[n]`: 各センサのタイムアウト監視

### 1.3 SpeedCtrl (車速制御)

#### 1.3.1 設定車速管理

```c
// SET+/SET-処理
if (set_plus_pressed && !set_plus_held) {
    set_speed += 1.0f;
    set_plus_held = true;
    hold_timer = 0;
} else if (set_plus_pressed && hold_timer >= 500) {
    set_speed += 5.0f * (elapsed_ms / 1000.0f);
}
```

- 範囲制限: 30 <= set_speed <= 180
- RESUME処理: 前回設定値 `previous_set_speed` を復元

#### 1.3.2 車速維持加速度計算

```c
// PID制御（簡易版: P制御のみ）
float speed_error = set_speed_mps - ego_speed_mps;
float accel_for_speed = Kp_speed * speed_error;

// 上限制限
if (accel_for_speed > MAX_ACCEL_MPS2) {
    accel_for_speed = MAX_ACCEL_MPS2;  // +2.0 m/s²
}
if (accel_for_speed < 0.0f) {
    accel_for_speed = 0.0f;  // 車速維持では減速しない
}
```

### 1.4 GapCtrl (車間制御)

#### 1.4.1 車間設定

```c
static const float GAP_TIME_TABLE[4] = {1.0f, 1.5f, 2.0f, 2.5f};
// gap_setting: 0=1.0s, 1=1.5s, 2=2.0s, 3=2.5s
```

#### 1.4.2 目標車間距離計算

```c
float ego_speed_mps = ego_speed_kph / 3.6f;
float target_gap_m = GAP_TIME_TABLE[gap_setting] * ego_speed_mps;
// 最小車間距離を保証
if (target_gap_m < MIN_GAP_M) {
    target_gap_m = MIN_GAP_M;  // 5m
}
```

#### 1.4.3 車間制御加速度計算

```c
float gap_error = target_distance_m - target_gap_m;
float closing_rate = -target_rel_speed_mps;  // 正=接近中

// 車間誤差と接近速度を考慮
float accel_for_gap = Kp_gap * gap_error - Kd_gap * closing_rate;

// 減速上限
if (accel_for_gap < MAX_DECEL_MPS2) {
    accel_for_gap = MAX_DECEL_MPS2;  // -3.5 m/s²
}
```

#### 1.4.4 車間警告判定

```c
float actual_gap_time = target_distance_m / ego_speed_mps;
float set_gap_time = GAP_TIME_TABLE[gap_setting];

if (actual_gap_time < set_gap_time * 0.30f) {
    warning_visual = true;
    warning_audio = true;
} else if (actual_gap_time < set_gap_time * 0.50f) {
    warning_visual = true;
    warning_audio = false;
}
```

### 1.5 Emergency (緊急制動)

#### 1.5.1 TTC計算

```c
float calc_ttc(float distance_m, float rel_speed_mps) {
    if (rel_speed_mps >= 0.0f) {
        return FLT_MAX;  // 離反中→衝突なし
    }
    return distance_m / (-rel_speed_mps);
}
```

#### 1.5.2 緊急制動判定

```c
float ttc = calc_ttc(target_distance_m, target_rel_speed_mps);

if (ttc < TTC_BRAKE_THRESHOLD && target_rel_speed_mps < 0.0f) {
    emergency_brake_active = true;
}

if (ttc > TTC_RELEASE_THRESHOLD || !target_detected) {
    emergency_brake_active = false;
}
```

- TTC_BRAKE_THRESHOLD = 1.5秒
- TTC_RELEASE_THRESHOLD = 3.0秒

### 1.6 Override (運転者介入)

#### 1.6.1 オーバーライド検出

```c
// ブレーキによるオーバーライド
bool brake_override = (input->brake_force_n > 10.0f);

// アクセルによるオーバーライド
float set_speed_accel_pct = calc_accel_for_speed(set_speed_kph + 5.0f);
bool accel_override = (input->accel_pedal_pct > set_speed_accel_pct);

override_detected = brake_override || accel_override;
```

#### 1.6.2 復帰判定

```c
if (!brake_override && !accel_override) {
    release_timer_ms += elapsed_ms;
    if (release_timer_ms >= 2000 && ego_speed_kph >= 25.0f) {
        return_to_active = true;
    }
} else {
    release_timer_ms = 0;
}
```

### 1.7 FaultMgr (故障管理)

#### 1.7.1 センサ診断

```c
// 自車速度センサ
if (input->ego_speed_kph < -1.0f || input->ego_speed_kph > 251.0f) {
    speed_sensor_fault = true;
}
if (speed_update_timer_ms > 500) {
    speed_sensor_fault = true;
}

// レーダーセンサ
if (input->target_distance_m < 0.0f || input->target_distance_m > 201.0f) {
    radar_fault = true;
}
if (radar_update_timer_ms > 200) {
    radar_fault = true;
}
```

#### 1.7.2 フォルトコード

| コード | 意味 |
|-------|------|
| 0x0000 | 正常 |
| 0x0001 | 自車速度センサ範囲外 |
| 0x0002 | 自車速度センサタイムアウト |
| 0x0004 | レーダー範囲外 |
| 0x0008 | レーダータイムアウト |
| 0x0010 | 制御異常 |

## 2. 加速度要求調停

```c
// 各モジュールからの加速度要求を収集
float accel_speed = speed_ctrl_accel;      // 車速維持
float accel_gap = gap_ctrl_accel;          // 車間維持
float accel_emergency = emergency_active ? EMERGENCY_DECEL : 0.0f;

// 最小値選択（安全側）
float final_accel = fminf(fminf(accel_speed, accel_gap), accel_emergency);

// 最終的な範囲制限
if (final_accel > MAX_ACCEL_MPS2) final_accel = MAX_ACCEL_MPS2;    // +2.0
if (final_accel < MAX_DECEL_MPS2) final_accel = MAX_DECEL_MPS2;    // -8.0
```

## 3. 定数定義

```c
#define ACC_MAX_ACCEL_MPS2          2.0f
#define ACC_MAX_DECEL_NORMAL_MPS2  -3.5f
#define ACC_MAX_DECEL_EMERG_MPS2   -8.0f
#define ACC_MIN_ACTIVE_SPEED_KPH   30.0f
#define ACC_MIN_SET_SPEED_KPH      30.0f
#define ACC_MAX_SET_SPEED_KPH     180.0f
#define ACC_OVERRIDE_TIMEOUT_MS   2000
#define ACC_TTC_BRAKE_THRESHOLD    1.5f
#define ACC_TTC_RELEASE_THRESHOLD  3.0f
#define ACC_BRAKE_OVERRIDE_N      10.0f
#define ACC_SPEED_SENSOR_TIMEOUT  500
#define ACC_RADAR_TIMEOUT         200
#define ACC_FAULT_OK_DURATION    1000
```

## 4. 要求トレーサビリティ

| 要求ID | 設計要素 |
|--------|---------|
| FR-001 | StateMgr: ACC_State |
| FR-002 | StateMgr: 状態遷移テーブル |
| FR-003 | SpeedCtrl: set_speed範囲チェック |
| FR-004 | SpeedCtrl: SET+/SET-/RESUME処理 |
| FR-005 | SpeedCtrl: 車速維持加速度計算 |
| FR-006 | GapCtrl: GAP_TIME_TABLE |
| FR-007 | GapCtrl: 車間制御加速度計算 |
| FR-008 | GapCtrl: 車間警告判定 |
| FR-009 | Emergency: TTC計算、緊急制動判定 |
| FR-010 | Emergency: 緊急制動解除判定 |
| FR-011 | Override: オーバーライド検出 |
| FR-012 | Override: 復帰判定 |
| FR-013 | FaultMgr: センサ診断 |
| FR-014 | StateMgr: FAULT遷移、FaultMgr: コード記録 |
| FR-015 | FaultMgr: リセット判定 |
| FR-016 | SpeedCtrl: 勾配補正（実装オプション） |
| FR-017 | SpeedCtrl: カーブ減速（実装オプション） |
