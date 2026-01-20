// ===================================================================================
// === ESP32 DECK FISICO - INTERFACE COMPLETA CORRIGIDA =============================
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
// === CONFIGURACOES GERAIS ================================================
// =========================================================================
const char *SSID_AP = "ESP32-Deck-Setup";
const char *PASS_AP = "12345678";
const char *PREFS_KEY = "wifi_config";
const char *PREFS_BRIGHTNESS_KEY = "led_brightness";
const char *PREFS_EFFECT_KEY = "led_effect";
const int TCP_PORT = 8000;
const char *FIRMWARE_VERSION = "v2.8 Professional";
const char *DEVELOPER = "Luiz F. R. Pimentel";
const char *GITHUB = "github.com/KanekiZLF";

// Constantes para Busca UDP
const int UDP_SEARCH_PORT = 4210;
const char *UDP_DISCOVER_MSG = "ESP32_DECK_DISCOVER";
const char *UDP_ACK_MSG = "ESP32_DECK_ACK";

// =========================================================================
// === CONFIGURACOES DO SHIFT REGISTER =====================================
// =========================================================================
const int dataPin = 17;
const int clockPin = 21;
const int latchPin = 22;
const int numBits = 16;

// =========================================================================
// === CONFIGURACOES DOS LEDs WS2812B ======================================
// =========================================================================
#define LED_PIN 12
#define NUM_LEDS 16
int LED_BRIGHTNESS = 50;

// =========================================================================
// === CONFIGURACOES DA BATERIA ============================================
// =========================================================================
#define PIN_BATT_ADC 33
#define PIN_TP4056_CE 13

float batteryVoltage = 0.0;
int batteryPercentage = 0;
bool isUsbConnected = false;
bool isCharging = false;
int lastBatteryPercentage = -1; // Para controle de atualização

// =========================================================================
// === CONFIGURACOES DO DISPLAY ============================================
// =========================================================================
TFT_eSPI tft = TFT_eSPI();
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
int lastMenuSelection = -1;
int lastMenuScrollOffset = -1;

// =========================================================================
// === CONFIGURACOES DO ENCODER EC11 =======================================
// =========================================================================
const int ENCODER_CLK_PIN = 25;
const int ENCODER_DT_PIN = 26;
const int ENCODER_BTN_PIN = 27;

// =========================================================================
// === VARIAVEIS GLOBAIS ===================================================
// =========================================================================

String savedEffect = "NONE";         // Efeito salvo nas preferências
bool shouldRestoreEffect = false;    // Flag para restaurar efeito após feedback
unsigned long feedbackStartTime = 0; // Quando começou o feedback
int feedbackDuration = 500;          // Duração do feedback em ms
bool isInFeedbackMode = false;       // Se está mostrando feedback de conexão

// Máscara de LEDs - LEDs com cores fixas
bool ledMask[NUM_LEDS] = {false, false, false, false, false, false, false, false,
                          false, false, false, false, false, false, false, false};
CRGB ledFixedColors[NUM_LEDS]; // Cores fixas para LEDs mascarados

// Enum para rastrear o protocolo ativo
enum ConnectionProtocol
{
    NONE,
    USB,
    WIFI
};
ConnectionProtocol activeProtocol = NONE;
bool lastWiFiConnected = false; // Para detectar mudanças no Wi-Fi

// Estados do sistema
enum SystemState
{
    STATE_LOADING,
    STATE_MAIN,
    STATE_SETTINGS_MENU,
    STATE_WIFI_CONFIG_MENU,
    STATE_WIFI_CONFIG_PORTAL,
    STATE_BRIGHTNESS_CONFIG,
    STATE_BATTERY_INFO,
    STATE_LED_EFFECTS,
    STATE_ADVANCED_SETTINGS,
    STATE_ABOUT_DEVICE
};
SystemState currentState = STATE_LOADING;

// Variaveis para menu com scroll
int menuSelection = 0;
int menuScrollOffset = 0;
const int MENU_ITEMS_COUNT = 8;   // Incluindo "Sobre Dispositivo"
const int VISIBLE_MENU_ITEMS = 7; // Itens visíveis na tela
int wifiMenuSelection = 0;
const int WIFI_MENU_ITEMS = 3;

int lastWifiMenuSelection = -1;

// LEDs - NÃO altera cor quando entra no menu
CRGB leds[NUM_LEDS];
bool manualControl = false;
bool effectActive = false;
String currentEffect = "";
unsigned long effectTimer = 0;
unsigned long ledFeedbackTimer = 0;
bool ledFeedbackActive = false;
int feedbackLedIndex = -1;

// Encoder
int lastEncoderState = 0;
int encoderBtnLastState = HIGH;
unsigned long lastEncoderBtnPress = 0;
const unsigned long ENCODER_DEBOUNCE_DELAY = 50;

// Wi-Fi e Rede
Preferences preferences;
WiFiServer serverTCP(TCP_PORT);
WiFiClient client;
WebServer server(80);
DNSServer dnsServer;
WiFiUDP Udp;
bool wifiConfigMode = false;

// Botoes
int lastButtonStates = 0;

// =========================================================================
// === CONFIGURACAO DE CORES ===============================================
// =========================================================================
#define BACKGROUND_COLOR TFT_BLACK
#define PRIMARY_COLOR TFT_CYAN
#define SECONDARY_COLOR 0x4A69 // Azul medio
#define ACCENT_COLOR TFT_YELLOW
#define SUCCESS_COLOR TFT_GREEN
#define WARNING_COLOR TFT_ORANGE
#define ERROR_COLOR TFT_RED
#define TEXT_COLOR TFT_WHITE
#define HIGHLIGHT_COLOR 0xF81F // Rosa
#define PANEL_COLOR 0x18E0     // Azul escuro
#define MENU_BG_COLOR 0x3186   // Cinza azulado

// =========================================================================
// === PROTOTIPOS DE FUNCOES ===============================================
// =========================================================================
// Sistema
void initializeDisplay();
void initButtons();
void initLEDs();
void initEncoder();
void checkWiFiConnection();

// Telas
void drawLoadingScreen();
void drawMainScreen();
void drawSettingsMenu();
void drawWifiConfigMenu();
void drawWifiConfigPortal();
void drawBrightnessConfigScreen();
void drawBatteryInfoScreen();
void drawLedEffectsScreen();
void drawAdvancedSettings();
void drawAboutDeviceScreen();

// Controles
void handleEncoder();
void handleEncoderButton();
void handleEncoderRotation();
void checkButtons();
int readButtons();
int mapButton(int bit);
void handleButtonPress(int buttonNumber);
void updateLedFeedback();

// LEDs
void updateLEDs();
void setStatusLEDs();
void clearAllLEDs();
void processLedCommand(const String &command);
void processIndividualLedCommand(const String &command);
void processAllLedCommand(const String &command);
void updateEffect();
// void showCurrentStatusLEDs();

void clearLedMask();
void applyLedMask(); // Aplica a máscara sobre os LEDs

// Wi-Fi
void initWiFi();
void startConfigPortal();
void handleRoot();
void handleWiFiSave();
void checkUdpSearch();
void resetWiFiCredentials();
void clearWiFiCredentials();

// Bateria
void updateBatteryLogic();
void updateBatteryDisplay();

// Comunicacao
void checkSerialCommands();

// =========================================================================
// === IMPLEMENTACOES DE FUNCOES ===========================================
// =========================================================================

// =========================================================================
// === INICIALIZACAO =======================================================
// =========================================================================

void initializeDisplay()
{
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(BACKGROUND_COLOR);
    tft.setTextColor(TEXT_COLOR);
    Serial.println("Display inicializado");
}

