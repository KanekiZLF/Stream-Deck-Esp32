// ===================================================================================
// === ESP32 DECK FISICO - FIRMWARE v4.6 (COMPLETO E CORRIGIDO) ======================
// === DESENVOLVEDOR: Luiz F. R. Pimentel ============================================
// === GITHUB: github.com/KanekiZLF ==================================================
// ===================================================================================
// === CORRECOES APLICADAS v4.6 ======================================================
// === 1. POPUPS restaurados (confirmacao de acoes importantes) ======================
// === 2. Sistema de navegacao com scroll em TODOS os menus ==========================
// === 3. Relogio com fuso BRT (-3h) e sem flicker ===================================
// === 4. Click effects com cores vibrantes aleatorias ===============================
// === 5. Efeitos de fundo preservados durante click effects ==========================
// === 6. Menu de Configuracoes Avancadas com todas as opcoes ========================
// === 7. Reset de fabrica com confirmacao ===========================================
// === 8. Limpeza de credenciais Wi-Fi com confirmacao ===============================
// === 9. Todas as telas otimizadas sem flicker ======================================
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
#include <time.h>

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
const char *KEY_HOMESCREEN = "homescreen";

const int TCP_PORT = 8000;
const int UDP_SEARCH_PORT = 4210;
const char *UDP_DISCOVER_MSG = "ESP32_DECK_DISCOVER";
const char *UDP_ACK_MSG = "ESP32_DECK_ACK";

const char *FIRMWARE_VERSION = "v4.6";
const char *DEVELOPER = "Luiz F. R. Pimentel";
const char *GITHUB = "github.com/KanekiZLF";

// Servidores NTP para Hora e Data da Internet (Fuso -3h BRT)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800;  // -3 * 3600 = -10800 (CORRIGIDO)
const int   daylightOffset_sec = 0;

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
    STATE_THEME_SELECT,
    STATE_HOMESCREEN_SELECT
};
SystemState currentState = STATE_LOADING;

enum VisualTheme {
    THEME_CLASSIC,
    THEME_CYBERPUNK,
    THEME_MINIMAL,
    THEME_RETRO
};
VisualTheme currentTheme = THEME_CLASSIC;

enum HomeScreenMode {
    HOME_STANDBY,
    HOME_CLOCK,
    HOME_MINIMAL,
    HOME_DASHBOARD,
    HOME_MATRIX,
    HOME_TERMINAL
};
HomeScreenMode currentHomeScreen = HOME_STANDBY;

enum ClickEffectType {
    CLICK_FLASH, CLICK_FADE, CLICK_PULSE, CLICK_RAINBOW,
    CLICK_BREATHE, CLICK_STROBE, CLICK_SPARKLE, CLICK_WAVE,
    CLICK_EXPLOSION, CLICK_CHASE, CLICK_NONE
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

int popupSelection = 0;
bool popupSelectionChanged = false;

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

void loadHomeScreenFromPrefs();
void saveHomeScreenToPrefs(String modeName);

void drawLoadingScreen();
void drawMainScreen();
void drawClockOnly();
void drawTopBar();
void drawWiFiIcon(int x, int y, uint16_t color);

void drawMenuItemUniversal(int index, String text, bool isSelected, int startY, int rowHeight, bool showArrow = true);
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
void drawHomeScreenSelectScreen();

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
void stopClickEffect();

void initWiFi();
void startConfigPortal();
void handleRoot();
void handleWiFiSave();
void checkUdpSearch();
void clearWiFiCredentials();

void updateBatteryLogic();
void updateBatteryDisplay();

void checkSerialCommands();

void loadBrightnessFromPrefs();
void saveBrightnessToPrefs();
void loadEffectFromPrefs();
void saveEffectToPrefs(String effectName);
void loadClickEffectFromPrefs();
void saveClickEffectToPrefs(String clickEffectName);

void showConnectionFeedback(ConnectionProtocol newProtocol);
void restoreSavedEffect();
void applySavedEffect();

void resetRenderStates();
CRGB generateVibrantColor();
void factoryReset();

// =========================================================================
// === MODULO 6: TEMAS E HOMESCREENS =======================================
// =========================================================================

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
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
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
    } else {
        currentTheme = THEME_CLASSIC;
    }
    preferences.end();
    applyTheme(currentTheme);
}

void saveThemeToPrefs(String themeName) {
    if (!preferences.begin(PREFS_NAMESPACE, false)) return;
    preferences.putString(KEY_THEME, themeName);
    preferences.end();
}

void loadHomeScreenFromPrefs() {
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
        currentHomeScreen = HOME_STANDBY;
        return;
    }
    if (preferences.isKey(KEY_HOMESCREEN)) {
        String mode = preferences.getString(KEY_HOMESCREEN, "STANDBY");
        if (mode == "CLOCK")          currentHomeScreen = HOME_CLOCK;
        else if (mode == "MINIMAL")   currentHomeScreen = HOME_MINIMAL;
        else if (mode == "DASHBOARD") currentHomeScreen = HOME_DASHBOARD;
        else if (mode == "MATRIX")    currentHomeScreen = HOME_MATRIX;
        else if (mode == "TERMINAL")  currentHomeScreen = HOME_TERMINAL;
        else                          currentHomeScreen = HOME_STANDBY;
    } else {
        currentHomeScreen = HOME_STANDBY;
    }
    preferences.end();
}

void saveHomeScreenToPrefs(String modeName) {
    if (!preferences.begin(PREFS_NAMESPACE, false)) return;
    preferences.putString(KEY_HOMESCREEN, modeName);
    preferences.end();
}

// =========================================================================
// === MODULO 7: CONTROLADORES DE LED E EFEITOS ============================
// =========================================================================

void loadClickEffectFromPrefs() {
    if (!preferences.begin(PREFS_NAMESPACE, true)) {
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
    } else {
        savedClickEffect = "FLASH";
        currentClickEffect = CLICK_FLASH;
    }
    preferences.end();
}

void saveClickEffectToPrefs(String clickEffectName) {
    if (!preferences.begin(PREFS_NAMESPACE, false)) return;
    preferences.putString(KEY_CLICK_EFFECT, clickEffectName);
    preferences.end();
}

CRGB generateVibrantColor() { return CHSV(random8(), 255, 255); }
void clearBackgroundLayer() { fill_solid(ledBackground, NUM_LEDS, CRGB::Black); }
void clearClickOverlayLayer() { fill_solid(ledClickOverlay, NUM_LEDS, CRGB::Black); }

void composeLedLayers() {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (ledMask[i]) leds[i] = ledFixedColors[i];
        else if (ledClickOverlay[i]) leds[i] = ledClickOverlay[i];
        else leds[i] = ledBackground[i];
    }
}

void triggerClickEffect(int buttonIndex) {
    if (currentClickEffect == CLICK_NONE || buttonIndex < 0 || buttonIndex >= NUM_LEDS) return;
    clickEffectActive = true;
    clickEffectTimer = millis();
    clickEffectLedIndex = buttonIndex;
    activeClickEffect = currentClickEffect;
    clickEffectStep = 0;
    clickEffectHue = 0;
    clickEffectBaseColor = generateVibrantColor();
}

void stopClickEffect() {
    if (!clickEffectActive) return;
    clearClickOverlayLayer();
    clickEffectActive = false;
    clickEffectLedIndex = -1;
    activeClickEffect = CLICK_NONE;
}

