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
const int TCP_PORT = 8000;
const char* FIRMWARE_VERSION = "v2.8 Professional";
const char* DEVELOPER = "Luiz F. R. Pimentel";
const char* GITHUB = "github.com/KanekiZLF";

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
int LED_BRIGHTNESS = 150;

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

// =========================================================================
// === CONFIGURACOES DO ENCODER EC11 =======================================
// =========================================================================
const int ENCODER_CLK_PIN = 25;
const int ENCODER_DT_PIN = 26;
const int ENCODER_BTN_PIN = 27;

// =========================================================================
// === VARIAVEIS GLOBAIS ===================================================
// =========================================================================

// Enum para rastrear o protocolo ativo
enum ConnectionProtocol {
    NONE,
    USB,
    WIFI
};
ConnectionProtocol activeProtocol = NONE;
bool lastWiFiConnected = false; // Para detectar mudanças no Wi-Fi

// Estados do sistema
enum SystemState {
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
const int MENU_ITEMS_COUNT = 8;  // Incluindo "Sobre Dispositivo"
const int VISIBLE_MENU_ITEMS = 6; // Itens visíveis na tela
int wifiMenuSelection = 0;
const int WIFI_MENU_ITEMS = 3;

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
#define SECONDARY_COLOR 0x4A69  // Azul medio
#define ACCENT_COLOR TFT_YELLOW
#define SUCCESS_COLOR TFT_GREEN
#define WARNING_COLOR TFT_ORANGE
#define ERROR_COLOR TFT_RED
#define TEXT_COLOR TFT_WHITE
#define HIGHLIGHT_COLOR 0xF81F  // Rosa
#define PANEL_COLOR 0x18E0      // Azul escuro
#define MENU_BG_COLOR 0x3186    // Cinza azulado

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
void processLedCommand(const String& command);
void processIndividualLedCommand(const String& command);
void processAllLedCommand(const String& command);
void updateEffect();

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

void initializeDisplay() {
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(BACKGROUND_COLOR);
    tft.setTextColor(TEXT_COLOR);
    Serial.println("Display inicializado");
}

void initButtons() {
    pinMode(latchPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    pinMode(dataPin, INPUT);
    digitalWrite(latchPin, HIGH);
    digitalWrite(clockPin, LOW);
    Serial.println("Botoes inicializados");
}

void initLEDs() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    clearAllLEDs();
    Serial.println("LEDs WS2812B inicializados");
}

void initEncoder() {
    pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
    pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
    lastEncoderState = digitalRead(ENCODER_CLK_PIN);
    Serial.println("Encoder EC11 inicializado");
}

// =========================================================================
// === SISTEMA DE TELAS OTIMIZADO =========================================
// =========================================================================

void drawLoadingScreen() {
    tft.fillScreen(BACKGROUND_COLOR);
    
    // Logo central com efeito
    for(int step = 0; step < 3; step++) {
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
        for(int led = 0; led < NUM_LEDS; led++) {
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
    
    for(int i = 0; i <= 100; i += 2) {
        int progressWidth = (barWidth * i) / 100;
        tft.fillRect(barX + 1, barY + 1, progressWidth, barHeight - 2, ACCENT_COLOR);
        
        delay(15);
    }
    
    manualControl = false;
    clearAllLEDs();
    
    delay(500);
}

void drawMainScreen() {
    tft.fillScreen(BACKGROUND_COLOR);
    
    // Barra superior compacta
    tft.fillRect(0, 0, SCREEN_WIDTH, 25, SECONDARY_COLOR);
    
    // Logo e status em fonte pequena
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    
    String statusText = "";
    if(activeProtocol == USB) {
        statusText = "USB";
    } else if(activeProtocol == WIFI) {
        statusText = "Wi-Fi";
    } else {
        statusText = "Desconectado";
    }
    
    tft.drawString("ESP-Deck " + statusText, 5, 8);
    
    // Bateria no canto direito (com limpeza de area)
    tft.fillRect(SCREEN_WIDTH - 50, 4, 50, 12, SECONDARY_COLOR); // Limpa area da bateria
    updateBatteryDisplay();
    
    // Linha divisoria
    tft.drawFastHLine(0, 27, SCREEN_WIDTH, PRIMARY_COLOR);
    
    // Conteudo principal
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    
    if(activeProtocol == USB) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("CONECTADO", SCREEN_WIDTH / 2, 55);
        tft.setTextSize(1);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Protocolo: USB Serial", SCREEN_WIDTH / 2, 75);
    } else if(activeProtocol == WIFI) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("CONECTADO", SCREEN_WIDTH / 2, 55);
        tft.setTextSize(1);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Protocolo: Wi-Fi", SCREEN_WIDTH / 2, 75);
        if(WiFi.status() == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            if(ip.length() > 15) ip = ip.substring(0, 12) + "...";
            tft.drawString("IP: " + ip, SCREEN_WIDTH / 2, 90);
        }
    } else {
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

void drawSettingsMenu() {
    tft.fillScreen(MENU_BG_COLOR);
    
    // Cabecalho
    tft.fillRect(0, 0, SCREEN_WIDTH, 22, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CONFIGURACOES", SCREEN_WIDTH / 2, 11);
    
    // Lista de itens do menu
    String menuItems[] = {
        "Configurar Wi-Fi",
        "Ajustar Brilho LEDs", 
        "Efeitos LEDs",
        "Informacoes Bateria",
        "Configuracoes Avancadas",
        "Sobre Dispositivo",
        "Teste de Sistema",
        "Voltar ao Menu Principal"
    };
    
    // Calcular scroll
    if(menuSelection < menuScrollOffset) {
        menuScrollOffset = menuSelection;
    } else if(menuSelection >= menuScrollOffset + VISIBLE_MENU_ITEMS) {
        menuScrollOffset = menuSelection - VISIBLE_MENU_ITEMS + 1;
    }
    
    // Desenhar itens visiveis
    for(int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
        int itemIndex = i + menuScrollOffset;
        if(itemIndex >= MENU_ITEMS_COUNT) break;
        
        int yPos = 35 + (i * 18);
        
        if(itemIndex == menuSelection) {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 14, HIGHLIGHT_COLOR);
            tft.setTextColor(BACKGROUND_COLOR);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 14, PANEL_COLOR);
            tft.setTextColor(TEXT_COLOR);
        }
        
        tft.setTextDatum(TL_DATUM);
        
        // Trunca texto longo
        String displayText = menuItems[itemIndex];
        if(displayText.length() > 22) {
            displayText = displayText.substring(0, 19) + "...";
        }
        
        tft.drawString(displayText, 10, yPos);
        
        if(itemIndex == menuSelection) {
            tft.setTextColor(BACKGROUND_COLOR);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
    
    // Barra de scroll se necessario
    if(MENU_ITEMS_COUNT > VISIBLE_MENU_ITEMS) {
        int scrollBarHeight = VISIBLE_MENU_ITEMS * 18 - 4;
        int scrollBarWidth = 4;
        int scrollBarX = SCREEN_WIDTH - 8;
        int scrollBarY = 35;
        
        tft.drawRect(scrollBarX, scrollBarY, scrollBarWidth, scrollBarHeight, TEXT_COLOR);
        
        // Posicao do indicador
        int indicatorHeight = (scrollBarHeight * VISIBLE_MENU_ITEMS) / MENU_ITEMS_COUNT;
        int indicatorY = scrollBarY + (scrollBarHeight * menuScrollOffset) / MENU_ITEMS_COUNT;
        
        tft.fillRect(scrollBarX, indicatorY, scrollBarWidth, indicatorHeight, ACCENT_COLOR);
    }
    
    // Contador de itens
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(menuSelection + 1) + "/" + String(MENU_ITEMS_COUNT), SCREEN_WIDTH - 5, 125);
    
    // Instrucoes
    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Gire: Navegar  |  Encoder: Selecionar", SCREEN_WIDTH / 2, 132);
}

void drawWifiConfigMenu() {
    tft.fillScreen(MENU_BG_COLOR);
    
    // Cabecalho
    tft.fillRect(0, 0, SCREEN_WIDTH, 22, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CONFIGURAR WI-FI", SCREEN_WIDTH / 2, 11);
    
    // Status atual em painel superior
    tft.fillRect(10, 30, SCREEN_WIDTH - 20, 35, PANEL_COLOR);
    tft.drawRect(10, 30, SCREEN_WIDTH - 20, 35, SECONDARY_COLOR);
    
    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Status:", 15, 38);
    
    if(WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("CONECTADO", 55, 38);
        tft.setTextColor(TEXT_COLOR);
        tft.drawString("Rede:", 15, 52);
        tft.drawString(WiFi.SSID().substring(0, 15), 55, 52);
    } else {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("DESCONECTADO", 55, 38);
    }
    
    // Opcoes do menu Wi-Fi - melhor organizadas
    String wifiOptions[] = {"Limpar Credenciais", "Configurar Nova Rede", "Voltar"};
    
    int optionHeight = 25;
    int startY = 75;
    
    for(int i = 0; i < WIFI_MENU_ITEMS; i++) {
        int yPos = startY + (i * optionHeight);
        
        if(i == wifiMenuSelection) {
            tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, HIGHLIGHT_COLOR);
            tft.setTextColor(BACKGROUND_COLOR);
        } else {
            tft.fillRect(15, yPos - 2, SCREEN_WIDTH - 30, optionHeight - 4, PANEL_COLOR);
            tft.setTextColor(TEXT_COLOR);
        }
        
        tft.setTextDatum(TL_DATUM);
        tft.drawString(wifiOptions[i], 20, yPos);
        
        if(i == wifiMenuSelection) {
            tft.setTextColor(BACKGROUND_COLOR);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 20, yPos);
        }
    }
    
    // Instrucoes
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Encoder: Selecionar opcao", SCREEN_WIDTH / 2, 132);
}

void drawWifiConfigPortal() {
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

void drawBrightnessConfigScreen() {
    tft.fillScreen(MENU_BG_COLOR);
    
    // Cabecalho
    tft.fillRect(0, 0, SCREEN_WIDTH, 22, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("BRILHO LEDs", SCREEN_WIDTH / 2, 11);
    
    // Valor atual grande
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(LED_BRIGHTNESS), SCREEN_WIDTH / 2, 50);
    
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Nivel (0-255)", SCREEN_WIDTH / 2, 70);
    
    // Barra de progresso
    int barWidth = 180;
    int barHeight = 12;
    int barX = (SCREEN_WIDTH - barWidth) / 2;
    int barY = 85;
    
    tft.drawRect(barX, barY, barWidth, barHeight, SECONDARY_COLOR);
    
    int progressWidth = map(LED_BRIGHTNESS, 0, 255, 0, barWidth - 2);
    tft.fillRect(barX + 1, barY + 1, progressWidth, barHeight - 2, ACCENT_COLOR);
    
    // Marcadores
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Min", barX, barY + barHeight + 5);
    tft.drawString("Max", barX + barWidth, barY + barHeight + 5);
    
    // Instrucoes
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(ACCENT_COLOR);
    tft.drawString("Gire encoder para ajustar", SCREEN_WIDTH / 2, 115);
    tft.drawString("Encoder: Voltar ao menu", SCREEN_WIDTH / 2, 130);
}

void drawBatteryInfoScreen() {
    tft.fillScreen(MENU_BG_COLOR);
    
    // Cabecalho
    tft.fillRect(0, 0, SCREEN_WIDTH, 22, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("INFORMACOES BATERIA", SCREEN_WIDTH / 2, 11);
    
    // Status
    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Status:", 10, 35);
    
    if(isUsbConnected) {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("USB CONECTADO", 50, 35);
    } else {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("MODO BATERIA", 50, 35);
    }
    
    // Dados
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Tensao:", 10, 55);
    tft.drawString(String(batteryVoltage, 2) + " V", 70, 55);
    
    tft.drawString("Porcentagem:", 10, 75);
    tft.setTextColor(batteryPercentage > 20 ? SUCCESS_COLOR : ERROR_COLOR);
    tft.drawString(String(batteryPercentage) + " %", 90, 75);
    
    // Barra visual grande
    int battWidth = 160;
    int battHeight = 30;
    int battX = (SCREEN_WIDTH - battWidth) / 2;
    int battY = 95;
    
    // Moldura
    tft.drawRect(battX, battY, battWidth, battHeight, TEXT_COLOR);
    tft.fillRect(battX + battWidth, battY + 8, 5, 14, TEXT_COLOR);
    
    // Preenchimento
    int fillWidth = map(batteryPercentage, 0, 100, 0, battWidth - 2);
    uint16_t fillColor = batteryPercentage > 60 ? SUCCESS_COLOR : 
                        batteryPercentage > 20 ? WARNING_COLOR : ERROR_COLOR;
    
    tft.fillRect(battX + 1, battY + 1, fillWidth, battHeight - 2, fillColor);
    
    // Status carga
    tft.setTextColor(TEXT_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Controle de Carga:", SCREEN_WIDTH / 2, 135);
    
    if(isUsbConnected) {
        tft.setTextColor(WARNING_COLOR);
        tft.drawString("BLOQUEADO (CE HIGH)", SCREEN_WIDTH / 2, 150);
    } else {
        tft.setTextColor(SUCCESS_COLOR);
        tft.drawString("HABILITADO (CE LOW)", SCREEN_WIDTH / 2, 150);
    }
    
    // Voltar
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Encoder: Voltar", SCREEN_WIDTH / 2, 170);
}

void drawLedEffectsScreen() {
    tft.fillScreen(MENU_BG_COLOR);
    
    // 1. Cabecalho mais compacto para ganhar espaço
    tft.fillRect(0, 0, SCREEN_WIDTH, 18, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("EFEITOS LEDs", SCREEN_WIDTH / 2, 9);
    
    // 2. Lista de efeitos (7 itens)
    String effects[] = {"Arco-Iris", "Piscante", "Onda Azul", "Fogo", "Estrelas", "Desligar", "Voltar"};
    int effectsCount = 7;
    
    // Ajuste de layout para tela de 1.14"
    int startY = 25;      // Onde começa o primeiro item
    int rowHeight = 14;   // Altura de cada linha (reduzido de 18)
    
    for(int i = 0; i < effectsCount; i++) {
        int yPos = startY + (i * rowHeight);
        
        if(i == menuSelection) {
            // Retângulo de seleção um pouco mais fino
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, HIGHLIGHT_COLOR);
            tft.setTextColor(BACKGROUND_COLOR);
        } else {
            tft.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 12, PANEL_COLOR);
            tft.setTextColor(TEXT_COLOR);
        }
        
        tft.setTextDatum(TL_DATUM);
        tft.drawString(effects[i], 10, yPos);
        
        if(i == menuSelection) {
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", SCREEN_WIDTH - 10, yPos);
        }
    }
    
    // 3. Instrucoes fixadas no final da tela (Considerando 135px de altura total)
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(BC_DATUM); 
    // Usando SCREEN_HEIGHT ou 130 para garantir que apareça no fim
    tft.drawString("Gire: Sel. | Click: OK", SCREEN_WIDTH / 2, 130);
}

void drawAdvancedSettings() {
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
    
    for(int i = 0; i < 5; i++) {
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

void drawAboutDeviceScreen() {
    tft.fillScreen(MENU_BG_COLOR);
    
    // Cabecalho
    tft.fillRect(0, 0, SCREEN_WIDTH, 22, PRIMARY_COLOR);
    tft.setTextColor(BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SOBRE DISPOSITIVO", SCREEN_WIDTH / 2, 11);
    
    // Informacoes do dispositivo
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ESP32 STREAM DECK", SCREEN_WIDTH / 2, 40);
    
    tft.setTextColor(TEXT_COLOR);
    tft.drawString(FIRMWARE_VERSION, SCREEN_WIDTH / 2, 60);
    
    // Separador
    tft.drawFastHLine(20, 75, SCREEN_WIDTH - 40, SECONDARY_COLOR);
    
    // Especificacoes
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Especificacoes:", 20, 85);
    tft.drawString("• 16 Botoes Programaveis", 25, 100);
    tft.drawString("• 16 LEDs RGB WS2812B", 25, 115);
    tft.drawString("• Wi-Fi + USB Serial", 25, 130);
    tft.drawString("• Monitor de Bateria", 25, 145);
    
    // Desenvolvedor
    tft.setTextColor(PRIMARY_COLOR);
    tft.drawString("Desenvolvedor:", 20, 160);
    tft.setTextColor(TEXT_COLOR);
    tft.drawString(DEVELOPER, 25, 175);
    
    // GitHub
    tft.setTextColor(SECONDARY_COLOR);
    tft.drawString(GITHUB, SCREEN_WIDTH / 2, 195);
    
    // Voltar
    tft.setTextColor(TEXT_COLOR);
    tft.drawString("Encoder: Voltar", SCREEN_WIDTH / 2, 215);
}

// =========================================================================
// === CONTROLE DO ENCODER =================================================
// =========================================================================

void handleEncoder() {
    handleEncoderButton();
    
    if(currentState == STATE_SETTINGS_MENU || 
       currentState == STATE_BRIGHTNESS_CONFIG ||
       currentState == STATE_LED_EFFECTS ||
       currentState == STATE_WIFI_CONFIG_MENU) {
        handleEncoderRotation();
    }
}

void handleEncoderButton() {
    int btnState = digitalRead(ENCODER_BTN_PIN);
    
    if(btnState == LOW && encoderBtnLastState == HIGH) {
        unsigned long now = millis();
        
        if(now - lastEncoderBtnPress > ENCODER_DEBOUNCE_DELAY) {
            // NÃO altera LEDs quando entra no menu
            // Apenas processa a navegação
            
            switch(currentState) {
                case STATE_MAIN:
                    currentState = STATE_SETTINGS_MENU;
                    menuSelection = 0;
                    menuScrollOffset = 0;
                    drawSettingsMenu();
                    Serial.println("Navegacao: Tela Principal -> Menu Config");
                    break;
                    
                case STATE_SETTINGS_MENU:
                    switch(menuSelection) {
                        case 0:  // Wi-Fi
                            currentState = STATE_WIFI_CONFIG_MENU;
                            wifiMenuSelection = 0;
                            drawWifiConfigMenu();
                            Serial.println("Navegacao: Menu -> Config Wi-Fi");
                            break;
                        case 1:  // Brilho
                            currentState = STATE_BRIGHTNESS_CONFIG;
                            drawBrightnessConfigScreen();
                            Serial.println("Navegacao: Menu -> Brilho LEDs");
                            break;
                        case 2:  // Efeitos
                            currentState = STATE_LED_EFFECTS;
                            menuSelection = 0;
                            drawLedEffectsScreen();
                            Serial.println("Navegacao: Menu -> Efeitos LEDs");
                            break;
                        case 3:  // Bateria
                            currentState = STATE_BATTERY_INFO;
                            drawBatteryInfoScreen();
                            Serial.println("Navegacao: Menu -> Info Bateria");
                            break;
                        case 4:  // Avancado
                            currentState = STATE_ADVANCED_SETTINGS;
                            drawAdvancedSettings();
                            Serial.println("Navegacao: Menu -> Avancado");
                            break;
                        case 5:  // Sobre Dispositivo
                            currentState = STATE_ABOUT_DEVICE;
                            drawAboutDeviceScreen();
                            Serial.println("Navegacao: Menu -> Sobre Dispositivo");
                            break;
                        case 6:  // Teste Sistema
                            Serial.println("Teste de Sistema selecionado");
                            // Futura implementacao
                            break;
                        case 7:  // Voltar
                            currentState = STATE_MAIN;
                            drawMainScreen();
                            Serial.println("Navegacao: Menu -> Tela Principal");
                            break;
                    }
                    break;
                    
                case STATE_WIFI_CONFIG_MENU:
                    switch(wifiMenuSelection) {
                        case 0:  // Limpar credenciais
                            clearWiFiCredentials();
                            currentState = STATE_SETTINGS_MENU;
                            drawSettingsMenu();
                            Serial.println("Credenciais Wi-Fi limpas");
                            break;
                        case 1:  // Configurar nova rede
                            currentState = STATE_WIFI_CONFIG_PORTAL;
                            resetWiFiCredentials();
                            break;
                        case 2:  // Voltar
                            currentState = STATE_SETTINGS_MENU;
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
                    drawSettingsMenu();
                    break;
                    
                case STATE_LED_EFFECTS:
                    if(!effectActive) {
                        effectActive = true;
                        manualControl = true;
                        
                        switch(menuSelection) {
                            case 0: currentEffect = "RAINBOW"; break;
                            case 1: currentEffect = "BLINK"; break;
                            case 2: currentEffect = "WAVE_BLUE"; break;
                            case 3: currentEffect = "FIRE"; break;
                            case 4: currentEffect = "TWINKLE"; break;
                            case 5: 
                                effectActive = false;
                                manualControl = false;
                                clearAllLEDs();
                                break;
                            case 6:  // Voltar
                                currentState = STATE_SETTINGS_MENU;
                                drawSettingsMenu();
                                break;
                        }
                        
                        if(effectActive) {
                            Serial.println("Efeito ativado via encoder: " + currentEffect);
                        }
                    } else {
                        effectActive = false;
                        manualControl = false;
                        clearAllLEDs();
                        Serial.println("Efeito desativado via encoder");
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
    
    if(currentStateEncoder != lastEncoderState) {
        int dtState = digitalRead(ENCODER_DT_PIN);
        
        if(currentState == STATE_SETTINGS_MENU) {
            if(dtState != currentStateEncoder) {
                menuSelection = (menuSelection + 1) % MENU_ITEMS_COUNT;
            } else {
                menuSelection = (menuSelection - 1 + MENU_ITEMS_COUNT) % MENU_ITEMS_COUNT;
            }
            drawSettingsMenu();
        } 
        else if(currentState == STATE_WIFI_CONFIG_MENU) {
            if(dtState != currentStateEncoder) {
                wifiMenuSelection = (wifiMenuSelection + 1) % WIFI_MENU_ITEMS;
            } else {
                wifiMenuSelection = (wifiMenuSelection - 1 + WIFI_MENU_ITEMS) % WIFI_MENU_ITEMS;
            }
            drawWifiConfigMenu();
        }
        else if(currentState == STATE_BRIGHTNESS_CONFIG) {
            int oldBrightness = LED_BRIGHTNESS;
            
            if(dtState != currentStateEncoder) {
                LED_BRIGHTNESS = min(255, LED_BRIGHTNESS + 5);
            } else {
                LED_BRIGHTNESS = max(5, LED_BRIGHTNESS - 5);
            }
            
            if(LED_BRIGHTNESS != oldBrightness) {
                FastLED.setBrightness(LED_BRIGHTNESS);
                FastLED.show();
                drawBrightnessConfigScreen();
                Serial.println("Brilho ajustado: " + String(LED_BRIGHTNESS));
            }
        }
        else if(currentState == STATE_LED_EFFECTS) {
            if(dtState != currentStateEncoder) {
                menuSelection = (menuSelection + 1) % 7;
            } else {
                menuSelection = (menuSelection - 1 + 7) % 7;
            }
            drawLedEffectsScreen();
        }
    }
    
    lastEncoderState = currentStateEncoder;
}

// =========================================================================
// === VERIFICAÇÃO DE CONEXÃO WI-FI =======================================
// =========================================================================

void checkWiFiConnection() {
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    // Se desconectou do Wi-Fi
    if(lastWiFiConnected && !wifiConnected) {
        Serial.println("⚠️ Wi-Fi desconectado!");
        
        // Se estava conectado via Wi-Fi, muda para desconectado
        if(activeProtocol == WIFI) {
            activeProtocol = NONE;
            Serial.println("Protocolo alterado: Wi-Fi -> Nenhum");
            
            // Atualiza tela se estiver na tela principal
            if(currentState == STATE_MAIN) {
                drawMainScreen();
            }
        }
        
        // Para cliente Wi-Fi se estiver conectado
        if(client.connected()) {
            client.stop();
            Serial.println("Cliente Wi-Fi desconectado");
        }
    }
    // Se reconectou ao Wi-Fi
    else if(!lastWiFiConnected && wifiConnected) {
        Serial.println("✅ Wi-Fi reconectado!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP().toString());
        
        // Reinicia servidores
        serverTCP.begin();
        Udp.begin(UDP_SEARCH_PORT);
    }
    
    lastWiFiConnected = wifiConnected;
}

// =========================================================================
// === CONTROLE DE BOTOES ==================================================
// =========================================================================

int readButtons() {
    digitalWrite(latchPin, LOW);
    delayMicroseconds(5);
    digitalWrite(latchPin, HIGH);
    
    int data = 0;
    for(int i = 0; i < numBits; i++) {
        if(digitalRead(dataPin)) {
            data |= (1 << i);
        }
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(clockPin, LOW);
    }
    return data;
}

int mapButton(int bit) {
    switch(bit) {
        case 0: return 9;
        case 1: return 10;
        case 2: return 11;
        case 3: return 12;
        case 4: return 16;
        case 5: return 15;
        case 6: return 14;
        case 7: return 13;
        case 8: return 8;
        case 9: return 7;
        case 10: return 6;
        case 11: return 5;
        case 12: return 1;
        case 13: return 2;
        case 14: return 3;
        case 15: return 4;
        default: return 0;
    }
}

void updateLedFeedback() {
    if(ledFeedbackActive && millis() - ledFeedbackTimer > 150) {
        ledFeedbackActive = false;
        feedbackLedIndex = -1;
    }
}

void checkButtons() {
    int currentButtonStates = readButtons();
    
    if(currentState == STATE_MAIN) {
        for(int i = 0; i < numBits; i++) {
            if(bitRead(currentButtonStates, i) && !bitRead(lastButtonStates, i)) {
                int buttonNumber = mapButton(i);
                handleButtonPress(buttonNumber);
            }
        }
    }
    
    lastButtonStates = currentButtonStates;
}

void handleButtonPress(int buttonNumber) {
    // Feedback visual apenas no LED pressionado
    if(buttonNumber >= 1 && buttonNumber <= NUM_LEDS) {
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
    
    if(activeProtocol == USB) {
        Serial.println(command);
    } 
    else if(activeProtocol == WIFI && client.connected()) {
        client.println(command);
    }
    
    Serial.println("Botao " + String(buttonNumber) + " pressionado");
}

// =========================================================================
// === CONTROLE DOS LEDs ===================================================
// =========================================================================

void clearAllLEDs() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setStatusLEDs() {
    if(wifiConfigMode) {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        
        if(millis() - lastBlink > 500) {
            blinkState = !blinkState;
            fill_solid(leds, NUM_LEDS, blinkState ? CRGB::Blue : CRGB::Black);
            FastLED.show();
            lastBlink = millis();
        }
        return;
    }
    
    // Se tem efeito ativo, mantém o efeito (não mostra status)
    if(effectActive) {
        return;
    }
    
    if(activeProtocol == USB) {
        fill_solid(leds, NUM_LEDS, CRGB::Blue);
        FastLED.show();
    } 
    else if(activeProtocol == WIFI) {
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
    } 
    else {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        
        if(millis() - lastBlink > 1000) {
            blinkState = !blinkState;
            fill_solid(leds, NUM_LEDS, blinkState ? CRGB::Red : CRGB::Black);
            FastLED.show();
            lastBlink = millis();
        }
    }
}

void updateLEDs() {
    updateLedFeedback();
    
    // Se tem efeito ativo, apenas atualiza o efeito
    if(effectActive) {
        updateEffect();
        return;
    }
    
    // Não faz nada durante controle manual ou config portal
    if(manualControl || wifiConfigMode || ledFeedbackActive) {
        return;
    }
    
    static unsigned long lastStatusUpdate = 0;
    if(millis() - lastStatusUpdate > 200) {
        setStatusLEDs();
        lastStatusUpdate = millis();
    }
}

// =========================================================================
// === FUNÇÕES DE PROCESSAMENTO DE COMANDOS LED ===========================
// =========================================================================

void processIndividualLedCommand(const String& command) {
    manualControl = true;
    effectActive = false;
    
    int firstColon = command.indexOf(':');
    int secondColon = command.indexOf(':', firstColon + 1);
    
    if(secondColon != -1) {
        int ledIndex = command.substring(firstColon + 1, secondColon).toInt();
        String colorStr = command.substring(secondColon + 1);
        
        if(colorStr.startsWith("#")) {
            colorStr = colorStr.substring(1);
        }
        
        long color = strtol(colorStr.c_str(), NULL, 16);
        CRGB ledColor = CRGB(
            (color >> 16) & 0xFF,
            (color >> 8) & 0xFF,
            color & 0xFF
        );
        
        if(ledIndex >= 0 && ledIndex < NUM_LEDS) {
            leds[ledIndex] = ledColor;
            FastLED.show();
            
            Serial.print("LED ");
            Serial.print(ledIndex);
            Serial.print(" definido para #");
            Serial.println(colorStr);
        }
    }
}

void processAllLedCommand(const String& command) {
    String subCmd = command.substring(8);
    
    if(subCmd == "ON") {
        fill_solid(leds, NUM_LEDS, CRGB::White);
        FastLED.show();
        effectActive = false;
        manualControl = true;
        Serial.println("Todos LEDs LIGADOS (branco)");
    }
    else if(subCmd == "OFF") {
        clearAllLEDs();
        effectActive = false;
        manualControl = false;
        Serial.println("Todos LEDs DESLIGADOS");
    }
    else {
        effectActive = true;
        currentEffect = subCmd;
        effectTimer = millis();
        Serial.println("Efeito " + subCmd + " ativado via comando");
    }
}

void processLedCommand(const String& command) {
    if(command.startsWith("LED:")) {
        processIndividualLedCommand(command);
    }
    else if(command.startsWith("ALL_LED:")) {
        processAllLedCommand(command);
    }
    else {
        Serial.print("Comando LED invalido: ");
        Serial.println(command);
    }
}

void updateEffect() {
    if(!effectActive) return;
    if(millis() - effectTimer < 50) return;
    
    if(currentEffect == "RAINBOW") {
        static uint8_t hue = 0;
        fill_rainbow(leds, NUM_LEDS, hue, 255 / NUM_LEDS);
        FastLED.show();
        hue += 5;
    } 
    else if(currentEffect == "BLINK") {
        static bool blinkState = false;
        blinkState = !blinkState;
        fill_solid(leds, NUM_LEDS, blinkState ? CRGB::White : CRGB::Black);
        FastLED.show();
    } 
    else if(currentEffect == "WAVE_BLUE") {
        static uint8_t offset = 0;
        for(int i = 0; i < NUM_LEDS; i++) {
            uint8_t brightness = sin8(i * 32 + offset);
            leds[i] = CRGB(0, 0, brightness);
        }
        FastLED.show();
        offset += 8;
    } 
    else if(currentEffect == "FIRE") {
        for(int i = 0; i < NUM_LEDS; i++) {
            int heat = random8(50, 255);
            leds[i] = HeatColor(heat);
        }
        FastLED.show();
    } 
    else if(currentEffect == "TWINKLE") {
        static uint8_t sparkle[NUM_LEDS];
        for(int i = 0; i < NUM_LEDS; i++) {
            if(sparkle[i] == 0 && random8() < 10) {
                sparkle[i] = 255;
            }
            if(sparkle[i] > 0) {
                sparkle[i] = qsub8(sparkle[i], 15);
                leds[i] = CRGB(sparkle[i], sparkle[i], sparkle[i]);
            } else {
                leds[i] = CRGB::Black;
            }
        }
        FastLED.show();
    }
    
    effectTimer = millis();
}

// =========================================================================
// === GERENCIAMENTO DA BATERIA ============================================
// =========================================================================

void updateBatteryLogic() {
    isUsbConnected = (activeProtocol == USB);
    
    if(isUsbConnected) {
        digitalWrite(PIN_TP4056_CE, HIGH);
        isCharging = false;
    } else {
        digitalWrite(PIN_TP4056_CE, LOW);
        isCharging = true;
    }
    
    int rawADC = 0;
    for(int i = 0; i < 10; i++) {
        rawADC += analogRead(PIN_BATT_ADC);
        delay(1);
    }
    rawADC /= 10;
    
    batteryVoltage = (rawADC / 4095.0) * 3.3 * 2.0;
    
    if(batteryVoltage >= 4.2) {
        batteryPercentage = 100;
    } else if(batteryVoltage <= 3.0) {
        batteryPercentage = 0;
    } else {
        batteryPercentage = map(batteryVoltage * 100, 300, 420, 0, 100);
    }
    
    batteryPercentage = constrain(batteryPercentage, 0, 100);
}

void updateBatteryDisplay() {
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
    if(batteryPercentage > 60) fillColor = SUCCESS_COLOR;
    else if(batteryPercentage > 20) fillColor = WARNING_COLOR;
    else fillColor = ERROR_COLOR;
    
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

void initWiFi() {
    String ssid = preferences.getString("ssid", "");
    String pass = preferences.getString("pass", "");
    
    if(ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        
        Serial.print("Conectando ao Wi-Fi ");
        Serial.print(ssid);
        
        for(int i = 0; i < 20; i++) {
            if(WiFi.status() == WL_CONNECTED) break;
            Serial.print(".");
            delay(500);
        }
        
        if(WiFi.status() == WL_CONNECTED) {
            Serial.println(" OK");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
            
            serverTCP.begin();
            Udp.begin(UDP_SEARCH_PORT);
            lastWiFiConnected = true;
        } else {
            Serial.println(" FALHA");
            preferences.clear();
            lastWiFiConnected = false;
        }
    }
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

void handleWiFiSave() {
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

void clearWiFiCredentials() {
    preferences.begin(PREFS_KEY, false);
    preferences.clear();
    preferences.end();
    
    WiFi.disconnect(true);
    delay(1000);
    
    Serial.println("Credenciais Wi-Fi limpas");
    
    // Atualiza status
    lastWiFiConnected = false;
}

void resetWiFiCredentials() {
    drawWifiConfigPortal();
    delay(2000);
    startConfigPortal();
}

void checkUdpSearch() {
    if(WiFi.status() != WL_CONNECTED) return;
    
    int packetSize = Udp.parsePacket();
    if(packetSize) {
        char incomingPacket[255];
        int len = Udp.read(incomingPacket, 255);
        if(len > 0) {
            incomingPacket[len] = 0;
            if(String(incomingPacket) == UDP_DISCOVER_MSG) {
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

void checkSerialCommands() {
    if(Serial.available()) {
        String message = Serial.readStringUntil('\n');
        message.trim();
        
        if(message.startsWith("LED:") || message.startsWith("ALL_LED:")) {
            processLedCommand(message);
        }
        else if(message == "CONNECTED") {
            activeProtocol = USB;
            manualControl = false;
            if(currentState == STATE_MAIN) drawMainScreen();
            Serial.println("Conectado via USB");
        } 
        else if(message == "DISCONNECT") {
            if(client.connected()) {
                activeProtocol = WIFI;
            } else {
                activeProtocol = NONE;
            }
            manualControl = false;
            if(currentState == STATE_MAIN) drawMainScreen();
            Serial.println("Desconectado do USB");
        } 
        else if(message == "STATUS") {
            Serial.println("\n=== STATUS DO SISTEMA ===");
            Serial.println("Firmware: " + String(FIRMWARE_VERSION));
            Serial.println("Estado: " + String(currentState));
            Serial.println("Protocolo: " + String(activeProtocol == USB ? "USB" : 
                                                    activeProtocol == WIFI ? "Wi-Fi" : "Nenhum"));
            Serial.println("Brilho LEDs: " + String(LED_BRIGHTNESS));
            Serial.println("Bateria: " + String(batteryPercentage) + "% (" + String(batteryVoltage, 1) + "V)");
            Serial.println("USB Conectado: " + String(isUsbConnected ? "Sim" : "Nao"));
            Serial.println("Carregando: " + String(isCharging ? "Sim" : "Nao"));
            Serial.println("Wi-Fi: " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado"));
            if(WiFi.status() == WL_CONNECTED) {
                Serial.println("IP: " + WiFi.localIP().toString());
            }
            Serial.println("Desenvolvedor: " + String(DEVELOPER));
            Serial.println("GitHub: " + String(GITHUB));
            Serial.println("==========================");
        }
        else if(message == "LED_HELP") {
            Serial.println("\n=== COMANDOS LED DISPONIVEIS ===");
            Serial.println("LED:0:FF0000      // LED 0 vermelho");
            Serial.println("LED:1:00FF00      // LED 1 verde");
            Serial.println("LED:2:0000FF      // LED 2 azul");
            Serial.println("LED:3:FFFF00      // LED 3 amarelo");
            Serial.println("LED:4:FF00FF      // LED 4 rosa");
            Serial.println("LED:5:00FFFF      // LED 5 ciano");
            Serial.println("ALL_LED:ON        // Liga todos branco");
            Serial.println("ALL_LED:OFF       // Desliga todos");
            Serial.println("ALL_LED:RAINBOW   // Efeito arco-iris");
            Serial.println("ALL_LED:BLINK     // Efeito piscante");
            Serial.println("ALL_LED:WAVE_BLUE // Onda azul");
            Serial.println("ALL_LED:FIRE      // Efeito fogo");
            Serial.println("ALL_LED:TWINKLE   // Efeito estrelas");
            Serial.println("================================");
        }
    }
}

// =========================================================================
// === SETUP PRINCIPAL =====================================================
// =========================================================================

void setup() {
    Serial.begin(115200);
    preferences.begin(PREFS_KEY, false);
    
    initializeDisplay();
    initButtons();
    initLEDs();
    initEncoder();
    
    pinMode(PIN_TP4056_CE, OUTPUT);
    digitalWrite(PIN_TP4056_CE, LOW);
    
    // Inicializacao
    drawLoadingScreen();
    initWiFi();
    
    // Tela principal
    currentState = STATE_MAIN;
    drawMainScreen();
    
    // Info inicial
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("       ESP32 DECK - " + String(FIRMWARE_VERSION));
    Serial.println("═══════════════════════════════════════");
    Serial.println("Desenvolvedor: " + String(DEVELOPER));
    Serial.println("GitHub: " + String(GITHUB));
    Serial.println("═══════════════════════════════════════");
    Serial.println("🎮 CONTROLES:");
    Serial.println("   • Encoder: Menu de configuracoes");
    Serial.println("   • Botões: Comandos (tela principal)");
    Serial.println("═══════════════════════════════════════");
    Serial.println("💡 COMANDOS LED:");
    Serial.println("   Envie 'LED_HELP' para ver opcoes");
    Serial.println("═══════════════════════════════════════");
}

// =========================================================================
// === LOOP PRINCIPAL ======================================================
// =========================================================================

void loop() {
    // Atualiza bateria
    updateBatteryLogic();
    
    // Verifica conexão Wi-Fi
    checkWiFiConnection();
    
    // Encoder
    static unsigned long lastEncoderCheck = 0;
    if(millis() - lastEncoderCheck > 10) {
        handleEncoder();
        lastEncoderCheck = millis();
    }
    
    // Atualiza bateria no display periodicamente
    static unsigned long lastBatteryUpdate = 0;
    if(currentState == STATE_MAIN && millis() - lastBatteryUpdate > 2000) {
        updateBatteryDisplay();
        lastBatteryUpdate = millis();
    }
    
    // Processa estado atual
    switch(currentState) {
        case STATE_MAIN:
            checkButtons();
            checkSerialCommands();
            updateLEDs();
            
            if(WiFi.status() == WL_CONNECTED) {
                checkUdpSearch();
                
                if(!client.connected()) {
                    WiFiClient newClient = serverTCP.available();
                    if(newClient) {
                        client = newClient;
                        activeProtocol = WIFI;
                        drawMainScreen();
                        Serial.println("Cliente Wi-Fi conectado");
                    }
                }
                
                // Processa comandos Wi-Fi
                if(client.connected()) {
                    while(client.available()) {
                        String msg = client.readStringUntil('\n');
                        msg.trim();
                        
                        if(msg.startsWith("LED:") || msg.startsWith("ALL_LED:")) {
                            processLedCommand(msg);
                        }
                        else if(msg == "PING") {
                            client.println("PONG");
                        }
                        else if(msg == "DISCONNECT") {
                            client.stop();
                            if(activeProtocol == WIFI) {
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
            if(wifiConfigMode) {
                dnsServer.processNextRequest();
                server.handleClient();
            }
            break;
            
        case STATE_LED_EFFECTS:
            if(effectActive) {
                updateEffect();
            }
            break;
    }
    
    // Efeitos de LED
    if(effectActive) {
        updateEffect();
    }
    
    delay(20);
}