void initButtons()
{
    pinMode(latchPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    pinMode(dataPin, INPUT);
    digitalWrite(latchPin, HIGH);
    digitalWrite(clockPin, LOW);
    Serial.println("Botoes inicializados");
}

void initLEDs()
{
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    clearAllLEDs();
    Serial.println("LEDs WS2812B inicializados");
}

void initEncoder()
{
    pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
    pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
    lastEncoderState = digitalRead(ENCODER_CLK_PIN);
    Serial.println("Encoder EC11 inicializado");
}

// =========================================================================
// === SISTEMA DE TELAS OTIMIZADO =========================================
// =========================================================================

void drawLoadingScreen()
{
    tft.fillScreen(BACKGROUND_COLOR);

    // Logo central com efeito
    for (int step = 0; step < 3; step++)
    {
        tft.fillScreen(BACKGROUND_COLOR);

        // Logo com efeito de crescimento
        tft.setTextColor(PRIMARY_COLOR);
        tft.setTextSize(2 + step);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("ESP32 DECK", SCREEN_WIDTH / 2, 30);

        // Versao
        tft.setTextSize(1);
        tft.setTextColor(SECONDARY_COLOR);
        tft.drawString(FIRMWARE_VERSION, SCREEN_WIDTH / 2, 55);

        // Status
        tft.setTextColor(ACCENT_COLOR);
        tft.drawString("Inicializando sistema...", SCREEN_WIDTH / 2, 75);

        // Informacoes adicionais
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("16 Botoes | 16 LEDs RGB", SCREEN_WIDTH / 2, 95);
        tft.drawString("Wi-Fi + USB Serial", SCREEN_WIDTH / 2, 110);

        // LEDs animados independentemente
        manualControl = true;
        for (int led = 0; led < NUM_LEDS; led++)
        {
            int hue = (step * 85 + led * 16) % 255;
            int brightness = 100 + (step * 50);
            leds[led] = CHSV(hue, 255, brightness);
        }
        FastLED.show();

        delay(300);
    }

    // Barra de progresso
    int barWidth = 180;
    int barHeight = 8;
    int barX = (SCREEN_WIDTH - barWidth) / 2;
    int barY = 120;

    tft.drawRoundRect(barX, barY, barWidth, barHeight, 3, PRIMARY_COLOR);

    for (int i = 0; i <= 100; i += 2)
    {
        int progressWidth = (barWidth * i) / 100;
        tft.fillRect(barX + 1, barY + 1, progressWidth, barHeight - 2, ACCENT_COLOR);

        delay(15);
    }

    manualControl = false;
    clearAllLEDs();

    delay(500);
}

void drawMainScreen()
{
    tft.fillScreen(BACKGROUND_COLOR);

    // Barra superior compacta
    tft.fillRect(0, 0, SCREEN_WIDTH, 25, SECONDARY_COLOR);

    // Logo e status em fonte pequena
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("ESP-Deck ", 5, 8);
    String statusText = "";
    if (activeProtocol == USB)
    {
        statusText = "USB";
        tft.setTextColor(TFT_GREEN);
    }
    else if (activeProtocol == WIFI)
    {
        statusText = "Wi-Fi";
        tft.setTextColor(TFT_GREEN);
    }
    else
    {
        statusText = "Desconectado";
        tft.setTextColor(TFT_RED);
    }
    tft.setTextColor(TFT_WHITE);
    tft.drawString(statusText, 80, 8);

    // Bateria no canto direito (com limpeza de area)
    tft.fillRect(SCREEN_WIDTH - 50, 4, 50, 12, SECONDARY_COLOR); // Limpa area da bateria
    updateBatteryDisplay();

    // Linha divisoria
    tft.drawFastHLine(0, 27, SCREEN_WIDTH, PRIMARY_COLOR);

    // Conteudo principal
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);

    if (activeProtocol == USB)
    {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("CONECTADO", SCREEN_WIDTH / 2, 55);
        tft.setTextSize(1);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Protocolo: USB Serial", SCREEN_WIDTH / 2, 75);
    }
    else if (activeProtocol == WIFI)
    {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("CONECTADO", SCREEN_WIDTH / 2, 55);
        tft.setTextSize(1);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Protocolo: Wi-Fi", SCREEN_WIDTH / 2, 75);
        if (WiFi.status() == WL_CONNECTED)
        {
            String ip = WiFi.localIP().toString();
            if (ip.length() > 15)
                ip = ip.substring(0, 12) + "...";
            tft.drawString("IP: " + ip, SCREEN_WIDTH / 2, 90);
        }
    }
    else
    {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("DESCONECTADO", SCREEN_WIDTH / 2, 55);
        tft.setTextSize(1);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Aguardando conexao", SCREEN_WIDTH / 2, 75);
    }

    // Informacoes do sistema
    tft.setTextColor(ACCENT_COLOR);
    tft.drawString("16 Botoes | 16 LEDs", SCREEN_WIDTH / 2, 105);

    // Instrucao
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Encoder: Menu", SCREEN_WIDTH / 2, 125);
}

void drawSettingsMenu()
{
    bool fullRedraw = false;

    // Verifica se precisa redesenhar tudo
    if (menuSelection != lastMenuSelection || menuScrollOffset != lastMenuScrollOffset)
    {
        if (lastMenuSelection == -1)
        {
            fullRedraw = true;
        }

        // Salva estados anteriores
        lastMenuSelection = menuSelection;
        lastMenuScrollOffset = menuScrollOffset;
    }

    if (fullRedraw)
    {
        tft.fillScreen(MENU_BG_COLOR);

        // 1. Cabeçalho mais compacto
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, PRIMARY_COLOR);
        tft.setTextColor(BACKGROUND_COLOR);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIGURACOES", SCREEN_WIDTH / 2, 9);

        // 5. Instruções fixadas no final da tela
        tft.setTextColor(ACCENT_COLOR);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Navegar  |  Click: Selecionar", SCREEN_WIDTH / 2, 130);
    }

    // 2. Lista de itens do menu
    String menuItems[] = {
        "Configurar Wi-Fi",
        "Ajustar Brilho LEDs",
        "Efeitos LEDs",
        "Informacoes Bateria",
        "Configuracoes Avancadas",
        "Sobre Dispositivo",
        "Teste de Sistema",
        "Voltar"};

    // Calcular scroll para 8 itens no total
    if (menuSelection < menuScrollOffset)
    {
        menuScrollOffset = menuSelection;
    }
    else if (menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS)
    {
        menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;
    }

    // 3. Layout otimizado para 240x135
    int startY = 25;    // Onde começa o primeiro item
    int rowHeight = 14; // Altura de cada linha

    // Desenhar itens visíveis (7 itens na tela)
    for (int i = 0; i < VISIBLE_MENU_ITEMS; i++)
    {
        int itemIndex = i + menuScrollOffset;
        if (itemIndex >= MENU_ITEMS_COUNT)
            break;

        int yPos = startY + (i * rowHeight);

        // Limpa apenas a área deste item
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, MENU_BG_COLOR);

        if (itemIndex == menuSelection)
        {
            // Retângulo de seleção
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, HIGHLIGHT_COLOR);
            tft.setTextColor(BACKGROUND_COLOR);
        }
        else
        {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, PANEL_COLOR);
            tft.setTextColor(TEXT_COLOR);
        }

        tft.setTextDatum(TL_DATUM);

        // Trunca texto longo
        String displayText = menuItems[itemIndex];
        if (displayText.length() > 22)
        {
            displayText = displayText.substring(0, 19) + "...";
        }

        tft.drawString(displayText, 10, yPos);

        if (itemIndex == menuSelection)
        {
            tft.setTextColor(BACKGROUND_COLOR);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
}

void drawWifiConfigMenu()
{
    // static int lastWifiMenuSelection = -1;

    bool fullRedraw = (lastWifiMenuSelection == -1);
    lastWifiMenuSelection = wifiMenuSelection;

    if (fullRedraw)
    {
        tft.fillScreen(MENU_BG_COLOR);

        // Cabecalho
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, PRIMARY_COLOR);
        tft.setTextColor(BACKGROUND_COLOR);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIGURAR WI-FI", SCREEN_WIDTH / 2, 9);

        // Status atual em painel superior
        tft.fillRect(10, 25, SCREEN_WIDTH - 20, 30, PANEL_COLOR);
        tft.drawRect(10, 25, SCREEN_WIDTH - 20, 30, SECONDARY_COLOR);

        tft.setTextColor(TEXT_COLOR);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Status:", 15, 32);

        if (WiFi.status() == WL_CONNECTED)
        {
            tft.setTextColor(SUCCESS_COLOR);
            tft.drawString("CONECTADO", 55, 32);
            tft.setTextColor(TEXT_COLOR);
            tft.drawString("Rede:", 15, 44);
            tft.drawString(WiFi.SSID().substring(0, 15), 55, 44);
        }
        else
        {
            tft.setTextColor(WARNING_COLOR);
            tft.drawString("DESCONECTADO", 55, 32);
        }

        // Instrucoes
        tft.setTextColor(ACCENT_COLOR);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Encoder: Selecionar opcao", SCREEN_WIDTH / 2, 130);
    }

    // Opcoes do menu Wi-Fi
    String wifiOptions[] = {"Limpar Credenciais", "Configurar Nova Rede", "Voltar"};

    int optionHeight = 22;
    int startY = 60;

    for (int i = 0; i < WIFI_MENU_ITEMS; i++)
    {
        int yPos = startY + (i * optionHeight);

        // Limpa apenas a área deste item
        tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, MENU_BG_COLOR);

        if (i == wifiMenuSelection)
        {
            tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, HIGHLIGHT_COLOR);
            tft.setTextColor(BACKGROUND_COLOR);
        }
        else
        {
            tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, PANEL_COLOR);
            tft.setTextColor(TEXT_COLOR);
        }

        tft.setTextDatum(TL_DATUM);
        tft.drawString(wifiOptions[i], 20, yPos);

        if (i == wifiMenuSelection)
        {
            tft.setTextColor(BACKGROUND_COLOR);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 20, yPos);
        }
    }
}

void drawWifiConfigPortal()
{
    tft.fillScreen(BACKGROUND_COLOR);

    tft.setTextColor(PRIMARY_COLOR);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MODO CONFIG.", SCREEN_WIDTH / 2, 40);
    tft.drawString("WI-FI", SCREEN_WIDTH / 2, 65);

    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Conecte-se a rede:", SCREEN_WIDTH / 2, 90);

    tft.setTextColor(ACCENT_COLOR);
    tft.drawString(SSID_AP, SCREEN_WIDTH / 2, 105);
    tft.drawString("Senha: " + String(PASS_AP), SCREEN_WIDTH / 2, 120);

    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("No navegador, acesse:", SCREEN_WIDTH / 2, 140);
    tft.drawString("192.168.4.1", SCREEN_WIDTH / 2, 155);
}