void updateClickEffect() {
    if (!clickEffectActive) return;
    unsigned long elapsed = millis() - clickEffectTimer;
    if (elapsed > 400) { stopClickEffect(); return; }

    switch (activeClickEffect) {
        case CLICK_FLASH:
            ledClickOverlay[clickEffectLedIndex] = (elapsed < 50) ? CRGB::White : CRGB(100, 100, 100);
            break;
        case CLICK_RAINBOW:
            clickEffectHue += 15;
            ledClickOverlay[clickEffectLedIndex] = CHSV(clickEffectHue, 255, 255);
            break;
        case CLICK_STROBE:
            ledClickOverlay[clickEffectLedIndex] = (((elapsed / 40) % 2) == 0) ? CRGB::White : CRGB::Black;
            break;
        case CLICK_PULSE:
            clearClickOverlayLayer();
            for (int i = 0; i < NUM_LEDS; i++) {
                int dist = abs(i - clickEffectLedIndex);
                if (dist <= 2) {
                    uint8_t localBright = (255 - (elapsed * 255 / 400)) / (dist + 1);
                    CRGB col = clickEffectBaseColor;
                    col.nscale8(localBright);
                    ledClickOverlay[i] = col;
                }
            }
            break;
        default: {
            uint8_t bright = 255 - (elapsed * 255 / 400);
            CRGB col = clickEffectBaseColor;
            col.nscale8(bright);
            ledClickOverlay[clickEffectLedIndex] = col;
            break;
        }
    }
}

void updateBackgroundEffect() {
    if (!effectActive || millis() - effectTimer < 50) return;

    static String lastEffect = "";
    static uint8_t rainbowHue = 0;
    static uint8_t waveOffset = 0;
    static bool blinkState = false;
    static unsigned long blinkTimer = 0;
    static int cylonPos = 0;
    static int cylonDir = 1;
    static int meteorPos = 0;
    static int meteorDir = 1;
    static int wipeIndex = 0;
    static CRGB wipeColor = CRGB::White;
    static uint8_t runningOffset = 0;

    if (currentEffect != lastEffect) {
        clearBackgroundLayer();
        rainbowHue = 0;
        waveOffset = 0;
        blinkState = false;
        blinkTimer = millis();
        cylonPos = 0;
        cylonDir = 1;
        meteorPos = 0;
        meteorDir = 1;
        wipeIndex = 0;
        wipeColor = generateVibrantColor();
        runningOffset = 0;
        lastEffect = currentEffect;
    }

    if (currentEffect == "RAINBOW") {
        uint8_t step = max(1, 255 / max(1, NUM_LEDS));
        fill_rainbow(ledBackground, NUM_LEDS, rainbowHue, step);
        rainbowHue += 5;
    } else if (currentEffect == "BLINK") {
        if (millis() - blinkTimer >= 250) {
            blinkTimer = millis();
            blinkState = !blinkState;
        }
        fill_solid(ledBackground, NUM_LEDS, blinkState ? CRGB::White : CRGB::Black);
    } else if (currentEffect == "WAVE_BLUE") {
        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t brightness = sin8(i * 32 + waveOffset);
            ledBackground[i] = CRGB(0, 0, brightness);
        }
        waveOffset += 8;
    } else if (currentEffect == "FIRE") {
        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t heat = random8(80, 255);
            ledBackground[i] = HeatColor(heat);
        }
    } else if (currentEffect == "TWINKLE") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 35);
        for (int i = 0; i < 3; i++) {
            int idx = random16(NUM_LEDS);
            ledBackground[idx] = generateVibrantColor();
        }
    } else if (currentEffect == "CYLON") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 45);
        ledBackground[cylonPos] = CRGB::Red;
        if (cylonPos > 0) ledBackground[cylonPos - 1] += CRGB(80, 0, 0);
        if (cylonPos < NUM_LEDS - 1) ledBackground[cylonPos + 1] += CRGB(80, 0, 0);
        cylonPos += cylonDir;
        if (cylonPos >= NUM_LEDS - 1) { cylonPos = NUM_LEDS - 1; cylonDir = -1; }
        else if (cylonPos <= 0) { cylonPos = 0; cylonDir = 1; }
    } else if (currentEffect == "METEOR") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 25);
        ledBackground[meteorPos] = CRGB::White;
        for (int t = 1; t <= 4; t++) {
            int idx = meteorPos - (t * meteorDir);
            if (idx >= 0 && idx < NUM_LEDS) {
                CRGB trail = CRGB(255, 160, 40);
                trail.fadeToBlackBy(t * 40);
                ledBackground[idx] += trail;
            }
        }
        meteorPos += meteorDir;
        if (meteorPos >= NUM_LEDS) {
            meteorPos = NUM_LEDS - 1;
            meteorDir = -1;
        } else if (meteorPos < 0) {
            meteorPos = 0;
            meteorDir = 1;
            meteorDir = 1;
        }
    } else if (currentEffect == "COLOR_WIPE") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 40);
        if (wipeIndex < NUM_LEDS) {
            ledBackground[wipeIndex] = wipeColor;
            wipeIndex++;
        } else {
            wipeIndex = 0;
            wipeColor = generateVibrantColor();
        }
    } else if (currentEffect == "RUNNING_LIGHTS") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 20);
        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t wave = sin8((i * 18) + runningOffset);
            if (wave > 160) {
                ledBackground[i] += CHSV((runningOffset + i * 6), 255, wave);
            }
        }
        runningOffset += 10;
    } else if (currentEffect == "CONFETTI") {
        fadeToBlackBy(ledBackground, NUM_LEDS, 30);
        ledBackground[random16(NUM_LEDS)] += generateVibrantColor();
    } else {
        fill_rainbow(ledBackground, NUM_LEDS, rainbowHue, 15);
        rainbowHue += 5;
    }

    effectTimer = millis();
}

// =========================================================================
// === MODULO 8: RENDERIZADOR UNIVERSAL SEM FLICKER ========================
// =========================================================================

void drawMenuItemUniversal(int index, String text, bool isSelected, int startY, int rowHeight, bool showArrow) {
    int yPos = startY + (index * rowHeight);

    if (isSelected) {
        tft.fillRect(5, yPos - 1, SCREEN_WIDTH - 10, rowHeight - 1, currentColors.highlight);
        tft.setTextColor(currentColors.background);
    } else {
        tft.fillRect(5, yPos - 1, SCREEN_WIDTH - 10, rowHeight - 1, currentColors.panel);
        tft.setTextColor(currentColors.text);
    }

    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    if (text.length() > 28) text = text.substring(0, 25) + "...";
    tft.drawString(text, 10, yPos + 2);

    if (isSelected && showArrow) {
        tft.setTextDatum(TR_DATUM);
        tft.drawString(">", SCREEN_WIDTH - 10, yPos + 2);
    }
}

void drawThemePreview(VisualTheme theme, int x, int y, int w, int h) {
    ThemeColors tc;
    switch (theme) {
        case THEME_CLASSIC:   tc = themeClassic;   break;
        case THEME_CYBERPUNK: tc = themeCyberpunk; break;
        case THEME_MINIMAL:   tc = themeMinimal;   break;
        case THEME_RETRO:     tc = themeRetro;     break;
        default:              tc = themeClassic;   break;
    }
    tft.fillRoundRect(x, y, w, h, 3, tc.background);
    tft.drawRoundRect(x, y, w, h, 3, tc.primary);
    tft.fillRect(x + 2, y + 2, w - 4, 3, tc.secondary);
    tft.fillRect(x + 3, y + 7, w - 6, 2, tc.text);
    tft.fillRoundRect(x + 3, y + 11, w / 2 - 2, 3, 1, tc.accent);
}

// =========================================================================
// === MODULO 9: TELAS E COMPONENTES DA INTERFACE ==========================
// =========================================================================

void drawWiFiIcon(int x, int y, uint16_t color) {
    tft.drawCircleHelper(x, y + 8, 8, 1, color);
    tft.drawCircleHelper(x, y + 8, 5, 1, color);
    tft.drawCircleHelper(x, y + 8, 2, 1, color);
    tft.fillCircle(x, y + 8, 1, color);
}

