// ===================================================================================
// === ESP32 DECK FISICO - FIRMWARE v4.0 =============================================
// === DESENVOLVEDOR: Luiz F. R. Pimentel ============================================
// === GITHUB: github.com/KanekiZLF ================================================
// ===================================================================================
// === CORRECOES E MELHORIAS v4.0 ====================================================
// === 1. Click Effects nao interrompem mais efeitos de fundo ========================
// === 2. Click Effects usam cores vibrantes aleatorias ==============================
// === 3. Sistema de scroll corrigido para TODOS os menus ============================
// === 4. Codigo organizado em modulos logicos =======================================
// === 5. Funcoes prototipadas TODAS implementadas ===================================
// === 6. Navegacao de menus corrigida (indices de retorno) ==========================
// === 7. Efeitos de clique sobrepostos ao fundo com transparencia ===================
// === 8. Sistema de "camadas" de LEDs implementado ==================================
// ===================================================================================

#include <FS.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WiFiUdp.h>
#include <FastLED.h>

using fs::FS;

// =========================================================================
// === MODULO 1: CONFIGURACOES GERAIS ======================================
// =========================================================================

const char *SSID_AP = "ESP32-Deck-Setup";
const char *PASS_AP = "12345678";
const char *PREFS_NAMESPACE = "deck_config";
const char *KEY_BRIGHTNESS = "brightness";
const char *KEY_EFFECT = "effect";
const char *KEY_CLICK_EFFECT = "click_effect";
const char *KEY_WIFI_SSID = "wifi_ssid";
const char *KEY_WIFI_PASS = "wifi_pass";
const char *KEY_THEME = "theme";

const int TCP_PORT = 8000;
const int UDP_SEARCH_PORT = 4210;
const char *UDP_DISCOVER_MSG = "ESP32_DECK_DISCOVER";
const char *UDP_ACK_MSG = "ESP32_DECK_ACK";

const char *FIRMWARE_VERSION = "v4.0";
const char *DEVELOPER = "Luiz F. R. Pimentel";
const char *GITHUB = "github.com/KanekiZLF";

const int dataPin = 17;
const int clockPin = 21;
const int latchPin = 22;
const int numBits = 16;

#define LED_PIN 12
#define NUM_LEDS 16
int LED_BRIGHTNESS = 50;

#define PIN_BATT_ADC 33
#define PIN_TP4056_CE 13

TFT_eSPI tft = TFT_eSPI();
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

const int ENCODER_CLK_PIN = 25;
const int ENCODER_DT_PIN = 26;
const int ENCODER_BTN_PIN = 27;

// =========================================================================
// === MODULO 2: ENUMS E ESTRUTURAS ========================================
// =========================================================================

enum ConnectionProtocol { NONE, USB, WIFI };
ConnectionProtocol activeProtocol = NONE;
bool lastWiFiConnected = false;

enum SystemState {
    STATE_LOADING,
    STATE_MAIN,
    STATE_SETTINGS_MENU,
    STATE_WIFI_CONFIG_MENU,
    STATE_WIFI_CONFIG_PORTAL,
    STATE_BRIGHTNESS_CONFIG,
    STATE_BATTERY_INFO,
    STATE_LED_EFFECTS,
    STATE_CLICK_EFFECTS,
    STATE_ADVANCED_SETTINGS,
    STATE_ABOUT_DEVICE,
    STATE_THEME_SELECT
};
SystemState currentState = STATE_LOADING;

enum VisualTheme {
    THEME_CLASSIC,
    THEME_CYBERPUNK,
    THEME_MINIMAL,
    THEME_RETRO
};
VisualTheme currentTheme = THEME_CLASSIC;

enum ClickEffectType {
    CLICK_FLASH,
    CLICK_FADE,
    CLICK_PULSE,
    CLICK_RAINBOW,
    CLICK_BREATHE,
    CLICK_STROBE,
    CLICK_SPARKLE,
    CLICK_WAVE,
    CLICK_EXPLOSION,
    CLICK_CHASE,
    CLICK_NONE
};
ClickEffectType currentClickEffect = CLICK_FLASH;

struct PopupConfig {
    String title;
    String message;
    String option1;
    String option2;
    uint16_t color1;
    uint16_t color2;
    int result;
};

struct ThemeColors {
    uint16_t background;
    uint16_t primary;
    uint16_t secondary;
    uint16_t accent;
    uint16_t success;
    uint16_t warning;
    uint16_t error;
    uint16_t text;
    uint16_t highlight;
    uint16_t panel;
    uint16_t menuBg;
};

// =========================================================================
// === MODULO 3: VARIAVEIS GLOBAIS =========================================
// =========================================================================

String savedEffect = "NONE";
String savedClickEffect = "FLASH";
bool shouldRestoreEffect = false;
unsigned long feedbackStartTime = 0;
int feedbackDuration = 500;
bool isInFeedbackMode = false;

int menuSelection = 0;
int menuScrollOffset = 0;
const int VISIBLE_MENU_ITEMS = 7;
int lastMenuSelection = -1;
int lastMenuScrollOffset = -1;

int wifiMenuSelection = 0;
const int WIFI_MENU_ITEMS = 3;
int lastWifiMenuSelection = -1;

CRGB leds[NUM_LEDS];
CRGB ledBackground[NUM_LEDS];
CRGB ledClickOverlay[NUM_LEDS];
bool manualControl = false;
bool effectActive = false;
String currentEffect = "";
unsigned long effectTimer = 0;

bool ledMask[NUM_LEDS] = {false};
CRGB ledFixedColors[NUM_LEDS];

bool clickEffectActive = false;
unsigned long clickEffectTimer = 0;
int clickEffectLedIndex = -1;
CRGB clickEffectOriginalColor;
ClickEffectType activeClickEffect = CLICK_NONE;
int clickEffectStep = 0;
uint8_t clickEffectHue = 0;
CRGB clickEffectBaseColor;

int lastEncoderState = 0;
int encoderBtnLastState = HIGH;
unsigned long lastEncoderBtnPress = 0;
const unsigned long ENCODER_DEBOUNCE_DELAY = 50;

float batteryVoltage = 0.0;
int batteryPercentage = 0;
bool isUsbConnected = false;
bool isCharging = false;
int lastBatteryPercentage = -1;

int lastButtonStates = 0;

Preferences preferences;
WiFiServer serverTCP(TCP_PORT);
WiFiClient client;
WebServer server(80);
DNSServer dnsServer;
WiFiUDP Udp;
bool wifiConfigMode = false;

PopupConfig currentPopup;
bool popupActive = false;
bool popupNeedsRedraw = false;

// =========================================================================
// === MODULO 4: TEMAS DE CORES ============================================
// =========================================================================

ThemeColors themeClassic = {
    TFT_BLACK, TFT_CYAN, 0x4A69, TFT_YELLOW,
    TFT_GREEN, TFT_ORANGE, TFT_RED, TFT_WHITE,
    0xF81F, 0x18E0, 0x3186
};

ThemeColors themeCyberpunk = {
    TFT_BLACK, 0xF81F, 0x8010, 0x07FF,
    0x07E0, 0xFFE0, 0xF800, TFT_WHITE,
    0xF81F, 0x3808, 0x1804
};

ThemeColors themeMinimal = {
    TFT_BLACK, TFT_WHITE, 0x8410, 0xC618,
    0x4A69, 0xFF40, 0xC800, TFT_WHITE,
    0xFFFF, 0x2104, 0x1082
};

ThemeColors themeRetro = {
    TFT_BLACK, 0x07E0, 0x03E0, 0x87F0,
    0x07E0, 0x87E0, 0xF800, 0x87E0,
    0x07E0, 0x0140, 0x0020
};

ThemeColors currentColors = themeClassic;

// =========================================================================
// === MODULO 5: PROTOTIPOS DE FUNCOES =====================================
// =========================================================================

void initializeDisplay();
void initButtons();
void initLEDs();
void initEncoder();
void checkWiFiConnection();

void loadThemeFromPrefs();
void saveThemeToPrefs(String themeName);
void applyTheme(VisualTheme theme);
void drawThemePreview(VisualTheme theme, int x, int y, int w, int h);

void drawLoadingScreen();
void drawMainScreen();
void drawSettingsMenu();
void drawWifiConfigMenu();
void drawWifiConfigPortal();
void drawBrightnessConfigScreen();
void drawBatteryInfoScreen();
void drawLedEffectsScreen();
void drawClickEffectsScreen();
void drawAdvancedSettings();
void drawAboutDeviceScreen();
void drawThemeSelectScreen();

void showPopup(const String &title, const String &message,
               const String &confirmText, const String &cancelText,
               uint16_t confirmColor, uint16_t cancelColor);
void hidePopup();
void drawPopup();
void handlePopupInput();
void executePopupAction();
void redrawPreviousScreen();
void updatePopupSelection(int selection);

void handleEncoder();
void handleEncoderButton();
void handleEncoderRotation();
void checkButtons();
int readButtons();
int mapButton(int bit);
void handleButtonPress(int buttonNumber);

void updateLEDs();
void composeLedLayers();
void setStatusLEDs();
void clearAllLEDs();
void clearBackgroundLayer();
void clearClickOverlayLayer();

void processLedCommand(const String &command);
void processIndividualLedCommand(const String &command);
void processAllLedCommand(const String &command);

void updateBackgroundEffect();

void triggerClickEffect(int buttonIndex);
void updateClickEffect();
void renderClickEffect_FLASH();
void renderClickEffect_FADE();
void renderClickEffect_PULSE();
void renderClickEffect_RAINBOW();
void renderClickEffect_BREATHE();
void renderClickEffect_STROBE();
void renderClickEffect_SPARKLE();
void renderClickEffect_WAVE();
void renderClickEffect_EXPLOSION();
void renderClickEffect_CHASE();
void stopClickEffect();

void clearLedMask();
void applyLedMask();

void initWiFi();
void startConfigPortal();
void handleRoot();
void handleWiFiSave();
void checkUdpSearch();
void resetWiFiCredentials();
void clearWiFiCredentials();

void updateBatteryLogic();
void updateBatteryDisplay();

void checkSerialCommands();

void loadBrightnessFromPrefs();
void saveBrightnessToPrefs();
void verifyBrightnessSave();
void loadEffectFromPrefs();
void saveEffectToPrefs(String effectName);
void verifyEffectSave(String expectedEffect);
void clearEffectPrefs();
void loadClickEffectFromPrefs();
void saveClickEffectToPrefs(String clickEffectName);
void verifyClickEffectSave(String expected);
void clearClickEffectPrefs();

void showConnectionFeedback(ConnectionProtocol newProtocol);
void restoreSavedEffect();
void applySavedEffect();

void listAllPreferences();
void resetRenderStates();
String getClickEffectName(ClickEffectType effect);
String getClickEffectDisplayName(ClickEffectType effect);
String getThemeName(VisualTheme theme);
String getThemeDisplayName(VisualTheme theme);
CRGB generateVibrantColor();

// =========================================================================
// === MODULO 6: FUNCOES DE TEMAS ==========================================
// =========================================================================

String getThemeName(VisualTheme theme) {
    switch (theme) {
        case THEME_CLASSIC:   return "CLASSIC";
        case THEME_CYBERPUNK: return "CYBERPUNK";
        case THEME_MINIMAL:   return "MINIMAL";
        case THEME_RETRO:     return "RETRO";
        default:              return "CLASSIC";
    }
}

String getThemeDisplayName(VisualTheme theme) {
    switch (theme) {
        case THEME_CLASSIC:   return "Classico";
        case THEME_CYBERPUNK: return "Cyberpunk";
        case THEME_MINIMAL:   return "Minimalista";
        case THEME_RETRO:     return "Retro Terminal";
        default:              return "Classico";
    }
}

void applyTheme(VisualTheme theme) {
    currentTheme = theme;
    switch (theme) {
        case THEME_CLASSIC:   currentColors = themeClassic;   break;
        case THEME_CYBERPUNK: currentColors = themeCyberpunk; break;
        case THEME_MINIMAL:   currentColors = themeMinimal;   break;
        case THEME_RETRO:     currentColors = themeRetro;     break;
    }
}

void loadThemeFromPrefs() {
    Serial.print("[PREF] Carregando tema visual... ");
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        Serial.println("ERRO ao abrir namespace!");
        currentTheme = THEME_CLASSIC;
        applyTheme(currentTheme);
        return;
    }
    if (preferences.isKey(KEY_THEME)) {
        String themeName = preferences.getString(KEY_THEME, "CLASSIC");
        if (themeName == "CYBERPUNK")       currentTheme = THEME_CYBERPUNK;
        else if (themeName == "MINIMAL")    currentTheme = THEME_MINIMAL;
        else if (themeName == "RETRO")      currentTheme = THEME_RETRO;
        else                                currentTheme = THEME_CLASSIC;
        Serial.println("OK: " + themeName);
    } else {
        currentTheme = THEME_CLASSIC;
        Serial.println("Usando padrao: CLASSIC");
    }
    preferences.end();
    applyTheme(currentTheme);
}

void saveThemeToPrefs(String themeName) {
    Serial.print("[PREF] Salvando tema '" + themeName + "'... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir namespace!");
        return;
    }
    preferences.putString(KEY_THEME, themeName);
    preferences.end();
    Serial.println("OK");
}

// =========================================================================
// === MODULO 7: FUNCOES DE CLICK EFFECTS ==================================
// =========================================================================

String getClickEffectName(ClickEffectType effect) {
    switch (effect) {
        case CLICK_FLASH:     return "FLASH";
        case CLICK_FADE:      return "FADE";
        case CLICK_PULSE:     return "PULSE";
        case CLICK_RAINBOW:   return "RAINBOW";
        case CLICK_BREATHE:   return "BREATHE";
        case CLICK_STROBE:    return "STROBE";
        case CLICK_SPARKLE:   return "SPARKLE";
        case CLICK_WAVE:      return "WAVE";
        case CLICK_EXPLOSION: return "EXPLOSION";
        case CLICK_CHASE:     return "CHASE";
        case CLICK_NONE:      return "NONE";
        default:              return "FLASH";
    }
}

String getClickEffectDisplayName(ClickEffectType effect) {
    switch (effect) {
        case CLICK_FLASH:     return "Flash Rapido";
        case CLICK_FADE:      return "Fade Suave";
        case CLICK_PULSE:     return "Pulso Expansivo";
        case CLICK_RAINBOW:   return "Arco-Iris";
        case CLICK_BREATHE:   return "Respiracao";
        case CLICK_STROBE:    return "Estroboscopio";
        case CLICK_SPARKLE:   return "Faiscas";
        case CLICK_WAVE:      return "Onda de Luz";
        case CLICK_EXPLOSION: return "Explosao";
        case CLICK_CHASE:     return "Perseguicao";
        case CLICK_NONE:      return "Desligado";
        default:              return "Flash Rapido";
    }
}