void drawBrightnessConfigScreen()
{
    tft.fillScreen(MENU_BG_COLOR);

    // 1. CABEÇALHO COM ÍCONE DE BRILHO
    tft.fillRect(0, 0, SCREEN_WIDTH, 24, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);

    // Desenha ícone de brilho (sol)
    int iconX = SCREEN_WIDTH / 2 - 40;
    tft.fillCircle(iconX, 12, 4, BACKGROUND_COLOR);
    for (int i = 0; i < 8; i++)
    {
        float angle = i * 45 * PI / 180;
        int x1 = iconX + (int)(cos(angle) * 8);
        int y1 = 12 + (int)(sin(angle) * 8);
        int x2 = iconX + (int)(cos(angle) * 12);
        int y2 = 12 + (int)(sin(angle) * 12);
        tft.drawLine(x1, y1, x2, y2, BACKGROUND_COLOR);
    }

    tft.drawString("BRILHO LEDs", SCREEN_WIDTH / 2, 12);

    // 2. VALOR ATUAL COM DESTAQUE
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(LED_BRIGHTNESS), SCREEN_WIDTH / 2, 45);

    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Nivel (5-255)", SCREEN_WIDTH / 2, 65);

    // 3. INDICADOR DE STATUS
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);

    if (LED_BRIGHTNESS <= 50)
    {
        tft.setTextColor(SECONDARY_COLOR);
        tft.drawString("BAIXO", SCREEN_WIDTH / 2, 75);
    }
    else if (LED_BRIGHTNESS <= 150)
    {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("MEDIO", SCREEN_WIDTH / 2, 75);
    }
    else
    {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("ALTO", SCREEN_WIDTH / 2, 75);
    }

    // 4. BARRA DE PROGRESSO COM MARCADORES
    int barWidth = 180;
    int barHeight = 14;
    int barX = (SCREEN_WIDTH - barWidth) / 2;
    int barY = 85;

    // Moldura da barra
    tft.drawRoundRect(barX, barY, barWidth, barHeight, 3, SECONDARY_COLOR);

    // Preenchimento gradiente
    int progressWidth = map(LED_BRIGHTNESS, 5, 255, 0, barWidth - 2);
    for (int i = 0; i < progressWidth; i++)
    {
        int colorPosition = map(i, 0, barWidth - 2, 0, 255);
        uint16_t segmentColor;

        if (i < (barWidth - 2) / 3)
        {
            segmentColor = tft.color565(0, colorPosition * 2, 100); // Azul para verde
        }
        else if (i < (barWidth - 2) * 2 / 3)
        {
            segmentColor = tft.color565(colorPosition, 180, 0); // Verde para amarelo
        }
        else
        {
            segmentColor = tft.color565(255, colorPosition, 0); // Amarelo para laranja
        }

        tft.drawFastVLine(barX + 1 + i, barY + 1, barHeight - 2, segmentColor);
    }

    // Marcadores
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Min", barX, barY + barHeight + 5);
    tft.drawString("Max", barX + barWidth, barY + barHeight + 5);

    // Marcador numérico no meio
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("128", barX + barWidth / 2, barY + barHeight + 5);

    // 5. INDICADOR VISUAL NA BARRA
    int markerX = barX + progressWidth;
    tft.fillTriangle(markerX, barY - 5, markerX - 4, barY - 1, markerX + 4, barY - 1, ACCENT_COLOR);

    // 6. EFETOS VISUAIS DE EXEMPLO
    tft.setTextColor(SECONDARY_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Efeito de exemplo:", SCREEN_WIDTH / 2, 115);

    // Desenha pequenos LEDs de exemplo
    int exampleY = 125;
    unsigned long currentMillis = millis(); // Usar variável local

    for (int i = 0; i < 8; i++)
    {
        int ledX = SCREEN_WIDTH / 2 - 35 + i * 10;
        int brightness = map(LED_BRIGHTNESS, 5, 255, 30, 255);

        // Corrigir o cálculo para evitar double
        int timeFactor = currentMillis / 500;
        float phase = timeFactor + i * 0.5;
        int ledBrightness = (int)(brightness * (0.5 + 0.5 * sin(phase)));

        // Usar min() com valores int
        int r = min(255, (int)(ledBrightness * 0.8));
        int g = min(255, (int)(ledBrightness * 0.6));
        int b = ledBrightness;

        // Aplicar limites
        r = (r > 255) ? 255 : r;
        g = (g > 255) ? 255 : g;
        b = (b > 255) ? 255 : b;

        uint16_t ledColor = tft.color565(r, g, b);

        tft.fillCircle(ledX, exampleY, 3, ledColor);
        tft.drawCircle(ledX, exampleY, 3, PANEL_COLOR);
    }

    // 7. INSTRUÇÕES
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Gire: Ajustar  |  Click: Salvar e Sair", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5);
}

void drawBatteryInfoScreen()
{
    tft.fillScreen(BACKGROUND_COLOR);

    // 1. CABEÇALHO COM ÍCONE DE BATERIA
    tft.fillRect(0, 0, SCREEN_WIDTH, 28, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);

    // Desenha ícone de bateria ao lado do texto
    int batteryIconX = SCREEN_WIDTH / 2 - 40;
    tft.drawRect(batteryIconX, 10, 20, 12, BACKGROUND_COLOR);
    tft.fillRect(batteryIconX + 20, 13, 3, 6, BACKGROUND_COLOR);

    // Texto do cabeçalho
    tft.drawString("INFORMACOES BATERIA", SCREEN_WIDTH / 2, 14);

    // 2. STATUS PRINCIPAL EM DESTAQUE
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);

    if (isUsbConnected)
    {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("USB CONECTADO", SCREEN_WIDTH / 2, 45);
    }
    else
    {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("MODO BATERIA", SCREEN_WIDTH / 2, 45);
    }

    // 3. DADOS NUMÉRICOS EM DUAS COLUNAS
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);

    // Coluna esquerda
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Tensao:", 15, 65);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString(String(batteryVoltage, 2) + " V", 70, 65);

    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Porcentagem:", 15, 80);
    tft.setTextColor(batteryPercentage > 20 ? SUCCESS_COLOR : ERROR_COLOR);
    tft.drawString(String(batteryPercentage) + " %", 90, 80);

    // Coluna direita
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Pino CE:", SCREEN_WIDTH / 2 + 15, 65);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString(isUsbConnected ? "HIGH" : "LOW", SCREEN_WIDTH / 2 + 70, 65);

    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Carregando:", SCREEN_WIDTH / 2 + 15, 80);
    tft.setTextColor(isCharging ? WARNING_COLOR : TEXT_COLOR);
    tft.drawString(isCharging ? "SIM" : "NAO", SCREEN_WIDTH / 2 + 85, 80);

    // 4. BARRA DE BATERIA GRANDE COM STATUS
    int battWidth = 200;
    int battHeight = 30;
    int battX = (SCREEN_WIDTH - battWidth) / 2;
    int battY = 100;

    // Moldura da bateria
    tft.drawRect(battX, battY, battWidth, battHeight, TEXT_COLOR);
    tft.fillRect(battX + battWidth, battY + 10, 5, 10, TEXT_COLOR);

    // Preenchimento colorido baseado na porcentagem
    int fillWidth = map(batteryPercentage, 0, 100, 0, battWidth - 2);

    // Escolhe cor baseado na porcentagem
    uint16_t fillColor;
    if (batteryPercentage >= 60)
    {
        fillColor = SUCCESS_COLOR; // Verde
    }
    else if (batteryPercentage >= 30)
    {
        fillColor = WARNING_COLOR; // Laranja
    }
    else if (batteryPercentage >= 15)
    {
        fillColor = TFT_YELLOW; // Amarelo
    }
    else
    {
        fillColor = ERROR_COLOR; // Vermelho
    }

    tft.fillRect(battX + 1, battY + 1, fillWidth, battHeight - 2, fillColor);

    // Texto dentro da barra (se houver espaço)
    if (fillWidth > 30)
    {
        tft.setTextColor(BACKGROUND_COLOR);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(1);
        tft.drawString(String(batteryPercentage) + "%", battX + fillWidth / 2, battY + battHeight / 2);
    }

    // 5. STATUS DO TP4056 (CONTROLE DE CARGA)
    int statusY = battY + battHeight + 15;

    tft.setTextColor(SECONDARY_COLOR);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Controle de Carga TP4056", SCREEN_WIDTH / 2, statusY);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);

    if (isUsbConnected)
    {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("BLOQUEADO (CE = HIGH)", SCREEN_WIDTH / 2, statusY + 15);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Carregamento desativado", SCREEN_WIDTH / 2, statusY + 30);
    }
    else
    {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("HABILITADO (CE = LOW)", SCREEN_WIDTH / 2, statusY + 15);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Carregamento ativo", SCREEN_WIDTH / 2, statusY + 30);
    }

    // 6. LEGENDA DE STATUS
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Encoder: Voltar ao Menu", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5);
}

// =========================================================================
// === FUNÇÕES PARA PERSISTÊNCIA DO BRILHO ================================
// =========================================================================

void loadBrightnessFromPrefs()
{
    preferences.begin(PREFS_BRIGHTNESS_KEY, true); // Modo leitura com chave separada
    LED_BRIGHTNESS = preferences.getInt("brightness", 150);
    preferences.end();

    // Garante que está dentro dos limites
    LED_BRIGHTNESS = constrain(LED_BRIGHTNESS, 5, 255);
    FastLED.setBrightness(LED_BRIGHTNESS);

    Serial.println("Brilho carregado: " + String(LED_BRIGHTNESS));
}

void saveBrightnessToPrefs()
{
    preferences.begin(PREFS_BRIGHTNESS_KEY, false); // Modo escrita com chave separada
    preferences.putInt("brightness", LED_BRIGHTNESS);
    preferences.end();

    Serial.println("Brilho salvo: " + String(LED_BRIGHTNESS));
}

void drawLedEffectsScreen()
{
    static int lastLedEffectsSelection = -1;

    bool fullRedraw = (lastLedEffectsSelection == -1);
    lastLedEffectsSelection = menuSelection;

    if (fullRedraw)
    {
        tft.fillScreen(MENU_BG_COLOR);

        // 1. Cabeçalho mais compacto
        tft.fillRect(0, 0, SCREEN_WIDTH, 18, PRIMARY_COLOR);
        tft.setTextColor(BACKGROUND_COLOR);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("EFEITOS LEDs", SCREEN_WIDTH / 2, 9);

        // 3. Instrucoes fixadas
        tft.setTextColor(ACCENT_COLOR);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("Gire: Sel. | Click: OK", SCREEN_WIDTH / 2, 130);
    }

    // 2. Lista de efeitos (7 itens)
    String effects[] = {"Arco-Iris", "Piscante", "Onda Azul", "Fogo", "Estrelas", "Desligar", "Voltar"};
    int effectsCount = 7;

    // Ajuste de layout para tela de 1.14"
    int startY = 25;    // Onde começa o primeiro item
    int rowHeight = 14; // Altura de cada linha

    for (int i = 0; i < effectsCount; i++)
    {
        int yPos = startY + (i * rowHeight);

        // Limpa apenas a área deste item
        tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, MENU_BG_COLOR);

        if (i == menuSelection)
        {
            // Retângulo de seleção
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, HIGHLIGHT_COLOR);
            tft.setTextColor(BACKGROUND_COLOR);
        }
        else
        {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, PANEL_COLOR);
            tft.setTextColor(TEXT_COLOR);
        }

        tft.setTextDatum(TL_DATUM);
        tft.drawString(effects[i], 10, yPos);

        if (i == menuSelection)
        {
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
}