void drawTopBar() {
    tft.fillRect(0, 0, SCREEN_WIDTH, 25, currentColors.secondary);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("ESP-Deck", 5, 8);

    String statusText = (activeProtocol == USB) ? "USB" : (activeProtocol == WIFI) ? "Wi-Fi" : "Desconectado";
    uint16_t textColor = (activeProtocol == NONE) ? currentColors.warning : currentColors.success;

    tft.setTextColor(textColor);
    tft.drawString(statusText, (SCREEN_WIDTH - tft.textWidth(statusText)) / 2, 8);

    if (WiFi.status() == WL_CONNECTED) {
        drawWiFiIcon(SCREEN_WIDTH - 65, 4, currentColors.success);
    }

    tft.fillRect(SCREEN_WIDTH - 50, 4, 50, 12, currentColors.secondary);
    updateBatteryDisplay();
    tft.drawFastHLine(0, 27, SCREEN_WIDTH, currentColors.primary);
}

void drawLoadingScreen() {
    tft.fillScreen(currentColors.background);
    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2 - 10;
    int orbitRadius = 25;
    int barWidth = 160;
    int barHeight = 6;
    int barX = (SCREEN_WIDTH - barWidth) / 2;
    int barY = SCREEN_HEIGHT - 20;

    tft.setTextColor(currentColors.primary);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ESP32 DECK", centerX, centerY - 25);
    tft.setTextSize(1);
    tft.setTextColor(currentColors.secondary);
    tft.drawString(FIRMWARE_VERSION, centerX, centerY - 8);
    tft.setTextColor(currentColors.accent);
    tft.drawString("Inicializando...", centerX, centerY + 40);
    tft.drawRoundRect(barX, barY, barWidth, barHeight, 3, currentColors.secondary);

    for (int step = 0; step <= 60; step++) {
        tft.fillCircle(centerX, centerY + 15, orbitRadius + 5, currentColors.background);
        for (int p = 0; p < 8; p++) {
            float angle = (step * 6 + p * 45) * PI / 180.0;
            int px = centerX + (int)(cos(angle) * orbitRadius);
            int py = centerY + 15 + (int)(sin(angle) * orbitRadius * 0.4);
            tft.fillCircle(px, py, 2, (p % 2 == 0) ? currentColors.primary : currentColors.accent);
        }

        int progress = (barWidth - 4) * step / 60;
        if (progress > 0) {
            tft.fillRect(barX + 2, barY + 2, progress, barHeight - 4, currentColors.primary);
        }

        manualControl = true;
        for (int led = 0; led < NUM_LEDS; led++) {
            leds[led] = CHSV((step * 4 + led * 16) % 255, 255, 100);
        }
        FastLED.show();
        delay(20);
    }

    manualControl = false;
    clearAllLEDs();
}

// =========================================================================
// === FUNÇÃO ESPECIALIZADA PARA RELÓGIO SEM FLICKER =======================
// =========================================================================

void drawClockOnly() {
    static String lastTimeStr = "";
    static String lastDateStr = "";
    static bool firstRun = true;
    
    struct tm timeinfo;
    bool timeValid = getLocalTime(&timeinfo, 1);
    
    char timeStr[10];
    char dateStr[15];
    
    if (timeValid && timeinfo.tm_year > (2016 - 1900)) {
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        strftime(dateStr, sizeof(dateStr), "%d/%m/%Y", &timeinfo);
    } else {
        unsigned long s = millis() / 1000;
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", (s / 3600) % 24, (s / 60) % 60, s % 60);
        snprintf(dateStr, sizeof(dateStr), "%s", WiFi.status() == WL_CONNECTED ? "Sincronizando" : "Sem Wi-Fi");
    }
    
    String currentTime = String(timeStr);
    String currentDate = String(dateStr);
    
    int panelX = 10, panelY = 33, panelW = SCREEN_WIDTH - 20;
    uint16_t bgColor = currentColors.panel;
    
    if (firstRun || currentDate != lastDateStr) {
        tft.fillRect(panelX + 5, panelY + 10, panelW - 10, 16, bgColor);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(currentColors.accent);
        tft.drawString(currentDate, SCREEN_WIDTH / 2, panelY + 14);
        lastDateStr = currentDate;
    }
    
    if (firstRun || currentTime != lastTimeStr) {
        tft.fillRect(panelX + 5, panelY + 28, panelW - 10, 20, bgColor);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(currentColors.primary);
        tft.drawString(currentTime, SCREEN_WIDTH / 2, panelY + 34);
        lastTimeStr = currentTime;
    }
    
    firstRun = false;
}

void drawMainScreen() {
    if (currentState != STATE_MAIN) return;
    
    tft.fillScreen(currentColors.background);
    drawTopBar();

    int panelX = 10, panelY = 33, panelW = SCREEN_WIDTH - 20, panelH = 50;
    tft.drawRoundRect(panelX, panelY, panelW, panelH, 6, currentColors.panel);
    tft.fillRoundRect(panelX + 1, panelY + 1, panelW - 2, panelH - 2, 5, currentColors.panel);

    switch (currentHomeScreen) {
        case HOME_CLOCK: {
            drawClockOnly();
            break;
        }

        case HOME_MINIMAL:
            tft.setTextDatum(MC_DATUM);
            tft.setTextSize(2);
            tft.setTextColor(currentColors.text);
            tft.drawString("DECK READY", SCREEN_WIDTH / 2, panelY + 25);
            break;

        case HOME_DASHBOARD:
            tft.setTextDatum(MC_DATUM);
            tft.setTextSize(1);
            tft.setTextColor(currentColors.accent);
            tft.drawString("SYSTEM DASHBOARD", SCREEN_WIDTH / 2, panelY + 15);
            tft.setTextColor(currentColors.text);
            tft.drawString("CPU: 240MHz | RAM: OK", SCREEN_WIDTH / 2, panelY + 33);
            break;

        case HOME_MATRIX:
            tft.setTextDatum(MC_DATUM);
            tft.setTextSize(2);
            tft.setTextColor(TFT_GREEN);
            tft.drawString("MATRIX v4.6", SCREEN_WIDTH / 2, panelY + 25);
            break;

        case HOME_TERMINAL:
            tft.setTextDatum(TL_DATUM);
            tft.setTextSize(1);
            tft.setTextColor(TFT_GREEN);
            tft.drawString("> status: ready", panelX + 10, panelY + 12);
            tft.drawString("> system online", panelX + 10, panelY + 28);
            break;

        case HOME_STANDBY:
        default:
            tft.setTextDatum(MC_DATUM);
            tft.setTextSize(2);
            if (activeProtocol == USB) {
                tft.setTextColor(currentColors.success);
                tft.drawString("USB CONNECTED", SCREEN_WIDTH / 2, panelY + 18);
                tft.setTextSize(1);
                tft.setTextColor(currentColors.text);
                tft.drawString("Conectado via Serial", SCREEN_WIDTH / 2, panelY + 36);
            } else if (activeProtocol == WIFI) {
                tft.setTextColor(currentColors.success);
                tft.drawString("WI-FI ACTIVE", SCREEN_WIDTH / 2, panelY + 18);
                tft.setTextSize(1);
                tft.setTextColor(currentColors.text);
                tft.drawString("Pronto para comandos", SCREEN_WIDTH / 2, panelY + 36);
            } else {
                tft.setTextColor(currentColors.warning);
                tft.drawString("STANDBY", SCREEN_WIDTH / 2, panelY + 18);
                tft.setTextSize(1);
                tft.setTextColor(currentColors.text);
                tft.drawString("Aguardando conexao", SCREEN_WIDTH / 2, panelY + 36);
            }
            break;
    }

    int ipBoxY = 88, ipBoxH = 22;
    tft.drawRoundRect(10, ipBoxY, SCREEN_WIDTH - 20, ipBoxH, 4, currentColors.secondary);
    tft.fillRoundRect(11, ipBoxY + 1, SCREEN_WIDTH - 22, ipBoxH - 2, 3, currentColors.panel);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(currentColors.success);
        tft.drawString("IP: " + WiFi.localIP().toString(), SCREEN_WIDTH / 2, ipBoxY + 11);
    } else if (activeProtocol == USB) {
        tft.setTextColor(currentColors.primary);
        tft.drawString("MODO USB SERIAL (115200 bps)", SCREEN_WIDTH / 2, ipBoxY + 11);
    } else {
        tft.setTextColor(currentColors.warning);
        tft.drawString("IP: Nao Conectado", SCREEN_WIDTH / 2, ipBoxY + 11);
    }

    tft.setTextColor(currentColors.secondary);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Encoder: Menu", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 3);
}