void loadClickEffectFromPrefs() {
    Serial.print("[PREF] Carregando efeito de clique... ");
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        Serial.println("ERRO ao abrir namespace!");
        savedClickEffect = "FLASH";
        currentClickEffect = CLICK_FLASH;
        return;
    }
    if (preferences.isKey(KEY_CLICK_EFFECT)) {
        savedClickEffect = preferences.getString(KEY_CLICK_EFFECT, "FLASH");
        if (savedClickEffect == "FADE")         currentClickEffect = CLICK_FADE;
        else if (savedClickEffect == "PULSE")   currentClickEffect = CLICK_PULSE;
        else if (savedClickEffect == "RAINBOW") currentClickEffect = CLICK_RAINBOW;
        else if (savedClickEffect == "BREATHE") currentClickEffect = CLICK_BREATHE;
        else if (savedClickEffect == "STROBE")  currentClickEffect = CLICK_STROBE;
        else if (savedClickEffect == "SPARKLE") currentClickEffect = CLICK_SPARKLE;
        else if (savedClickEffect == "WAVE")    currentClickEffect = CLICK_WAVE;
        else if (savedClickEffect == "EXPLOSION") currentClickEffect = CLICK_EXPLOSION;
        else if (savedClickEffect == "CHASE")   currentClickEffect = CLICK_CHASE;
        else if (savedClickEffect == "NONE")    currentClickEffect = CLICK_NONE;
        else                                    currentClickEffect = CLICK_FLASH;
        Serial.println("OK: " + savedClickEffect);
    } else {
        savedClickEffect = "FLASH";
        currentClickEffect = CLICK_FLASH;
        Serial.println("Usando padrao: FLASH");
    }
    preferences.end();
}

void saveClickEffectToPrefs(String clickEffectName) {
    Serial.print("[PREF] Salvando click effect '" + clickEffectName + "'... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir namespace!");
        return;
    }
    preferences.putString(KEY_CLICK_EFFECT, clickEffectName);
    preferences.end();
    Serial.println("OK");
}

void verifyClickEffectSave(String expected) {
    preferences.begin(PREFS_NAMESPACE, true);
    String saved = preferences.getString(KEY_CLICK_EFFECT, "ERROR");
    preferences.end();
    if (saved == expected) {
        Serial.println("[VERIFY] ✅ Click effect verificado: " + expected);
        savedClickEffect = expected;
    } else {
        Serial.println("[VERIFY] ❌ Falha! Esperado: '" + expected + "', Lido: '" + saved + "'");
    }
}

void clearClickEffectPrefs() {
    Serial.print("[PREF] Limpando click effect... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO!");
        return;
    }
    preferences.remove(KEY_CLICK_EFFECT);
    preferences.end();
    savedClickEffect = "FLASH";
    currentClickEffect = CLICK_FLASH;
    Serial.println("OK");
}

// =========================================================================
// === MODULO 8: SISTEMA DE CAMADAS LED ====================================
// =========================================================================

CRGB generateVibrantColor() {
    uint8_t hue = random8();
    return CHSV(hue, 255, 255);
}

void clearBackgroundLayer() {
    fill_solid(ledBackground, NUM_LEDS, CRGB::Black);
}

void clearClickOverlayLayer() {
    fill_solid(ledClickOverlay, NUM_LEDS, CRGB::Black);
}

void composeLedLayers() {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (ledMask[i]) {
            leds[i] = ledFixedColors[i];
        } else if (ledClickOverlay[i]) {
            leds[i] = ledClickOverlay[i];
        } else {
            leds[i] = ledBackground[i];
        }
    }
}

// =========================================================================
// === MODULO 9: TRIGGER E UPDATE DE CLICK EFFECTS =========================
// =========================================================================

void triggerClickEffect(int buttonIndex) {
    if (currentClickEffect == CLICK_NONE) return;
    if (buttonIndex < 0 || buttonIndex >= NUM_LEDS) return;

    clickEffectActive = true;
    clickEffectTimer = millis();
    clickEffectLedIndex = buttonIndex;
    activeClickEffect = currentClickEffect;
    clickEffectStep = 0;
    clickEffectHue = 0;
    clickEffectBaseColor = generateVibrantColor();

    Serial.println("🎆 Click effect ativado: " + getClickEffectName(currentClickEffect) + 
                   " no LED " + String(buttonIndex) + " | Cor: " + 
                   String(clickEffectBaseColor.r) + "," + 
                   String(clickEffectBaseColor.g) + "," + 
                   String(clickEffectBaseColor.b));
}

void stopClickEffect() {
    if (!clickEffectActive) return;
    clearClickOverlayLayer();
    clickEffectActive = false;
    clickEffectLedIndex = -1;
    activeClickEffect = CLICK_NONE;
    clickEffectStep = 0;
}

void renderClickEffect_FLASH() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 50) {
        ledClickOverlay[clickEffectLedIndex] = CRGB::White;
    } else if (elapsed < 100) {
        ledClickOverlay[clickEffectLedIndex] = CRGB(128, 128, 128);
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_FADE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 400) {
        uint8_t brightness = 255 - (elapsed * 255 / 400);
        ledClickOverlay[clickEffectLedIndex] = clickEffectBaseColor;
        ledClickOverlay[clickEffectLedIndex].nscale8(brightness);
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_PULSE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 600) {
        int wave = sin8(elapsed * 255 / 600);
        uint8_t brightness = wave;
        clearClickOverlayLayer();
        for (int i = 0; i < NUM_LEDS; i++) {
            int dist = abs(i - clickEffectLedIndex);
            if (dist <= 3) {
                uint8_t localBright = brightness / (dist + 1);
                CRGB pulseColor = clickEffectBaseColor;
                pulseColor.nscale8(localBright);
                if (!ledMask[i]) {
                    ledClickOverlay[i] = pulseColor;
                }
            }
        }
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_RAINBOW() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 500) {
        clickEffectHue += 15;
        ledClickOverlay[clickEffectLedIndex] = CHSV(clickEffectHue, 255, 255);
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_BREATHE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 800) {
        uint8_t breath = sin8(elapsed * 255 / 800);
        ledClickOverlay[clickEffectLedIndex] = clickEffectBaseColor;
        ledClickOverlay[clickEffectLedIndex].nscale8(breath);
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_STROBE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 400) {
        bool on = ((elapsed / 40) % 2) == 0;
        ledClickOverlay[clickEffectLedIndex] = on ? CRGB::White : CRGB::Black;
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_SPARKLE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 500) {
        clearClickOverlayLayer();
        ledClickOverlay[clickEffectLedIndex] = CRGB::White;
        for (int i = 0; i < NUM_LEDS; i++) {
            if (i != clickEffectLedIndex && random8() < 30) {
                if (!ledMask[i]) {
                    ledClickOverlay[i] = CRGB(random8(100, 255), random8(100, 255), random8(100, 255));
                }
            }
        }
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_WAVE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 700) {
        clearClickOverlayLayer();
        int wavePos = (elapsed * NUM_LEDS) / 700;
        for (int i = 0; i < NUM_LEDS; i++) {
            int dist = abs(i - clickEffectLedIndex);
            int waveDist = abs(dist - wavePos);
            if (waveDist < 2) {
                uint8_t bright = 255 - (waveDist * 128);
                if (!ledMask[i]) {
                    ledClickOverlay[i] = CRGB(bright, bright, bright);
                }
            }
        }
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_EXPLOSION() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 600) {
        clearClickOverlayLayer();
        int radius = (elapsed * 4) / 600;
        for (int i = 0; i < NUM_LEDS; i++) {
            int dist = abs(i - clickEffectLedIndex);
            if (dist <= radius) {
                uint8_t bright = 255 - (dist * 50);
                if (!ledMask[i]) {
                    uint8_t r = bright;
                    uint8_t g = (dist < 2) ? bright : bright / 2;
                    uint8_t b = 0;
                    ledClickOverlay[i] = CRGB(r, g, b);
                }
            }
        }
    } else {
        stopClickEffect();
    }
}

void renderClickEffect_CHASE() {
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed < 800) {
        clearClickOverlayLayer();
        int pos = ((elapsed * NUM_LEDS) / 800 + clickEffectLedIndex) % NUM_LEDS;
        for (int t = 0; t < 3; t++) {
            int trailPos = (pos - t + NUM_LEDS) % NUM_LEDS;
            uint8_t trailBright = 255 - (t * 80);
            if (!ledMask[trailPos]) {
                ledClickOverlay[trailPos] = CRGB(trailBright, trailBright / 2, 0);
            }
        }
        if (!ledMask[pos]) {
            ledClickOverlay[pos] = CRGB::White;
        }
    } else {
        stopClickEffect();
    }
}

void updateClickEffect() {
    if (!clickEffectActive) return;
    switch (activeClickEffect) {
        case CLICK_FLASH:     renderClickEffect_FLASH();     break;
        case CLICK_FADE:      renderClickEffect_FADE();      break;
        case CLICK_PULSE:     renderClickEffect_PULSE();     break;
        case CLICK_RAINBOW:   renderClickEffect_RAINBOW();   break;
        case CLICK_BREATHE:   renderClickEffect_BREATHE();   break;
        case CLICK_STROBE:    renderClickEffect_STROBE();    break;
        case CLICK_SPARKLE:   renderClickEffect_SPARKLE();   break;
        case CLICK_WAVE:      renderClickEffect_WAVE();      break;
        case CLICK_EXPLOSION: renderClickEffect_EXPLOSION(); break;
        case CLICK_CHASE:     renderClickEffect_CHASE();     break;
        default:              stopClickEffect();             break;
    }
}

// =========================================================================
// === MODULO 10: TELAS OTIMIZADAS =========================================
// =========================================================================

void drawLoadingScreen() {
    tft.fillScreen(currentColors.background);
    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2 - 10;
    int numParticles = 8;
    int orbitRadius = 25;

    for (int step = 0; step < 60; step++) {
        tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, currentColors.background);
        tft.setTextColor(currentColors.primary);
        tft.setTextSize(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("ESP32 DECK", centerX, centerY - 25);
        tft.setTextSize(1);
        tft.setTextColor(currentColors.secondary);
        tft.drawString(FIRMWARE_VERSION, centerX, centerY - 8);

        for (int p = 0; p < numParticles; p++) {
            float angle = (step * 6 + p * (360 / numParticles)) * PI / 180;
            int px = centerX + (int)(cos(angle) * orbitRadius);
            int py = centerY + 15 + (int)(sin(angle) * orbitRadius * 0.4);
            uint16_t particleColor;
            switch (currentTheme) {
                case THEME_CYBERPUNK: particleColor = (p % 2 == 0) ? 0xF81F : 0x07FF; break;
                case THEME_RETRO:     particleColor = 0x07E0; break;
                case THEME_MINIMAL:   particleColor = (p % 2 == 0) ? TFT_WHITE : 0x8410; break;
                default:              particleColor = (p % 2 == 0) ? currentColors.primary : currentColors.accent; break;
            }
            int size = 2 + (step % 3);
            tft.fillCircle(px, py, size, particleColor);
        }

        tft.setTextColor(currentColors.accent);
        tft.drawString("Inicializando...", centerX, centerY + 40);

        int barWidth = 160;
        int barHeight = 4;
        int barX = (SCREEN_WIDTH - barWidth) / 2;
        int barY = SCREEN_HEIGHT - 15;
        tft.drawRect(barX, barY, barWidth, barHeight, currentColors.secondary);
        int progress = (barWidth * step) / 60;
        tft.fillRect(barX + 1, barY + 1, progress, barHeight - 2, currentColors.primary);

        manualControl = true;
        for (int led = 0; led < NUM_LEDS; led++) {
            int hue = (step * 4 + led * 16) % 255;
            int brightness = 80 + sin8(step * 4 + led * 16) / 4;
            leds[led] = CHSV(hue, 255, brightness);
        }
        FastLED.show();
        delay(30);
    }

    manualControl = false;
    clearAllLEDs();
}

void drawMainScreen() {
    tft.fillScreen(currentColors.background);
    tft.fillRect(0, 0, SCREEN_WIDTH, 25, currentColors.secondary);
    tft.setTextColor(currentColors.text);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("ESP-Deck ", 5, 8);

    String statusText = "";
    uint16_t textColor = TFT_WHITE;
    if (activeProtocol == USB) {
        statusText = "USB";
        textColor = currentColors.success;
    } else if (activeProtocol == WIFI) {
        statusText = "Wi-Fi";
        textColor = currentColors.success;
    } else {
        statusText = "Desconectado";
        textColor = currentColors.warning;
    }

    int textWidth = tft.textWidth(statusText);
    int xPosition = (SCREEN_WIDTH - textWidth) / 2;
    tft.setTextColor(textColor);
    tft.drawString(statusText, xPosition, 8);

    tft.fillRect(SCREEN_WIDTH - 50, 4, 50, 12, currentColors.secondary);
    updateBatteryDisplay();
    tft.drawFastHLine(0, 27, SCREEN_WIDTH, currentColors.primary);

    int panelX = 10;
    int panelY = 35;
    int panelW = SCREEN_WIDTH - 20;
    int panelH = 60;
    tft.drawRoundRect(panelX, panelY, panelW, panelH, 8, currentColors.panel);
    tft.fillRoundRect(panelX + 1, panelY + 1, panelW - 2, panelH - 2, 7, currentColors.panel);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);

    if (activeProtocol == USB) {
        tft.setTextColor(currentColors.success);
        tft.drawString("USB", SCREEN_WIDTH / 2, panelY + 18);
        tft.setTextSize(1);
        tft.setTextColor(currentColors.text);
        tft.drawString("Conectado via Serial", SCREEN_WIDTH / 2, panelY + 38);
        tft.fillCircle(SCREEN_WIDTH / 2, panelY + 52, 3, currentColors.success);
    } else if (activeProtocol == WIFI) {
        tft.setTextColor(currentColors.success);
        tft.drawString("Wi-Fi", SCREEN_WIDTH / 2, panelY + 18);
        tft.setTextSize(1);
        tft.setTextColor(currentColors.text);
        if (WiFi.status() == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            if (ip.length() > 15) ip = ip.substring(0, 12) + "...";
            tft.drawString("IP: " + ip, SCREEN_WIDTH / 2, panelY + 38);
        }
        for (int i = 0; i < 3; i++) {
            int barH = 4 + i * 3;
            int barX = SCREEN_WIDTH / 2 - 8 + i * 7;
            int barY = panelY + 50 - barH;
            tft.fillRect(barX, barY, 5, barH, currentColors.success);
        }
    } else {
        tft.setTextColor(currentColors.warning);
        tft.drawString("STANDBY", SCREEN_WIDTH / 2, panelY + 18);
        tft.setTextSize(1);
        tft.setTextColor(currentColors.text);
        tft.drawString("Aguardando conexao", SCREEN_WIDTH / 2, panelY + 38);
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        if (millis() - lastBlink > 800) {
            blinkState = !blinkState;
            lastBlink = millis();
        }
        tft.fillCircle(SCREEN_WIDTH / 2, panelY + 52, 3, blinkState ? currentColors.warning : currentColors.panel);
    }

    int statsY = 105;
    int statBoxW = (SCREEN_WIDTH - 30) / 3;
    tft.drawRoundRect(10, statsY, statBoxW, 22, 4, currentColors.secondary);
    tft.setTextColor(currentColors.primary);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("16 BTN", 10 + statBoxW / 2, statsY + 11);
    tft.drawRoundRect(15 + statBoxW, statsY, statBoxW, 22, 4, currentColors.secondary);
    tft.drawString("16 LED", 15 + statBoxW + statBoxW / 2, statsY + 11);
    tft.drawRoundRect(20 + statBoxW * 2, statsY, statBoxW, 22, 4, currentColors.secondary);
    String effectShort = effectActive ? currentEffect.substring(0, min(6, (int)currentEffect.length())) : "OFF";
    tft.drawString(effectShort, 20 + statBoxW * 2 + statBoxW / 2, statsY + 11);

    tft.setTextColor(currentColors.secondary);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Encoder: Menu", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 3);
}

