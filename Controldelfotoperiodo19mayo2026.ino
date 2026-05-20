#include <Adafruit_GFX.h>    
#include <Adafruit_ST7789.h> 
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>
#include <math.h>

// ===== PINES ESP32-S3 SUPERMINI == direccion mac 94:A9:90:37:7A:EC ===
#define TFT_CS 10
#define TFT_RST 9
#define TFT_DC 8
#define TOUCH_CS 7
#define SD_CS 5
#define RELAY_PIN 4

#define MI_NEGRO 0x0000
#define MI_BLANCO 0xFFFF
#define MI_MORADO 0xFFE0
#define MI_NARANJA 0xFD20

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

// ===== VARIABLES CULTIVO =====
int lightHours = 12, darkHours = 12, daysVeg = 0, daysFlower = 0;
bool isVegetative = true, inLightMode = true, showGraph = false;
double photoSecondsElapsed = 0;
float history[24]; 

// =====================================================
// ================= ESP NOW ============================
// =====================================================
typedef struct struct_message {
    int lightHours;
    int darkHours;
    int daysVeg;
    int daysFlower;
    bool isVegetative;
    bool inLightMode;
    float progressPercent;
    int hour;
    int minute;
    int second;
    float vpd;
} struct_message;

struct_message growData;

typedef struct greenhouse_message {
    float vpd;
    float tds;
    float ph;
    float soil1;
    float soil2;
    float co2;
    float airTemp;
    float airHum;
    bool relayState;
} greenhouse_message;

greenhouse_message greenhouseData;

typedef struct soil_message {

    float soil1;
    float soil2;
    float co2;

} soil_message;

// =====================================================
// ================= MAC RECEPTOR ======================
// =====================================================
uint8_t macInvernadero[] = {
    0x94, 0xA9, 0x90,
    0x37, 0x7A, 0xEC
};
uint8_t macCalendarioESP[] = {
    0xE0, 0x72, 0xA1,
    0xE7, 0x7C, 0x8C
};
uint8_t macSoilNode[] = {
    0xAC, 0xA7, 0x04,
    0xB8, 0x0C, 0xAC
};

unsigned long lastMillis, lastUIRefresh, lastSave, lastHistoryUpdate;
unsigned long lastWifiCheck = 0; // Para el refresco de WiFi
uint8_t wifiChannel = 1;
unsigned long touchStartTime = 0;
unsigned long lastEspNowSend = 0;
unsigned long lastAutoSave = 0;
float remoteVPD = 0;
float remoteTDS = 0;
float remotePH = 0;
float remoteSoil1 = 0;
float remoteSoil2 = 0;
float remoteCO2 = 0;
float remoteTemp = 0;
float remoteHum = 0;
bool remoteRelay = false;
unsigned long lastEspNowReceiveMs = 0;
void drawWeedagotchi();
float lastDisplayedVPD = -999.0;
float lastDisplayedTDS = -999.0;
float lastDisplayedSoil1 = -999.0;
float lastDisplayedSoil2 = -999.0;
float lastDisplayedCO2 = -999.0;
unsigned long lastVpdDraw = 0;
unsigned long lastTdsDraw = 0;
unsigned long lastSoilDraw = 0;
unsigned long lastCO2Draw = 0;

// Dirty flags + cache
bool uiDirtyClock = true;
bool uiDirtyProgress = true;
bool uiDirtySensors = true;
bool uiDirtySettings = true;
bool graphDirty = true;
bool stateDirty = false;
bool historyDirty = false;

char lastClock[12] = "";
char lastPhase[12] = "";
int lastDaysVeg = -1;
int lastDaysFlower = -1;

struct tm cachedTime;
bool cachedTimeValid = false;
unsigned long lastTimeCacheMs = 0;

unsigned long lastTouchDebounceMs = 0;
bool lastTouchState = false;
unsigned long touchHoldStartMs = 0;
unsigned long lastTouchRepeatMs = 0;

unsigned long lastPerfMs = 0;
uint32_t frameTime = 0;

unsigned long lastStateSaveMs = 0;
unsigned long lastHistorySaveMs = 0;

// Variables Salvapantallas
bool screensaverActive = false;
unsigned long lastTouchTime = 0;

// Credenciales WiFi
const char* ssid = "IZZI-367E";
const char* password = "ehwa3pX7btcw";

