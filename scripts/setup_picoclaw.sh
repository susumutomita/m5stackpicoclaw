#!/bin/bash
# =============================================================
# M5Stack LLM Module - PicoClaw セットアップスクリプト
# =============================================================
# このスクリプトはM5Stack LLM Module (AX630C) に SSH 接続して実行します。
#
# 前提条件:
#   - M5Stack LLM Module がネットワークに接続済み
#   - SSH でアクセス可能
#   - M5Stack apt リポジトリが設定済み
# =============================================================

set -e

PICOCLAW_VERSION="v0.2.2"
PICOCLAW_ARCH="aarch64"  # AX630C は ARM Cortex-A53 (64-bit)
PICOCLAW_URL="https://github.com/sipeed/picoclaw/releases/download/${PICOCLAW_VERSION}/picoclaw-${PICOCLAW_VERSION}-linux-${PICOCLAW_ARCH}.tar.gz"
INSTALL_DIR="/opt/picoclaw"
CONFIG_DIR="/root/.picoclaw"

echo "=========================================="
echo " M5Stack LLM Module - PicoClaw Setup"
echo "=========================================="

# --- Step 1: OpenAI API プラグインのインストール ---
echo ""
echo "[1/5] OpenAI API プラグインをインストール中..."
apt-get update
apt-get install -y llm-sys llm-llm llm-openai-api

# --- Step 2: PicoClaw バイナリのダウンロード ---
echo ""
echo "[2/5] PicoClaw ${PICOCLAW_VERSION} をダウンロード中..."
mkdir -p "${INSTALL_DIR}"
cd /tmp
wget -q "${PICOCLAW_URL}" -O picoclaw.tar.gz
tar xzf picoclaw.tar.gz -C "${INSTALL_DIR}"
chmod +x "${INSTALL_DIR}/picoclaw"
ln -sf "${INSTALL_DIR}/picoclaw" /usr/local/bin/picoclaw

echo "PicoClaw installed: $(picoclaw --version 2>/dev/null || echo 'binary ready')"

# --- Step 3: PicoClaw 設定ファイルの生成 ---
echo ""
echo "[3/5] PicoClaw 設定ファイルを生成中..."
mkdir -p "${CONFIG_DIR}"

cat > "${CONFIG_DIR}/config.json" << 'CONFIGEOF'
{
  "default_model": "local-qwen",
  "model_list": [
    {
      "name": "local-qwen",
      "provider": "openai",
      "model": "qwen2.5-0.5b",
      "api_key": "not-needed",
      "base_url": "http://127.0.0.1:8000/v1"
    }
  ],
  "agent": {
    "max_steps": 20,
    "timeout": 60
  },
  "gateway": {
    "type": "stdio"
  }
}
CONFIGEOF

echo "Config written to ${CONFIG_DIR}/config.json"

# --- Step 4: systemd サービスの作成 ---
echo ""
echo "[4/5] systemd サービスを作成中..."

cat > /etc/systemd/system/picoclaw.service << 'SERVICEEOF'
[Unit]
Description=PicoClaw AI Agent
After=network.target llm-openai-api.service
Wants=llm-openai-api.service

[Service]
Type=simple
ExecStartPre=/bin/sleep 5
ExecStart=/usr/local/bin/picoclaw
Restart=on-failure
RestartSec=10
Environment=HOME=/root

[Install]
WantedBy=multi-user.target
SERVICEEOF

systemctl daemon-reload
systemctl enable picoclaw.service

# --- Step 5: UART ブリッジスクリプトの作成 ---
echo ""
echo "[5/5] UART ブリッジスクリプトを作成中..."

cat > "${INSTALL_DIR}/uart_bridge.py" << 'BRIDGEEOF'
#!/usr/bin/env python3
"""
M5Stack ESP32 <-> PicoClaw UART ブリッジ
M5Stack Core (ESP32) から UART 経由でコマンドを受信し、
PicoClaw に転送して結果を返します。
"""
import serial
import subprocess
import json
import sys
import time
import threading