// =========================================================================
// === MODULO 11: MENU PRINCIPAL ===========================================
// =========================================================================

void drawSettingsMenu() {
    bool fullRedraw = false;
    if (menuSelection != lastMenuSelection || menuScrollOffset != lastMenuScrollOffset) {
        if (lastMenuSelection == -1) fullRedraw = true;
        lastMenuSelection = menuSelection;
        lastMenuScrollOffset = menuScrollOffset;
    }

    if (fullRedraw) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIGURACOES", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Navegar  |  Click: Selecionar", SCREEN_WIDTH / 2, 130);
    }

    String menuItems[] = {
        "Configurar Wi-Fi", "Ajustar Brilho LEDs", "Efeitos LEDs",
        "Efeitos de Clique", "Tema Visual", "Informacoes Bateria",
        "Configuracoes Avancadas", "Sobre Dispositivo", "Voltar"
    };
    const int ITEMS_COUNT = 9;

    if (menuSelection < menuScrollOffset)
        menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS)
        menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;

    int startY = 25;
    int rowHeight = 14;

    for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
        int itemIndex = i + menuScrollOffset;
        if (itemIndex >= ITEMS_COUNT) break;
        int yPos = startY + (i * rowHeight);
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.menuBg);
        if (itemIndex == menuSelection) {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.highlight);
            tft.setTextColor(currentColors.background);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.panel);
            tft.setTextColor(currentColors.text);
        }
        tft.setTextDatum(TL_DATUM);
        String displayText = menuItems[itemIndex];
        if (displayText.length() > 29) displayText = displayText.substring(0, 19) + "...";
        tft.drawString(displayText, 10, yPos);
        if (itemIndex == menuSelection) {
            tft.setTextColor(currentColors.background);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
}

// =========================================================================
// === MODULO 12: TELA DE EFEITOS DE CLIQUE (COM SCROLL) ===================
// =========================================================================

void drawClickEffectsScreen() {
    static int lastClickEffectSelection = -1;
    static int lastClickEffectScroll = -1;
    bool fullRedraw = (lastClickEffectSelection == -1);
    if (menuSelection != lastClickEffectSelection || menuScrollOffset != lastClickEffectScroll) {
        if (lastClickEffectSelection == -1) fullRedraw = true;
    }
    lastClickEffectSelection = menuSelection;
    lastClickEffectScroll = menuScrollOffset;

    if (fullRedraw) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("EFEITOS DE CLIQUE", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: OK", SCREEN_WIDTH / 2, 130);
    }

    String effects[] = {
        "Flash Rapido", "Fade Suave", "Pulso Expansivo", "Arco-Iris",
        "Respiracao", "Estroboscopio", "Faiscas", "Onda de Luz",
        "Explosao", "Perseguicao", "Desligado", "Voltar"
    };
    const int EFFECTS_COUNT = 12;
    const int MAX_VISIBLE = 7;

    if (menuSelection < menuScrollOffset)
        menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + MAX_VISIBLE)
        menuScrollOffset = menuSelection - MAX_VISIBLE + 1;

    int startY = 25;
    int rowHeight = 14;

    for (int i = 0; i < MAX_VISIBLE; i++) {
        int itemIndex = i + menuScrollOffset;
        if (itemIndex >= EFFECTS_COUNT) break;
        int yPos = startY + (i * rowHeight);
        if (yPos > 120) break;
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.menuBg);
        if (itemIndex == menuSelection) {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.highlight);
            tft.setTextColor(currentColors.background);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.panel);
            tft.setTextColor(currentColors.text);
        }
        tft.setTextDatum(TL_DATUM);
        tft.drawString(effects[itemIndex], 10, yPos);
        if (itemIndex == menuSelection) {
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
}

// =========================================================================
// === MODULO 13: TELA DE EFEITOS LED (COM SCROLL) =========================
// =========================================================================

void drawLedEffectsScreen() {
    static int lastLedEffectSelection = -1;
    static int lastLedEffectScroll = -1;
    bool fullRedraw = (lastLedEffectSelection == -1);
    if (menuSelection != lastLedEffectSelection || menuScrollOffset != lastLedEffectScroll) {
        if (lastLedEffectSelection == -1) fullRedraw = true;
    }
    lastLedEffectSelection = menuSelection;
    lastLedEffectScroll = menuScrollOffset;

    if (fullRedraw) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("EFEITOS LEDs", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: OK", SCREEN_WIDTH / 2, 130);
    }

    String effects[] = {
        "Arco-Iris", "Piscante", "Onda Azul", "Fogo", "Estrelas",
        "Cylon", "Meteoros", "Color Wipe", "Luzes Correndo", "Confetes",
        "Desligar", "Voltar"
    };
    const int EFFECTS_COUNT = 12;
    const int MAX_VISIBLE = 7;

    if (menuSelection < menuScrollOffset)
        menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + MAX_VISIBLE)
        menuScrollOffset = menuSelection - MAX_VISIBLE + 1;

    int startY = 25;
    int rowHeight = 14;

    for (int i = 0; i < MAX_VISIBLE; i++) {
        int itemIndex = i + menuScrollOffset;
        if (itemIndex >= EFFECTS_COUNT) break;
        int yPos = startY + (i * rowHeight);
        if (yPos > 120) break;
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.menuBg);
        if (itemIndex == menuSelection) {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.highlight);
            tft.setTextColor(currentColors.background);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.panel);
            tft.setTextColor(currentColors.text);
        }
        tft.setTextDatum(TL_DATUM);
        tft.drawString(effects[itemIndex], 10, yPos);
        if (itemIndex == menuSelection) {
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
}

// =========================================================================
// === MODULO 14: TELA DE SELECAO DE TEMA ================================
// =========================================================================

void drawThemePreview(VisualTheme theme, int x, int y, int w, int h) {
    ThemeColors tc;
    switch (theme) {
        case THEME_CLASSIC:   tc = themeClassic;   break;
        case THEME_CYBERPUNK: tc = themeCyberpunk; break;
        case THEME_MINIMAL:   tc = themeMinimal;   break;
        case THEME_RETRO:     tc = themeRetro;     break;
        default:              tc = themeClassic;   break;
    }
    tft.fillRoundRect(x, y, w, h, 4, tc.background);
    tft.drawRoundRect(x, y, w, h, 4, tc.primary);
    tft.fillRect(x + 2, y + 2, w - 4, 6, tc.secondary);
    tft.fillRect(x + 4, y + 12, w - 8, 3, tc.text);
    tft.fillRect(x + 4, y + 18, (w - 8) * 2 / 3, 3, tc.primary);
    tft.fillRoundRect(x + 4, y + 26, w / 2 - 6, 6, 2, tc.success);
}

void drawThemeSelectScreen() {
    static int lastThemeSelection = -1;
    bool fullRedraw = (lastThemeSelection == -1);
    lastThemeSelection = menuSelection;

    if (fullRedraw) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("TEMA VISUAL", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: Aplicar", SCREEN_WIDTH / 2, 130);
    }

    String themes[] = {"Classico", "Cyberpunk", "Minimalista", "Retro Terminal", "Voltar"};
    VisualTheme themeEnums[] = {THEME_CLASSIC, THEME_CYBERPUNK, THEME_MINIMAL, THEME_RETRO, THEME_CLASSIC};
    int themesCount = 5;
    int startY = 25;
    int rowHeight = 22;

    for (int i = 0; i < themesCount; i++) {
        int yPos = startY + (i * rowHeight);
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 20, currentColors.menuBg);
        if (i == menuSelection) {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 20, currentColors.highlight);
            tft.setTextColor(currentColors.background);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 20, currentColors.panel);
            tft.setTextColor(currentColors.text);
        }
        if (i < 4) {
            drawThemePreview(themeEnums[i], SCREEN_WIDTH - 45, yPos - 1, 35, 18);
        }
        tft.setTextDatum(TL_DATUM);
        tft.drawString(themes[i], 10, yPos + 3);
        if (i == menuSelection) {
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 50, yPos + 3);
        }
    }
}

// =========================================================================
// === MODULO 15: TELAS EXISTENTES =========================================
// =========================================================================

void drawWifiConfigMenu() {
    bool fullRedraw = (lastWifiMenuSelection == -1);
    lastWifiMenuSelection = wifiMenuSelection;
    if (fullRedraw) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIGURAR WI-FI", SCREEN_WIDTH / 2, 9);
        tft.fillRect(10, 25, SCREEN_WIDTH - 20, 30, currentColors.panel);
        tft.drawRect(10, 25, SCREEN_WIDTH - 20, 30, currentColors.secondary);
        tft.setTextColor(currentColors.text);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Status:", 15, 32);
        if (WiFi.status() == WL_CONNECTED) {
            tft.setTextColor(currentColors.success);
            tft.drawString("CONECTADO", 55, 32);
            tft.setTextColor(currentColors.text);
            tft.drawString("Rede:", 15, 44);
            tft.drawString(WiFi.SSID().substring(0, 15), 55, 44);
        } else {
            tft.setTextColor(currentColors.warning);
            tft.drawString("DESCONECTADO", 55, 32);
        }
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Encoder: Selecionar opcao", SCREEN_WIDTH / 2, 130);
    }
    String wifiOptions[] = {"Limpar Credenciais", "Configurar Nova Rede", "Voltar"};
    int optionHeight = 22;
    int startY = 60;
    for (int i = 0; i < WIFI_MENU_ITEMS; i++) {
        int yPos = startY + (i * optionHeight);
        tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, currentColors.menuBg);
        if (i == wifiMenuSelection) {
            tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, currentColors.highlight);
            tft.setTextColor(currentColors.background);
        } else {
            tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, currentColors.panel);
            tft.setTextColor(currentColors.text);
        }
        tft.setTextDatum(TL_DATUM);
        tft.drawString(wifiOptions[i], 20, yPos);
        if (i == wifiMenuSelection) {
            tft.setTextColor(currentColors.background);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 20, yPos);
        }
    }
}

void drawWifiConfigPortal() {
    tft.fillScreen(currentColors.background);
    tft.setTextColor(currentColors.primary);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MODO CONFIG.", SCREEN_WIDTH / 2, 40);
    tft.drawString("WI-FI", SCREEN_WIDTH / 2, 65);
    tft.setTextSize(1);
    tft.setTextColor(currentColors.text);
    tft.drawString("Conecte-se a rede:", SCREEN_WIDTH / 2, 90);
    tft.setTextColor(currentColors.accent);
    tft.drawString(SSID_AP, SCREEN_WIDTH / 2, 105);
    tft.drawString("Senha: " + String(PASS_AP), SCREEN_WIDTH / 2, 120);
    tft.setTextColor(currentColors.secondary);
    tft.drawString("No navegador, acesse:", SCREEN_WIDTH / 2, 140);
    tft.drawString("192.168.4.1", SCREEN_WIDTH / 2, 155);
}

void drawBrightnessConfigScreen() {
    tft.fillScreen(currentColors.menuBg);
    tft.fillRect(0, 0, SCREEN_WIDTH, 24, currentColors.primary);
    tft.setTextColor(currentColors.background);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    int iconX = SCREEN_WIDTH / 2 - 50;
    tft.fillCircle(iconX, 12, 4, currentColors.background);
    for (int i = 0; i < 8; i++) {
        float angle = i * 45 * PI / 180;
        int x1 = iconX + (int)(cos(angle) * 8);
        int y1 = 12 + (int)(sin(angle) * 8);
        int x2 = iconX + (int)(cos(angle) * 12);
        int y2 = 12 + (int)(sin(angle) * 12);
        tft.drawLine(x1, y1, x2, y2, currentColors.background);
    }
    tft.drawString("BRILHO LEDs", SCREEN_WIDTH / 2, 12);
    tft.setTextColor(currentColors.accent);
    tft.setTextSize(3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(LED_BRIGHTNESS), SCREEN_WIDTH / 2, 45);
    tft.setTextSize(1);
    tft.setTextColor(currentColors.text);
    tft.drawString("Nivel (5-255)", SCREEN_WIDTH / 2, 65);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    if (LED_BRIGHTNESS <= 50) {
        tft.setTextColor(currentColors.secondary);
        tft.drawString("BAIXO", SCREEN_WIDTH / 2, 75);
    } else if (LED_BRIGHTNESS <= 150) {
        tft.setTextColor(currentColors.success);
        tft.drawString("MEDIO", SCREEN_WIDTH / 2, 75);
    } else {
        tft.setTextColor(currentColors.warning);
        tft.drawString("ALTO", SCREEN_WIDTH / 2, 75);
    }
    int barWidth = 180;
    int barHeight = 14;
    int barX = (SCREEN_WIDTH - barWidth) / 2;
    int barY = 85;
    tft.drawRoundRect(barX, barY, barWidth, barHeight, 3, currentColors.secondary);
    int progressWidth = map(LED_BRIGHTNESS, 5, 255, 0, barWidth - 2);
    for (int i = 0; i < progressWidth; i++) {
        int colorPosition = map(i, 0, barWidth - 2, 0, 255);
        uint16_t segmentColor;
        if (i < (barWidth - 2) / 3)
            segmentColor = tft.color565(0, colorPosition * 2, 100);
        else if (i < (barWidth - 2) * 2 / 3)
            segmentColor = tft.color565(colorPosition, 180, 0);
        else
            segmentColor = tft.color565(255, colorPosition, 0);
        tft.drawFastVLine(barX + 1 + i, barY + 1, barHeight - 2, segmentColor);
    }
    tft.setTextSize(1);
    tft.setTextColor(currentColors.text);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Min", barX, barY + barHeight + 5);
    tft.drawString("Max", barX + barWidth, barY + barHeight + 5);
    int markerX = barX + progressWidth;
    tft.fillTriangle(markerX, barY - 5, markerX - 4, barY - 1, markerX + 4, barY - 1, currentColors.accent);
    tft.setTextColor(currentColors.accent);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Gire: Ajustar  |  Click: Salvar e Sair", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5);
}