// ===== PERSISTENCIA SD =====
// =====================================================
// ================= ESP NOW CALLBACK ==================
// =====================================================
void OnDataSent(
    const wifi_tx_info_t *info,
    esp_now_send_status_t status) {
    Serial.print("ESP NOW: ");
    if (status == ESP_NOW_SEND_SUCCESS)
        Serial.println("OK");
    else
        Serial.println("ERROR");
}

void OnDataRecv(
    const esp_now_recv_info_t *info,
    const uint8_t *incomingData,
    int len
) {
    Serial.print("FROM: ");

    char macStr[18];

    snprintf(
        macStr,
        sizeof(macStr),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        info->src_addr[0],
        info->src_addr[1],
        info->src_addr[2],
        info->src_addr[3],
        info->src_addr[4],
        info->src_addr[5]
    );

    Serial.println(macStr);

    //=====================================================
    // DATOS DEL SENSOR S1 S2 CO2
    //=====================================================
    
    if (len == sizeof(soil_message)) {

        soil_message incomingSoil;

        memcpy(
            &incomingSoil,
            incomingData,
            sizeof(incomingSoil)
        );

        remoteSoil1 = constrain(
          incomingSoil.soil1,
          0.0f,
          100.0f
        );

        remoteSoil2 = constrain(
          incomingSoil.soil2,
          0.0f,
          100.0f
        );

        remoteCO2 = max(
          incomingSoil.co2,
          0.0f
        );

        // FORZAR REDIBUJO
        lastDisplayedSoil1 = -999;
        uiDirtySensors = true;
        lastDisplayedSoil2 = -999;
        lastDisplayedCO2 = -999;

        lastEspNowReceiveMs = millis();

        Serial.println("[ESP RX SOIL]");
        Serial.printf("S1 %.1f\n", remoteSoil1);
        Serial.printf("S2 %.1f\n", remoteSoil2);
        Serial.printf("CO2 %.0f\n", remoteCO2);

        return;
    }


    if (len == sizeof(greenhouse_message)) {

        greenhouse_message incoming;

        memcpy(
            &incoming,
            incomingData,
            sizeof(incoming)
        );

        remoteVPD = incoming.vpd;
        remoteTDS = incoming.tds;

        remoteSoil1 = constrain(
            incoming.soil1,
            0.0f,
            100.0f
        );

        remoteSoil2 = constrain(
            incoming.soil2,
            0.0f,
            100.0f
        );

        remoteCO2 =
            max(
               incoming.co2,
               0.0f
            );

        remoteTemp = incoming.airTemp;
        remoteHum = incoming.airHum;

        remoteRelay =
            incoming.relayState;

        // FORZAR REFRESH
        lastDisplayedVPD = -999;
        uiDirtySensors = true;
        lastDisplayedTDS = -999;
        lastDisplayedSoil1 = -999;
        uiDirtySensors = true;
        lastDisplayedSoil2 = -999;
        lastDisplayedCO2 = -999;

        lastEspNowReceiveMs =
            millis();

        Serial.println(
            "[ESP RX GREENHOUSE]"
        );

        return;
    }

    if (len == sizeof(struct_message)) {
        struct_message incomingMessage;
        memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
        Serial.println("[ESP-NOW RX struct_message OK]");
        return;
    }

    Serial.println("[ESP-NOW RX] Unknown payload size");
}

void saveHistory() {
    SD.remove("/history.txt");
    File file = SD.open("/history.txt", FILE_WRITE);
    if (!file) return;
    for (int i = 0; i < 24; i++) {
        if (i > 0) file.print(',');
        file.printf("%.3f", history[i]);
    }
    file.close();
    Serial.println("[SD] Historial guardado");
}

void loadHistory() {
    for (int i = 0; i < 24; i++) history[i] = 0.0f;
    if (!SD.exists("/history.txt")) {
        saveHistory();
        return;
    }
    File file = SD.open("/history.txt");
    if (!file) return;
    char data[320] = {0};
    size_t n = file.readBytes(data, sizeof(data) - 1);
    data[n] = '\0';
    file.close();

    char *token = strtok(data, ",");
    int i = 0;
    while (token && i < 24) {
        history[i++] = atof(token);
        token = strtok(NULL, ",");
    }
    Serial.println("[SD] Historial cargado");
}