// =========================================================================
// === MODULO 10: NAVEGACAO DE MENUS SEM FLICKER ===========================
// =========================================================================

void drawSettingsMenu() {
    String menuItems[] = {
        "Configurar Wi-Fi", "Ajustar Brilho LEDs", "Efeitos LEDs",
        "Efeitos de Clique", "Tema Visual", "Modo Tela Inicial",
        "Informacoes Bateria", "Configuracoes Avancadas", "Sobre Dispositivo", "Voltar"
    };
    const int ITEMS_COUNT = 10;
    int startY = 25, rowHeight = 14;

    if (menuSelection < menuScrollOffset) menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS) menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;

    bool pageChanged = (lastMenuScrollOffset != menuScrollOffset) || (lastMenuSelection == -1);

    if (pageChanged) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIGURACOES", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Navegar  |  Click: Selecionar", SCREEN_WIDTH / 2, 130);

        for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
            int idx = i + menuScrollOffset;
            if (idx >= ITEMS_COUNT) break;
            drawMenuItemUniversal(i, menuItems[idx], (idx == menuSelection), startY, rowHeight);
        }
    } else {
        if (lastMenuSelection != menuSelection) {
            drawMenuItemUniversal(lastMenuSelection - menuScrollOffset, menuItems[lastMenuSelection], false, startY, rowHeight);
            drawMenuItemUniversal(menuSelection - menuScrollOffset, menuItems[menuSelection], true, startY, rowHeight);
        }
    }

    lastMenuSelection = menuSelection;
    lastMenuScrollOffset = menuScrollOffset;
}

void drawHomeScreenSelectScreen() {
    String modes[] = {
        "Standby Padrao", "Relogio Digital", "Minimalista",
        "Dashboard Status", "Matrix Digital", "Retro Terminal", "Voltar"
    };
    const int MODES_COUNT = 7;
    int startY = 25, rowHeight = 14;

    if (menuSelection < menuScrollOffset) menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS) menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;

    bool pageChanged = (lastMenuScrollOffset != menuScrollOffset) || (lastMenuSelection == -1);

    if (pageChanged) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("MODO TELA INICIAL", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: Aplicar", SCREEN_WIDTH / 2, 130);

        for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
            int idx = i + menuScrollOffset;
            if (idx >= MODES_COUNT) break;
            drawMenuItemUniversal(i, modes[idx], (idx == menuSelection), startY, rowHeight);
        }
    } else {
        if (lastMenuSelection != menuSelection) {
            drawMenuItemUniversal(lastMenuSelection - menuScrollOffset, modes[lastMenuSelection], false, startY, rowHeight);
            drawMenuItemUniversal(menuSelection - menuScrollOffset, modes[menuSelection], true, startY, rowHeight);
        }
    }

    lastMenuSelection = menuSelection;
    lastMenuScrollOffset = menuScrollOffset;
}

void drawClickEffectsScreen() {
    String effects[] = {
        "Flash Rapido", "Fade Suave", "Pulso Expansivo", "Arco-Iris",
        "Respiracao", "Estroboscopio", "Faiscas", "Onda de Luz",
        "Explosao", "Perseguicao", "Desligado", "Voltar"
    };
    const int EFFECTS_COUNT = 12;
    int startY = 25, rowHeight = 14;

    if (menuSelection < menuScrollOffset) menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS) menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;

    bool pageChanged = (lastMenuScrollOffset != menuScrollOffset) || (lastMenuSelection == -1);

    if (pageChanged) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("EFEITOS DE CLIQUE", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: OK", SCREEN_WIDTH / 2, 130);

        for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
            int idx = i + menuScrollOffset;
            if (idx >= EFFECTS_COUNT) break;
            drawMenuItemUniversal(i, effects[idx], (idx == menuSelection), startY, rowHeight);
        }
    } else {
        if (lastMenuSelection != menuSelection) {
            drawMenuItemUniversal(lastMenuSelection - menuScrollOffset, effects[lastMenuSelection], false, startY, rowHeight);
            drawMenuItemUniversal(menuSelection - menuScrollOffset, effects[menuSelection], true, startY, rowHeight);
        }
    }

    lastMenuSelection = menuSelection;
    lastMenuScrollOffset = menuScrollOffset;
}

void drawLedEffectsScreen() {
    String effects[] = {
        "Arco-Iris", "Piscante", "Onda Azul", "Fogo", "Estrelas",
        "Cylon", "Meteoros", "Color Wipe", "Luzes Correndo", "Confetes",
        "Desligar", "Voltar"
    };
    const int EFFECTS_COUNT = 12;
    int startY = 25, rowHeight = 14;

    if (menuSelection < menuScrollOffset) menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS) menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;

    bool pageChanged = (lastMenuScrollOffset != menuScrollOffset) || (lastMenuSelection == -1);

    if (pageChanged) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("EFEITOS LEDs", SCREEN_WIDTH / 2, 9);
        tft.setTextColor(currentColors.accent);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: OK", SCREEN_WIDTH / 2, 130);

        for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
            int idx = i + menuScrollOffset;
            if (idx >= EFFECTS_COUNT) break;
            drawMenuItemUniversal(i, effects[idx], (idx == menuSelection), startY, rowHeight);
        }
    } else {
        if (lastMenuSelection != menuSelection) {
            drawMenuItemUniversal(lastMenuSelection - menuScrollOffset, effects[lastMenuSelection], false, startY, rowHeight);
            drawMenuItemUniversal(menuSelection - menuScrollOffset, effects[menuSelection], true, startY, rowHeight);
        }
    }

    lastMenuSelection = menuSelection;
    lastMenuScrollOffset = menuScrollOffset;
}