void drawBatteryInfoScreen() {
    uint16_t CUSTOM_BG = tft.color565(10, 20, 30);
    tft.fillScreen(CUSTOM_BG);
    tft.fillRect(0, 0, SCREEN_WIDTH, 20, currentColors.primary);
    tft.setTextColor(currentColors.background);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MONITOR DE ENERGIA", SCREEN_WIDTH / 2, 10);
    int bW = 160;
    int bH = 24;
    int bX = (SCREEN_WIDTH - bW) / 2 - 5;
    int bY = 32;
    tft.drawRect(bX, bY, bW, bH, currentColors.text);
    tft.fillRect(bX + bW, bY + 6, 4, 12, currentColors.text);
    uint16_t bCol = (batteryPercentage > 60) ? currentColors.success : 
                    (batteryPercentage > 25) ? currentColors.warning : currentColors.error;
    int fillW = map(batteryPercentage, 0, 100, 0, bW - 4);
    if (fillW > 0)
        tft.fillRect(bX + 2, bY + 2, fillW, bH - 4, bCol);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(currentColors.text);
    tft.setTextSize(2);
    tft.drawString(String(batteryPercentage) + "%", bX + bW + 12, bY + 12);
    tft.setTextSize(1);
    int infoY = 68;
    int col1 = 15;
    int col2 = 130;
    int spacing = 15;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(currentColors.secondary);
    tft.drawString("TENSAO:", col1, infoY);
    tft.setTextColor(currentColors.text);
    tft.drawString(String(batteryVoltage, 2) + "V", col1 + 55, infoY);
    tft.setTextColor(currentColors.secondary);
    tft.drawString("FONTE:", col1, infoY + spacing);
    tft.setTextColor(isUsbConnected ? currentColors.success : currentColors.accent);
    tft.drawString(isUsbConnected ? "USB-C" : "BATERIA", col1 + 55, infoY + spacing);
    tft.setTextColor(currentColors.secondary);
    tft.drawString("STATUS:", col2, infoY);
    tft.setTextColor(isCharging ? currentColors.warning : currentColors.text);
    tft.drawString(isCharging ? "CARREGANDO" : "STANDBY", col2 + 50, infoY);
    tft.setTextColor(currentColors.secondary);
    tft.drawString("PINO CE:", col2, infoY + spacing);
    tft.setTextColor(currentColors.text);
    tft.drawString(isUsbConnected ? "HIGH" : "LOW", col2 + 50, infoY + spacing);
    int boxY = 102;
    tft.drawRoundRect(10, boxY, SCREEN_WIDTH - 20, 22, 4, currentColors.panel);
    tft.setTextDatum(MC_DATUM);
    if (isUsbConnected) {
        tft.setTextColor(currentColors.error);
        tft.drawString("CARGA BLOQUEADA (SEGURANCA USB)", SCREEN_WIDTH / 2, boxY + 11);
    } else {
        tft.setTextColor(currentColors.success);
        tft.drawString("SISTEMA OPERANDO VIA Li-Ion", SCREEN_WIDTH / 2, boxY + 11);
    }
    tft.setTextColor(currentColors.secondary);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Click no Encoder para voltar", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 2);
}

void drawAdvancedSettings() {
    static int lastAdvSelection = -1;
    static int lastAdvScrollOffset = -1;
    bool fullRedraw = false;
    if (menuSelection != lastAdvSelection || menuScrollOffset != lastAdvScrollOffset) {
        if (lastAdvSelection == -1) fullRedraw = true;
        lastAdvSelection = menuSelection;
        lastAdvScrollOffset = menuScrollOffset;
    }
    if (fullRedraw) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIG. AVANCADAS", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Navegar  |  Click: Selecionar", SCREEN_WIDTH / 2, 130);
    }
    String advancedOptions[] = {
        "Reset de Fabrica", "Teste de LEDs", "Teste de Botoes",
        "Info do Sistema", "Calibrar Bateria", "Diagnostico",
        "Logs do Sistema", "Voltar"
    };
    const int ADV_ITEMS_COUNT = 8;
    const int ADV_VISIBLE_ITEMS = 7;
    if (menuSelection < menuScrollOffset)
        menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + ADV_VISIBLE_ITEMS)
        menuScrollOffset = menuSelection - ADV_VISIBLE_ITEMS + 1;
    int startY = 25;
    int rowHeight = 14;
    for (int i = 0; i < ADV_VISIBLE_ITEMS; i++) {
        int itemIndex = i + menuScrollOffset;
        if (itemIndex >= ADV_ITEMS_COUNT) break;
        int yPos = startY + (i * rowHeight);
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.menuBg);
        if (itemIndex == menuSelection) {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.highlight);
            tft.setTextColor(currentColors.background);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, currentColors.panel);
            tft.setTextColor(currentColors.text);
        }
        tft.setTextDatum(TL_DATUM);
        String displayText = advancedOptions[itemIndex];
        if (displayText.length() > 29) displayText = displayText.substring(0, 26) + "...";
        tft.drawString(displayText, 10, yPos);
        if (itemIndex == menuSelection) {
            tft.setTextColor(currentColors.background);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
    tft.setTextColor(currentColors.secondary);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    String statusMsg = "";
    switch (menuSelection) {
        case 0: statusMsg = "Apaga todas as configuracoes"; break;
        case 1: statusMsg = "Testa sequencialmente todos os LEDs"; break;
        case 2: statusMsg = "Verifica funcionamento dos botoes"; break;
        case 3: statusMsg = "Exibe informacoes tecnicas"; break;
        case 4: statusMsg = "Calibra sensor de bateria"; break;
        case 5: statusMsg = "Executa diagnostico completo"; break;
        case 6: statusMsg = "Mostra logs do sistema"; break;
        case 7: statusMsg = "Retorna ao menu anterior"; break;
    }
    tft.fillRect(10, 120, SCREEN_WIDTH - 20, 10, currentColors.menuBg);
    tft.drawString(statusMsg, SCREEN_WIDTH / 2, 120);
}

void drawAboutDeviceScreen() {
    uint16_t CUSTOM_BG = tft.color565(10, 20, 30);
    tft.fillScreen(CUSTOM_BG);
    tft.fillRect(0, 0, SCREEN_WIDTH, 20, currentColors.primary);
    tft.setTextColor(currentColors.background);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Informacoes do Sistema", SCREEN_WIDTH / 2, 10);
    int cardPadding = 4;
    int cardW = (SCREEN_WIDTH / 2) - 8;
    tft.setTextSize(1);
    int lx = 5;
    int ly = 28;
    tft.setTextColor(currentColors.accent);
    tft.drawString("HARDWARE", lx + 30, ly);
    tft.drawFastHLine(lx, ly + 8, cardW, currentColors.primary);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFFFF);
    tft.drawString("CPU:", lx, ly + 15);
    tft.setTextColor(0x07FF);
    tft.drawString("240MHz", lx + 35, ly + 15);
    tft.setTextColor(0xFFFF);
    tft.drawString("Core:", lx, ly + 27);
    tft.setTextColor(0x07FF);
    tft.drawString("Dual Core", lx + 35, ly + 27);
    tft.setTextColor(0xFFFF);
    tft.drawString("Esp32:", lx, ly + 39);
    tft.drawString("WROVER", lx + 35, ly + 39);
    int rx = SCREEN_WIDTH / 2 + 5;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(currentColors.accent);
    tft.drawString("STORAGE", rx + 30, ly);
    tft.drawFastHLine(rx, ly + 8, cardW, currentColors.primary);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFFFF);
    tft.drawString("Flash:", rx, ly + 15);
    tft.setTextColor(0x07FF);
    tft.drawString("16MB", rx + 40, ly + 15);
    tft.setTextColor(0xFFFF);
    tft.drawString("PsRam:", rx, ly + 27);
    tft.setTextColor(0x07FF);
    tft.drawString("4MB", rx + 40, ly + 27);
    tft.setTextColor(0xFFFF);
    tft.drawString("FW:", rx, ly + 39);
    tft.setTextColor(0x07FF);
    tft.drawString(FIRMWARE_VERSION, rx + 40, ly + 39);
    int devBoxY = 85;
    tft.drawRoundRect(5, devBoxY, SCREEN_WIDTH - 10, 38, 5, currentColors.panel);
    tft.fillRoundRect(6, devBoxY + 1, SCREEN_WIDTH - 12, 10, 3, currentColors.panel);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFFFF);
    tft.drawString("Development", SCREEN_WIDTH / 2, devBoxY + 5);
    tft.setTextColor(0x07FF);
    tft.drawString(DEVELOPER, SCREEN_WIDTH / 2, devBoxY + 18);
    tft.setTextColor(currentColors.secondary);
    tft.setTextSize(1);
    tft.drawString("github.com/KanekiZLF", SCREEN_WIDTH / 2, devBoxY + 28);
    for (int i = 0; i < SCREEN_WIDTH; i += 10) {
        tft.drawFastHLine(i, SCREEN_HEIGHT - 12, 5, currentColors.primary);
    }
    tft.setTextColor(0xFFFF);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Click: Voltar", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 2);
}

// =========================================================================
// === MODULO 16: CONTROLE DO ENCODER ======================================
// =========================================================================

void handleEncoder() {
    handleEncoderButton();
    if (currentState == STATE_SETTINGS_MENU ||
        currentState == STATE_BRIGHTNESS_CONFIG ||
        currentState == STATE_LED_EFFECTS ||
        currentState == STATE_WIFI_CONFIG_MENU ||
        currentState == STATE_ADVANCED_SETTINGS ||
        currentState == STATE_CLICK_EFFECTS ||
        currentState == STATE_THEME_SELECT) {
        handleEncoderRotation();
    }
}

void handleEncoderButton() {
    int btnState = digitalRead(ENCODER_BTN_PIN);
    if (btnState == LOW && encoderBtnLastState == HIGH) {
        unsigned long now = millis();
        if (now - lastEncoderBtnPress > ENCODER_DEBOUNCE_DELAY) {
            switch (currentState) {
                case STATE_MAIN:
                    currentState = STATE_SETTINGS_MENU;
                    menuSelection = 0;
                    menuScrollOffset = 0;
                    resetRenderStates();
                    drawSettingsMenu();
                    Serial.println("Navegacao: Tela Principal -> Menu Config");
                    break;

                case STATE_SETTINGS_MENU:
                    switch (menuSelection) {
                        case 0: // Wi-Fi
                            currentState = STATE_WIFI_CONFIG_MENU;
                            wifiMenuSelection = 0;
                            resetRenderStates();
                            drawWifiConfigMenu();
                            Serial.println("Navegacao: Menu -> Config Wi-Fi");
                            break;
                        case 1: // Brilho
                            currentState = STATE_BRIGHTNESS_CONFIG;
                            resetRenderStates();
                            drawBrightnessConfigScreen();
                            Serial.println("Navegacao: Menu -> Brilho LEDs");
                            break;
                        case 2: // Efeitos LED
                            currentState = STATE_LED_EFFECTS;
                            menuSelection = 0;
                            menuScrollOffset = 0;
                            resetRenderStates();
                            drawLedEffectsScreen();
                            Serial.println("Navegacao: Menu -> Efeitos LEDs");
                            break;
                        case 3: // Efeitos de Clique
                            currentState = STATE_CLICK_EFFECTS;
                            menuSelection = 0;
                            menuScrollOffset = 0;
                            resetRenderStates();
                            drawClickEffectsScreen();
                            Serial.println("Navegacao: Menu -> Efeitos de Clique");
                            break;
                        case 4: // Tema Visual
                            currentState = STATE_THEME_SELECT;
                            menuSelection = 0;
                            menuScrollOffset = 0;
                            resetRenderStates();
                            drawThemeSelectScreen();
                            Serial.println("Navegacao: Menu -> Tema Visual");
                            break;
                        case 5: // Bateria
                            currentState = STATE_BATTERY_INFO;
                            resetRenderStates();
                            drawBatteryInfoScreen();
                            Serial.println("Navegacao: Menu -> Info Bateria");
                            break;
                        case 6: // Avancado
                            currentState = STATE_ADVANCED_SETTINGS;
                            menuSelection = 0;
                            menuScrollOffset = 0;
                            resetRenderStates();
                            drawAdvancedSettings();
                            Serial.println("Navegacao: Menu -> Avancado");
                            break;
                        case 7: // Sobre
                            currentState = STATE_ABOUT_DEVICE;
                            resetRenderStates();
                            drawAboutDeviceScreen();
                            Serial.println("Navegacao: Menu -> Sobre Dispositivo");
                            break;
                        case 8: // Voltar
                            currentState = STATE_MAIN;
                            resetRenderStates();
                            drawMainScreen();
                            Serial.println("Navegacao: Menu -> Tela Principal");
                            break;
                    }
                    break;

                case STATE_WIFI_CONFIG_MENU:
                    switch (wifiMenuSelection) {
                        case 0:
                            showPopup("LIMPAR WI-FI",
                                      "Tem certeza que deseja apagar as credenciais Wi-Fi salvas?",
                                      "APAGAR", "CANCELAR",
                                      currentColors.error, currentColors.secondary);
                            Serial.println("Mostrando popup para limpar Wi-Fi");
                            break;
                        case 1:
                            showPopup("CONFIGURAR REDE",
                                      "Deseja configurar uma nova rede Wi-Fi?",
                                      "CONFIGURAR", "CANCELAR",
                                      currentColors.primary, currentColors.secondary);
                            break;
                        case 2:
                            currentState = STATE_SETTINGS_MENU;
                            menuSelection = 0;
                            resetRenderStates();
                            drawSettingsMenu();
                            break;
                    }
                    break;

                case STATE_BRIGHTNESS_CONFIG:
                case STATE_BATTERY_INFO:
                case STATE_ABOUT_DEVICE:
                    currentState = STATE_SETTINGS_MENU;
                    menuSelection = 0;
                    menuScrollOffset = 0;
                    resetRenderStates();
                    drawSettingsMenu();
                    break;

                case STATE_ADVANCED_SETTINGS:
                    switch (menuSelection) {
                        case 0: // Reset de Fabrica
                            showPopup("RESET DE FABRICA",
                                      "Apagar TODAS as configuracoes? Nao pode ser desfeito!",
                                      "CONFIRMAR", "CANCELAR",
                                      currentColors.error, currentColors.secondary);
                            Serial.println("Popup: Reset de Fabrica");
                            break;
                        case 1: // Teste de LEDs
                            Serial.println("Executando teste de LEDs...");
                            {
                                CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue,
                                                 CRGB::Yellow, CRGB::Cyan, CRGB::Magenta, CRGB::White};
                                for (int i = 0; i < 7; i++) {
                                    fill_solid(leds, NUM_LEDS, colors[i]);
                                    FastLED.show();
                                    delay(200);
                                }
                                clearAllLEDs();
                                if (effectActive) {
                                    effectTimer = millis();
                                    updateBackgroundEffect();
                                }
                            }
                            break;
                        case 2: // Teste de Botoes
                            Serial.println("Modo teste de botoes ativado. Pressione botoes...");
                            break;
                        case 3: // Info do Sistema
                            Serial.println("Info do Sistema solicitada");
                            break;
                        case 4: // Calibrar Bateria
                            Serial.println("Calibracao de bateria solicitada");
                            break;
                        case 5: // Diagnostico
                            Serial.println("Diagnostico completo iniciado");
                            break;
                        case 6: // Logs
                            Serial.println("Logs do sistema solicitados");
                            break;
                        case 7: // Voltar
                            currentState = STATE_SETTINGS_MENU;
                            menuSelection = 6;
                            menuScrollOffset = 0;
                            resetRenderStates();
                            drawSettingsMenu();
                            Serial.println("Navegacao: Avancado -> Menu Config");
                            break;
                    }
                    break;

                case STATE_LED_EFFECTS:
                    if (menuSelection == 11) { // Voltar
                        currentState = STATE_SETTINGS_MENU;
                        menuSelection = 2;
                        menuScrollOffset = 0;
                        resetRenderStates();
                        drawSettingsMenu();
                        break;
                    }
                    if (menuSelection == 10) { // Desligar
                        effectActive = false;
                        manualControl = false;
                        clearBackgroundLayer();
                        composeLedLayers();
                        FastLED.show();
                        saveEffectToPrefs("NONE");
                        Serial.println("LEDs desligados e efeito NONE salvo");
                        break;
                    }
                    effectActive = true;
                    switch (menuSelection) {
                        case 0: currentEffect = "RAINBOW"; saveEffectToPrefs("RAINBOW"); break;
                        case 1: currentEffect = "BLINK"; saveEffectToPrefs("BLINK"); break;
                        case 2: currentEffect = "WAVE_BLUE"; saveEffectToPrefs("WAVE_BLUE"); break;
                        case 3: currentEffect = "FIRE"; saveEffectToPrefs("FIRE"); break;
                        case 4: currentEffect = "TWINKLE"; saveEffectToPrefs("TWINKLE"); break;
                        case 5: currentEffect = "CYLON"; saveEffectToPrefs("CYLON"); break;
                        case 6: currentEffect = "METEOR"; saveEffectToPrefs("METEOR"); break;
                        case 7: currentEffect = "COLOR_WIPE"; saveEffectToPrefs("COLOR_WIPE"); break;
                        case 8: currentEffect = "RUNNING_LIGHTS"; saveEffectToPrefs("RUNNING_LIGHTS"); break;
                        case 9: currentEffect = "CONFETTI"; saveEffectToPrefs("CONFETTI"); break;
                    }
                    effectTimer = millis();
                    updateBackgroundEffect();
                    Serial.println("Efeito ativado e salvo: " + currentEffect);
                    break;

                case STATE_CLICK_EFFECTS:
                    if (menuSelection == 11) { // Voltar
                        currentState = STATE_SETTINGS_MENU;
                        menuSelection = 3;
                        menuScrollOffset = 0;
                        resetRenderStates();
                        drawSettingsMenu();
                        break;
                    }
                    switch (menuSelection) {
                        case 0: currentClickEffect = CLICK_FLASH; saveClickEffectToPrefs("FLASH"); break;
                        case 1: currentClickEffect = CLICK_FADE; saveClickEffectToPrefs("FADE"); break;
                        case 2: currentClickEffect = CLICK_PULSE; saveClickEffectToPrefs("PULSE"); break;
                        case 3: currentClickEffect = CLICK_RAINBOW; saveClickEffectToPrefs("RAINBOW"); break;
                        case 4: currentClickEffect = CLICK_BREATHE; saveClickEffectToPrefs("BREATHE"); break;
                        case 5: currentClickEffect = CLICK_STROBE; saveClickEffectToPrefs("STROBE"); break;
                        case 6: currentClickEffect = CLICK_SPARKLE; saveClickEffectToPrefs("SPARKLE"); break;
                        case 7: currentClickEffect = CLICK_WAVE; saveClickEffectToPrefs("WAVE"); break;
                        case 8: currentClickEffect = CLICK_EXPLOSION; saveClickEffectToPrefs("EXPLOSION"); break;
                        case 9: currentClickEffect = CLICK_CHASE; saveClickEffectToPrefs("CHASE"); break;
                        case 10: currentClickEffect = CLICK_NONE; saveClickEffectToPrefs("NONE"); break;
                    }
                    triggerClickEffect(7);
                    Serial.println("Click effect selecionado e salvo: " + getClickEffectName(currentClickEffect));
                    break;

                case STATE_THEME_SELECT:
                    if (menuSelection == 4) { // Voltar
                        currentState = STATE_SETTINGS_MENU;
                        menuSelection = 4;
                        menuScrollOffset = 0;
                        resetRenderStates();
                        drawSettingsMenu();
                        break;
                    }
                    switch (menuSelection) {
                        case 0: applyTheme(THEME_CLASSIC); saveThemeToPrefs("CLASSIC"); break;
                        case 1: applyTheme(THEME_CYBERPUNK); saveThemeToPrefs("CYBERPUNK"); break;
                        case 2: applyTheme(THEME_MINIMAL); saveThemeToPrefs("MINIMAL"); break;
                        case 3: applyTheme(THEME_RETRO); saveThemeToPrefs("RETRO"); break;
                    }
                    tft.fillScreen(currentColors.background);
                    drawThemeSelectScreen();
                    Serial.println("Tema aplicado: " + getThemeName(currentTheme));
                    break;
            }
            lastEncoderBtnPress = now;
        }
    }
    encoderBtnLastState = btnState;
}