void saveState() {
    SD.remove("/estado.txt");
    File file = SD.open("/estado.txt", FILE_WRITE);
    if (file) {
        file.printf("%d,%d,%d,%d,%d,%.2f,%.2f,%d", lightHours, darkHours, (isVegetative ? 1 : 0), daysVeg, daysFlower, photoSecondsElapsed, remoteVPD, (inLightMode ? 1 : 0));
        file.close();
        saveHistory();
        Serial.println("[SD] Estado guardado");
    }
}

void loadState() {
    if (!SD.exists("/estado.txt")) {
        stateDirty = true; historyDirty = true;
        return;
    }
    File file = SD.open("/estado.txt");
    if (file) {
        char data[192] = {0};
        size_t n = file.readBytes(data, sizeof(data) - 1);
        data[n] = '\0';
        int v2, lightMode = 1;
        float savedVPD = 0.0f;
        int parsed = sscanf(data, "%d,%d,%d,%d,%d,%lf,%f,%d", &lightHours, &darkHours, &v2, &daysVeg, &daysFlower, &photoSecondsElapsed, &savedVPD, &lightMode);
        if (parsed >= 6) {
            isVegetative = (v2 == 1);
            if (parsed >= 7) remoteVPD = savedVPD;
            if (parsed >= 8) inLightMode = (lightMode == 1);
        }
        file.close();
        Serial.println("[SD] Estado cargado");
    }
}

void handleAutoSave() {
    static int prevLightHours = lightHours;
    static int prevDarkHours = darkHours;
    static bool prevVegetative = isVegetative;
    static bool prevLightMode = inLightMode;
    static float prevHistoryLast = history[23];

    bool configChanged = (lightHours != prevLightHours) || (darkHours != prevDarkHours) || (isVegetative != prevVegetative);
    bool phaseChanged = (inLightMode != prevLightMode);
    bool historyChanged = fabs(history[23] - prevHistoryLast) > 0.001f;
    bool timeElapsed = millis() - lastAutoSave > 120000;

    if (configChanged || phaseChanged || historyChanged || timeElapsed) {
        stateDirty = true; historyDirty = true;
        lastAutoSave = millis();
        prevLightHours = lightHours;
        prevDarkHours = darkHours;
        prevVegetative = isVegetative;
        prevLightMode = inLightMode;
        prevHistoryLast = history[23];
    }
    if (historyChanged && millis() - lastHistorySaveMs > 300000) {
        saveHistory();
        lastHistorySaveMs = millis();
    }
}


void setup() {
    Serial.begin(115200);
    // =====================================================
    // ================= WIFI STA ==========================
    // =====================================================
    WiFi.mode(WIFI_STA);
    Serial.print("MAC EMISOR: ");
    Serial.println(WiFi.macAddress());

    // WiFi.begin(ssid, password);
    // while (WiFi.status() != WL_CONNECTED) {
    //     delay(500);
    // }
    //
    // wifiChannel = WiFi.channel();
    // Serial.print("WIFI CHANNEL: ");
    // Serial.println(wifiChannel);


    // =====================================================
    // ================= ESP NOW INIT ======================
    // =====================================================
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP NOW ERROR");
        return;
    }
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // =====================================================
    // ================= PEER ==============================
    // =====================================================
    esp_now_peer_info_t peerInfo = {};
    memcpy(
        peerInfo.peer_addr,
        macInvernadero,
        6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo)
        != ESP_OK) {
        Serial.println("PEER ERROR");
        return;
    }
    esp_now_peer_info_t peerInfo2 = {};
    memcpy(peerInfo2.peer_addr, macSoilNode, 6);
    peerInfo2.channel = 0;
    peerInfo2.encrypt = false;
    if (esp_now_add_peer(&peerInfo2) != ESP_OK) {
        Serial.println("PEER2 ERROR");
        return;
    }
    esp_now_peer_info_t peerInfo3 = {};
    memcpy(peerInfo3.peer_addr, macCalendarioESP, 6);
    peerInfo3.channel = 0;
    peerInfo3.encrypt = false;
    if (esp_now_add_peer(&peerInfo3) != ESP_OK) {
        Serial.println("PEER3 ERROR");
        return;
    }

    delay(1000); 

    SPI.begin(12, 13, 11); 
    tft.init(240, 320); 
    tft.setRotation(1); 
    tft.invertDisplay(false); 
    tft.fillScreen(MI_NEGRO);
    
    ts.begin(); 
    ts.setRotation(1);
    
    if (SD.begin(SD_CS)) {
        loadState();
        loadHistory();
    }
    
    pinMode(RELAY_PIN, OUTPUT);
    
    configTime(-21600, 0, "pool.ntp.org"); // UTC-6
    
    lastMillis = millis();
    lastSave = millis();
    lastTouchTime = millis();
    lastAutoSave = millis();
}

