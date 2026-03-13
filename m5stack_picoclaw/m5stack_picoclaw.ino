/**
 * M5Stack PicoClaw Controller
 *
 * M5Stack Basic/Gray から LLM Module 上の PicoClaw を制御する
 * Arduino スケッチ。
 *
 * 機能:
 *   - ボタンA: PicoClaw 起動/停止
 *   - ボタンB: ステータス確認
 *   - ボタンC: チャットモード（シリアルから入力）
 *   - 画面: ステータス表示、チャット応答表示
 *
 * 必要なライブラリ:
 *   - M5Stack (Board Manager: M5Stack)
 *   - ArduinoJson
 *
 * 接続:
 *   M5Stack Core (ESP32) <--UART--> LLM Module (AX630C)
 *   Port C: GPIO16(RX), GPIO17(TX) @ 115200bps
 */

#include <M5Stack.h>
#include <ArduinoJson.h>

// --- UART 設定 ---
#define LLM_SERIAL    Serial2
#define LLM_BAUD      115200
#define LLM_RX_PIN    16  // Port C RX
#define LLM_TX_PIN    17  // Port C TX

// --- 状態管理 ---
enum AppState {
    STATE_INIT,
    STATE_READY,
    STATE_STARTING,
    STATE_RUNNING,
    STATE_CHATTING,
    STATE_ERROR
};

AppState currentState = STATE_INIT;
String lastResponse = "";
String inputBuffer = "";
unsigned long lastPingTime = 0;
const unsigned long PING_INTERVAL = 10000;  // 10秒ごとにping

// --- 画面レイアウト ---
const int HEADER_Y = 0;
const int STATUS_Y = 30;
const int CONTENT_Y = 60;
const int FOOTER_Y = 220;

// --- 関数プロトタイプ ---
void drawHeader();
void drawStatus(const char* status, uint16_t color);
void drawContent(const String& text);
void drawFooter();
void sendCommand(const String& action, const String& prompt = "");
void processResponse(const String& line);
void handleButtonA();
void handleButtonB();
void handleButtonC();
void handleSerialInput();

void setup() {
    M5.begin();
    Serial.begin(115200);
    LLM_SERIAL.begin(LLM_BAUD, SERIAL_8N1, LLM_RX_PIN, LLM_TX_PIN);

    M5.Lcd.setTextSize(2);
    M5.Lcd.fillScreen(BLACK);

    drawHeader();
    drawStatus("Initializing...", YELLOW);
    drawFooter();

    Serial.println("[M5Stack PicoClaw Controller]");
    Serial.println("Waiting for UART bridge...");

    // 初期化完了後 ping を送信
    delay(2000);
    sendCommand("ping");

    currentState = STATE_INIT;
}

void loop() {
    M5.update();

    // UART からのレスポンスを処理
    while (LLM_SERIAL.available()) {
        char c = LLM_SERIAL.read();
        if (c == '\n') {
            if (inputBuffer.length() > 0) {
                processResponse(inputBuffer);
                inputBuffer = "";
            }
        } else if (c != '\r') {
            inputBuffer += c;
        }
    }

    // USB シリアルからの入力（チャットモード用）
    handleSerialInput();

    // ボタン処理
    if (M5.BtnA.wasPressed()) handleButtonA();
    if (M5.BtnB.wasPressed()) handleButtonB();
    if (M5.BtnC.wasPressed()) handleButtonC();

    // 定期的な ping
    if (currentState != STATE_INIT && millis() - lastPingTime > PING_INTERVAL) {
        sendCommand("ping");
        lastPingTime = millis();
    }

    delay(10);
}

// --- コマンド送信 ---
void sendCommand(const String& action, const String& prompt) {
    StaticJsonDocument<512> doc;
    doc["action"] = action;
    if (prompt.length() > 0) {
        doc["prompt"] = prompt;
    }

    String json;
    serializeJson(doc, json);
    LLM_SERIAL.println(json);

    Serial.print("[TX] ");
    Serial.println(json);
}