void handleEncoderRotation() {
    int currentStateEncoder = digitalRead(ENCODER_CLK_PIN);
    if (currentStateEncoder != lastEncoderState) {
        int dtState = digitalRead(ENCODER_DT_PIN);

        if (currentState == STATE_SETTINGS_MENU) {
            const int ITEMS_COUNT = 9;
            if (dtState != currentStateEncoder)
                menuSelection = (menuSelection + 1) % ITEMS_COUNT;
            else
                menuSelection = (menuSelection - 1 + ITEMS_COUNT) % ITEMS_COUNT;
            drawSettingsMenu();
        }
        else if (currentState == STATE_WIFI_CONFIG_MENU) {
            if (dtState != currentStateEncoder)
                wifiMenuSelection = (wifiMenuSelection + 1) % WIFI_MENU_ITEMS;
            else
                wifiMenuSelection = (wifiMenuSelection - 1 + WIFI_MENU_ITEMS) % WIFI_MENU_ITEMS;
            drawWifiConfigMenu();
        }
        else if (currentState == STATE_BRIGHTNESS_CONFIG) {
            int oldBrightness = LED_BRIGHTNESS;
            if (dtState != currentStateEncoder)
                LED_BRIGHTNESS = min(255, LED_BRIGHTNESS + 5);
            else
                LED_BRIGHTNESS = max(5, LED_BRIGHTNESS - 5);
            if (LED_BRIGHTNESS != oldBrightness) {
                FastLED.setBrightness(LED_BRIGHTNESS);
                FastLED.show();
                drawBrightnessConfigScreen();
                saveBrightnessToPrefs();
                Serial.println("Brilho ajustado: " + String(LED_BRIGHTNESS));
            }
        }
        else if (currentState == STATE_LED_EFFECTS) {
            const int EFFECTS_COUNT = 12;
            if (dtState != currentStateEncoder)
                menuSelection = (menuSelection + 1) % EFFECTS_COUNT;
            else
                menuSelection = (menuSelection - 1 + EFFECTS_COUNT) % EFFECTS_COUNT;
            drawLedEffectsScreen();
        }
        else if (currentState == STATE_CLICK_EFFECTS) {
            const int CLICK_EFFECTS_COUNT = 12;
            if (dtState != currentStateEncoder)
                menuSelection = (menuSelection + 1) % CLICK_EFFECTS_COUNT;
            else
                menuSelection = (menuSelection - 1 + CLICK_EFFECTS_COUNT) % CLICK_EFFECTS_COUNT;
            drawClickEffectsScreen();
        }
        else if (currentState == STATE_THEME_SELECT) {
            const int THEMES_COUNT = 5;
            if (dtState != currentStateEncoder)
                menuSelection = (menuSelection + 1) % THEMES_COUNT;
            else
                menuSelection = (menuSelection - 1 + THEMES_COUNT) % THEMES_COUNT;
            drawThemeSelectScreen();
        }
        else if (currentState == STATE_ADVANCED_SETTINGS) {
            const int ADV_ITEMS_COUNT = 8;
            if (dtState != currentStateEncoder)
                menuSelection = (menuSelection + 1) % ADV_ITEMS_COUNT;
            else
                menuSelection = (menuSelection - 1 + ADV_ITEMS_COUNT) % ADV_ITEMS_COUNT;
            drawAdvancedSettings();
        }
    }
    lastEncoderState = currentStateEncoder;
}

void resetRenderStates() {
    lastMenuSelection = -1;
    lastMenuScrollOffset = -1;
    lastWifiMenuSelection = -1;
}

// =========================================================================
// === MODULO 17: CONTROLE DE BOTOES =======================================
// =========================================================================

int readButtons() {
    digitalWrite(latchPin, LOW);
    delayMicroseconds(5);
    digitalWrite(latchPin, HIGH);
    int data = 0;
    for (int i = 0; i < numBits; i++) {
        if (digitalRead(dataPin)) {
            data |= (1 << i);
        }
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(clockPin, LOW);
    }
    return data;
}

int mapButton(int bit) {
    switch (bit) {
        case 0:  return 9;
        case 1:  return 10;
        case 2:  return 11;
        case 3:  return 12;
        case 4:  return 16;
        case 5:  return 15;
        case 6:  return 14;
        case 7:  return 13;
        case 8:  return 8;
        case 9:  return 7;
        case 10: return 6;
        case 11: return 5;
        case 12: return 1;
        case 13: return 2;
        case 14: return 3;
        case 15: return 4;
        default: return 0;
    }
}

void checkButtons() {
    int currentButtonStates = readButtons();
    if (currentState == STATE_MAIN) {
        for (int i = 0; i < numBits; i++) {
            if (bitRead(currentButtonStates, i) && !bitRead(lastButtonStates, i)) {
                int buttonNumber = mapButton(i);
                handleButtonPress(buttonNumber);
            }
        }
    }
    lastButtonStates = currentButtonStates;
}

void handleButtonPress(int buttonNumber) {
    if (buttonNumber >= 1 && buttonNumber <= NUM_LEDS) {
        int ledIndex = buttonNumber - 1;
        triggerClickEffect(ledIndex);
        String command = "BTN:" + String(buttonNumber);
        if (activeProtocol == USB) {
            Serial.println(command);
        } else if (activeProtocol == WIFI && client.connected()) {
            client.println(command);
        }
        Serial.println("Botao " + String(buttonNumber) + " pressionado | Click Effect: " + getClickEffectName(currentClickEffect));
    }
}

// =========================================================================
// === MODULO 18: CONTROLE DOS LEDs - CAMADAS ==============================
// =========================================================================

void clearAllLEDs() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    fill_solid(ledBackground, NUM_LEDS, CRGB::Black);
    fill_solid(ledClickOverlay, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setStatusLEDs() {
    if (wifiConfigMode) {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        if (millis() - lastBlink > 500) {
            blinkState = !blinkState;
            fill_solid(ledBackground, NUM_LEDS, blinkState ? CRGB::Blue : CRGB::Black);
            composeLedLayers();
            FastLED.show();
            lastBlink = millis();
        }
        return;
    }
    if (effectActive || clickEffectActive) return;
    if (activeProtocol == USB) {
        fill_solid(ledBackground, NUM_LEDS, CRGB::Blue);
    } else if (activeProtocol == WIFI) {
        fill_solid(ledBackground, NUM_LEDS, CRGB::Green);
    } else {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        if (millis() - lastBlink > 1000) {
            blinkState = !blinkState;
            fill_solid(ledBackground, NUM_LEDS, blinkState ? CRGB::Red : CRGB::Black);
            composeLedLayers();
            FastLED.show();
            lastBlink = millis();
        }
        return;
    }
    composeLedLayers();
    FastLED.show();
}

void updateLEDs() {
    // 1. Feedback mode
    if (isInFeedbackMode) {
        if (millis() - feedbackStartTime > feedbackDuration) {
            if (shouldRestoreEffect && savedEffect != "NONE") {
                restoreSavedEffect();
            } else {
                isInFeedbackMode = false;
            }
        }
        return;
    }

    // 2. Atualiza efeito de fundo se ativo
    if (effectActive) {
        updateBackgroundEffect();
    }

    // 3. Atualiza click effect se ativo
    if (clickEffectActive) {
        updateClickEffect();
    }

    // 4. Compoe e mostra (sempre, exceto em controle manual individual ou config portal)
    if (!manualControl && !wifiConfigMode) {
        composeLedLayers();
        FastLED.show();
    } else if (manualControl) {
        // Em modo manual individual, ainda precisamos mostrar click effects
        if (clickEffectActive) {
            updateClickEffect();
            composeLedLayers();
            FastLED.show();
        }
    }
}

// =========================================================================
// === MODULO 19: EFEITOS DE LED DE FUNDO ==================================
// =========================================================================

void updateBackgroundEffect() {
    if (!effectActive) return;
    if (millis() - effectTimer < 50) return;

    if (currentEffect == "RAINBOW") {
        static uint8_t hue = 0;
        fill_rainbow(ledBackground, NUM_LEDS, hue, 255 / NUM_LEDS);
        hue += 5;
    }
    else if (currentEffect == "BLINK") {
        static bool blinkState = false;
        blinkState = !blinkState;
        fill_solid(ledBackground, NUM_LEDS, blinkState ? CRGB::White : CRGB::Black);
    }
    else if (currentEffect == "WAVE_BLUE") {
        static uint8_t offset = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t brightness = sin8(i * 32 + offset);
            ledBackground[i] = CRGB(0, 0, brightness);
        }
        offset += 8;
    }
    else if (currentEffect == "FIRE") {
        for (int i = 0; i < NUM_LEDS; i++) {
            int heat = random8(50, 255);
            ledBackground[i] = HeatColor(heat);
        }
    }
    else if (currentEffect == "TWINKLE") {
        static uint8_t sparkle[NUM_LEDS];
        for (int i = 0; i < NUM_LEDS; i++) {
            if (sparkle[i] == 0 && random8() < 10) {
                sparkle[i] = 255;
            }
            if (sparkle[i] > 0) {
                sparkle[i] = qsub8(sparkle[i], 15);
                ledBackground[i] = CRGB(sparkle[i], sparkle[i], sparkle[i]);
            } else {
                ledBackground[i] = CRGB::Black;
            }
        }
    }
    else if (currentEffect == "CYLON") {
        static int pos = 0;
        static int direction = 1;
        fadeToBlackBy(ledBackground, NUM_LEDS, 50);
        ledBackground[pos] = CRGB::Red;
        ledBackground[(pos + 1) % NUM_LEDS] = CRGB(128, 0, 0);
        ledBackground[(pos + NUM_LEDS - 1) % NUM_LEDS] = CRGB(128, 0, 0);
        pos += direction;
        if (pos >= NUM_LEDS - 1 || pos <= 0) direction = -direction;
    }
    else if (currentEffect == "METEOR") {
        static int meteorPos = 0;
        static uint8_t meteorTrail[NUM_LEDS];
        fadeToBlackBy(ledBackground, NUM_LEDS, 30);
        for (int i = 0; i < NUM_LEDS; i++) {
            if (meteorTrail[i] > 0) {
                ledBackground[i] = CRGB(meteorTrail[i], meteorTrail[i] / 2, 0);
                meteorTrail[i] = qsub8(meteorTrail[i], 40);
            }
        }
        meteorTrail[meteorPos] = 255;
        ledBackground[meteorPos] = CRGB::White;
        meteorPos = (meteorPos + 1) % NUM_LEDS;
    }
    else if (currentEffect == "COLOR_WIPE") {
        static int wipePos = 0;
        static uint8_t wipeHue = 0;
        if (wipePos < NUM_LEDS) {
            ledBackground[wipePos] = CHSV(wipeHue, 255, 255);
            wipePos++;
        } else {
            wipePos = 0;
            wipeHue += 32;
        }
    }
    else if (currentEffect == "RUNNING_LIGHTS") {
        static uint8_t offset = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            int brightness = sin8(i * 30 + offset);
            ledBackground[i] = CRGB(brightness, 0, brightness / 2);
        }
        offset += 10;
    }
    else if (currentEffect == "CONFETTI") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 20);
        if (random8() < 40) {
            int pos = random16(NUM_LEDS);
            ledBackground[pos] = CHSV(random8(), 255, 255);
        }
    }

    effectTimer = millis();
}