void drawScreensaver() {
    static int xPos = 60;
    static int yPos = 100;
    static unsigned long lastMove = 0;

    if (millis() - lastMove > 30000 || screensaverActive == false) {
        tft.fillScreen(MI_NEGRO);
        struct tm ti;
        if (getLocalTime(&ti)) {
            char b[12];
            strftime(b, 12, "%I:%M %p", &ti);
            tft.setTextSize(2);
            tft.setTextColor(0x4208); 
            xPos = random(10, 160);
            yPos = random(20, 200);
            tft.setCursor(xPos, yPos);
            tft.print(b);
        }
        lastMove = millis();
        screensaverActive = true;
    }
}

void drawVPD() {
    if (
        fabs(remoteVPD - lastDisplayedVPD) > 0.01f ||
        millis() - lastVpdDraw > 2000
    ) {
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_YELLOW, MI_NEGRO);
        tft.setCursor(270, 16);
        tft.printf("VPD %.2f", remoteVPD);

        lastDisplayedVPD = remoteVPD;
        lastVpdDraw = millis();
    }
}

void drawTDS() {
    if (
        fabs(remoteTDS - lastDisplayedTDS) > 1.0f ||
        millis() - lastTdsDraw > 2000
    ) {
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_CYAN, MI_NEGRO);
        tft.setCursor(270, 28);
        tft.printf("TDS %.0f", remoteTDS);

        lastDisplayedTDS = remoteTDS;
        lastTdsDraw = millis();
    }
}


void drawCO2() {

    if (
        fabs(remoteCO2 - lastDisplayedCO2) < 1.0f
        &&
        millis() - lastCO2Draw < 250
    ) return;

    tft.fillRect(
        266,
        38,
        54,
        12,
        MI_NEGRO
    );

    tft.setTextSize(1);

    uint16_t co2Color =
        (
            remoteCO2 > 1900
        )
        ?
        ST77XX_RED
        :
        ST77XX_WHITE;

    tft.setTextColor(
        co2Color,
        MI_NEGRO
    );

    tft.setCursor(
        270,
        40
    );

    tft.printf(
        "CO2 %.0f",
        remoteCO2
    );

    lastDisplayedCO2 =
        remoteCO2;

    lastCO2Draw =
        millis();
   
}


void drawSoilBars() {

    if (
        fabs(
            remoteSoil1
            -
            lastDisplayedSoil1
        ) < 0.5f
        &&
        fabs(
            remoteSoil2
            -
            lastDisplayedSoil2
        ) < 0.5f
    ) return;

    const int barW = 10;
    const int barH = 45;

    const int barY = 55;

    const int bar1X = 275;
    const int bar2X = 292;

    auto drawBar =
    [&](int x,float value,uint16_t c)
    {

        value =
        constrain(
            value,
            0,
            100
        );

        int fill =
        (
            barH
            *
            value
        )
        /
        100;

        tft.fillRect(
            x,
            barY,
            barW,
            barH,
            MI_NEGRO
        );

        tft.drawRect(
            x,
            barY,
            barW,
            barH,
            0x8410
        );

        if(fill>0)
        {
            tft.fillRect(
                x+1,
                barY+
                (
                    barH
                    -
                    fill
                ),
                barW-2,
                fill-1,
                c
            );
        }
    };

    drawBar(
        bar1X,
        remoteSoil1,
        ST77XX_CYAN
    );

    drawBar(
        bar2X,
        remoteSoil2,
        ST77XX_BLUE
    );

    tft.setTextSize(1);

    tft.setTextColor(
        MI_BLANCO,
        MI_NEGRO
    );

    tft.setCursor(
        274,
        104
    );

    tft.print("S1");

    tft.setCursor(
        291,
        104
    );

    tft.print("S2");

    lastDisplayedSoil1 =
        remoteSoil1;

    lastDisplayedSoil2 =
        remoteSoil2;

    lastSoilDraw =
        millis();
    
}

