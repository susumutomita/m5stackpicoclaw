# M5Stack PicoClaw セットアップガイド

M5Stack Basic/Gray + LLM Module (AX630C) で Sipeed PicoClaw を動かす手順。

## アーキテクチャ

```
┌─────────────────────┐     UART      ┌──────────────────────────┐
│  M5Stack Basic      │  (115200bps)  │  LLM Module (AX630C)     │
│  (ESP32)            │◄────────────►│  Linux (ARM Cortex-A53)  │
│                     │   Port C      │                          │
│  - UI 表示          │   GPIO16/17   │  - PicoClaw (AI Agent)   │
│  - ボタン操作       │               │  - UART Bridge (Python)  │
│  - チャット入力     │               │  - OpenAI API Plugin     │
│                     │               │  - Qwen2.5-0.5B (LLM)   │
└─────────────────────┘               └──────────────────────────┘
```

## 必要なもの

- M5Stack Basic/Gray
- M5Stack LLM Module (AX630C)
- USB-C ケーブル
- Wi-Fi 接続環境（初期セットアップ用）
- Arduino IDE（M5Stack プログラム書き込み用）

## 手順

### Step 1: LLM Module の初期設定

1. M5Stack に LLM Module を接続
2. USB-C で PC に接続
3. LLM Module の IP アドレスを確認（M5Stack の画面または UART で確認）

### Step 2: LLM Module に SSH 接続

```bash
# LLM Module の IP アドレスに SSH 接続
ssh root@<LLM_MODULE_IP>
# デフォルトパスワードは M5Stack の公式ドキュメントを参照
```

### Step 3: M5Stack apt リポジトリの追加

```bash
# M5Stack 公式ドキュメントに従い apt リポジトリを追加
# https://docs.m5stack.com/en/stackflow/module_llm/software
```

### Step 4: PicoClaw セットアップスクリプトの実行

```bash
# スクリプトを LLM Module にコピー
scp scripts/setup_picoclaw.sh root@<LLM_MODULE_IP>:/tmp/

# SSH で接続してスクリプトを実行
ssh root@<LLM_MODULE_IP>
chmod +x /tmp/setup_picoclaw.sh
/tmp/setup_picoclaw.sh
```

スクリプトが以下を自動でインストール・設定します:
- OpenAI API プラグイン（ローカル LLM のAPI化）
- PicoClaw v0.2.2 (ARM64 バイナリ)
- UART ブリッジスクリプト
- systemd サービス

### Step 5: LLM Module を再起動

```bash
reboot
```

### Step 6: Arduino スケッチの書き込み

1. Arduino IDE を開く
2. **ボードマネージャ**: `M5Stack` ボードをインストール
3. **ライブラリマネージャ**:
   - `M5Stack` をインストール
   - `ArduinoJson` をインストール
4. `m5stack_picoclaw/m5stack_picoclaw.ino` を開く
5. ボードを `M5Stack-Core-ESP32` に設定
6. コンパイル＆アップロード

### Step 7: 動作確認

1. M5Stack の画面に「PicoClaw Controller」が表示される
2. UART ブリッジが接続されると「Bridge Connected」と表示
3. **ボタン A** を押して PicoClaw を起動
4. 「PicoClaw Running」と表示されれば成功

## 操作方法

| ボタン | 機能 |
|--------|------|
| A | PicoClaw 起動/停止 |
| B | ステータス確認 |
| C | チャットモード ON/OFF |

### チャットモード

1. **ボタン C** を押してチャットモードに入る
2. PC の **シリアルモニタ**（115200bps）でメッセージを入力
3. Enter で送信
4. LLM Module 上の Qwen2.5-0.5B が応答を生成
5. M5Stack 画面に結果が表示される

## トラブルシューティング

### Bridge に接続できない
- LLM Module が正しく装着されているか確認
- `picoclaw-bridge` サービスが動いているか SSH で確認:
  ```bash
  systemctl status picoclaw-bridge
  ```

### PicoClaw が起動しない
- OpenAI API プラグインが動いているか確認:
  ```bash
  systemctl status llm-openai-api
  curl http://127.0.0.1:8000/v1/models
  ```

### チャットの応答がない
- LLM モデルがロードされているか確認:
  ```bash
  curl -X POST http://127.0.0.1:8000/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{"model":"qwen2.5-0.5b","messages":[{"role":"user","content":"hello"}]}'
  ```

## ファイル構成

```
m5stackpicoclaw/
├── m5stack_picoclaw/
│   └── m5stack_picoclaw.ino    # M5Stack Arduino スケッチ
├── scripts/
│   └── setup_picoclaw.sh       # LLM Module セットアップスクリプト
├── SETUP.md                    # このファイル
└── README.md
```

## 参考リンク

- [M5Stack LLM Module ドキュメント](https://docs.m5stack.com/en/module/Module-llm)
- [M5Stack OpenAI API ガイド](https://docs.m5stack.com/en/guide/llm/openai_api/intro)
- [Sipeed PicoClaw GitHub](https://github.com/sipeed/picoclaw)
- [M5Module-LLM Arduino ライブラリ](https://github.com/m5stack/M5Module-LLM)