// =========================================================================
// === MODULO 20: PROCESSAMENTO DE COMANDOS LED ============================
// =========================================================================

void processIndividualLedCommand(const String &command) {
    manualControl = true;
    int firstColon = command.indexOf(':');
    int secondColon = command.indexOf(':', firstColon + 1);
    if (secondColon != -1) {
        int ledIndex = command.substring(firstColon + 1, secondColon).toInt();
        String colorStr = command.substring(secondColon + 1);
        if (colorStr == "OFF" || colorStr == "RESET") {
            if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
                ledMask[ledIndex] = false;
                Serial.println("LED " + String(ledIndex) + " liberado da cor fixa");
                return;
            }
        }
        if (colorStr.startsWith("#")) {
            colorStr = colorStr.substring(1);
        }
        long color = strtol(colorStr.c_str(), NULL, 16);
        CRGB ledColor = CRGB(
            (color >> 16) & 0xFF,
            (color >> 8) & 0xFF,
            color & 0xFF);
        if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
            ledMask[ledIndex] = true;
            ledFixedColors[ledIndex] = ledColor;
            leds[ledIndex] = ledColor;
            FastLED.show();
            Serial.print("LED ");
            Serial.print(ledIndex);
            Serial.print(" com cor fixa: #");
            Serial.println(colorStr);
        }
    }
}

void processAllLedCommand(const String &command) {
    String subCmd = command.substring(8);
    if (subCmd == "ON") {
        fill_solid(ledBackground, NUM_LEDS, CRGB::White);
        composeLedLayers();
        FastLED.show();
        effectActive = false;
        manualControl = true;
        for (int i = 0; i < NUM_LEDS; i++) ledMask[i] = false;
        Serial.println("Todos LEDs LIGADOS (branco)");
    }
    else if (subCmd == "OFF") {
        clearAllLEDs();
        effectActive = false;
        manualControl = false;
        for (int i = 0; i < NUM_LEDS; i++) ledMask[i] = false;
        Serial.println("Todos LEDs DESLIGADOS");
    }
    else if (subCmd == "CLEAR_MASK") {
        for (int i = 0; i < NUM_LEDS; i++) ledMask[i] = false;
        Serial.println("Mascara de LEDs limpa");
    }
    else if (subCmd == "SHOW_MASK") {
        Serial.println("\n═══════════════════════════════════════");
        Serial.println("    STATUS DA MASCARA DE LEDs (MEMORIA)");
        Serial.println("═══════════════════════════════════════");
        for (int i = 0; i < NUM_LEDS; i++) {
            if (ledMask[i]) {
                Serial.print("  LED ");
                Serial.print(i);
                Serial.print(": Cor fixa (");
                Serial.print(ledFixedColors[i].r);
                Serial.print(",");
                Serial.print(ledFixedColors[i].g);
                Serial.print(",");
                Serial.print(ledFixedColors[i].b);
                Serial.println(")");
            }
        }
        Serial.println("═══════════════════════════════════════");
    }
    else {
        effectActive = true;
        currentEffect = subCmd;
        effectTimer = millis();
        Serial.println("Efeito " + subCmd + " ativado via comando");
    }
}

void processLedCommand(const String &command) {
    if (command.startsWith("LED:")) {
        processIndividualLedCommand(command);
    } else if (command.startsWith("ALL_LED:")) {
        processAllLedCommand(command);
    } else {
        Serial.print("Comando LED invalido: ");
        Serial.println(command);
    }
}

void clearLedMask() {
    for (int i = 0; i < NUM_LEDS; i++) {
        ledMask[i] = false;
    }
    Serial.println("Mascara de LEDs limpa da memoria");
}

void applyLedMask() {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (ledMask[i]) {
            leds[i] = ledFixedColors[i];
        }
    }
}

// =========================================================================
// === MODULO 21: GERENCIAMENTO DA BATERIA =================================
// =========================================================================

void updateBatteryLogic() {
    isUsbConnected = (activeProtocol == USB);
    if (isUsbConnected) {
        digitalWrite(PIN_TP4056_CE, HIGH);
        isCharging = false;
    } else {
        digitalWrite(PIN_TP4056_CE, LOW);
        isCharging = true;
    }
    int rawADC = 0;
    for (int i = 0; i < 10; i++) {
        rawADC += analogRead(PIN_BATT_ADC);
        delay(1);
    }
    rawADC /= 10;
    batteryVoltage = (rawADC / 4095.0) * 3.3 * 2.0;
    if (batteryVoltage >= 4.2) {
        batteryPercentage = 100;
    } else if (batteryVoltage >= 4.0) {
        batteryPercentage = map(batteryVoltage * 100, 400, 420, 80, 100);
    } else if (batteryVoltage >= 3.8) {
        batteryPercentage = map(batteryVoltage * 100, 380, 400, 50, 80);
    } else if (batteryVoltage >= 3.6) {
        batteryPercentage = map(batteryVoltage * 100, 360, 380, 20, 50);
    } else if (batteryVoltage >= 3.3) {
        batteryPercentage = map(batteryVoltage * 100, 330, 360, 0, 20);
    } else {
        batteryPercentage = 0;
    }
    batteryPercentage = constrain(batteryPercentage, 0, 100);
}

void updateBatteryDisplay() {
    int batteryWidth = 25;
    int batteryHeight = 10;
    int batteryX = SCREEN_WIDTH - 35;
    int batteryY = 6;
    tft.fillRect(batteryX - 30, batteryY - 2, 55, 14, currentColors.secondary);
    tft.drawRect(batteryX, batteryY, batteryWidth, batteryHeight, TFT_WHITE);
    int poleSize = 4;
    int poleX = batteryX + batteryWidth;
    int poleY = batteryY + (batteryHeight / 2) - (poleSize / 2);
    tft.fillRect(poleX, poleY, 2, poleSize, TFT_WHITE);
    int fillWidth = map(batteryPercentage, 0, 100, 0, batteryWidth - 2);
    uint16_t fillColor;
    if (batteryPercentage > 70) fillColor = TFT_GREEN;
    else if (batteryPercentage > 30) fillColor = TFT_YELLOW;
    else fillColor = TFT_RED;
    if (fillWidth > 0) {
        tft.fillRect(batteryX + 1, batteryY + 1, fillWidth, batteryHeight - 2, fillColor);
    }
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    int textX = batteryX - 25;
    int textY = batteryY + 2;
    tft.fillRect(textX - 2, textY - 2, 25, 10, currentColors.secondary);
    tft.setCursor(textX, textY);
    if (batteryPercentage == 100) {
        tft.print("100%");
    } else if (batteryPercentage >= 10) {
        tft.print(" ");
        tft.print(batteryPercentage);
        tft.print("%");
    } else {
        tft.print("  ");
        tft.print(batteryPercentage);
        tft.print("%");
    }
}

// =========================================================================
// === MODULO 22: GERENCIAMENTO WI-FI ======================================
// =========================================================================

void initWiFi() {
    Serial.print("[WiFi] Inicializando... ");
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        Serial.println("ERRO ao abrir preferences!");
        return;
    }
    String ssid = preferences.getString(KEY_WIFI_SSID, "");
    String pass = preferences.getString(KEY_WIFI_PASS, "");
    preferences.end();
    if (ssid.length() == 0) {
        Serial.println("Nenhuma rede configurada");
        return;
    }
    Serial.print("Conectando a " + ssid + "... ");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 15; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        Serial.print(".");
        delay(300);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" OK");
        Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
        serverTCP.begin();
        Udp.begin(UDP_SEARCH_PORT);
        lastWiFiConnected = true;
    } else {
        Serial.println(" FALHA");
        lastWiFiConnected = false;
    }
}

void handleWiFiSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    Serial.print("[WiFi] Salvando credenciais para: " + ssid + "... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir preferences!");
        return;
    }
    preferences.putString(KEY_WIFI_SSID, ssid);
    preferences.putString(KEY_WIFI_PASS, pass);
    preferences.end();
    delay(100);
    Serial.println("OK");
    String html = "<html><body style='font-family:Arial;text-align:center;'><h1>Configurado!</h1><p>Wi-Fi salvo com sucesso.</p><p>Reconectando...</p></body></html>";
    server.send(200, "text/html", html);
    delay(1000);
    server.stop();
    wifiConfigMode = false;
    WiFi.softAPdisconnect(true);
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    currentState = STATE_MAIN;
    drawMainScreen();
}

void startConfigPortal() {
    wifiConfigMode = true;
    drawWifiConfigPortal();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(SSID_AP, PASS_AP);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(53, "*", apIP);
    server.on("/", handleRoot);
    server.on("/save", handleWiFiSave);
    server.begin();
    Serial.println("AP: " + String(SSID_AP));
    Serial.println("IP: " + WiFi.softAPIP().toString());
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>ESP32 Deck</title><style>body{font-family:Arial;background:#222;color:#fff;text-align:center;}.container{max-width:300px;margin:50px auto;background:#333;padding:20px;border-radius:10px;}input{padding:10px;margin:5px;width:90%;border-radius:5px;border:1px solid #555;background:#444;color:#fff;}button{padding:10px 20px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer;}</style></head><body><div class='container'><h1>ESP32 Deck</h1><p>Configurar Wi-Fi</p><form method='get' action='/save'><input type='text' name='ssid' placeholder='Nome da rede' required><input type='password' name='pass' placeholder='Senha' required><button type='submit'>Salvar</button></form></div></body></html>";
    server.send(200, "text/html", html);
}

void clearWiFiCredentials() {
    Serial.print("[WiFi] Limpando credenciais... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir preferences!");
        return;
    }
    preferences.remove(KEY_WIFI_SSID);
    preferences.remove(KEY_WIFI_PASS);
    preferences.end();
    Serial.println("OK");
    WiFi.disconnect(true);
    delay(1000);
    lastWiFiConnected = false;
    if (currentState == STATE_MAIN) {
        drawMainScreen();
    }
}

void resetWiFiCredentials() {
    drawWifiConfigPortal();
    delay(2000);
    startConfigPortal();
}

void checkUdpSearch() {
    if (WiFi.status() != WL_CONNECTED) return;
    int packetSize = Udp.parsePacket();
    if (packetSize) {
        char incomingPacket[255];
        int len = Udp.read(incomingPacket, 255);
        if (len > 0) {
            incomingPacket[len] = 0;
            if (String(incomingPacket) == UDP_DISCOVER_MSG) {
                IPAddress remoteIP = Udp.remoteIP();
                Udp.beginPacket(remoteIP, Udp.remotePort());
                Udp.write((const uint8_t *)UDP_ACK_MSG, strlen(UDP_ACK_MSG));
                Udp.endPacket();
                manualControl = true;
                fill_solid(leds, NUM_LEDS, CRGB::Yellow);
                FastLED.show();
                delay(50);
                manualControl = false;
            }
        }
    }
}