void drawAdvancedSettings()
{
    tft.fillScreen(MENU_BG_COLOR);

    // Cabecalho
    tft.fillRect(0, 0, SCREEN_WIDTH, 22, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CONFIG. AVANCADAS", SCREEN_WIDTH / 2, 11);

    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(TL_DATUM);

    // Opcoes
    String options[] = {"Reset de Fabrica", "Teste LEDs", "Teste Botoes", "Info Sistema", "Calibrar Bateria"};

    for (int i = 0; i < 5; i++)
    {
        int yPos = 35 + (i * 18);
        tft.drawString(options[i], 20, yPos);
    }

    // Status
    tft.setTextColor(ACCENT_COLOR);
    tft.drawString("Funcionalidades futuras", SCREEN_WIDTH / 2, 130);

    // Voltar
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Encoder: Voltar", SCREEN_WIDTH / 2, 150);
}

void drawAboutDeviceScreen()
{
    // 1. FUNDO COM GRADIENTE OU TEXTURA
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        // Gradiente suave de preto para azul escuro
        uint16_t lineColor = tft.color565(0, 0, y / 8);
        tft.drawFastHLine(0, y, SCREEN_WIDTH, lineColor);
    }

    // Adiciona padrão de pontos sutis
    for (int i = 0; i < 30; i++)
    {
        int x = random(SCREEN_WIDTH);
        int y = random(SCREEN_HEIGHT);
        tft.drawPixel(x, y, PANEL_COLOR);
    }

    // 2. MOLDURA/CARTÃO PRINCIPAL
    int cardX = 10;
    int cardY = 10;
    int cardWidth = SCREEN_WIDTH - 20;
    int cardHeight = SCREEN_HEIGHT - 20;

    // Cartão com sombra e borda
    tft.fillRoundRect(cardX + 2, cardY + 2, cardWidth, cardHeight, 8, 0x3186);
    tft.fillRoundRect(cardX, cardY, cardWidth, cardHeight, 8, BACKGROUND_COLOR);
    tft.drawRoundRect(cardX, cardY, cardWidth, cardHeight, 8, PRIMARY_COLOR);

    // 3. CABEÇALHO DENTRO DO CARTÃO
    tft.setTextColor(PRIMARY_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("ESP32 STREAM DECK", SCREEN_WIDTH / 2, cardY + 15);

    // Linha decorativa abaixo do título
    tft.drawFastHLine(cardX + 20, cardY + 28, cardWidth - 40, SECONDARY_COLOR);

    // 4. INFORMAÇÕES PRINCIPAIS - COLUNA ESQUERDA
    int infoX = cardX + 15;
    int infoY = cardY + 40;
    int lineHeight = 16;

    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);

    // Modelo/Placa
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Modelo:", infoX, infoY);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("ESP32-S3", infoX + 50, infoY);

    // Firmware
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Firmware:", infoX, infoY + lineHeight);
    tft.setTextColor(ACCENT_COLOR);
    tft.drawString(FIRMWARE_VERSION, infoX + 50, infoY + lineHeight);

    // CPU
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("CPU:", infoX, infoY + lineHeight * 2);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Xtensa LX7", infoX + 50, infoY + lineHeight * 2);

    // 5. INFORMAÇÕES PRINCIPAIS - COLUNA DIREITA
    int infoX2 = SCREEN_WIDTH / 2;

    // Memória Flash
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Flash:", infoX2, infoY);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("8MB", infoX2 + 35, infoY);

    // RAM
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("RAM:", infoX2, infoY + lineHeight);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("512KB", infoX2 + 35, infoY + lineHeight);

    // Display
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString("Display:", infoX2, infoY + lineHeight * 2);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("1.14\" IPS", infoX2 + 50, infoY + lineHeight * 2);

    // 6. SEÇÃO DE DESENVOLVEDOR
    tft.drawFastHLine(cardX + 20, infoY + lineHeight * 3 + 10, cardWidth - 40, PANEL_COLOR);

    tft.setTextColor(PRIMARY_COLOR);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("DESENVOLVEDOR", SCREEN_WIDTH / 2, infoY + lineHeight * 3 + 20);

    tft.setTextColor(TEXT_COLOR);
    tft.drawString(DEVELOPER, SCREEN_WIDTH / 2, infoY + lineHeight * 3 + 35);

    // 7. CONTATO/GITHUB
    tft.setTextColor(SECONDARY_COLOR);
    tft.setTextSize(1);

    // Quebra o GitHub em duas linhas se necessário
    String githubLine1 = "github.com/";
    String githubLine2 = "KanekiZLF";

    tft.drawString(githubLine1, SCREEN_WIDTH / 2, infoY + lineHeight * 3 + 50);
    tft.drawString(githubLine2, SCREEN_WIDTH / 2, infoY + lineHeight * 3 + 62);

    // 8. INSTRUÇÃO NO RODAPÉ DO CARTÃO
    tft.drawFastHLine(cardX + 10, cardY + cardHeight - 20, cardWidth - 20, SECONDARY_COLOR);

    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(1);
    tft.drawString("Pressione ENCODER para voltar", SCREEN_WIDTH / 2, cardY + cardHeight - 10);
}

// =========================================================================
// === CONTROLE DO ENCODER =================================================
// =========================================================================

void handleEncoder()
{
    handleEncoderButton();

    if (currentState == STATE_SETTINGS_MENU ||
        currentState == STATE_BRIGHTNESS_CONFIG ||
        currentState == STATE_LED_EFFECTS ||
        currentState == STATE_WIFI_CONFIG_MENU)
    {
        handleEncoderRotation();
    }
}

void handleEncoderButton()
{
    int btnState = digitalRead(ENCODER_BTN_PIN);

    if (btnState == LOW && encoderBtnLastState == HIGH)
    {
        unsigned long now = millis();

        if (now - lastEncoderBtnPress > ENCODER_DEBOUNCE_DELAY)
        {
            // Salva estado anterior
            SystemState previousState = currentState;

            // NÃO altera LEDs quando entra no menu
            // Apenas processa a navegação

            switch (currentState)
            {
            case STATE_MAIN:
                currentState = STATE_SETTINGS_MENU;
                menuSelection = 0;
                menuScrollOffset = 0;
                resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                drawSettingsMenu();
                Serial.println("Navegacao: Tela Principal -> Menu Config");
                break;

            case STATE_SETTINGS_MENU:
                switch (menuSelection)
                {
                case 0: // Wi-Fi
                    currentState = STATE_WIFI_CONFIG_MENU;
                    wifiMenuSelection = 0;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawWifiConfigMenu();
                    Serial.println("Navegacao: Menu -> Config Wi-Fi");
                    break;
                case 1: // Brilho
                    currentState = STATE_BRIGHTNESS_CONFIG;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawBrightnessConfigScreen();
                    Serial.println("Navegacao: Menu -> Brilho LEDs");
                    break;
                case 2: // Efeitos
                    currentState = STATE_LED_EFFECTS;
                    menuSelection = 0;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawLedEffectsScreen();
                    Serial.println("Navegacao: Menu -> Efeitos LEDs");
                    break;
                case 3: // Bateria
                    currentState = STATE_BATTERY_INFO;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawBatteryInfoScreen();
                    Serial.println("Navegacao: Menu -> Info Bateria");
                    break;
                case 4: // Avancado
                    currentState = STATE_ADVANCED_SETTINGS;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawAdvancedSettings();
                    Serial.println("Navegacao: Menu -> Avancado");
                    break;
                case 5: // Sobre Dispositivo
                    currentState = STATE_ABOUT_DEVICE;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawAboutDeviceScreen();
                    Serial.println("Navegacao: Menu -> Sobre Dispositivo");
                    break;
                case 6: // Teste Sistema
                    Serial.println("Teste de Sistema selecionado");
                    // Futura implementacao
                    break;
                case 7: // Voltar
                    currentState = STATE_MAIN;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawMainScreen();
                    Serial.println("Navegacao: Menu -> Tela Principal");
                    break;
                }
                break;

            case STATE_WIFI_CONFIG_MENU:
                switch (wifiMenuSelection)
                {
                case 0: // Limpar credenciais
                    clearWiFiCredentials();
                    currentState = STATE_SETTINGS_MENU;
                    menuSelection = 0;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawSettingsMenu();
                    Serial.println("Credenciais Wi-Fi limpas");
                    break;
                case 1: // Configurar nova rede
                    currentState = STATE_WIFI_CONFIG_PORTAL;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    resetWiFiCredentials();
                    break;
                case 2: // Voltar
                    currentState = STATE_SETTINGS_MENU;
                    menuSelection = 0;
                    resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                    drawSettingsMenu();
                    break;
                }
                break;

            case STATE_BRIGHTNESS_CONFIG:
            case STATE_BATTERY_INFO:
            case STATE_ADVANCED_SETTINGS:
            case STATE_ABOUT_DEVICE:
                currentState = STATE_SETTINGS_MENU;
                menuSelection = 0;
                menuScrollOffset = 0;
                resetRenderStates(); // <-- RESETA ESTADOS DE RENDER
                drawSettingsMenu();
                break;

            case STATE_LED_EFFECTS:
                // Se selecionou "Voltar", sempre volta
                if (menuSelection == 6)
                { // "Voltar" é o item 6
                    currentState = STATE_SETTINGS_MENU;
                    // NÃO desativa o efeito aqui! Apenas muda de tela
                    menuSelection = 2; // Volta para o item "Efeitos LEDs"
                    resetRenderStates();
                    drawSettingsMenu();
                    break;
                }

                // Se selecionou "Desligar"
                if (menuSelection == 5)
                { // "Desligar" é o item 5
                    effectActive = false;
                    manualControl = false;
                    clearAllLEDs();
                    saveEffectToPrefs("NONE"); // Salva que não tem efeito
                    break;
                }

                // Para os efeitos: ativa o efeito selecionado
                effectActive = true;
                manualControl = true;

                switch (menuSelection)
                {
                case 0:
                    currentEffect = "RAINBOW";
                    saveEffectToPrefs("RAINBOW");
                    break;
                case 1:
                    currentEffect = "BLINK";
                    saveEffectToPrefs("BLINK");
                    break;
                case 2:
                    currentEffect = "WAVE_BLUE";
                    saveEffectToPrefs("WAVE_BLUE");
                    break;
                case 3:
                    currentEffect = "FIRE";
                    saveEffectToPrefs("FIRE");
                    break;
                case 4:
                    currentEffect = "TWINKLE";
                    saveEffectToPrefs("TWINKLE");
                    break;
                }

                // ATUALIZA O EFEITO IMEDIATAMENTE
                effectTimer = millis();
                updateEffect();

                Serial.println("Efeito ativado e salvo: " + currentEffect);
                break;
            }

            lastEncoderBtnPress = now;
        }
    }

    encoderBtnLastState = btnState;
}
void handleEncoderRotation()
{
    int currentStateEncoder = digitalRead(ENCODER_CLK_PIN);

    if (currentStateEncoder != lastEncoderState)
    {
        int dtState = digitalRead(ENCODER_DT_PIN);

        if (currentState == STATE_SETTINGS_MENU)
        {
            if (dtState != currentStateEncoder)
            {
                menuSelection = (menuSelection + 1) % MENU_ITEMS_COUNT;
            }
            else
            {
                menuSelection = (menuSelection - 1 + MENU_ITEMS_COUNT) % MENU_ITEMS_COUNT;
            }
            drawSettingsMenu();
        }
        else if (currentState == STATE_WIFI_CONFIG_MENU)
        {
            if (dtState != currentStateEncoder)
            {
                wifiMenuSelection = (wifiMenuSelection + 1) % WIFI_MENU_ITEMS;
            }
            else
            {
                wifiMenuSelection = (wifiMenuSelection - 1 + WIFI_MENU_ITEMS) % WIFI_MENU_ITEMS;
            }
            drawWifiConfigMenu();
        }
        else if (currentState == STATE_BRIGHTNESS_CONFIG)
        {
            int oldBrightness = LED_BRIGHTNESS;

            if (dtState != currentStateEncoder)
            {
                LED_BRIGHTNESS = min(255, LED_BRIGHTNESS + 5);
            }
            else
            {
                LED_BRIGHTNESS = max(5, LED_BRIGHTNESS - 5);
            }

            if (LED_BRIGHTNESS != oldBrightness)
            {
                // Aplica o novo brilho
                FastLED.setBrightness(LED_BRIGHTNESS);
                FastLED.show();

                // Atualiza a tela
                drawBrightnessConfigScreen();

                // Salva nas preferências
                saveBrightnessToPrefs();

                Serial.println("Brilho ajustado: " + String(LED_BRIGHTNESS));
            }
        }
        else if (currentState == STATE_LED_EFFECTS)
        {
            if (dtState != currentStateEncoder)
            {
                menuSelection = (menuSelection + 1) % 7;
            }
            else
            {
                menuSelection = (menuSelection - 1 + 7) % 7;
            }
            drawLedEffectsScreen();
        }
    }

    lastEncoderState = currentStateEncoder;
}