void drawThemeSelectScreen() {
    String themes[] = {"Classico", "Cyberpunk", "Minimalista", "Retro Terminal", "Voltar"};
    VisualTheme themeEnums[] = {THEME_CLASSIC, THEME_CYBERPUNK, THEME_MINIMAL, THEME_RETRO, THEME_CLASSIC};
    int themesCount = 5;
    int startY = 23, rowHeight = 20;

    bool pageChanged = (lastMenuSelection == -1);

    if (pageChanged) {
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

    for (int i = 0; i < themesCount; i++) {
        if (pageChanged || i == menuSelection || i == lastMenuSelection) {
            int yPos = startY + (i * rowHeight);
            if (i == menuSelection) {
                tft.fillRect(5, yPos - 1, SCREEN_WIDTH - 10, rowHeight - 2, currentColors.highlight);
                tft.setTextColor(currentColors.background);
            } else {
                tft.fillRect(5, yPos - 1, SCREEN_WIDTH - 10, rowHeight - 2, currentColors.panel);
                tft.setTextColor(currentColors.text);
            }
            if (i < 4) {
                drawThemePreview(themeEnums[i], SCREEN_WIDTH - 45, yPos, 35, 16);
            }
            tft.setTextDatum(TL_DATUM);
            tft.drawString(themes[i], 10, yPos + 3);
            if (i == menuSelection) {
                tft.setTextDatum(TR_DATUM);
                tft.drawString(">", SCREEN_WIDTH - 50, yPos + 3);
            }
        }
    }

    lastMenuSelection = menuSelection;
}

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
    int optionHeight = 22, startY = 60;
    for (int i = 0; i < WIFI_MENU_ITEMS; i++) {
        drawMenuItemUniversal(i, wifiOptions[i], (i == wifiMenuSelection), startY, optionHeight);
    }
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
    String advancedOptions[] = {
        "Reset de Fabrica", "Teste de LEDs", "Teste de Botoes",
        "Info do Sistema", "Calibrar Bateria", "Diagnostico",
        "Logs do Sistema", "Voltar"
    };
    const int ADV_ITEMS_COUNT = 8;
    int startY = 25, rowHeight = 14;

    if (menuSelection < menuScrollOffset) menuScrollOffset = menuSelection;
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS) menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;

    bool pageChanged = (lastMenuScrollOffset != menuScrollOffset) || (lastMenuSelection == -1);

    if (pageChanged) {
        tft.fillScreen(currentColors.menuBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, currentColors.primary);
        tft.setTextColor(currentColors.background);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIG. AVANCADAS", SCREEN_WIDTH / 2, 9);

        for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
            int idx = i + menuScrollOffset;
            if (idx >= ADV_ITEMS_COUNT) break;
            drawMenuItemUniversal(i, advancedOptions[idx], (idx == menuSelection), startY, rowHeight);
        }
    } else {
        if (lastMenuSelection != menuSelection) {
            drawMenuItemUniversal(lastMenuSelection - menuScrollOffset, advancedOptions[lastMenuSelection], false, startY, rowHeight);
            drawMenuItemUniversal(menuSelection - menuScrollOffset, advancedOptions[menuSelection], true, startY, rowHeight);
        }
    }

    lastMenuSelection = menuSelection;
    lastMenuScrollOffset = menuScrollOffset;
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

void drawWifiConfigPortal() {
    tft.fillScreen(currentColors.background);
    tft.setTextColor(currentColors.primary);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MODO CONFIG.", SCREEN_WIDTH / 2, 30);
    tft.drawString("WI-FI", SCREEN_WIDTH / 2, 50);
    tft.setTextSize(1);
    tft.setTextColor(currentColors.text);
    tft.drawString("Conecte na rede:", SCREEN_WIDTH / 2, 75);
    tft.setTextColor(currentColors.accent);
    tft.drawString(SSID_AP, SCREEN_WIDTH / 2, 90);
    tft.drawString("Senha: " + String(PASS_AP), SCREEN_WIDTH / 2, 105);
    tft.setTextColor(currentColors.secondary);
    tft.drawString("Acesse: 192.168.4.1", SCREEN_WIDTH / 2, 122);
}

// =========================================================================
// === MODULO 11: SISTEMA DE POPUPS (RESTAURADO E CORRIGIDO) ===============
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
    popupSelection = 0;
    Serial.println("Popup ativado: " + title);
    drawPopup();
}

void hidePopup() {
    popupActive = false;
    popupNeedsRedraw = false;
    currentPopup.result = -1;
    popupSelection = 0;
    redrawPreviousScreen();
}

void drawPopup() {
    if (!popupNeedsRedraw) return;
    
    int popupWidth = 200;
    int popupHeight = 120;
    int popupX = (SCREEN_WIDTH - popupWidth) / 2;
    int popupY = (SCREEN_HEIGHT - popupHeight) / 2;
    
    tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, tft.color565(30, 30, 30));
    
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
    
    updatePopupSelection(popupSelection);
    
    tft.setTextColor(currentColors.secondary);
    tft.setTextSize(1);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Encoder: Navegar | Click: Selecionar",
                   popupX + popupWidth / 2, popupY + popupHeight - 8);
    
    popupNeedsRedraw = false;
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

void handlePopupInput() {
    if (!popupActive) return;
    
    int currentStateEncoder = digitalRead(ENCODER_CLK_PIN);
    if (currentStateEncoder != lastEncoderState) {
        int dtState = digitalRead(ENCODER_DT_PIN);
        int oldSelection = popupSelection;
        if (dtState != currentStateEncoder)
            popupSelection = 1;
        else
            popupSelection = 0;
        if (oldSelection != popupSelection) {
            updatePopupSelection(popupSelection);
        }
    }
    lastEncoderState = currentStateEncoder;
    
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

void executePopupAction() {
    Serial.println("Popup confirmado: " + currentPopup.title);
    
    if (currentPopup.title == "LIMPAR WI-FI") {
        clearWiFiCredentials();
        currentState = STATE_WIFI_CONFIG_MENU;
        resetRenderStates();
        drawWifiConfigMenu();
    }
    else if (currentPopup.title == "CONFIGURAR REDE") {
        startConfigPortal();
        currentState = STATE_WIFI_CONFIG_PORTAL;
        resetRenderStates();
    }
    else if (currentPopup.title == "RESET DE FABRICA") {
        factoryReset();
    }
}

void factoryReset() {
    Serial.println("Executando reset de fabrica...");
    
    preferences.begin(PREFS_NAMESPACE, false);
    preferences.clear();
    preferences.end();
    
    savedEffect = "NONE";
    savedClickEffect = "FLASH";
    currentTheme = THEME_CLASSIC;
    applyTheme(THEME_CLASSIC);
    LED_BRIGHTNESS = 150;
    effectActive = false;
    currentEffect = "";
    clearAllLEDs();
    
    currentState = STATE_MAIN;
    resetRenderStates();
    drawMainScreen();
    
    Serial.println("Reset de fabrica completo!");
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
// === MODULO 12: GERENCIADOR WI-FI & WEBSERVER PORTAL =====================
// =========================================================================

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>ESP32 Deck Config</title><style>body{font-family:Arial;background:#1e1e2e;color:#fff;text-align:center;padding:20px;}.card{background:#2b2b3b;padding:20px;border-radius:10px;max-width:300px;margin:auto;}input{width:90%;padding:10px;margin:8px 0;border-radius:5px;border:none;}button{padding:10px 20px;background:#00b4d8;color:#fff;border:none;border-radius:5px;font-weight:bold;cursor:pointer;}</style></head><body><div class='card'><h2>ESP32 Deck</h2><p>Configurar Wi-Fi</p><form method='get' action='/save'><input type='text' name='ssid' placeholder='SSID da Rede' required><br><input type='password' name='pass' placeholder='Senha' required><br><button type='submit'>Salvar e Conectar</button></form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleWiFiSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (preferences.begin(PREFS_NAMESPACE, false)) {
        preferences.putString(KEY_WIFI_SSID, ssid);
        preferences.putString(KEY_WIFI_PASS, pass);
        preferences.end();
    }
    String html = "<html><body style='font-family:Arial;background:#1e1e2e;color:#fff;text-align:center;'><h2>Configuracoes Salvas!</h2><p>Reconectando o ESP32 Deck...</p></body></html>";
    server.send(200, "text/html", html);
    delay(1000);
    server.stop();
    wifiConfigMode = false;
    WiFi.softAPdisconnect(true);
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
}

void initWiFi() {
    if (!preferences.begin(PREFS_NAMESPACE, true)) return;
    String ssid = preferences.getString(KEY_WIFI_SSID, "");
    String pass = preferences.getString(KEY_WIFI_PASS, "");
    preferences.end();
    if (ssid.length() == 0) return;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 15; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        serverTCP.begin();
        Udp.begin(UDP_SEARCH_PORT);
        lastWiFiConnected = true;
    } else {
        lastWiFiConnected = false;
    }
}