void updateCachedTime() {
    unsigned long now = millis();
    if (now - lastTimeCacheMs >= 1000 || !cachedTimeValid) {
        cachedTimeValid = getLocalTime(&cachedTime);
        lastTimeCacheMs = now;
        uiDirtyClock = true;
        uiDirtyProgress = true;
    }
}

void handleTouchInput() {
    unsigned long now = millis();
    bool touched = ts.touched();
    if (touched != lastTouchState && now - lastTouchDebounceMs > 25) {
        lastTouchDebounceMs = now;
        lastTouchState = touched;
        if (touched) {
            TS_Point p = ts.getPoint();
            int tx = map(p.y, 200, 3800, 320, 0);
            int ty = map(p.x, 200, 3800, 0, 240);
            touchHoldStartMs = now;
            lastTouchRepeatMs = now;
            handleAction(tx, ty, 1);
            lastTouchTime = now;
        } else {
            touchHoldStartMs = 0;
        }
    }

    if (touched && touchHoldStartMs > 0) {
        TS_Point p = ts.getPoint();
        int tx = map(p.y, 200, 3800, 320, 0);
        int ty = map(p.x, 200, 3800, 0, 240);
        unsigned long held = now - touchHoldStartMs;
        unsigned long rep = (held > 1000) ? 90 : (held > 450 ? 140 : 220);
        int step = (held > 1200) ? 3 : 1;
        if (now - lastTouchRepeatMs >= rep) {
            handleAction(tx, ty, step);
            lastTouchRepeatMs = now;
        }
    }
}

void loop() {
    uint32_t loopStart = micros();
    updateCachedTime();
    updatePhotoperiod();
    handleTouchInput();

    if (millis() - lastWifiCheck > 30000) {
        if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
        lastWifiCheck = millis();
    }

    bool isNight = cachedTimeValid && cachedTime.tm_hour >= 0 && cachedTime.tm_hour < 8;

    if (isNight && (millis() - lastTouchTime > 30000)) {
        drawScreensaver();
    } else {
        if (screensaverActive) {
            screensaverActive = false;
            tft.fillScreen(MI_NEGRO);
            uiDirtyClock = uiDirtyProgress = uiDirtySensors = uiDirtySettings = true;
            graphDirty = true;
        }
        if (showGraph) drawGraph();
        else drawUI();
    }

    drawVPD(); drawTDS(); drawCO2(); drawSoilBars(); drawWeedagotchi();

    growData.lightHours = lightHours; growData.darkHours = darkHours;
    growData.daysVeg = daysVeg; growData.daysFlower = daysFlower;
    growData.isVegetative = isVegetative; growData.inLightMode = inLightMode;
    growData.progressPercent = (photoSecondsElapsed / ((lightHours + darkHours) * 3600.0)) * 100.0;
    growData.vpd = remoteVPD;
    if (cachedTimeValid) { growData.hour = cachedTime.tm_hour; growData.minute = cachedTime.tm_min; growData.second = cachedTime.tm_sec; }

    static struct_message lastSent = {};
    bool changed = memcmp(&lastSent, &growData, sizeof(growData)) != 0;
    if (changed || millis() - lastEspNowSend > 10000) {
        esp_now_send(macInvernadero, (uint8_t*)&growData, sizeof(growData));
        esp_now_send(macCalendarioESP, (uint8_t*)&growData, sizeof(growData));
        memcpy(&lastSent, &growData, sizeof(growData));
        lastEspNowSend = millis();
    }

    handleAutoSave();

    frameTime = micros() - loopStart;
    if (millis() - lastPerfMs >= 1000) {
        float loopMs = frameTime / 1000.0f;
        float fps = (frameTime > 0) ? 1000000.0f / frameTime : 0.0f;
        Serial.printf("[PERF] Loop %.2f ms | FPS %.1f\n", loopMs, fps);
        lastPerfMs = millis();
    }
}