void checkWiFiConnection() {
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (lastWiFiConnected && !wifiConnected) {
        Serial.println("Wi-Fi desconectado!");
        if (activeProtocol == WIFI) {
            activeProtocol = NONE;
            showConnectionFeedback(NONE);
            Serial.println("Protocolo alterado: Wi-Fi -> Nenhum");
        }
        if (client.connected()) {
            client.stop();
            Serial.println("Cliente Wi-Fi desconectado");
        }
    } else if (!lastWiFiConnected && wifiConnected) {
        Serial.println("Wi-Fi reconectado!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP().toString());
        if (activeProtocol == NONE) {
            activeProtocol = WIFI;
            showConnectionFeedback(WIFI);
        }
        serverTCP.begin();
        Udp.begin(UDP_SEARCH_PORT);
    }
    lastWiFiConnected = wifiConnected;
}

// =========================================================================
// === MODULO 23: COMUNICACAO SERIAL ========================================
// =========================================================================

void checkSerialCommands() {
    if (Serial.available()) {
        String message = Serial.readStringUntil('\n');
        message.trim();

        if (message.startsWith("LED:") || message.startsWith("ALL_LED:")) {
            processLedCommand(message);
        }
        else if (message == "CONNECTED") {
            if (activeProtocol != USB) {
                activeProtocol = USB;
                showConnectionFeedback(USB);
                if (currentState == STATE_MAIN)
                    drawMainScreen();
                Serial.println("Conectado via USB");
            }
        }
        else if (message == "DISCONNECT") {
            loadEffectFromPrefs();
            applySavedEffect();
            if (activeProtocol == USB) {
                activeProtocol = client.connected() ? WIFI : NONE;
                showConnectionFeedback(activeProtocol);
                if (currentState == STATE_MAIN)
                    drawMainScreen();
                Serial.println("Desconectado do USB");
            }
        }
        else if (message == "STATUS" || message == "status") {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("       ESP32 DECK - STATUS DO SISTEMA");
            Serial.println("═══════════════════════════════════════");
            Serial.println("FIRMWARE: " + String(FIRMWARE_VERSION));
            Serial.println("DESENVOLVEDOR: " + String(DEVELOPER));
            Serial.println("GITHUB: " + String(GITHUB));
            Serial.println("═══════════════════════════════════════");
            Serial.println("ESTADO DO SISTEMA:");
            Serial.println("   • Estado atual: " + String(currentState));
            Serial.println("   • Protocolo: " + String(activeProtocol == USB ? "USB" : activeProtocol == WIFI ? "Wi-Fi" : "Nenhum"));
            Serial.println("   • Tema Visual: " + getThemeName(currentTheme));
            Serial.println("   • Click Effect: " + getClickEffectName(currentClickEffect));
            Serial.println("═══════════════════════════════════════");
            Serial.println("BATERIA:");
            Serial.println("   • Porcentagem: " + String(batteryPercentage) + "%");
            Serial.println("   • Tensao: " + String(batteryVoltage, 1) + "V");
            Serial.println("   • USB Conectado: " + String(isUsbConnected ? "Sim" : "Nao"));
            Serial.println("   • Carregando: " + String(isCharging ? "Sim" : "Nao"));
            Serial.println("═══════════════════════════════════════");
            Serial.println("REDE WI-FI:");
            Serial.println("   • Status: " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado"));
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("   • SSID: " + WiFi.SSID());
                Serial.println("   • IP: " + WiFi.localIP().toString());
            }
            Serial.println("═══════════════════════════════════════");
            Serial.println("CONFIGURACOES LED:");
            Serial.println("   • Brilho: " + String(LED_BRIGHTNESS) + "/255");
            Serial.println("   • Efeito salvo: " + savedEffect);
            Serial.println("   • Efeito ativo: " + String(effectActive ? currentEffect : "Nenhum"));
            Serial.println("   • Click Effect: " + getClickEffectName(currentClickEffect));
            Serial.println("   • Modo manual: " + String(manualControl ? "Sim" : "Nao"));
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "LED_HELP" || message == "Led_Help" || message == "led_help") {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         COMANDOS LED DISPONIVEIS");
            Serial.println("═══════════════════════════════════════");
            Serial.println("CONTROLE INDIVIDUAL:");
            Serial.println("   LED:0:FF0000      // LED 0 vermelho");
            Serial.println("   LED:1:00FF00      // LED 1 verde");
            Serial.println("   LED:2:0000FF      // LED 2 azul");
            Serial.println("");
            Serial.println("CONTROLE GERAL:");
            Serial.println("   ALL_LED:ON        // Liga todos (branco)");
            Serial.println("   ALL_LED:OFF       // Desliga todos");
            Serial.println("");
            Serial.println("EFEITOS PRE-DEFINIDOS:");
            Serial.println("   ALL_LED:RAINBOW   // Arco-iris");
            Serial.println("   ALL_LED:BLINK     // Piscante");
            Serial.println("   ALL_LED:WAVE_BLUE // Onda azul");
            Serial.println("   ALL_LED:FIRE      // Fogo");
            Serial.println("   ALL_LED:TWINKLE   // Estrelas");
            Serial.println("   ALL_LED:CYLON     // Scanner KITT");
            Serial.println("   ALL_LED:METEOR    // Chuva de meteoros");
            Serial.println("   ALL_LED:COLOR_WIPE // Preenchimento");
            Serial.println("   ALL_LED:RUNNING_LIGHTS // Luzes correndo");
            Serial.println("   ALL_LED:CONFETTI  // Confetes");
            Serial.println("");
            Serial.println("EFEITOS DE CLIQUE:");
            Serial.println("   CLICK_EFFECT:FLASH     // Flash rapido");
            Serial.println("   CLICK_EFFECT:FADE      // Fade suave");
            Serial.println("   CLICK_EFFECT:PULSE     // Pulso expansivo");
            Serial.println("   CLICK_EFFECT:RAINBOW   // Arco-iris");
            Serial.println("   CLICK_EFFECT:BREATHE   // Respiracao");
            Serial.println("   CLICK_EFFECT:STROBE    // Estroboscopio");
            Serial.println("   CLICK_EFFECT:SPARKLE   // Faiscas");
            Serial.println("   CLICK_EFFECT:WAVE      // Onda de luz");
            Serial.println("   CLICK_EFFECT:EXPLOSION // Explosao");
            Serial.println("   CLICK_EFFECT:CHASE     // Perseguicao");
            Serial.println("   CLICK_EFFECT:NONE      // Desligado");
            Serial.println("");
            Serial.println("TEMAS VISUAIS:");
            Serial.println("   THEME:CLASSIC     // Classico");
            Serial.println("   THEME:CYBERPUNK   // Cyberpunk");
            Serial.println("   THEME:MINIMAL     // Minimalista");
            Serial.println("   THEME:RETRO       // Retro Terminal");
            Serial.println("");
            Serial.println("COMANDOS DO SISTEMA:");
            Serial.println("   STATUS            // Status completo");
            Serial.println("   EFFECT_STATUS     // Status dos efeitos");
            Serial.println("   SAVE_EFFECT       // Salva efeito atual");
            Serial.println("   CLEAR_EFFECT      // Limpa efeito salvo");
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "EFFECT_STATUS") {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         STATUS DE EFEITOS LED");
            Serial.println("═══════════════════════════════════════");
            Serial.println("EFEITO SALVO:");
            Serial.println("   • Nome: " + savedEffect);
            Serial.println("   • Click Effect: " + savedClickEffect);
            Serial.println("");
            Serial.println("ESTADO ATUAL:");
            Serial.println("   • Efeito ativo: " + String(effectActive ? currentEffect : "Nenhum"));
            Serial.println("   • Click Effect: " + getClickEffectName(currentClickEffect));
            Serial.println("   • Modo manual: " + String(manualControl ? "Sim" : "Nao"));
            Serial.println("");
            Serial.println("TEMA VISUAL:");
            Serial.println("   • Tema: " + getThemeName(currentTheme));
            Serial.println("═══════════════════════════════════════");
        }
        else if (message.startsWith("CLICK_EFFECT:")) {
            String effectName = message.substring(13);
            if (effectName == "FLASH")       { currentClickEffect = CLICK_FLASH; saveClickEffectToPrefs("FLASH"); }
            else if (effectName == "FADE")   { currentClickEffect = CLICK_FADE; saveClickEffectToPrefs("FADE"); }
            else if (effectName == "PULSE")  { currentClickEffect = CLICK_PULSE; saveClickEffectToPrefs("PULSE"); }
            else if (effectName == "RAINBOW") { currentClickEffect = CLICK_RAINBOW; saveClickEffectToPrefs("RAINBOW"); }
            else if (effectName == "BREATHE") { currentClickEffect = CLICK_BREATHE; saveClickEffectToPrefs("BREATHE"); }
            else if (effectName == "STROBE")  { currentClickEffect = CLICK_STROBE; saveClickEffectToPrefs("STROBE"); }
            else if (effectName == "SPARKLE") { currentClickEffect = CLICK_SPARKLE; saveClickEffectToPrefs("SPARKLE"); }
            else if (effectName == "WAVE")    { currentClickEffect = CLICK_WAVE; saveClickEffectToPrefs("WAVE"); }
            else if (effectName == "EXPLOSION") { currentClickEffect = CLICK_EXPLOSION; saveClickEffectToPrefs("EXPLOSION"); }
            else if (effectName == "CHASE")   { currentClickEffect = CLICK_CHASE; saveClickEffectToPrefs("CHASE"); }
            else if (effectName == "NONE")    { currentClickEffect = CLICK_NONE; saveClickEffectToPrefs("NONE"); }
            Serial.println("Click effect alterado para: " + effectName);
        }
        else if (message.startsWith("THEME:")) {
            String themeName = message.substring(6);
            if (themeName == "CLASSIC")      { applyTheme(THEME_CLASSIC); saveThemeToPrefs("CLASSIC"); }
            else if (themeName == "CYBERPUNK") { applyTheme(THEME_CYBERPUNK); saveThemeToPrefs("CYBERPUNK"); }
            else if (themeName == "MINIMAL")  { applyTheme(THEME_MINIMAL); saveThemeToPrefs("MINIMAL"); }
            else if (themeName == "RETRO")    { applyTheme(THEME_RETRO); saveThemeToPrefs("RETRO"); }
            if (currentState == STATE_MAIN) drawMainScreen();
            Serial.println("Tema alterado para: " + themeName);
        }
        else if (message == "SAVE_EFFECT") {
            if (effectActive) {
                saveEffectToPrefs(currentEffect);
                Serial.println("Efeito atual salvo: " + currentEffect);
            } else {
                saveEffectToPrefs("NONE");
                Serial.println("Configuracao 'sem efeito' salva");
            }
        }
        else if (message == "CLEAR_EFFECT") {
            clearEffectPrefs();
            Serial.println("Efeito salvo foi removido");
        }
        else if (message == "RESET_LEDS") {
            effectActive = false;
            manualControl = false;
            clearAllLEDs();
            saveEffectToPrefs("NONE");
            Serial.println("LEDs resetados para estado padrao");
        }
        else if (message == "TEST_LEDS") {
            Serial.println("Teste sequencial de LEDs...");
            CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue,
                             CRGB::Yellow, CRGB::Cyan, CRGB::Magenta, CRGB::White};
            for (int i = 0; i < 7; i++) {
                fill_solid(leds, NUM_LEDS, colors[i]);
                FastLED.show();
                Serial.println("   Cor " + String(i + 1) + " de 7");
                delay(300);
            }
            Serial.println("   Teste individual de LEDs...");
            clearAllLEDs();
            for (int i = 0; i < NUM_LEDS; i++) {
                leds[i] = CRGB::White;
                FastLED.show();
                delay(50);
                leds[i] = CRGB::Black;
            }
            if (effectActive) {
                effectActive = true;
                effectTimer = millis();
                Serial.println("   Restaurando efeito anterior...");
            } else if (savedEffect != "NONE") {
                effectActive = true;
                currentEffect = savedEffect;
                effectTimer = millis();
                Serial.println("   Restaurando efeito salvo: " + savedEffect);
            } else {
                clearAllLEDs();
                Serial.println("   LEDs desligados");
            }
            Serial.println("Teste de LEDs completo!");
        }
        else if (message == "SYSTEM_INFO") {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         INFORMACOES DO SISTEMA");
            Serial.println("═══════════════════════════════════════");
            Serial.println("ESPECIFICACOES:");
            Serial.println("   • CPU: Xtensa LX7 Dual-Core");
            Serial.println("   • Frequencia: 240 MHz");
            Serial.println("   • RAM: 512KB SRAM");
            Serial.println("   • Flash: 8MB");
            Serial.println("");
            Serial.println("PERIFERICOS:");
            Serial.println("   • Botoes: 16 (via shift register)");
            Serial.println("   • LEDs: 16 RGB WS2812B");
            Serial.println("   • Display: 1.14\" IPS (240x135)");
            Serial.println("   • Encoder: EC11 (rotacao + botao)");
            Serial.println("   • Bateria: Li-Ion com TP4056");
            Serial.println("");
            Serial.println("CONECTIVIDADE:");
            Serial.println("   • Wi-Fi: 802.11 b/g/n");
            Serial.println("   • Bluetooth: BLE");
            Serial.println("   • USB: Serial/UART");
            Serial.println("   • Protocolos: TCP, UDP, HTTP");
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "HELP" || message == "?") {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         COMANDOS DO SISTEMA");
            Serial.println("═══════════════════════════════════════");
            Serial.println("CONTROLE BASICO:");
            Serial.println("   CONNECTED         // Simula conexao USB");
            Serial.println("   DISCONNECT        // Simula desconexao");
            Serial.println("   STATUS            // Status completo");
            Serial.println("   SYSTEM_INFO       // Especificacoes");
            Serial.println("");
            Serial.println("CONTROLE DE LEDs:");
            Serial.println("   LED_HELP          // Todos comandos LED");
            Serial.println("   EFFECT_STATUS     // Status de efeitos");
            Serial.println("   TEST_LEDS         // Teste sequencial");
            Serial.println("   RESET_LEDS        // Reset para padrao");
            Serial.println("");
            Serial.println("EFEITOS DE CLIQUE:");
            Serial.println("   CLICK_EFFECT:XXX  // Define efeito de clique");
            Serial.println("   (FLASH, FADE, PULSE, RAINBOW, BREATHE,");
            Serial.println("    STROBE, SPARKLE, WAVE, EXPLOSION, CHASE, NONE)");
            Serial.println("");
            Serial.println("TEMAS VISUAIS:");
            Serial.println("   THEME:XXX         // Define tema do display");
            Serial.println("   (CLASSIC, CYBERPUNK, MINIMAL, RETRO)");
            Serial.println("");
            Serial.println("CONFIGURACAO:");
            Serial.println("   SAVE_EFFECT       // Salva efeito atual");
            Serial.println("   CLEAR_EFFECT      // Limpa efeito salvo");
            Serial.println("");
            Serial.println("AJUDA:");
            Serial.println("   HELP ou ?         // Esta mensagem");
            Serial.println("═══════════════════════════════════════");
        }
        else {
            Serial.println("Comando nao reconhecido: " + message);
            Serial.println("   Digite 'HELP' para ver comandos disponiveis");
        }
    }
}

// =========================================================================
// === MODULO 24: PERSISTENCIA =============================================
// =========================================================================

void loadBrightnessFromPrefs() {
    Serial.print("[PREF] Carregando brilho... ");
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        Serial.println("ERRO ao abrir namespace!");
        LED_BRIGHTNESS = 150;
        return;
    }
    if (preferences.isKey(KEY_BRIGHTNESS)) {
        LED_BRIGHTNESS = preferences.getInt(KEY_BRIGHTNESS, 150);
        Serial.println("OK: " + String(LED_BRIGHTNESS));
    } else {
        LED_BRIGHTNESS = 150;
        Serial.println("Usando padrao: 150");
    }
    preferences.end();
    LED_BRIGHTNESS = constrain(LED_BRIGHTNESS, 5, 255);
    FastLED.setBrightness(LED_BRIGHTNESS);
}

void saveBrightnessToPrefs() {
    Serial.print("[PREF] Salvando brilho " + String(LED_BRIGHTNESS) + "... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir namespace!");
        return;
    }
    preferences.putInt(KEY_BRIGHTNESS, LED_BRIGHTNESS);
    preferences.end();
    Serial.println("OK");
    verifyBrightnessSave();
}

void verifyBrightnessSave() {
    preferences.begin(PREFS_NAMESPACE, true);
    int saved = preferences.getInt(KEY_BRIGHTNESS, -1);
    preferences.end();
    if (saved == LED_BRIGHTNESS) {
        Serial.println("[VERIFY] Brilho verificado: " + String(LED_BRIGHTNESS));
    } else {
        Serial.println("[VERIFY] Falha! Esperado: " + String(LED_BRIGHTNESS) + ", Lido: " + String(saved));
    }
}

void loadEffectFromPrefs() {
    Serial.print("[PREF] Carregando efeito... ");
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        Serial.println("ERRO ao abrir namespace!");
        savedEffect = "NONE";
        return;
    }
    if (preferences.isKey(KEY_EFFECT)) {
        savedEffect = preferences.getString(KEY_EFFECT, "NONE");
        Serial.println("OK: " + savedEffect);
    } else {
        savedEffect = "NONE";
        Serial.println("Nenhum efeito salvo");
    }
    preferences.end();
}

void saveEffectToPrefs(String effectName) {
    Serial.print("[PREF] Salvando efeito '" + effectName + "'... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir namespace!");
        return;
    }
    preferences.putString(KEY_EFFECT, effectName);
    preferences.end();
    delay(50);
    Serial.println("OK");
    verifyEffectSave(effectName);
}

void verifyEffectSave(String expectedEffect) {
    preferences.begin(PREFS_NAMESPACE, true);
    String saved = preferences.getString(KEY_EFFECT, "ERROR");
    preferences.end();
    if (saved == expectedEffect) {
        Serial.println("[VERIFY] Efeito verificado: " + expectedEffect);
        savedEffect = expectedEffect;
    } else {
        Serial.println("[VERIFY] Falha! Esperado: '" + expectedEffect + "', Lido: '" + saved + "'");
    }
}

void clearEffectPrefs() {
    Serial.print("[PREF] Limpando efeito salvo... ");
    if (!preferences.begin(PREFS_NAMESPACE, false)) {
        Serial.println("ERRO ao abrir namespace!");
        return;
    }
    preferences.remove(KEY_EFFECT);
    preferences.end();
    savedEffect = "NONE";
    Serial.println("OK");
}

// =========================================================================
// === MODULO 25: FEEDBACK VISUAL ==========================================
// =========================================================================

void showConnectionFeedback(ConnectionProtocol newProtocol) {
    isInFeedbackMode = true;
    feedbackStartTime = millis();
    shouldRestoreEffect = effectActive;
    if (effectActive) {
        effectActive = false;
    }
    if (newProtocol == USB) {
        fill_solid(ledBackground, NUM_LEDS, CRGB::Blue);
        composeLedLayers();
        FastLED.show();
        Serial.println("Feedback: Conectado via USB");
    } else if (newProtocol == WIFI) {
        fill_solid(ledBackground, NUM_LEDS, CRGB::Green);
        composeLedLayers();
        FastLED.show();
        Serial.println("Feedback: Conectado via Wi-Fi");
    } else {
        fill_solid(ledBackground, NUM_LEDS, CRGB::Red);
        composeLedLayers();
        FastLED.show();
        Serial.println("Feedback: Desconectado");
    }
}

void restoreSavedEffect() {
    isInFeedbackMode = false;
    shouldRestoreEffect = false;
    if (savedEffect != "NONE" && savedEffect != "") {
        applySavedEffect();
        Serial.println("Efeito restaurado apos feedback");
    } else {
        currentEffect = "RAINBOW";
        saveEffectToPrefs("RAINBOW");
        Serial.println("Nenhum efeito salvo, aplicando padrao");
    }
}