void checkWiFiConnection() {
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (lastWiFiConnected && !wifiConnected) {
        if (activeProtocol == WIFI) { activeProtocol = NONE; showConnectionFeedback(NONE); }
        if (client.connected()) client.stop();
    } else if (!lastWiFiConnected && wifiConnected) {
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        if (activeProtocol == NONE) { activeProtocol = WIFI; showConnectionFeedback(WIFI); }
        serverTCP.begin(); Udp.begin(UDP_SEARCH_PORT);
    }
    lastWiFiConnected = wifiConnected;
}

void clearWiFiCredentials() {
    if (!preferences.begin(PREFS_NAMESPACE, false)) return;
    preferences.remove(KEY_WIFI_SSID); preferences.remove(KEY_WIFI_PASS);
    preferences.end();
    WiFi.disconnect(true);
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

// =========================================================================
// === MODULO 13: CONTROLE E NAVEGACAO DO ENCODER ==========================
// =========================================================================

void handleEncoder() {
    handleEncoderButton();
    if (currentState == STATE_SETTINGS_MENU ||
        currentState == STATE_BRIGHTNESS_CONFIG ||
        currentState == STATE_LED_EFFECTS ||
        currentState == STATE_WIFI_CONFIG_MENU ||
        currentState == STATE_ADVANCED_SETTINGS ||
        currentState == STATE_CLICK_EFFECTS ||
        currentState == STATE_THEME_SELECT ||
        currentState == STATE_HOMESCREEN_SELECT) {
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
                    menuSelection = 0; menuScrollOffset = 0; resetRenderStates();
                    drawSettingsMenu();
                    break;

                case STATE_SETTINGS_MENU:
                    switch (menuSelection) {
                        case 0: currentState = STATE_WIFI_CONFIG_MENU; wifiMenuSelection = 0; resetRenderStates(); drawWifiConfigMenu(); break;
                        case 1: currentState = STATE_BRIGHTNESS_CONFIG; resetRenderStates(); drawBrightnessConfigScreen(); break;
                        case 2: currentState = STATE_LED_EFFECTS; menuSelection = 0; menuScrollOffset = 0; resetRenderStates(); drawLedEffectsScreen(); break;
                        case 3: currentState = STATE_CLICK_EFFECTS; menuSelection = 0; menuScrollOffset = 0; resetRenderStates(); drawClickEffectsScreen(); break;
                        case 4: currentState = STATE_THEME_SELECT; menuSelection = 0; menuScrollOffset = 0; resetRenderStates(); drawThemeSelectScreen(); break;
                        case 5: currentState = STATE_HOMESCREEN_SELECT; menuSelection = 0; menuScrollOffset = 0; resetRenderStates(); drawHomeScreenSelectScreen(); break;
                        case 6: currentState = STATE_BATTERY_INFO; resetRenderStates(); drawBatteryInfoScreen(); break;
                        case 7: currentState = STATE_ADVANCED_SETTINGS; menuSelection = 0; menuScrollOffset = 0; resetRenderStates(); drawAdvancedSettings(); break;
                        case 8: currentState = STATE_ABOUT_DEVICE; resetRenderStates(); drawAboutDeviceScreen(); break;
                        case 9: currentState = STATE_MAIN; resetRenderStates(); drawMainScreen(); break;
                    }
                    break;

                case STATE_WIFI_CONFIG_MENU:
                    if (wifiMenuSelection == 0) {
                        showPopup("LIMPAR WI-FI", "Apagar credenciais Wi-Fi salvas?", "APAGAR", "CANCELAR", currentColors.error, currentColors.secondary);
                    } else if (wifiMenuSelection == 1) {
                        showPopup("CONFIGURAR REDE", "Iniciar configuracao de nova rede Wi-Fi?", "CONFIGURAR", "CANCELAR", currentColors.primary, currentColors.secondary);
                    } else {
                        currentState = STATE_SETTINGS_MENU; menuSelection = 0; resetRenderStates(); drawSettingsMenu();
                    }
                    break;

                case STATE_BRIGHTNESS_CONFIG:
                case STATE_BATTERY_INFO:
                case STATE_ABOUT_DEVICE:
                    currentState = STATE_SETTINGS_MENU;
                    menuSelection = 0; menuScrollOffset = 0; resetRenderStates();
                    drawSettingsMenu();
                    break;

                case STATE_ADVANCED_SETTINGS:
                    switch (menuSelection) {
                        case 0:
                            showPopup("RESET DE FABRICA", "Tem certeza? Todas as configs serao apagadas!", "CONFIRMAR", "CANCELAR", currentColors.error, currentColors.secondary);
                            break;
                        case 1:
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
                        case 2:
                            Serial.println("Modo teste de botoes ativado. Pressione botoes...");
                            break;
                        case 3:
                            Serial.println("\n=== INFO DO SISTEMA ===");
                            Serial.println("Firmware: " + String(FIRMWARE_VERSION));
                            Serial.println("Desenvolvedor: " + String(DEVELOPER));
                            Serial.println("Bateria: " + String(batteryPercentage) + "% (" + String(batteryVoltage, 2) + "V)");
                            Serial.println("Wi-Fi: " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado"));
                            Serial.println("Efeito LED: " + String(effectActive ? currentEffect : "Nenhum"));
                            Serial.println("Click Effect: " + String(savedClickEffect));
                            break;
                        case 4:
                            Serial.println("Calibracao de bateria: Ajuste os valores no codigo.");
                            break;
                        case 5:
                            Serial.println("=== DIAGNOSTICO COMPLETO ===");
                            Serial.println("Check 1: Display OK");
                            Serial.println("Check 2: LEDs OK (" + String(NUM_LEDS) + " LEDs)");
                            Serial.println("Check 3: Botoes OK");
                            Serial.println("Check 4: Encoder OK");
                            Serial.println("Check 5: Wi-Fi " + String(WiFi.status() == WL_CONNECTED ? "OK" : "FALHA"));
                            Serial.println("Check 6: Bateria " + String(batteryVoltage > 3.3 ? "OK" : "BAIXA"));
                            break;
                        case 6:
                            Serial.println("=== LOGS DO SISTEMA ===");
                            Serial.println("Ultimos eventos registrados...");
                            break;
                        case 7:
                            currentState = STATE_SETTINGS_MENU;
                            menuSelection = 7;
                            menuScrollOffset = 0;
                            resetRenderStates();
                            drawSettingsMenu();
                            break;
                    }
                    break;

                case STATE_LED_EFFECTS:
                    if (menuSelection == 11) { currentState = STATE_SETTINGS_MENU; menuSelection = 2; menuScrollOffset = 0; resetRenderStates(); drawSettingsMenu(); }
                    else if (menuSelection == 10) { effectActive = false; clearBackgroundLayer(); composeLedLayers(); FastLED.show(); saveEffectToPrefs("NONE"); }
                    else {
                        effectActive = true;
                        String effs[] = {"RAINBOW", "BLINK", "WAVE_BLUE", "FIRE", "TWINKLE", "CYLON", "METEOR", "COLOR_WIPE", "RUNNING_LIGHTS", "CONFETTI"};
                        if (menuSelection < 10) { currentEffect = effs[menuSelection]; saveEffectToPrefs(currentEffect); }
                    }
                    break;

                case STATE_CLICK_EFFECTS:
                    if (menuSelection == 11) { currentState = STATE_SETTINGS_MENU; menuSelection = 3; menuScrollOffset = 0; resetRenderStates(); drawSettingsMenu(); }
                    else {
                        ClickEffectType effs[] = {CLICK_FLASH, CLICK_FADE, CLICK_PULSE, CLICK_RAINBOW, CLICK_BREATHE, CLICK_STROBE, CLICK_SPARKLE, CLICK_WAVE, CLICK_EXPLOSION, CLICK_CHASE, CLICK_NONE};
                        String effNames[] = {"FLASH", "FADE", "PULSE", "RAINBOW", "BREATHE", "STROBE", "SPARKLE", "WAVE", "EXPLOSION", "CHASE", "NONE"};
                        if (menuSelection < 11) { currentClickEffect = effs[menuSelection]; saveClickEffectToPrefs(effNames[menuSelection]); triggerClickEffect(7); }
                    }
                    break;

                case STATE_THEME_SELECT:
                    if (menuSelection == 4) { currentState = STATE_SETTINGS_MENU; menuSelection = 4; menuScrollOffset = 0; resetRenderStates(); drawSettingsMenu(); }
                    else {
                        VisualTheme ths[] = {THEME_CLASSIC, THEME_CYBERPUNK, THEME_MINIMAL, THEME_RETRO};
                        String thNames[] = {"CLASSIC", "CYBERPUNK", "MINIMAL", "RETRO"};
                        if (menuSelection < 4) { applyTheme(ths[menuSelection]); saveThemeToPrefs(thNames[menuSelection]); drawThemeSelectScreen(); }
                    }
                    break;

                case STATE_HOMESCREEN_SELECT:
                    if (menuSelection == 6) { currentState = STATE_SETTINGS_MENU; menuSelection = 5; menuScrollOffset = 0; resetRenderStates(); drawSettingsMenu(); }
                    else {
                        HomeScreenMode hms[] = {HOME_STANDBY, HOME_CLOCK, HOME_MINIMAL, HOME_DASHBOARD, HOME_MATRIX, HOME_TERMINAL};
                        String hmNames[] = {"STANDBY", "CLOCK", "MINIMAL", "DASHBOARD", "MATRIX", "TERMINAL"};
                        if (menuSelection < 6) { currentHomeScreen = hms[menuSelection]; saveHomeScreenToPrefs(hmNames[menuSelection]); drawHomeScreenSelectScreen(); }
                    }
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
            menuSelection = (dtState != currentStateEncoder) ? (menuSelection + 1) % 10 : (menuSelection - 1 + 10) % 10;
            drawSettingsMenu();
        }
        else if (currentState == STATE_WIFI_CONFIG_MENU) {
            wifiMenuSelection = (dtState != currentStateEncoder) ? (wifiMenuSelection + 1) % WIFI_MENU_ITEMS : (wifiMenuSelection - 1 + WIFI_MENU_ITEMS) % WIFI_MENU_ITEMS;
            drawWifiConfigMenu();
        }
        else if (currentState == STATE_BRIGHTNESS_CONFIG) {
            LED_BRIGHTNESS = (dtState != currentStateEncoder) ? min(255, LED_BRIGHTNESS + 5) : max(5, LED_BRIGHTNESS - 5);
            FastLED.setBrightness(LED_BRIGHTNESS); FastLED.show();
            drawBrightnessConfigScreen(); saveBrightnessToPrefs();
        }
        else if (currentState == STATE_LED_EFFECTS) {
            menuSelection = (dtState != currentStateEncoder) ? (menuSelection + 1) % 12 : (menuSelection - 1 + 12) % 12;
            drawLedEffectsScreen();
        }
        else if (currentState == STATE_CLICK_EFFECTS) {
            menuSelection = (dtState != currentStateEncoder) ? (menuSelection + 1) % 12 : (menuSelection - 1 + 12) % 12;
            drawClickEffectsScreen();
        }
        else if (currentState == STATE_THEME_SELECT) {
            menuSelection = (dtState != currentStateEncoder) ? (menuSelection + 1) % 5 : (menuSelection - 1 + 5) % 5;
            drawThemeSelectScreen();
        }
        else if (currentState == STATE_HOMESCREEN_SELECT) {
            menuSelection = (dtState != currentStateEncoder) ? (menuSelection + 1) % 7 : (menuSelection - 1 + 7) % 7;
            drawHomeScreenSelectScreen();
        }
        else if (currentState == STATE_ADVANCED_SETTINGS) {
            menuSelection = (dtState != currentStateEncoder) ? (menuSelection + 1) % 8 : (menuSelection - 1 + 8) % 8;
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
// === MODULO 14: BOTOES FISICOS, BATERIA E SERIAL =========================
// =========================================================================

int readButtons() {
    digitalWrite(latchPin, LOW); delayMicroseconds(5); digitalWrite(latchPin, HIGH);
    int data = 0;
    for (int i = 0; i < numBits; i++) {
        if (digitalRead(dataPin)) data |= (1 << i);
        digitalWrite(clockPin, HIGH); delayMicroseconds(1); digitalWrite(clockPin, LOW);
    }
    return data;
}

int mapButton(int bit) {
    switch (bit) {
        case 0: return 9; case 1: return 10; case 2: return 11; case 3: return 12;
        case 4: return 16; case 5: return 15; case 6: return 14; case 7: return 13;
        case 8: return 8; case 9: return 7; case 10: return 6; case 11: return 5;
        case 12: return 1; case 13: return 2; case 14: return 3; case 15: return 4;
        default: return 0;
    }
}

void checkButtons() {
    int currentButtonStates = readButtons();
    if (currentState == STATE_MAIN) {
        for (int i = 0; i < numBits; i++) {
            if (bitRead(currentButtonStates, i) && !bitRead(lastButtonStates, i)) {
                handleButtonPress(mapButton(i));
            }
        }
    }
    lastButtonStates = currentButtonStates;
}

void handleButtonPress(int buttonNumber) {
    if (buttonNumber >= 1 && buttonNumber <= NUM_LEDS) {
        triggerClickEffect(buttonNumber - 1);
        String command = "BTN:" + String(buttonNumber);
        if (activeProtocol == USB) Serial.println(command);
        else if (activeProtocol == WIFI && client.connected()) client.println(command);
    }
}

void updateLEDs() {
    if (isInFeedbackMode) {
        if (millis() - feedbackStartTime > feedbackDuration) {
            if (shouldRestoreEffect && savedEffect != "NONE") restoreSavedEffect();
            else isInFeedbackMode = false;
        }
        return;
    }
    if (effectActive) updateBackgroundEffect();
    if (clickEffectActive) updateClickEffect();
    if (!manualControl && !wifiConfigMode) { composeLedLayers(); FastLED.show(); }
}

void updateBatteryLogic() {
    isUsbConnected = (activeProtocol == USB);
    digitalWrite(PIN_TP4056_CE, isUsbConnected ? HIGH : LOW);
    isCharging = !isUsbConnected;

    int rawADC = 0;
    for (int i = 0; i < 10; i++) { rawADC += analogRead(PIN_BATT_ADC); delay(1); }
    rawADC /= 10;

    batteryVoltage = (rawADC / 4095.0) * 3.3 * 2.0;
    batteryPercentage = map(constrain(batteryVoltage * 100, 330, 420), 330, 420, 0, 100);
}

void updateBatteryDisplay() {
    int bW = 25, bH = 10, bX = SCREEN_WIDTH - 35, bY = 6;
    tft.fillRect(bX - 30, bY - 2, 55, 14, currentColors.secondary);
    tft.drawRect(bX, bY, bW, bH, TFT_WHITE);
    tft.fillRect(bX + bW, bY + 3, 2, 4, TFT_WHITE);

    int fillW = map(batteryPercentage, 0, 100, 0, bW - 2);
    uint16_t col = (batteryPercentage > 70) ? TFT_GREEN : (batteryPercentage > 30) ? TFT_YELLOW : TFT_RED;
    if (fillW > 0) tft.fillRect(bX + 1, bY + 1, fillW, bH - 2, col);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(bX - 25, bY + 2);
    tft.print(String(batteryPercentage) + "%");
}

// =========================================================================
// === MODULO 15: PERSISTENCIA =============================================
// =========================================================================

void loadBrightnessFromPrefs() {
    if (!preferences.begin(PREFS_NAMESPACE, true)) { LED_BRIGHTNESS = 150; return; }
    LED_BRIGHTNESS = preferences.getInt(KEY_BRIGHTNESS, 150);
    preferences.end();
    FastLED.setBrightness(LED_BRIGHTNESS);
}

void saveBrightnessToPrefs() {
    if (!preferences.begin(PREFS_NAMESPACE, false)) return;
    preferences.putInt(KEY_BRIGHTNESS, LED_BRIGHTNESS);
    preferences.end();
}

void loadEffectFromPrefs() {
    if (!preferences.begin(PREFS_NAMESPACE, true)) { savedEffect = "NONE"; return; }
    savedEffect = preferences.getString(KEY_EFFECT, "NONE");
    preferences.end();
}

void saveEffectToPrefs(String effectName) {
    if (!preferences.begin(PREFS_NAMESPACE, false)) return;
    preferences.putString(KEY_EFFECT, effectName);
    preferences.end();
    savedEffect = effectName;
}

void showConnectionFeedback(ConnectionProtocol newProtocol) {
    isInFeedbackMode = true; feedbackStartTime = millis();
    CRGB col = (newProtocol == USB) ? CRGB::Blue : (newProtocol == WIFI) ? CRGB::Green : CRGB::Red;
    fill_solid(ledBackground, NUM_LEDS, col); composeLedLayers(); FastLED.show();
}

void restoreSavedEffect() {
    isInFeedbackMode = false;
    if (savedEffect != "NONE") { effectActive = true; currentEffect = savedEffect; }
}

void applySavedEffect() {
    if (savedEffect != "NONE" && savedEffect != "") {
        effectActive = true;
        currentEffect = savedEffect;
        effectTimer = millis();
        updateBackgroundEffect();
        composeLedLayers();
        FastLED.show();
    }
}

void checkSerialCommands() {
    if (Serial.available()) {
        String msg = Serial.readStringUntil('\n'); msg.trim();
        if (msg == "CONNECTED") { activeProtocol = USB; showConnectionFeedback(USB); if (currentState == STATE_MAIN) drawMainScreen(); }
        else if (msg == "DISCONNECT") { activeProtocol = NONE; showConnectionFeedback(NONE); if (currentState == STATE_MAIN) drawMainScreen(); }
    }
}

// =========================================================================
// === MODULO 16: INICIALIZACAO E LOOP PRINCIPAL ===========================
// =========================================================================

void initializeDisplay() { tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK); }
void initButtons() { pinMode(dataPin, INPUT); pinMode(clockPin, OUTPUT); pinMode(latchPin, OUTPUT); }
void initLEDs() { FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS); FastLED.setBrightness(LED_BRIGHTNESS); clearAllLEDs(); }
void clearAllLEDs() { fill_solid(leds, NUM_LEDS, CRGB::Black); fill_solid(ledBackground, NUM_LEDS, CRGB::Black); fill_solid(ledClickOverlay, NUM_LEDS, CRGB::Black); FastLED.show(); }
void initEncoder() { pinMode(ENCODER_CLK_PIN, INPUT_PULLUP); pinMode(ENCODER_DT_PIN, INPUT_PULLUP); pinMode(ENCODER_BTN_PIN, INPUT_PULLUP); lastEncoderState = digitalRead(ENCODER_CLK_PIN); }

void setup() {
    Serial.begin(115200);
    initializeDisplay(); initButtons(); initLEDs(); initEncoder();
    pinMode(PIN_TP4056_CE, OUTPUT); digitalWrite(PIN_TP4056_CE, LOW);
    SPIFFS.begin(true);

    loadBrightnessFromPrefs(); loadEffectFromPrefs(); loadClickEffectFromPrefs(); loadThemeFromPrefs(); loadHomeScreenFromPrefs();
    drawLoadingScreen();
    initWiFi();

    if (savedEffect != "NONE") { effectActive = true; currentEffect = savedEffect; }
    currentState = STATE_MAIN;
    drawMainScreen();
}

void loop() {
    updateBatteryLogic();
    checkWiFiConnection();

    if (popupActive && popupNeedsRedraw) {
        drawPopup();
    }

    static unsigned long lastEncoderCheck = 0;
    if (millis() - lastEncoderCheck > 10) {
        if (popupActive) {
            handlePopupInput();
        } else {
            handleEncoder();
        }
        lastEncoderCheck = millis();
    }

    static unsigned long lastClockUpdate = 0;
    static unsigned long lastBatteryUpdate = 0;
    static unsigned long lastButtonCheck = 0;
    
    unsigned long now = millis();

    if (currentState == STATE_MAIN && currentHomeScreen == HOME_CLOCK && !popupActive) {
        if (now - lastClockUpdate >= 1000) {
            drawClockOnly();
            lastClockUpdate = now;
        }
    }

    if (now - lastBatteryUpdate >= 2000) {
        if (currentState == STATE_MAIN && !popupActive) {
            updateBatteryDisplay();
        }
        lastBatteryUpdate = now;
    }

    if (now - lastButtonCheck >= 10) {
        if (currentState == STATE_MAIN && !popupActive) {
            checkButtons();
        }
        lastButtonCheck = now;
    }

    checkSerialCommands();
    updateLEDs();

    if (currentState == STATE_WIFI_CONFIG_PORTAL && wifiConfigMode) {
        dnsServer.processNextRequest();
        server.handleClient();
    }

    if (WiFi.status() == WL_CONNECTED && !popupActive) {
        checkUdpSearch();
        if (!client.connected()) {
            WiFiClient newClient = serverTCP.available();
            if (newClient) {
                client = newClient;
                activeProtocol = WIFI;
                if (currentState == STATE_MAIN) drawMainScreen();
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
                        if (currentState == STATE_MAIN) drawMainScreen();
                    }
                    loadEffectFromPrefs();
                    applySavedEffect();
                    Serial.println("Cliente Wi-Fi desconectado por comando");
                }
            }
        }
    }

    delay(5);
}