void updatePhotoperiod() {
    unsigned long now = millis();
    double delta = (now - lastMillis) / 1000.0;
    lastMillis = now;
    double totalSecs = (lightHours + darkHours) * 3600.0;
    double lightSecs = lightHours * 3600.0;
    photoSecondsElapsed += delta;

    if (photoSecondsElapsed >= totalSecs) {
        photoSecondsElapsed = 0;
        if (isVegetative) daysVeg++; else daysFlower++;
        stateDirty = true; historyDirty = true;
    }

    if (photoSecondsElapsed < lightSecs) {
        digitalWrite(RELAY_PIN, LOW); 
        inLightMode = true;
    } else {
        digitalWrite(RELAY_PIN, HIGH); 
        inLightMode = false;
    }

    if (now - lastHistoryUpdate > 3600000) {
        for (int i = 0; i < 23; i++) history[i] = history[i+1];
        history[23] = (photoSecondsElapsed / totalSecs) * 100.0;
        lastHistoryUpdate = now;
        historyDirty = true;
        graphDirty = true;
    }
}

void printStyled(int x, int y, const char* label, const char* value, uint16_t valCol, int size, bool arrows = true) {
    tft.setTextSize(size); tft.setCursor(x, y);
    if (arrows) { tft.setTextColor(ST77XX_RED, MI_NEGRO); tft.print("< "); }
    tft.setTextColor(MI_BLANCO, MI_NEGRO); tft.print(label);
    tft.setTextColor(valCol, MI_NEGRO); tft.print(value);
    if (arrows) { tft.setTextColor(ST77XX_RED, MI_NEGRO); tft.print(">"); }
}

int happyFace[19][21] = {
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,0},
    {0,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,0},
    {0,0,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,1,0,0},
    {0,0,1,1,1,0,0,0,1,1,1,1,1,0,0,0,1,1,1,0,0},
    {0,0,1,1,1,1,0,0,1,1,1,1,1,0,0,1,1,1,1,0,0},
    {0,0,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,1,1,1,1,0,1,1,1,0,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0},
    {1,1,1,1,0,0,1,1,0,1,1,1,0,1,1,0,0,1,1,1,1},
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,0,1,1,1,0,0,0,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,1,1,1,1,0,0,0,1,0,0,0,1,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0}
};

int angryFace[19][21] = {
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,0},
    {0,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,0},
    {0,0,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,1,0,0},
    {0,0,1,1,1,0,0,0,1,1,1,1,1,0,0,0,1,1,1,0,0},
    {0,0,1,1,1,1,0,0,1,1,1,1,1,0,0,1,1,1,1,0,0},
    {0,0,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,1,1,1,1,0,1,1,1,0,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0},
    {1,1,1,1,0,0,1,1,0,0,1,0,0,1,1,0,0,1,1,1,1},
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,1,1,1,1,0,0,0,1,0,0,0,1,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0}
};

int sadFace[19][21] = {
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,0},
    {0,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,0},
    {0,0,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,1,0,0},
    {0,0,1,1,1,0,0,0,1,1,1,1,1,0,0,0,1,1,1,0,0},
    {0,0,1,1,1,1,0,0,1,1,1,1,1,0,0,1,1,1,1,0,0},
    {0,0,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,1,1,1,1,0,1,1,1,0,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0},
    {1,1,1,1,0,0,1,1,0,1,1,1,0,1,1,0,0,1,1,1,1},
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,0,1,1,0,1,1,1,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,1,1,1,1,0,0,0,1,0,0,0,1,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0}
};

int deadFace[19][21] = {
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,0},
    {0,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,0},
    {0,0,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,1,0,0},
    {0,0,1,1,1,0,0,0,1,1,1,1,1,0,0,0,1,1,1,0,0},
    {0,0,1,1,1,1,0,0,1,1,1,1,1,0,0,1,1,1,1,0,0},
    {0,0,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,1,1,0,1,0,1,0,1,0,1,1,0,0,0,0,0},
    {1,1,1,1,0,0,1,1,0,1,1,1,0,1,1,0,0,1,1,1,1},
    {0,1,1,1,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,1,1,1,1,0,0,0,1,0,0,0,1,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0}
};