void resetRenderStates()
{
    lastMenuSelection = -1;
    lastMenuScrollOffset = -1;
    lastWifiMenuSelection = -1;
}

// =========================================================================
// === VERIFICAÇÃO DE CONEXÃO WI-FI =======================================
// =========================================================================

// =========================================================================
// === VERIFICAÇÃO DE CONEXÃO WI-FI - ATUALIZADA ==========================
// =========================================================================

void checkWiFiConnection()
{
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);

    // Se desconectou do Wi-Fi
    if (lastWiFiConnected && !wifiConnected)
    {
        Serial.println("⚠️ Wi-Fi desconectado!");

        // Mostra feedback visual
        if (activeProtocol == WIFI)
        {
            activeProtocol = NONE;
            showConnectionFeedback(NONE); // Mostra feedback vermelho
            Serial.println("Protocolo alterado: Wi-Fi -> Nenhum");
        }

        if (client.connected())
        {
            client.stop();
            Serial.println("Cliente Wi-Fi desconectado");
        }
    }
    // Se reconectou ao Wi-Fi
    else if (!lastWiFiConnected && wifiConnected)
    {
        Serial.println("✅ Wi-Fi reconectado!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP().toString());

        // Mostra feedback visual
        if (activeProtocol == NONE)
        {
            activeProtocol = WIFI;
            showConnectionFeedback(WIFI); // Mostra feedback verde
        }

        // Reinicia servidores
        serverTCP.begin();
        Udp.begin(UDP_SEARCH_PORT);
    }

    lastWiFiConnected = wifiConnected;
}

// =========================================================================
// === CONTROLE DE BOTOES ==================================================
// =========================================================================

int readButtons()
{
    digitalWrite(latchPin, LOW);
    delayMicroseconds(5);
    digitalWrite(latchPin, HIGH);

    int data = 0;
    for (int i = 0; i < numBits; i++)
    {
        if (digitalRead(dataPin))
        {
            data |= (1 << i);
        }
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(clockPin, LOW);
    }
    return data;
}

int mapButton(int bit)
{
    switch (bit)
    {
    case 0:
        return 9;
    case 1:
        return 10;
    case 2:
        return 11;
    case 3:
        return 12;
    case 4:
        return 16;
    case 5:
        return 15;
    case 6:
        return 14;
    case 7:
        return 13;
    case 8:
        return 8;
    case 9:
        return 7;
    case 10:
        return 6;
    case 11:
        return 5;
    case 12:
        return 1;
    case 13:
        return 2;
    case 14:
        return 3;
    case 15:
        return 4;
    default:
        return 0;
    }
}

void updateLedFeedback()
{
    if (ledFeedbackActive && millis() - ledFeedbackTimer > 150)
    {
        ledFeedbackActive = false;
        feedbackLedIndex = -1;
    }
}

void checkButtons()
{
    int currentButtonStates = readButtons();

    if (currentState == STATE_MAIN)
    {
        for (int i = 0; i < numBits; i++)
        {
            if (bitRead(currentButtonStates, i) && !bitRead(lastButtonStates, i))
            {
                int buttonNumber = mapButton(i);
                handleButtonPress(buttonNumber);
            }
        }
    }

    lastButtonStates = currentButtonStates;
}

void handleButtonPress(int buttonNumber)
{
    // Feedback visual apenas no LED pressionado
    if (buttonNumber >= 1 && buttonNumber <= NUM_LEDS)
    {
        ledFeedbackActive = true;
        feedbackLedIndex = buttonNumber - 1;
        ledFeedbackTimer = millis();

        // Salva cor atual do LED
        CRGB originalColor = leds[feedbackLedIndex];

        // Acende em branco
        leds[feedbackLedIndex] = CRGB::White;
        FastLED.show();
        delay(100);

        // Restaura cor original
        leds[feedbackLedIndex] = originalColor;
        FastLED.show();
    }

    // Envia comando
    String command = "BTN:" + String(buttonNumber);

    if (activeProtocol == USB)
    {
        Serial.println(command);
    }
    else if (activeProtocol == WIFI && client.connected())
    {
        client.println(command);
    }

    Serial.println("Botao " + String(buttonNumber) + " pressionado");
}

// =========================================================================
// === CONTROLE DOS LEDs ===================================================
// =========================================================================

void clearAllLEDs()
{
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setStatusLEDs()
{
    if (wifiConfigMode)
    {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;

        if (millis() - lastBlink > 500)
        {
            blinkState = !blinkState;
            fill_solid(leds, NUM_LEDS, blinkState ? CRGB::Blue : CRGB::Black);
            FastLED.show();
            lastBlink = millis();
        }
        return;
    }

    // Se tem efeito ativo, mantém o efeito (não mostra status)
    if (effectActive)
    {
        return;
    }

    if (activeProtocol == USB)
    {
        fill_solid(leds, NUM_LEDS, CRGB::Blue);
        FastLED.show();
    }
    else if (activeProtocol == WIFI)
    {
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
    }
    else
    {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;

        if (millis() - lastBlink > 1000)
        {
            blinkState = !blinkState;
            fill_solid(leds, NUM_LEDS, blinkState ? CRGB::Red : CRGB::Black);
            FastLED.show();
            lastBlink = millis();
        }
    }
}

// =========================================================================
// === CONTROLE DOS LEDs - ATUALIZADA =====================================
// =========================================================================

void updateLEDs()
{
    updateLedFeedback();

    // 1. Primeiro verifica se está em modo feedback
    if (isInFeedbackMode)
    {
        if (millis() - feedbackStartTime > feedbackDuration)
        {
            if (shouldRestoreEffect && savedEffect != "NONE")
            {
                restoreSavedEffect();
            }
            else
            {
                isInFeedbackMode = false;
            }
        }
        return;
    }

    // 2. Se tem efeito ativo, atualiza
    if (effectActive)
    {
        updateEffect();
        return;
    }

    // 3. Não faz nada durante controle manual ou config portal
    if (manualControl || wifiConfigMode || ledFeedbackActive)
    {
        return;
    }

    // 4. Se não tem efeito ativo, aplica máscara se houver LEDs fixos
    bool hasFixedLeds = false;
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (ledMask[i])
        {
            hasFixedLeds = true;
            break;
        }
    }

    if (hasFixedLeds)
    {
        // Aplica apenas as cores fixas
        applyLedMask();
        FastLED.show();
    }
    else
    {
        // Nenhum LED fixo, desliga todos
        clearAllLEDs();
    }
}

// =========================================================================
// === FUNÇÕES DE PROCESSAMENTO DE COMANDOS LED - ATUALIZADA ==============
// =========================================================================