UART_PORT = "/dev/ttyS1"  # M5Stack LLM Module の内部 UART
UART_BAUD = 115200

class UARTBridge:
    def __init__(self):
        self.ser = serial.Serial(UART_PORT, UART_BAUD, timeout=1)
        self.running = True

    def send_response(self, msg_type, data):
        response = json.dumps({"type": msg_type, "data": data}) + "\n"
        self.ser.write(response.encode("utf-8"))
        self.ser.flush()

    def handle_command(self, cmd):
        try:
            parsed = json.loads(cmd)
        except json.JSONDecodeError:
            self.send_response("error", "invalid JSON")
            return

        action = parsed.get("action", "")

        if action == "start":
            subprocess.Popen(["systemctl", "start", "picoclaw"])
            self.send_response("status", "picoclaw started")

        elif action == "stop":
            subprocess.Popen(["systemctl", "stop", "picoclaw"])
            self.send_response("status", "picoclaw stopped")

        elif action == "status":
            result = subprocess.run(
                ["systemctl", "is-active", "picoclaw"],
                capture_output=True, text=True
            )
            self.send_response("status", result.stdout.strip())

        elif action == "chat":
            prompt = parsed.get("prompt", "")
            if not prompt:
                self.send_response("error", "no prompt provided")
                return
            self.run_chat(prompt)

        elif action == "ping":
            self.send_response("pong", "ok")

        else:
            self.send_response("error", f"unknown action: {action}")

    def run_chat(self, prompt):
        """OpenAI API 経由でローカル LLM にプロンプトを送信"""
        try:
            import urllib.request
            req_data = json.dumps({
                "model": "qwen2.5-0.5b",
                "messages": [{"role": "user", "content": prompt}],
                "max_tokens": 512,
                "stream": False
            }).encode("utf-8")

            req = urllib.request.Request(
                "http://127.0.0.1:8000/v1/chat/completions",
                data=req_data,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=30) as resp:
                result = json.loads(resp.read().decode("utf-8"))
                content = result["choices"][0]["message"]["content"]
                self.send_response("chat", content)
        except Exception as e:
            self.send_response("error", str(e))

    def run(self):
        print(f"UART Bridge started on {UART_PORT} @ {UART_BAUD}bps")
        self.send_response("status", "bridge ready")
        buf = ""
        while self.running:
            try:
                data = self.ser.read(256)
                if data:
                    buf += data.decode("utf-8", errors="ignore")
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip()
                        if line:
                            self.handle_command(line)
            except serial.SerialException:
                print("UART connection lost, reconnecting...")
                time.sleep(2)
                try:
                    self.ser.close()
                    self.ser = serial.Serial(UART_PORT, UART_BAUD, timeout=1)
                except Exception:
                    pass
            except KeyboardInterrupt:
                self.running = False

        self.ser.close()
        print("UART Bridge stopped")

if __name__ == "__main__":
    bridge = UARTBridge()
    bridge.run()
BRIDGEEOF

chmod +x "${INSTALL_DIR}/uart_bridge.py"

# UART ブリッジの systemd サービス
cat > /etc/systemd/system/picoclaw-bridge.service << 'BSERVICEEOF'
[Unit]
Description=PicoClaw UART Bridge for M5Stack
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /opt/picoclaw/uart_bridge.py
Restart=on-failure
RestartSec=5
Environment=HOME=/root

[Install]
WantedBy=multi-user.target
BSERVICEEOF

systemctl daemon-reload
systemctl enable picoclaw-bridge.service

echo ""
echo "=========================================="
echo " セットアップ完了！"
echo "=========================================="
echo ""
echo "次のステップ:"
echo "  1. デバイスを再起動: reboot"
echo "  2. 再起動後、サービスを確認:"
echo "     systemctl status picoclaw-bridge"
echo "     systemctl status picoclaw"
echo ""
echo "手動で起動する場合:"
echo "  systemctl start picoclaw-bridge"
echo "  systemctl start picoclaw"
echo ""