void drawGameboyFrame(int x, int y, int p, int frame[19][21]) {
    uint16_t dark = 0x0320;
    uint16_t light = 0x07E0;

    auto px = [&](int xx, int yy, uint16_t c) {
        tft.fillRect(x + xx * p, y + yy * p, p, p, c);
    };

    for (int yy = 0; yy < 19; yy++) {
        for (int xx = 0; xx < 21; xx++) {
            if (frame[yy][xx]) {
                px(xx - 1, yy, dark);
                px(xx + 1, yy, dark);
                px(xx, yy - 1, dark);
                px(xx, yy + 1, dark);
            }
        }
    }

    for (int yy = 0; yy < 19; yy++) {
        for (int xx = 0; xx < 21; xx++) {
            if (frame[yy][xx]) {
                px(xx, yy, light);
            }
        }
    }
}

void drawWeedagotchi() {
    static int frame = 0;
    static int lastSway = 999;
    static int lastMoodBucket = -1;
    static unsigned long lastAnimMs = 0;
    if (millis() - lastAnimMs < 130) return;
    lastAnimMs = millis();
    frame = (frame + 1) % 4;
    int sway = (frame < 2) ? 0 : 1;
    float mood = (remoteSoil1 + remoteSoil2) * 0.5f;
    int moodBucket = (mood >= 60.0f) ? 0 : (mood >= 40.0f) ? 1 : (mood >= 10.0f) ? 2 : 3;

    if (sway == lastSway && moodBucket == lastMoodBucket) return;

    tft.fillRect(236, 116, 6, 60, MI_NEGRO);
    tft.fillRect(296, 116, 6, 60, MI_NEGRO);

    if (moodBucket == 0) {
        drawGameboyFrame(238 + sway, 118, 3, happyFace);
    } else if (moodBucket == 1) {
        drawGameboyFrame(238 + sway, 118, 3, angryFace);
    } else if (moodBucket == 2) {
        drawGameboyFrame(238 + sway, 118, 3, sadFace);
    } else {
        drawGameboyFrame(238 + sway, 118, 3, deadFace);
    }

    lastSway = sway;
    lastMoodBucket = moodBucket;
}

void drawUI() {
    if (uiDirtySettings) {
        tft.setTextSize(1);
        char buf[32];
        tft.fillRect(130, 45, 120, 12, MI_NEGRO);
        snprintf(buf, sizeof(buf), "Ciclo %dh", (lightHours + darkHours));
        tft.setCursor(135, 45); tft.setTextColor(ST77XX_YELLOW, MI_NEGRO); tft.print(buf);
        char l[12], d[12], vg[12], fl[12], mode[16];
        snprintf(l, sizeof(l), "%dh  ", lightHours); snprintf(d, sizeof(d), "%dh  ", darkHours);
        snprintf(vg, sizeof(vg), "%d  ", daysVeg); snprintf(fl, sizeof(fl), "%d  ", daysFlower);
        snprintf(mode, sizeof(mode), "%s", isVegetative ? "VEGETACION" : "FLORACION  ");
        printStyled(10, 70, "LUZ: ", l, ST77XX_YELLOW, 1);
        printStyled(170, 70, "OSC: ", d, MI_BLANCO, 1);
        printStyled(10, 100, "MODO: ", mode, isVegetative ? ST77XX_GREEN : MI_MORADO, 2);
        printStyled(10, 130, "DIAS VEG:  ", vg, ST77XX_GREEN, 2);
        printStyled(10, 160, "DIAS FLOR: ", fl, MI_MORADO, 2);
        uiDirtySettings = false;
    }
    if (uiDirtyClock) {
        tft.fillRect(55, 15, 150, 26, MI_NEGRO);
        tft.setTextSize(3); tft.setCursor(55, 15); tft.setTextColor(MI_BLANCO, MI_NEGRO);
        if (cachedTimeValid) { char b[12]; strftime(b, 12, "%I:%M %p", &cachedTime); if (strcmp(lastClock,b)!=0){ strcpy(lastClock,b); tft.print(b);} }
        else tft.print("--:-- --");
        uiDirtyClock = false;
    }
    if (uiDirtyProgress) {
        int bx=30,bw=260,by=190; double phaseDur=inLightMode?(lightHours*3600.0):(darkHours*3600.0);
        double phaseElap=inLightMode?photoSecondsElapsed:(photoSecondsElapsed-(lightHours*3600.0));
        float perc=(phaseElap/phaseDur)*100.0f; int fill=(int)((bw-4)*(perc/100.0f)); uint16_t mCol=isVegetative?ST77XX_GREEN:MI_MORADO;
        tft.drawRect(bx, by, bw, 15, MI_BLANCO); tft.fillRect(bx+2, by+2, fill, 11, mCol); tft.fillRect(bx+2+fill, by+2, (bw-4)-fill, 11, MI_NEGRO);
        tft.fillRect(10,220,220,14,MI_NEGRO);
        printStyled(10,220,"FASE: ",inLightMode?"LUZ      ":"OSCURIDAD",MI_BLANCO,1,true);
        tft.setCursor(170,220); tft.setTextColor(MI_BLANCO, MI_NEGRO); int h=(int)(phaseElap/3600),m=(int)((long)phaseElap%3600/60),sec=(int)((long)phaseElap%60);
        tft.printf("%02d:%02d:%02d ",h,m,sec); tft.setTextColor(MI_NARANJA, MI_NEGRO); tft.printf("%3d%%  ",(int)perc);
        uiDirtyProgress=false;
    }
}