void processIndividualLedCommand(const String &command)
{
    manualControl = true;

    int firstColon = command.indexOf(':');
    int secondColon = command.indexOf(':', firstColon + 1);

    if (secondColon != -1)
    {
        int ledIndex = command.substring(firstColon + 1, secondColon).toInt();
        String colorStr = command.substring(secondColon + 1);

        // Verifica se é comando para remover cor fixa
        if (colorStr == "OFF" || colorStr == "RESET")
        {
            if (ledIndex >= 0 && ledIndex < NUM_LEDS)
            {
                ledMask[ledIndex] = false; // Apenas desativa na memória
                Serial.println("LED " + String(ledIndex) + " liberado da cor fixa");
                return;
            }
        }

        if (colorStr.startsWith("#"))
        {
            colorStr = colorStr.substring(1);
        }

        long color = strtol(colorStr.c_str(), NULL, 16);
        CRGB ledColor = CRGB(
            (color >> 16) & 0xFF,
            (color >> 8) & 0xFF,
            color & 0xFF);

        if (ledIndex >= 0 && ledIndex < NUM_LEDS)
        {
            // Define cor fixa apenas em memória
            ledMask[ledIndex] = true;
            ledFixedColors[ledIndex] = ledColor;

            // Aplica imediatamente
            leds[ledIndex] = ledColor;
            FastLED.show();

            Serial.print("LED ");
            Serial.print(ledIndex);
            Serial.print(" com cor fixa: #");
            Serial.println(colorStr);
        }
    }
}

void processAllLedCommand(const String &command)
{
    String subCmd = command.substring(8);

    if (subCmd == "ON")
    {
        // Liga todos os LEDs em branco
        fill_solid(leds, NUM_LEDS, CRGB::White);
        FastLED.show();
        effectActive = false;
        manualControl = true;

        // Limpa máscara (apenas em memória)
        for (int i = 0; i < NUM_LEDS; i++)
        {
            ledMask[i] = false;
        }

        Serial.println("Todos LEDs LIGADOS (branco)");
    }
    else if (subCmd == "OFF")
    {
        // Desliga todos os LEDs
        clearAllLEDs();
        effectActive = false;
        manualControl = false;

        // Limpa máscara (apenas em memória)
        for (int i = 0; i < NUM_LEDS; i++)
        {
            ledMask[i] = false;
        }

        Serial.println("Todos LEDs DESLIGADOS");
    }
    else if (subCmd == "CLEAR_MASK")
    {
        // Limpa máscara de LEDs fixos
        for (int i = 0; i < NUM_LEDS; i++)
        {
            ledMask[i] = false;
        }
        Serial.println("Máscara de LEDs limpa");
    }
    else if (subCmd == "SHOW_MASK")
    {
        // Mostra status atual da máscara (apenas em memória)
        Serial.println("\n═══════════════════════════════════════");
        Serial.println("    STATUS DA MÁSCARA DE LEDs (MEMÓRIA)");
        Serial.println("═══════════════════════════════════════");
        for (int i = 0; i < NUM_LEDS; i++)
        {
            if (ledMask[i])
            {
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
    else
    {
        // Ativa um efeito
        effectActive = true;
        currentEffect = subCmd;
        effectTimer = millis();
        Serial.println("Efeito " + subCmd + " ativado via comando");
    }
}

void processLedCommand(const String &command)
{
    if (command.startsWith("LED:"))
    {
        processIndividualLedCommand(command);
    }
    else if (command.startsWith("ALL_LED:"))
    {
        processAllLedCommand(command);
    }
    else
    {
        Serial.print("Comando LED invalido: ");
        Serial.println(command);
    }
}

void updateEffect()
{
    if (!effectActive)
        return;
    if (millis() - effectTimer < 50)
        return;

    // Salva as cores dos LEDs mascarados antes de atualizar
    CRGB savedMaskedColors[NUM_LEDS];
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (ledMask[i])
        {
            savedMaskedColors[i] = leds[i];
        }
    }

    if (currentEffect == "RAINBOW")
    {
        static uint8_t hue = 0;
        fill_rainbow(leds, NUM_LEDS, hue, 255 / NUM_LEDS);
        hue += 5;
    }
    else if (currentEffect == "BLINK")
    {
        static bool blinkState = false;
        blinkState = !blinkState;
        fill_solid(leds, NUM_LEDS, blinkState ? CRGB::White : CRGB::Black);
    }
    else if (currentEffect == "WAVE_BLUE")
    {
        static uint8_t offset = 0;
        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint8_t brightness = sin8(i * 32 + offset);
            leds[i] = CRGB(0, 0, brightness);
        }
        offset += 8;
    }
    else if (currentEffect == "FIRE")
    {
        for (int i = 0; i < NUM_LEDS; i++)
        {
            int heat = random8(50, 255);
            leds[i] = HeatColor(heat);
        }
    }
    else if (currentEffect == "TWINKLE")
    {
        static uint8_t sparkle[NUM_LEDS];
        for (int i = 0; i < NUM_LEDS; i++)
        {
            if (sparkle[i] == 0 && random8() < 10)
            {
                sparkle[i] = 255;
            }
            if (sparkle[i] > 0)
            {
                sparkle[i] = qsub8(sparkle[i], 15);
                leds[i] = CRGB(sparkle[i], sparkle[i], sparkle[i]);
            }
            else
            {
                leds[i] = CRGB::Black;
            }
        }
    }

    // RESTAURA as cores dos LEDs mascarados após atualizar o efeito
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (ledMask[i])
        {
            leds[i] = savedMaskedColors[i];
        }
    }

    FastLED.show();
    effectTimer = millis();
}

// =========================================================================
// === GERENCIAMENTO DA BATERIA ============================================
// =========================================================================

void updateBatteryLogic()
{
    isUsbConnected = (activeProtocol == USB);

    if (isUsbConnected)
    {
        digitalWrite(PIN_TP4056_CE, HIGH);
        isCharging = false;
    }
    else
    {
        digitalWrite(PIN_TP4056_CE, LOW);
        isCharging = true;
    }

    int rawADC = 0;
    for (int i = 0; i < 10; i++)
    {
        rawADC += analogRead(PIN_BATT_ADC);
        delay(1);
    }
    rawADC /= 10;

    batteryVoltage = (rawADC / 4095.0) * 3.3 * 2.0;

    if (batteryVoltage >= 4.2)
    {
        batteryPercentage = 100;
    }
    else if (batteryVoltage <= 3.0)
    {
        batteryPercentage = 0;
    }
    else
    {
        batteryPercentage = map(batteryVoltage * 100, 300, 420, 0, 100);
    }

    batteryPercentage = constrain(batteryPercentage, 0, 100);
}

void updateBatteryDisplay()
{
    int battX = SCREEN_WIDTH - 45;
    int battY = 4;

    // Limpa area da bateria antes de desenhar
    tft.fillRect(battX - 30, battY, 50, 15, SECONDARY_COLOR);

    // Icone da bateria
    tft.drawRect(battX, battY, 30, 12, TEXT_COLOR);
    tft.fillRect(battX + 30, battY + 3, 2, 6, TEXT_COLOR);

    // Barra de carga
    int fillWidth = map(batteryPercentage, 0, 100, 0, 28);

    uint16_t fillColor;
    if (batteryPercentage > 60)
        fillColor = SUCCESS_COLOR;
    else if (batteryPercentage > 20)
        fillColor = WARNING_COLOR;
    else
        fillColor = ERROR_COLOR;

    tft.fillRect(battX + 1, battY + 1, fillWidth, 10, fillColor);

    // Porcentagem - sempre atualiza, evita sobreposição
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(TR_DATUM);

    // Limpa area do texto antes de escrever
    tft.fillRect(battX - 30, battY, 25, 10, SECONDARY_COLOR);

    // Desenha porcentagem
    tft.drawString(String(batteryPercentage) + "%", battX - 3, battY + 3);

    lastBatteryPercentage = batteryPercentage;
}

// =========================================================================
// === GERENCIAMENTO WI-FI =================================================
// =========================================================================

void initWiFi()
{
    preferences.begin(PREFS_KEY, true);
    String ssid = preferences.getString("ssid", "");
    String pass = preferences.getString("pass", "");

    if (ssid.length() > 0)
    {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());

        Serial.print("Conectando ao Wi-Fi ");
        Serial.print(ssid);

        for (int i = 0; i < 20; i++)
        {
            if (WiFi.status() == WL_CONNECTED)
                break;
            Serial.print(".");
            delay(500);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println(" OK");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());

            serverTCP.begin();
            Udp.begin(UDP_SEARCH_PORT);
            lastWiFiConnected = true;
        }
        else
        {
            Serial.println(" FALHA");
            lastWiFiConnected = false;
        }
    }
}