// --- レスポンス処理 ---
void processResponse(const String& line) {
    Serial.print("[RX] ");
    Serial.println(line);

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    const char* type = doc["type"] | "unknown";
    const char* data = doc["data"] | "";

    if (strcmp(type, "pong") == 0) {
        if (currentState == STATE_INIT) {
            currentState = STATE_READY;
            drawStatus("Bridge Connected", GREEN);
            drawContent("PicoClaw ready.\nPress [A] to start.");
        }
    }
    else if (strcmp(type, "status") == 0) {
        if (strcmp(data, "picoclaw started") == 0) {
            currentState = STATE_RUNNING;
            drawStatus("PicoClaw Running", GREEN);
            drawContent("PicoClaw is active.\n\nPress [C] for chat mode.\nPress [A] to stop.");
        }
        else if (strcmp(data, "picoclaw stopped") == 0) {
            currentState = STATE_READY;
            drawStatus("PicoClaw Stopped", YELLOW);
            drawContent("Press [A] to start.");
        }
        else if (strcmp(data, "active") == 0) {
            currentState = STATE_RUNNING;
            drawStatus("PicoClaw Running", GREEN);
        }
        else if (strcmp(data, "inactive") == 0 || strcmp(data, "failed") == 0) {
            currentState = STATE_READY;
            drawStatus("PicoClaw Stopped", YELLOW);
        }
        else if (strcmp(data, "bridge ready") == 0) {
            currentState = STATE_READY;
            drawStatus("Bridge Ready", GREEN);
            drawContent("Press [A] to start PicoClaw.");
        }
        else {
            drawContent(String("Status: ") + data);
        }
    }
    else if (strcmp(type, "chat") == 0) {
        currentState = STATE_RUNNING;
        drawStatus("PicoClaw Running", GREEN);
        lastResponse = data;
        drawContent(lastResponse);
        Serial.print("[Chat Response] ");
        Serial.println(data);
    }
    else if (strcmp(type, "error") == 0) {
        drawStatus("Error", RED);
        drawContent(String("Error: ") + data);
        Serial.print("[Error] ");
        Serial.println(data);
    }
}

// --- ボタンハンドラ ---
void handleButtonA() {
    switch (currentState) {
        case STATE_READY:
            currentState = STATE_STARTING;
            drawStatus("Starting PicoClaw...", YELLOW);
            sendCommand("start");
            break;
        case STATE_RUNNING:
        case STATE_CHATTING:
            drawStatus("Stopping PicoClaw...", YELLOW);
            sendCommand("stop");
            break;
        default:
            sendCommand("ping");
            break;
    }
}

void handleButtonB() {
    drawStatus("Checking status...", CYAN);
    sendCommand("status");
}

void handleButtonC() {
    if (currentState == STATE_RUNNING) {
        currentState = STATE_CHATTING;
        drawStatus("Chat Mode", MAGENTA);
        drawContent("Type in Serial Monitor\nand press Enter to chat.\n\nPress [A] to exit.");
        Serial.println("\n--- Chat Mode ---");
        Serial.println("Type your message and press Enter:");
    } else if (currentState == STATE_CHATTING) {
        currentState = STATE_RUNNING;
        drawStatus("PicoClaw Running", GREEN);
        drawContent("Chat mode exited.");
    } else {
        drawContent("Start PicoClaw first.\nPress [A] to start.");
    }
}

// --- シリアル入力処理（チャットモード） ---
void handleSerialInput() {
    static String serialBuf = "";

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBuf.length() > 0) {
                if (currentState == STATE_CHATTING || currentState == STATE_RUNNING) {
                    Serial.print("[Sending] ");
                    Serial.println(serialBuf);
                    drawStatus("Thinking...", YELLOW);
                    drawContent(String("Q: ") + serialBuf + "\n\nWaiting...");
                    sendCommand("chat", serialBuf);
                } else {
                    Serial.println("Not in chat mode. Press [C] first.");
                }
                serialBuf = "";
            }
        } else {
            serialBuf += c;
        }
    }
}

// --- 画面描画 ---
void drawHeader() {
    M5.Lcd.fillRect(0, HEADER_Y, 320, 28, NAVY);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, HEADER_Y + 5);
    M5.Lcd.print("PicoClaw Controller");
}

void drawStatus(const char* status, uint16_t color) {
    M5.Lcd.fillRect(0, STATUS_Y, 320, 25, BLACK);
    M5.Lcd.setTextColor(color);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, STATUS_Y + 3);
    M5.Lcd.print(status);
}

void drawContent(const String& text) {
    M5.Lcd.fillRect(0, CONTENT_Y, 320, FOOTER_Y - CONTENT_Y, BLACK);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(5, CONTENT_Y + 5);

    // テキストを行ごとに描画（自動改行対応）
    int x = 5, y = CONTENT_Y + 5;
    int maxY = FOOTER_Y - 10;
    for (unsigned int i = 0; i < text.length() && y < maxY; i++) {
        char c = text.charAt(i);
        if (c == '\n') {
            x = 5;
            y += 12;
            M5.Lcd.setCursor(x, y);
        } else {
            M5.Lcd.print(c);
            x += 6;
            if (x > 310) {
                x = 5;
                y += 12;
                M5.Lcd.setCursor(x, y);
            }
        }
    }
}

void drawFooter() {
    M5.Lcd.fillRect(0, FOOTER_Y, 320, 20, DARKGREY);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, FOOTER_Y + 5);
    M5.Lcd.print("[A]Start/Stop  [B]Status  [C]Chat");
}