void drawGraph() {
    static bool initialized = false;
    if (!initialized || graphDirty) {
        tft.fillScreen(MI_NEGRO);
        tft.setTextColor(MI_BLANCO); tft.setTextSize(2); tft.setCursor(60, 10); tft.print("HISTORIAL 24H (%)");
        tft.drawLine(30, 40, 30, 200, MI_BLANCO); tft.drawLine(30, 200, 300, 200, MI_BLANCO);
        initialized = true;
    }
    static float lastHist[24] = {0};
    for (int i=0;i<23;i++) {
        if (graphDirty || fabs(lastHist[i]-history[i])>0.01f || fabs(lastHist[i+1]-history[i+1])>0.01f) {
            int x1=30+(i*11),x2=30+((i+1)*11); tft.drawLine(x1,40,x2,200,MI_NEGRO);
            int y1=200-(history[i]*1.5), y2=200-(history[i+1]*1.5); tft.drawLine(x1,y1,x2,y2,ST77XX_GREEN);
            lastHist[i]=history[i]; lastHist[i+1]=history[i+1];
        }
    }
    tft.setTextSize(1); tft.setTextColor(ST77XX_RED); tft.setCursor(100,220); tft.print("< TOCAR PARA VOLVER >");
    graphDirty=false;
}

void handleAction(int tx, int ty, int step) {
    if (showGraph) { showGraph = false; tft.fillScreen(MI_NEGRO); return; }
    if (ty >= 180 && ty <= 210) {
        float ratio = constrain((float)(tx - 30) / 260.0, 0.0, 1.0);
        double phDur = inLightMode ? (lightHours*3600.0) : (darkHours*3600.0);
        photoSecondsElapsed = inLightMode ? (ratio*phDur) : (lightHours*3600.0 + (ratio*phDur));
    }
    else if (ty > 60 && ty < 90 && tx < 150) {
        if (tx < 75) lightHours = (lightHours > step) ? lightHours - step : 1;
        else lightHours = (lightHours < (100 - step)) ? lightHours + step : 99;
    }
    else if (ty > 60 && ty < 90 && tx >= 150) {
        if (tx < 230) darkHours = (darkHours > step) ? darkHours - step : 1;
        else darkHours = (darkHours < (100 - step)) ? darkHours + step : 99;
    }
    else if (ty > 90 && ty < 120) { isVegetative = !isVegetative; }
    else if (ty > 120 && ty < 150) { 
        if (tx < 150) daysVeg = (daysVeg >= step) ? daysVeg - step : 0;
        else daysVeg += step; 
    }
    else if (ty > 150 && ty < 180) { 
        if (tx < 150) daysFlower = (daysFlower >= step) ? daysFlower - step : 0;
        else daysFlower += step;
    }
    else if (ty > 210 && tx < 160) { 
        photoSecondsElapsed = inLightMode ? (lightHours * 3600.0 + 1) : 0; 
    }
    
    double lightSecs = lightHours * 3600.0;
    digitalWrite(RELAY_PIN, (photoSecondsElapsed < lightSecs) ? LOW : HIGH);
    inLightMode = (photoSecondsElapsed < lightSecs);

    stateDirty = true;
    uiDirtySettings = true;
    uiDirtyProgress = true;
    uiDirtyClock = true;
    graphDirty = true;
}