void applySavedEffect() {
    for (int i = 0; i < NUM_LEDS; i++) {
        ledMask[i] = false;
    }
    if (savedEffect != "NONE" && savedEffect != "") {
        effectActive = true;
        currentEffect = savedEffect;
        effectTimer = millis();
        updateBackgroundEffect();
        composeLedLayers();
        FastLED.show();
        Serial.println("Efeito aplicado: " + savedEffect);
    } else {
        currentEffect = "RAINBOW";
        saveEffectToPrefs("RAINBOW");
        Serial.println("Nenhum Efeito encontrado, aplicando efeito: " + savedEffect);
    }
}

// =========================================================================
// === MODULO 26: SISTEMA DE POP-UP ========================================
// =========================================================================

void showPopup(const String &title, const String &message,
               const String &confirmText, const String &cancelText,
               uint16_t confirmColor, uint16_t cancelColor) {
    currentPopup.title = title;
    currentPopup.message = message;
    currentPopup.option1 = confirmText;
    currentPopup.option2 = cancelText;
    currentPopup.color1 = confirmColor;
    currentPopup.color2 = cancelColor;
    currentPopup.result = -1;
    popupActive = true;
    popupNeedsRedraw = true;
    Serial.println("Popup ativado: " + title);
}

void hidePopup() {
    popupActive = false;
    currentPopup.result = -1;
    redrawPreviousScreen();
}

void drawPopup() {
    if (!popupNeedsRedraw) return;
    tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, tft.color565(30, 30, 30));
    int popupWidth = 200;
    int popupHeight = 120;
    int popupX = (SCREEN_WIDTH - popupWidth) / 2;
    int popupY = (SCREEN_HEIGHT - popupHeight) / 2;
    tft.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, currentColors.menuBg);
    tft.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, currentColors.primary);
    tft.fillRoundRect(popupX, popupY, popupWidth, 25, 8, currentColors.primary);
    tft.setTextColor(currentColors.background);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(currentPopup.title, popupX + popupWidth / 2, popupY + 12);
    tft.setTextColor(currentColors.text);
    tft.setTextDatum(TC_DATUM);
    String message = currentPopup.message;
    int maxCharsPerLine = 28;
    int lineHeight = 12;
    int textY = popupY + 40;
    while (message.length() > 0) {
        String line;
        if (message.length() > maxCharsPerLine) {
            int lastSpace = message.lastIndexOf(' ', maxCharsPerLine);
            if (lastSpace > 0) {
                line = message.substring(0, lastSpace);
                message = message.substring(lastSpace + 1);
            } else {
                line = message.substring(0, maxCharsPerLine);
                message = message.substring(maxCharsPerLine);
            }
        } else {
            line = message;
            message = "";
        }
        tft.drawString(line, popupX + popupWidth / 2, textY);
        textY += lineHeight;
    }
    int buttonWidth = 80;
    int buttonHeight = 26;
    int buttonY = popupY + popupHeight - 35;
    int cancelX = popupX + 15;
    tft.fillRoundRect(cancelX, buttonY, buttonWidth, buttonHeight, 5, currentPopup.color2);
    tft.drawRoundRect(cancelX, buttonY, buttonWidth, buttonHeight, 5, currentColors.background);
    tft.setTextColor(currentColors.background);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(currentPopup.option2, cancelX + buttonWidth / 2, buttonY + buttonHeight / 2);
    int confirmX = popupX + popupWidth - buttonWidth - 15;
    tft.fillRoundRect(confirmX, buttonY, buttonWidth, buttonHeight, 5, currentPopup.color1);
    tft.drawRoundRect(confirmX, buttonY, buttonWidth, buttonHeight, 5, currentColors.background);
    tft.drawString(currentPopup.option1, confirmX + buttonWidth / 2, buttonY + buttonHeight / 2);
    tft.setTextColor(currentColors.secondary);
    tft.setTextSize(1);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Encoder: Navegar | Click: Selecionar",
                   popupX + popupWidth / 2, popupY + popupHeight - 8);
    tft.drawRoundRect(cancelX - 2, buttonY - 2,
                      buttonWidth + 4, buttonHeight + 4, 7, currentColors.accent);
    popupNeedsRedraw = false;
}

void handlePopupInput() {
    if (!popupActive) return;
    static int popupSelection = 0;
    static bool selectionChanged = false;
    int currentStateEncoder = digitalRead(ENCODER_CLK_PIN);
    if (currentStateEncoder != lastEncoderState) {
        int dtState = digitalRead(ENCODER_DT_PIN);
        int oldSelection = popupSelection;
        if (dtState != currentStateEncoder)
            popupSelection = 1;
        else
            popupSelection = 0;
        if (oldSelection != popupSelection)
            selectionChanged = true;
    }
    lastEncoderState = currentStateEncoder;
    if (selectionChanged) {
        updatePopupSelection(popupSelection);
        selectionChanged = false;
    }
    int btnState = digitalRead(ENCODER_BTN_PIN);
    if (btnState == LOW && encoderBtnLastState == HIGH) {
        unsigned long now = millis();
        if (now - lastEncoderBtnPress > ENCODER_DEBOUNCE_DELAY) {
            currentPopup.result = popupSelection;
            if (currentPopup.result == 1) {
                executePopupAction();
            }
            hidePopup();
            popupSelection = 0;
            lastEncoderBtnPress = now;
        }
    }
    encoderBtnLastState = btnState;
}

void updatePopupSelection(int selection) {
    int popupWidth = 200;
    int popupHeight = 120;
    int popupX = (SCREEN_WIDTH - popupWidth) / 2;
    int popupY = (SCREEN_HEIGHT - popupHeight) / 2;
    int buttonWidth = 80;
    int buttonHeight = 26;
    int buttonY = popupY + popupHeight - 35;
    int cancelX = popupX + 15;
    int confirmX = popupX + popupWidth - buttonWidth - 15;
    tft.drawRoundRect(cancelX - 2, buttonY - 2,
                      buttonWidth + 4, buttonHeight + 4, 7, currentColors.menuBg);
    tft.drawRoundRect(confirmX - 2, buttonY - 2,
                      buttonWidth + 4, buttonHeight + 4, 7, currentColors.menuBg);
    if (selection == 0) {
        tft.drawRoundRect(cancelX - 2, buttonY - 2,
                          buttonWidth + 4, buttonHeight + 4, 7, currentColors.accent);
    } else {
        tft.drawRoundRect(confirmX - 2, buttonY - 2,
                          buttonWidth + 4, buttonHeight + 4, 7, currentColors.accent);
    }
}

void executePopupAction() {
    Serial.println("Popup confirmado: " + currentPopup.title);
    if (currentPopup.title == "LIMPAR WI-FI") {
        clearWiFiCredentials();
        currentState = STATE_WIFI_CONFIG_MENU;
        resetRenderStates();
        drawWifiConfigMenu();
    }
    else if (currentPopup.title == "CONFIGURAR REDE") {
        currentState = STATE_WIFI_CONFIG_PORTAL;
        resetRenderStates();
        resetWiFiCredentials();
    }
    else if (currentPopup.title == "RESET DE FABRICA") {
        Serial.println("Executando reset de fabrica...");
        // Limpa todas as preferencias
        preferences.begin(PREFS_NAMESPACE, false);
        preferences.clear();
        preferences.end();
        savedEffect = "NONE";
        savedClickEffect = "FLASH";
        currentTheme = THEME_CLASSIC;
        applyTheme(THEME_CLASSIC);
        LED_BRIGHTNESS = 150;
        effectActive = false;
        clearAllLEDs();
        currentState = STATE_MAIN;
        resetRenderStates();
        drawMainScreen();
        Serial.println("Reset de fabrica completo!");
    }
}

void redrawPreviousScreen() {
    tft.fillScreen(currentColors.background);
    switch (currentState) {
        case STATE_SETTINGS_MENU:
            resetRenderStates();
            drawSettingsMenu();
            break;
        case STATE_WIFI_CONFIG_MENU:
            lastWifiMenuSelection = -1;
            drawWifiConfigMenu();
            break;
        case STATE_WIFI_CONFIG_PORTAL:
            drawWifiConfigPortal();
            break;
        case STATE_MAIN:
            drawMainScreen();
            break;
        case STATE_ADVANCED_SETTINGS:
            resetRenderStates();
            drawAdvancedSettings();
            break;
        case STATE_BRIGHTNESS_CONFIG:
            drawBrightnessConfigScreen();
            break;
        case STATE_LED_EFFECTS:
            menuSelection = 0;
            drawLedEffectsScreen();
            break;
        case STATE_CLICK_EFFECTS:
            menuSelection = 0;
            drawClickEffectsScreen();
            break;
        case STATE_THEME_SELECT:
            menuSelection = 0;
            drawThemeSelectScreen();
            break;
        case STATE_BATTERY_INFO:
            drawBatteryInfoScreen();
            break;
        case STATE_ABOUT_DEVICE:
            drawAboutDeviceScreen();
            break;
        default:
            break;
    }
}

// =========================================================================
// === MODULO 27: UTILITARIOS ==============================================
// =========================================================================

void listAllPreferences() {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("LISTA COMPLETA DE PREFERENCIAS");
    Serial.println("═══════════════════════════════════════");
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        Serial.println("ERRO: Nao foi possivel abrir preferences!");
        return;
    }
    Serial.println("Chaves conhecidas:");
    if (preferences.isKey(KEY_BRIGHTNESS)) {
        int val = preferences.getInt(KEY_BRIGHTNESS, -1);
        Serial.println("  • " + String(KEY_BRIGHTNESS) + ": " + String(val));
    } else {
        Serial.println("  • " + String(KEY_BRIGHTNESS) + ": NAO ENCONTRADA");
    }
    if (preferences.isKey(KEY_EFFECT)) {
        String val = preferences.getString(KEY_EFFECT, "NONE");
        Serial.println("  • " + String(KEY_EFFECT) + ": " + val);
    } else {
        Serial.println("  • " + String(KEY_EFFECT) + ": NAO ENCONTRADA");
    }
    if (preferences.isKey(KEY_CLICK_EFFECT)) {
        String val = preferences.getString(KEY_CLICK_EFFECT, "FLASH");
        Serial.println("  • " + String(KEY_CLICK_EFFECT) + ": " + val);
    } else {
        Serial.println("  • " + String(KEY_CLICK_EFFECT) + ": NAO ENCONTRADA");
    }
    if (preferences.isKey(KEY_THEME)) {
        String val = preferences.getString(KEY_THEME, "CLASSIC");
        Serial.println("  • " + String(KEY_THEME) + ": " + val);
    } else {
        Serial.println("  • " + String(KEY_THEME) + ": NAO ENCONTRADA");
    }
    if (preferences.isKey(KEY_WIFI_SSID)) {
        String val = preferences.getString(KEY_WIFI_SSID, "");
        Serial.println("  • " + String(KEY_WIFI_SSID) + ": " + (val.length() > 0 ? "CONFIGURADA" : "VAZIA"));
    } else {
        Serial.println("  • " + String(KEY_WIFI_SSID) + ": NAO ENCONTRADA");
    }
    preferences.end();
    Serial.println("═══════════════════════════════════════\n");
}

// =========================================================================
// === MODULO 28: INICIALIZACAO DE HARDWARE =================================
// =========================================================================

void initializeDisplay() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
}

void initButtons() {
    pinMode(dataPin, INPUT);
    pinMode(clockPin, OUTPUT);
    pinMode(latchPin, OUTPUT);
}

void initLEDs() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    clearAllLEDs();
}

void initEncoder() {
    pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
    pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
    lastEncoderState = digitalRead(ENCODER_CLK_PIN);
}

// =========================================================================
// === MODULO 29: SETUP =====================================================
// =========================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n═══════════════════════════════════════");
    Serial.println("ESP32 DECK - " + String(FIRMWARE_VERSION));
    Serial.println("═══════════════════════════════════════");

    initializeDisplay();
    initButtons();
    initLEDs();
    initEncoder();

    pinMode(PIN_TP4056_CE, OUTPUT);
    digitalWrite(PIN_TP4056_CE, LOW);

    if (!SPIFFS.begin(true)) {
        Serial.println("ERRO: Falha ao montar SPIFFS");
    }

    Serial.println("\n[SETUP] Carregando configuracoes...");
    loadBrightnessFromPrefs();
    loadEffectFromPrefs();
    loadClickEffectFromPrefs();
    loadThemeFromPrefs();
    listAllPreferences();

    drawLoadingScreen();
    initWiFi();

    if (savedEffect != "NONE" && savedEffect != "") {
        Serial.println("[SETUP] Restaurando efeito: " + savedEffect);
        effectActive = true;
        currentEffect = savedEffect;
        effectTimer = millis();
        updateBackgroundEffect();
        composeLedLayers();
        FastLED.show();
    } else {
        Serial.println("[SETUP] Nenhum efeito para restaurar");
        clearAllLEDs();
    }

    currentState = STATE_MAIN;
    drawMainScreen();

    Serial.println("\n═══════════════════════════════════════");
    Serial.println("SISTEMA PRONTO");
    Serial.println("═══════════════════════════════════════\n");
}

// =========================================================================
// === MODULO 30: LOOP PRINCIPAL ===========================================
// =========================================================================

void loop() {
    updateBatteryLogic();
    checkWiFiConnection();

    static unsigned long lastEncoderCheck = 0;
    if (millis() - lastEncoderCheck > 10) {
        if (popupActive) {
            handlePopupInput();
        } else {
            handleEncoder();
        }
        lastEncoderCheck = millis();
    }

    static unsigned long lastBatteryUpdate = 0;
    if (currentState == STATE_MAIN && millis() - lastBatteryUpdate > 2000) {
        updateBatteryDisplay();
        lastBatteryUpdate = millis();
    }

    updateLEDs();

    if (popupActive) {
        drawPopup();
    } else {
        switch (currentState) {
            case STATE_MAIN:
                checkButtons();
                checkSerialCommands();
                if (WiFi.status() == WL_CONNECTED) {
                    checkUdpSearch();
                    if (!client.connected()) {
                        WiFiClient newClient = serverTCP.available();
                        if (newClient) {
                            client = newClient;
                            activeProtocol = WIFI;
                            drawMainScreen();
                            Serial.println("Cliente Wi-Fi conectado");
                        }
                    }
                    if (client.connected()) {
                        while (client.available()) {
                            String msg = client.readStringUntil('\n');
                            msg.trim();
                            if (msg.startsWith("LED:") || msg.startsWith("ALL_LED:")) {
                                processLedCommand(msg);
                            } else if (msg == "PING") {
                                client.println("PONG");
                            } else if (msg == "DISCONNECT") {
                                client.stop();
                                if (activeProtocol == WIFI) {
                                    activeProtocol = NONE;
                                    drawMainScreen();
                                }
                                loadEffectFromPrefs();
                                applySavedEffect();
                                Serial.println("Cliente Wi-Fi desconectado por comando");
                            }
                        }
                    }
                }
                break;

            case STATE_WIFI_CONFIG_PORTAL:
                if (wifiConfigMode) {
                    dnsServer.processNextRequest();
                    server.handleClient();
                }
                break;

            default:
                break;
        }
    }

    delay(20);
}