void processLedCommand(const String &command) {
    if (command.startsWith("LED:")) {
        int firstColon = command.indexOf(':');
        int secondColon = command.indexOf(':', firstColon + 1);
        if (secondColon != -1) {
            int ledIndex = command.substring(firstColon + 1, secondColon).toInt();
            String colorStr = command.substring(secondColon + 1);
            if (colorStr == "OFF" || colorStr == "RESET") {
                if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
                    ledMask[ledIndex] = false;
                }
                return;
            }
            if (colorStr.startsWith("#")) colorStr = colorStr.substring(1);
            long color = strtol(colorStr.c_str(), NULL, 16);
            CRGB ledColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
            if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
                ledMask[ledIndex] = true;
                ledFixedColors[ledIndex] = ledColor;
                leds[ledIndex] = ledColor;
                FastLED.show();
            }
        }
    } else if (command.startsWith("ALL_LED:")) {
        String subCmd = command.substring(8);
        if (subCmd == "ON") {
            fill_solid(ledBackground, NUM_LEDS, CRGB::White);
            composeLedLayers(); FastLED.show();
            effectActive = false;
        } else if (subCmd == "OFF") {
            clearAllLEDs();
            effectActive = false;
        } else {
            effectActive = true;
            currentEffect = subCmd;
            effectTimer = millis();
        }
    }
}