void startConfigPortal()
{
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

void handleRoot()
{
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>ESP32 Deck</title><style>body{font-family:Arial;background:#222;color:#fff;text-align:center;}.container{max-width:300px;margin:50px auto;background:#333;padding:20px;border-radius:10px;}input{padding:10px;margin:5px;width:90%;border-radius:5px;border:1px solid #555;background:#444;color:#fff;}button{padding:10px 20px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer;}</style></head><body><div class='container'><h1>ESP32 Deck</h1><p>Configurar Wi-Fi</p><form method='get' action='/save'><input type='text' name='ssid' placeholder='Nome da rede' required><input type='password' name='pass' placeholder='Senha' required><button type='submit'>Salvar</button></form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleWiFiSave()
{
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    preferences.begin(PREFS_KEY, false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();

    String html = "<html><body style='font-family:Arial;text-align:center;'><h1>Configurado!</h1><p>Reiniciando...</p></body></html>";
    server.send(200, "text/html", html);

    delay(1000);
    server.stop();
    wifiConfigMode = false;

    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(1000);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    currentState = STATE_MAIN;
    drawMainScreen();

    Serial.println("Wi-Fi reconfigurado");
}

void clearWiFiCredentials()
{
    preferences.begin(PREFS_KEY, false);
    preferences.clear();
    preferences.end();

    WiFi.disconnect(true);
    delay(1000);

    Serial.println("Credenciais Wi-Fi limpas");

    // Atualiza status
    lastWiFiConnected = false;
}

void resetWiFiCredentials()
{
    drawWifiConfigPortal();
    delay(2000);
    startConfigPortal();
}

void checkUdpSearch()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    int packetSize = Udp.parsePacket();
    if (packetSize)
    {
        char incomingPacket[255];
        int len = Udp.read(incomingPacket, 255);
        if (len > 0)
        {
            incomingPacket[len] = 0;
            if (String(incomingPacket) == UDP_DISCOVER_MSG)
            {
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
// === COMUNICACAO SERIAL ==================================================
// =========================================================================

void checkSerialCommands()
{
    if (Serial.available())
    {
        String message = Serial.readStringUntil('\n');
        message.trim();

        if (message.startsWith("LED:") || message.startsWith("ALL_LED:"))
        {
            processLedCommand(message);
        }
        else if (message == "CONNECTED")
        {
            if (activeProtocol != USB)
            {
                activeProtocol = USB;
                showConnectionFeedback(USB); // Mostra feedback azul
                if (currentState == STATE_MAIN)
                    drawMainScreen();
                Serial.println("Conectado via USB");
            }
        }
        else if (message == "DISCONNECT")
        {
            if (activeProtocol == USB)
            {
                activeProtocol = client.connected() ? WIFI : NONE;
                showConnectionFeedback(activeProtocol); // Mostra feedback apropriado
                if (currentState == STATE_MAIN)
                    drawMainScreen();
                Serial.println("Desconectado do USB");
            }
        }
        else if (message == "STATUS")
        {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("       ESP32 DECK - STATUS DO SISTEMA");
            Serial.println("═══════════════════════════════════════");
            Serial.println("FIRMWARE: " + String(FIRMWARE_VERSION));
            Serial.println("DESENVOLVEDOR: " + String(DEVELOPER));
            Serial.println("GITHUB: " + String(GITHUB));
            Serial.println("═══════════════════════════════════════");
            Serial.println("🎮 ESTADO DO SISTEMA:");
            Serial.println("   • Estado atual: " + String(currentState));
            Serial.println("   • Protocolo: " + String(activeProtocol == USB ? "USB ⚡" : activeProtocol == WIFI ? "Wi-Fi 📶"
                                                                                                                 : "Nenhum ⭕"));
            Serial.println("═══════════════════════════════════════");
            Serial.println("🔋 BATERIA:");
            Serial.println("   • Porcentagem: " + String(batteryPercentage) + "%");
            Serial.println("   • Tensão: " + String(batteryVoltage, 1) + "V");
            Serial.println("   • USB Conectado: " + String(isUsbConnected ? "Sim ✅" : "Não ❌"));
            Serial.println("   • Carregando: " + String(isCharging ? "Sim 🔌" : "Não ⚡"));
            Serial.println("═══════════════════════════════════════");
            Serial.println("🌐 REDE WI-FI:");
            Serial.println("   • Status: " + String(WiFi.status() == WL_CONNECTED ? "Conectado ✅" : "Desconectado ❌"));
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("   • SSID: " + WiFi.SSID());
                Serial.println("   • IP: " + WiFi.localIP().toString());
            }
            Serial.println("═══════════════════════════════════════");
            Serial.println("💡 CONFIGURAÇÕES LED:");
            Serial.println("   • Brilho: " + String(LED_BRIGHTNESS) + "/255");
            Serial.println("   • Efeito salvo: " + savedEffect);
            Serial.println("   • Efeito ativo: " + String(effectActive ? currentEffect : "Nenhum"));
            Serial.println("   • Modo manual: " + String(manualControl ? "Sim" : "Não"));
            Serial.println("   • Feedback ativo: " + String(isInFeedbackMode ? "Sim" : "Não"));
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "LED_HELP")
        {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         COMANDOS LED DISPONÍVEIS");
            Serial.println("═══════════════════════════════════════");
            Serial.println("🎯 CONTROLE INDIVIDUAL:");
            Serial.println("   LED:0:FF0000      // LED 0 vermelho");
            Serial.println("   LED:1:00FF00      // LED 1 verde");
            Serial.println("   LED:2:0000FF      // LED 2 azul");
            Serial.println("   LED:3:FFFF00      // LED 3 amarelo");
            Serial.println("   LED:4:FF00FF      // LED 4 rosa");
            Serial.println("   LED:5:00FFFF      // LED 5 ciano");
            Serial.println("   LED:6:FFFFFF      // LED 6 branco");
            Serial.println("   LED:7:FF8000      // LED 7 laranja");
            Serial.println("");
            Serial.println("🎯 CONTROLE GERAL:");
            Serial.println("   ALL_LED:ON        // Liga todos (branco)");
            Serial.println("   ALL_LED:OFF       // Desliga todos");
            Serial.println("");
            Serial.println("🎯 EFEITOS PRÉ-DEFINIDOS:");
            Serial.println("   ALL_LED:RAINBOW   // Efeito arco-íris");
            Serial.println("   ALL_LED:BLINK     // Efeito piscante");
            Serial.println("   ALL_LED:WAVE_BLUE // Onda azul");
            Serial.println("   ALL_LED:FIRE      // Efeito fogo");
            Serial.println("   ALL_LED:TWINKLE   // Efeito estrelas");
            Serial.println("   ALL_LED:CHASE     // Efeito perseguição");
            Serial.println("   ALL_LED:SPECTRUM  // Espectro de cores");
            Serial.println("");
            Serial.println("🎯 COMANDOS DO SISTEMA:");
            Serial.println("   STATUS            // Status completo");
            Serial.println("   EFFECT_STATUS     // Status dos efeitos");
            Serial.println("   SAVE_EFFECT       // Salva efeito atual");
            Serial.println("   CLEAR_EFFECT      // Limpa efeito salvo");
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "EFFECT_STATUS")
        {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         STATUS DE EFEITOS LED");
            Serial.println("═══════════════════════════════════════");
            Serial.println("💾 EFEITO SALVO NAS PREFERÊNCIAS:");
            Serial.println("   • Nome: " + savedEffect);
            Serial.println("");
            Serial.println("🎮 ESTADO ATUAL:");
            Serial.println("   • Efeito ativo: " + String(effectActive ? currentEffect : "Nenhum"));
            Serial.println("   • Modo manual: " + String(manualControl ? "Sim ✅" : "Não ❌"));
            Serial.println("   • Controle automático: " + String(!manualControl ? "Sim ✅" : "Não ❌"));
            Serial.println("");
            Serial.println("⚙️ SISTEMA:");
            Serial.println("   • Em feedback: " + String(isInFeedbackMode ? "Sim ⚡" : "Não"));
            Serial.println("   • Deve restaurar: " + String(shouldRestoreEffect ? "Sim 🔄" : "Não"));
            Serial.println("");
            Serial.println("🎯 AÇÕES DISPONÍVEIS:");
            Serial.println("   • Envie 'SAVE_EFFECT' para salvar atual");
            Serial.println("   • Envie 'CLEAR_EFFECT' para limpar");
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "SAVE_EFFECT")
        {
            if (effectActive)
            {
                saveEffectToPrefs(currentEffect);
                Serial.println("✅ Efeito atual salvo: " + currentEffect);
                Serial.println("   Será restaurado na próxima inicialização");
            }
            else
            {
                saveEffectToPrefs("NONE");
                Serial.println("✅ Configuração 'sem efeito' salva");
                Serial.println("   LEDs permanecerão desligados na inicialização");
            }
        }
        else if (message == "CLEAR_EFFECT")
        {
            clearEffectPrefs();
            Serial.println("✅ Efeito salvo foi removido");
            Serial.println("   Próxima inicialização usará padrão do sistema");
        }
        else if (message == "RESET_LEDS")
        {
            effectActive = false;
            manualControl = false;
            clearAllLEDs();
            saveEffectToPrefs("NONE");
            Serial.println("✅ LEDs resetados para estado padrão");
        }
        else if (message == "TEST_LEDS")
        {
            Serial.println("🔧 Teste sequencial de LEDs...");

            // Teste de todas as cores básicas
            CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue,
                             CRGB::Yellow, CRGB::Cyan, CRGB::Magenta,
                             CRGB::White};

            for (int i = 0; i < 7; i++)
            {
                fill_solid(leds, NUM_LEDS, colors[i]);
                FastLED.show();
                Serial.println("   Cor " + String(i + 1) + " de 7");
                delay(300);
            }

            // Teste individual
            Serial.println("   Teste individual de LEDs...");
            clearAllLEDs();
            for (int i = 0; i < NUM_LEDS; i++)
            {
                leds[i] = CRGB::White;
                FastLED.show();
                delay(50);
                leds[i] = CRGB::Black;
            }

            // Restaura estado anterior
            if (effectActive)
            {
                effectActive = true;
                effectTimer = millis();
                Serial.println("   Restaurando efeito anterior...");
            }
            else if (savedEffect != "NONE")
            {
                effectActive = true;
                currentEffect = savedEffect;
                effectTimer = millis();
                Serial.println("   Restaurando efeito salvo: " + savedEffect);
            }
            else
            {
                clearAllLEDs();
                Serial.println("   LEDs desligados");
            }

            Serial.println("✅ Teste de LEDs completo!");
        }
        else if (message == "SYSTEM_INFO")
        {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         INFORMAÇÕES DO SISTEMA");
            Serial.println("═══════════════════════════════════════");
            Serial.println("📊 ESPECIFICAÇÕES:");
            Serial.println("   • CPU: Xtensa LX7 Dual-Core");
            Serial.println("   • Frequência: 240 MHz");
            Serial.println("   • RAM: 512KB SRAM");
            Serial.println("   • Flash: 8MB");
            Serial.println("");
            Serial.println("🎮 PERIFÉRICOS:");
            Serial.println("   • Botões: 16 (via shift register)");
            Serial.println("   • LEDs: 16 RGB WS2812B");
            Serial.println("   • Display: 1.14\" IPS (240x135)");
            Serial.println("   • Encoder: EC11 (rotação + botão)");
            Serial.println("   • Bateria: Li-Po com TP4056");
            Serial.println("");
            Serial.println("🌐 CONECTIVIDADE:");
            Serial.println("   • Wi-Fi: 802.11 b/g/n");
            Serial.println("   • Bluetooth: BLE");
            Serial.println("   • USB: Serial/UART");
            Serial.println("   • Protocolos: TCP, UDP, HTTP");
            Serial.println("═══════════════════════════════════════");
        }
        else if (message == "HELP" || message == "?")
        {
            Serial.println("\n═══════════════════════════════════════");
            Serial.println("         COMANDOS DO SISTEMA");
            Serial.println("═══════════════════════════════════════");
            Serial.println("🎮 CONTROLE BÁSICO:");
            Serial.println("   CONNECTED         // Simula conexão USB");
            Serial.println("   DISCONNECT        // Simula desconexão");
            Serial.println("   STATUS            // Status completo");
            Serial.println("   SYSTEM_INFO       // Especificações");
            Serial.println("");
            Serial.println("💡 CONTROLE DE LEDs:");
            Serial.println("   LED_HELP          // Todos comandos LED");
            Serial.println("   EFFECT_STATUS     // Status de efeitos");
            Serial.println("   TEST_LEDS         // Teste sequencial");
            Serial.println("   RESET_LEDS        // Reset para padrão");
            Serial.println("");
            Serial.println("⚙️ CONFIGURAÇÃO:");
            Serial.println("   SAVE_EFFECT       // Salva efeito atual");
            Serial.println("   CLEAR_EFFECT      // Limpa efeito salvo");
            Serial.println("");
            Serial.println("❓ AJUDA:");
            Serial.println("   HELP ou ?         // Esta mensagem");
            Serial.println("═══════════════════════════════════════");
        }
        else
        {
            Serial.println("❌ Comando não reconhecido: " + message);
            Serial.println("   Digite 'HELP' para ver comandos disponíveis");
        }
    }
}

// =========================================================================
// === FUNÇÕES PARA PERSISTÊNCIA DE EFEITOS LED ===========================
// =========================================================================

void loadEffectFromPrefs()
{
    preferences.begin(PREFS_EFFECT_KEY, true); // Modo leitura

    if (preferences.isKey("effect"))
    {
        savedEffect = preferences.getString("effect", "NONE");
        Serial.println("Efeito carregado: " + savedEffect);
    }
    else
    {
        savedEffect = "NONE";
        Serial.println("Nenhum efeito salvo encontrado");
    }

    preferences.end();
}

void saveEffectToPrefs(String effectName)
{
    preferences.begin(PREFS_EFFECT_KEY, false); // Modo escrita
    preferences.putString("effect", effectName);
    preferences.end();

    savedEffect = effectName;
    Serial.println("Efeito salvo: " + effectName);
}

void clearEffectPrefs()
{
    preferences.begin(PREFS_EFFECT_KEY, false);
    preferences.clear();
    preferences.end();

    savedEffect = "NONE";
    Serial.println("Efeitos limpos das preferências");
}

// =========================================================================
// === FUNÇÃO DE FEEDBACK VISUAL ==========================================
// =========================================================================

void showConnectionFeedback(ConnectionProtocol newProtocol)
{
    isInFeedbackMode = true;
    feedbackStartTime = millis();
    shouldRestoreEffect = effectActive; // Salva se tinha efeito ativo

    // Pausa efeito atual
    if (effectActive)
    {
        effectActive = false;
    }

    // Mostra feedback baseado no protocolo
    if (newProtocol == USB)
    {
        // Feedback azul para USB
        fill_solid(leds, NUM_LEDS, CRGB::Blue);
        FastLED.show();
        Serial.println("✅ Feedback: Conectado via USB");
    }
    else if (newProtocol == WIFI)
    {
        // Feedback verde para Wi-Fi
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        Serial.println("✅ Feedback: Conectado via Wi-Fi");
    }
    else
    {
        // Feedback vermelho para desconexão
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        Serial.println("⚠️ Feedback: Desconectado");
    }
}

// =========================================================================
// === RESTAURAR EFEITO APÓS FEEDBACK =====================================
// =========================================================================

void restoreSavedEffect()
{

    isInFeedbackMode = false;
    shouldRestoreEffect = false;
    effectActive = false;
    manualControl = false;
    clearAllLEDs();

    Serial.println("🔄 Estado resetado após feedback");
}

// =========================================================================
// === MOSTRAR STATUS ATUAL DOS LEDs ======================================
// =========================================================================

/*void showCurrentStatusLEDs() {
    // NÃO faz nada se tem efeito ativo!
    if(effectActive) {
        return;  // <-- IMPORTANTE: Deixa o efeito rodando
    }

    if(wifiConfigMode) {
        // Modo config portal - LEDs azuis fixos (não piscam)
        fill_solid(leds, NUM_LEDS, CRGB::Blue);
        FastLED.show();
        return;
    }

    // Mostra cor baseada no protocolo ativo (sem piscar)
    if(activeProtocol == USB) {
        // Conectado via USB - azul fixo
        fill_solid(leds, NUM_LEDS, CRGB::Blue);
        FastLED.show();
    }
    else if(activeProtocol == WIFI) {
        // Conectado via Wi-Fi - verde fixo
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
    }
    else {
        // Desconectado - LEDs desligados (não piscam mais)
        clearAllLEDs();
    }
}*/

// =========================================================================
// === FUNÇÕES PARA MÁSCARA DE LEDs =======================================
// =========================================================================

void clearLedMask()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        ledMask[i] = false;
    }
    Serial.println("Máscara de LEDs limpa da memória");
}

void applyLedMask()
{
    // Aplica a máscara: LEDs com cor fixa mantêm sua cor
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (ledMask[i])
        {
            leds[i] = ledFixedColors[i];
        }
    }
}

// =========================================================================
// === SETUP PRINCIPAL - ATUALIZADO ========================================
// =========================================================================

void setup()
{
    Serial.begin(115200);

    initializeDisplay();
    initButtons();
    initLEDs();
    initEncoder();

    pinMode(PIN_TP4056_CE, OUTPUT);
    digitalWrite(PIN_TP4056_CE, LOW);

    // Carrega configurações
    loadBrightnessFromPrefs();
    loadEffectFromPrefs();

    // Inicialização com animação própria
    drawLoadingScreen();
    initWiFi();

    // APÓS inicialização, RESTAURA configurações
    if (savedEffect != "NONE" && savedEffect != "")
    {
        effectActive = true;
        currentEffect = savedEffect;
        manualControl = true;
        effectTimer = millis();

        Serial.println("✅ Efeito restaurado: " + savedEffect);
        Serial.println("✅ Máscara de LEDs carregada");
    }
    else
    {
        bool hasFixedLeds = false;
        for (int i = 0; i < NUM_LEDS; i++)
        {
            if (ledMask[i])
                hasFixedLeds = true;
        }

        if (hasFixedLeds)
        {
            effectActive = false;
            manualControl = true;
            applyLedMask();
            FastLED.show();
            Serial.println("✅ LEDs fixos restaurados (sem efeito)");
        }
        else
        {
            Serial.println("Nenhuma configuração salva para restaurar");
        }
    }

    // Tela principal
    currentState = STATE_MAIN;
    drawMainScreen();

    // Info inicial atualizada
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("       ESP32 DECK - " + String(FIRMWARE_VERSION));
    Serial.println("═══════════════════════════════════════");
    Serial.println("🎮 NOVO SISTEMA DE LEDs:");
    Serial.println("   • Efeitos + LEDs fixos simultâneos");
    Serial.println("   • Cores fixas são salvas e restauradas");
    Serial.println("   • Digite 'MASK_HELP' para comandos");
    Serial.println("═══════════════════════════════════════");
}

// =========================================================================
// === LOOP PRINCIPAL - MODIFICADO =========================================
// =========================================================================

void loop()
{
    // Atualiza bateria
    updateBatteryLogic();

    // Verifica conexão Wi-Fi
    checkWiFiConnection();

    // Encoder
    static unsigned long lastEncoderCheck = 0;
    if (millis() - lastEncoderCheck > 10)
    {
        handleEncoder();
        lastEncoderCheck = millis();
    }

    // Atualiza bateria no display periodicamente
    static unsigned long lastBatteryUpdate = 0;
    if (currentState == STATE_MAIN && millis() - lastBatteryUpdate > 2000)
    {
        updateBatteryDisplay();
        lastBatteryUpdate = millis();
    }

    // ATUALIZA LEDs SEMPRE (independente do estado)
    updateLEDs();

    // Processa estado atual
    switch (currentState)
    {
    case STATE_MAIN:
        checkButtons();
        checkSerialCommands();

        if (WiFi.status() == WL_CONNECTED)
        {
            checkUdpSearch();

            if (!client.connected())
            {
                WiFiClient newClient = serverTCP.available();
                if (newClient)
                {
                    client = newClient;
                    activeProtocol = WIFI;
                    drawMainScreen();
                    Serial.println("Cliente Wi-Fi conectado");
                }
            }

            // Processa comandos Wi-Fi
            if (client.connected())
            {
                while (client.available())
                {
                    String msg = client.readStringUntil('\n');
                    msg.trim();

                    if (msg.startsWith("LED:") || msg.startsWith("ALL_LED:"))
                    {
                        processLedCommand(msg);
                    }
                    else if (msg == "PING")
                    {
                        client.println("PONG");
                    }
                    else if (msg == "DISCONNECT")
                    {
                        client.stop();
                        if (activeProtocol == WIFI)
                        {
                            activeProtocol = NONE;
                            drawMainScreen();
                        }
                        Serial.println("Cliente Wi-Fi desconectado por comando");
                    }
                }
            }
        }
        break;

    case STATE_WIFI_CONFIG_PORTAL:
        if (wifiConfigMode)
        {
            dnsServer.processNextRequest();
            server.handleClient();
        }
        break;
    }

    delay(20);
}