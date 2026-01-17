// ===================================================================================
// === ESP32 DECK FÍSICO - MÚLTIPLAS INTERFACES ======================================
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
// === CONFIGURAÇÕES GERAIS E WI-FI ========================================
// =========================================================================
const char *SSID_AP = "ESP32-Deck-Setup";
const char *PASS_AP = "12345678";
const char *PREFS_KEY = "wifi_config";

const int TCP_PORT = 8000;

// Constantes para Busca UDP
const int UDP_SEARCH_PORT = 4210;
const char *UDP_DISCOVER_MSG = "ESP32_DECK_DISCOVER";
const char *UDP_ACK_MSG = "ESP32_DECK_ACK";

// Configuração da Senha de Acesso (Sequência: 1, 5, 8)
const int SEQUENCE_TIMEOUT_MS = 2000; // 2 segundos
const int SEQUENCE_TARGET[] = {1, 5, 8};
const int SEQUENCE_LENGTH = 3;

// =========================================================================
// === CONFIGURAÇÕES DO SHIFT REGISTER =====================================
// =========================================================================
const int dataPin = 17;
const int clockPin = 21;
const int latchPin = 22;
const int numBits = 16;

// =========================================================================
// === CONFIGURAÇÕES DOS LEDs WS2812B (usando WS2812B que é compatível) ====
// =========================================================================
#define LED_PIN 12        // Pino de dados dos LEDs WS2812B
#define NUM_LEDS 16       // Quantidade de LEDs na sua fita
int LED_BRIGHTNESS = 150; // Brilho (0-255)

#define PIN_BATT_ADC 33    // Pino para ler o divisor de tensão (ADC1_CH6)
#define PIN_TP4056_CE 13    // Conectado ao pino CE do TP4056 (através de um transistor se necessário)

float batteryVoltage = 0.0;
int batteryPercentage = 0;
bool isUsbConnected = false;

CRGB leds[NUM_LEDS];     // Array para controlar os LEDs

// =========================================================================
// === CONFIGURAÇÕES DO DISPLAY ============================================
// =========================================================================
TFT_eSPI tft = TFT_eSPI();

// =========================================================================
// === VARIÁVEIS GLOBAIS ===================================================
// =========================================================================

// Enum para rastrear o protocolo ativo
enum ConnectionProtocol
{
  NONE,
  USB,
  WIFI
};
ConnectionProtocol activeProtocol = NONE;

bool inSettingsMenu = false;
int lastButtonStates = 0;
bool wifiConfigMode = false;
Preferences preferences;
WiFiServer serverTCP(TCP_PORT);
WiFiClient client;
WebServer server(80);
DNSServer dnsServer;
WiFiUDP Udp;

bool lastOverallConnectionStatus = false;
bool wifiStatusHandled = false;
// Variáveis para Sequência de Acesso
int sequenceState = 0;
unsigned long sequenceTimer = 0;

// Variáveis para controle dos LEDs - SIMPLIFICADO
unsigned long lastStatusUpdate = 0;
bool effectActive = false;
String currentEffect = "";
unsigned long effectTimer = 0;
bool manualControl = false; // Nova flag: quando true, updateLEDs() não faz nada

// =========================================================================
// === CONFIGURAÇÕES DO ENCODER EC11 =======================================
// =========================================================================
const int ENCODER_CLK_PIN = 25;
const int ENCODER_DT_PIN = 26;
const int ENCODER_BTN_PIN = 27;

// Variáveis para o encoder
bool encoderModeActive = false;
int lastEncoderState = 0;
int encoderBtnLastState = HIGH;
unsigned long lastEncoderBtnPress = 0;
const unsigned long ENCODER_DEBOUNCE_DELAY = 50;

// =========================================================================
// === CONFIGURAÇÃO DE CORES ===============================================
// =========================================================================
#define BACKGROUND_COLOR TFT_BLACK
#define TEXT_COLOR TFT_WHITE
#define CONNECTED_COLOR TFT_GREEN
#define DISCONNECTED_COLOR TFT_RED
#define BUTTON_ACTIVE_COLOR TFT_BLUE
#define BUTTON_INACTIVE_COLOR TFT_DARKGREY
#define ACCENT_COLOR TFT_CYAN
#define INFO_COLOR TFT_LIGHTGREY
#define WARNING_COLOR TFT_ORANGE
#define SUCCESS_COLOR TFT_GREEN

// =========================================================================
// === PROTÓTIPOS DE FUNÇÕES ===============================================
// =========================================================================
void initializeDisplay();
void initButtons();
void initLEDs();
void updateLEDs();
void processLedCommand(const String& command);
void processIndividualLedCommand(const String& command);
void processAllLedCommand(const String& command);
void updateEffect();
void setStatusLEDs(); // Nova função apenas para status
void clearAllLEDs(); // Nova função
void drawBootScreen();
void drawMainInterface();
void drawSettingsPanel();
void drawAccessPointInfo();
void drawConfigPortalScreen();
void updateConnectionStatus(ConnectionProtocol protocol);
void drawStatusMessage(const String &message);
void resetWiFiCredentials();
void checkSerialCommands();
void updateBatteryDisplay();
void initEncoder();
void checkEncoder();
void handleEncoderButton();
void handleEncoderRotation();
void updateBrightnessDisplay();
void initWiFi();
void startConfigPortal();
void handleRoot();
void handleWiFiSave();

int readButtons();
int mapButton(int bit);
void checkButtons();
void handleButtonPress(int buttonNumber);
void checkConnectionChange();
void checkUdpSearch();

void drawPanelCompact();
void drawPanelModern();
void drawPanelMinimal();
void drawPanelTechnical();
void drawPanelGaming();
void drawPanelClassic();

// =========================================================================
// === IMPLEMENTAÇÕES DE FUNÇÕES ===========================================
// =========================================================================

void initLEDs() {
  // WS2812B é compatível com WS2812B, então usamos WS2812B
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  
  // Inicializa todos os LEDs apagados
  clearAllLEDs();
  
  Serial.println("✅ LEDs WS2812B inicializados no pino 2 (usando driver WS2812B)");
}

void clearAllLEDs() {
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
}

void setStatusLEDs() {
  // Esta função SÓ é chamada para mostrar status de conexão
  // Não interfere com controle manual ou efeitos
  
  if (wifiConfigMode) {
    // Modo configuração: piscando azul
    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    
    if (millis() - lastBlink > 300) {
      blinkState = !blinkState;
      CRGB color = blinkState ? CRGB::Blue : CRGB::Black;
      fill_solid(leds, NUM_LEDS, color);
      FastLED.show();
      lastBlink = millis();
    }
    return;
  }
  
  if (activeProtocol == USB) {
    // Conectado USB: Azul sólido
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
    FastLED.show();
  }
  else if (activeProtocol == WIFI) {
    // Conectado Wi-Fi: Verde sólido
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
  }
  else {
    // Desconectado: Vermelho piscando
    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    
    if (millis() - lastBlink > 500) {
      blinkState = !blinkState;
      CRGB color = blinkState ? CRGB::Red : CRGB::Black;
      fill_solid(leds, NUM_LEDS, color);
      FastLED.show();
      lastBlink = millis();
    }
  }
}

void updateLEDs() {
  // Esta função agora é SIMPLES:
  // 1. Se temos controle manual ou efeito ativo, NÃO FAZ NADA
  // 2. Caso contrário, mostra o status normal
  
  if (manualControl || effectActive) {
    return; // Não interfere com controle manual ou efeitos
  }
  
  // Só atualiza status a cada 100ms para economizar processamento
  if (millis() - lastStatusUpdate > 100) {
    setStatusLEDs();
    lastStatusUpdate = millis();
  }
}

void processIndividualLedCommand(const String& command) {
  // Formato: LED:<ID>:<RRGGBB>
  manualControl = true; // Ativa controle manual
  effectActive = false; // Desativa efeitos
  
  int firstColon = command.indexOf(':');
  int secondColon = command.indexOf(':', firstColon + 1);
  
  if (secondColon != -1) {
    int ledIndex = command.substring(firstColon + 1, secondColon).toInt();
    String colorStr = command.substring(secondColon + 1);
    
    // Remove o # se existir
    if (colorStr.startsWith("#")) {
      colorStr = colorStr.substring(1);
    }
    
    // Converter hex para CRGB
    long color = strtol(colorStr.c_str(), NULL, 16);
    CRGB ledColor = CRGB(
      (color >> 16) & 0xFF,
      (color >> 8) & 0xFF,
      color & 0xFF
    );
    
    if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
      leds[ledIndex] = ledColor;
      FastLED.show();
      
      Serial.print("✅ LED ");
      Serial.print(ledIndex);
      Serial.print(" definido para cor: #");
      Serial.println(colorStr);
    } else {
      Serial.print("❌ Índice LED inválido: ");
      Serial.println(ledIndex);
    }
  }
}

void updateEffect() {
  if (!effectActive) return;
  
  // Para efeitos lentos, atualiza a cada 50ms
  if (millis() - effectTimer < 50) return;
  
  if (currentEffect == "RAINBOW") {
    // Efeito arco-íris
    static uint8_t hue = 0;
    fill_rainbow(leds, NUM_LEDS, hue, 255 / NUM_LEDS);
    FastLED.show();
    hue += 5;
  }
  else if (currentEffect == "BLINK") {
    // Efeito piscante
    static bool blinkState = false;
    blinkState = !blinkState;
    CRGB color = blinkState ? CRGB::White : CRGB::Black;
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();
  }
  else if (currentEffect == "WAVE_BLUE") {
    // Onda azul
    static uint8_t offset = 0;
    for(int i = 0; i < NUM_LEDS; i++) {
      uint8_t brightness = sin8(i * 32 + offset);
      leds[i] = CRGB(0, 0, brightness);
    }
    FastLED.show();
    offset += 8;
  }
  else if (currentEffect == "FIRE") {
    // Efeito fogo
    for(int i = 0; i < NUM_LEDS; i++) {
      int heat = random8(50, 255);
      leds[i] = HeatColor(heat);
    }
    FastLED.show();
  }
  else if (currentEffect == "GRADIENT") {
    // Gradiente de cores
    static uint8_t hue = 0;
    fill_gradient_RGB(leds, NUM_LEDS, 
                     CHSV(hue, 255, 255), 
                     CHSV(hue + 128, 255, 255));
    FastLED.show();
    hue += 1;
  }
  else if (currentEffect == "TWINKLE") {
    // Estrelas piscando
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
  else if (currentEffect == "ALERT") {
    // Alerta vermelho piscante
    static bool alertState = false;
    alertState = !alertState;
    CRGB color = alertState ? CRGB::Red : CRGB::Black;
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();
  }
  
  effectTimer = millis();
}

void processAllLedCommand(const String& command) {
  String subCmd = command.substring(8);
  manualControl = true; // Ativa controle manual
  
  if (subCmd == "ON") {
    // Liga todos os LEDs (branco)
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
    effectActive = false;
    Serial.println("✅ Todos LEDs LIGADOS (branco)");
  }
  else if (subCmd == "OFF") {
    // Desliga todos os LEDs
    clearAllLEDs();
    effectActive = false;
    Serial.println("✅ Todos LEDs DESLIGADOS");
  }
  else {
    // É um efeito contínuo
    effectActive = true;
    currentEffect = subCmd;
    effectTimer = millis();
    
    if (subCmd == "RAINBOW") {
      Serial.println("✅ Efeito ARCO-ÍRIS ativado");
    }
    else if (subCmd == "BLINK") {
      Serial.println("✅ Efeito PISCANTE ativado");
    }
    else if (subCmd == "WAVE_BLUE") {
      Serial.println("✅ Efeito ONDA AZUL ativado");
    }
    else if (subCmd == "FIRE") {
      Serial.println("✅ Efeito FOGO ativado");
    }
    else if (subCmd == "GRADIENT") {
      Serial.println("✅ Efeito GRADIENTE ativado");
    }
    else if (subCmd == "TWINKLE") {
      Serial.println("✅ Efeito ESTRELAS ativado");
    }
    else if (subCmd == "ALERT") {
      Serial.println("✅ Efeito ALERTA ativado");
    }
    else {
      effectActive = false;
      manualControl = false;
      Serial.print("❌ Comando desconhecido: ");
      Serial.println(subCmd);
    }
  }
}

void processLedCommand(const String& command) {
  // Processa TODOS os comandos LED
  
  if (command.startsWith("LED:")) {
    processIndividualLedCommand(command);
  }
  else if (command.startsWith("ALL_LED:")) {
    processAllLedCommand(command);
  }
  else {
    Serial.print("❌ Comando LED inválido: ");
    Serial.println(command);
  }
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
      String receivedMsg = String(incomingPacket);

      if (receivedMsg == UDP_DISCOVER_MSG)
      {
        IPAddress remoteIP = Udp.remoteIP();
        int remotePort = Udp.remotePort();

        Serial.print("🔍 Busca UDP recebida de: ");
        Serial.println(remoteIP.toString());

        Udp.beginPacket(remoteIP, remotePort);
        Udp.write((const uint8_t *)UDP_ACK_MSG, strlen(UDP_ACK_MSG));
        Udp.endPacket();

        Serial.println("ACK UDP enviado.");
        
        // Feedback visual nos LEDs
        manualControl = true;
        for(int i = 0; i < NUM_LEDS; i++) {
          leds[i] = CRGB::Yellow;
        }
        FastLED.show();
        delay(100);
        manualControl = false;
      }
    }
  }
}

void drawStatusMessage(const String &message)
{
  const int STATUS_AREA_Y = 150;
  const int LINE_HEIGHT = 12;
  const int PADDING_TOP = 5;

  tft.fillRect(10, STATUS_AREA_Y + 5, tft.width() - 20, 30, BACKGROUND_COLOR);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  if (message.length() > 20)
  {
    tft.drawString(message.substring(0, 20), 15, STATUS_AREA_Y + PADDING_TOP);
    tft.drawString(message.substring(20), 15, STATUS_AREA_Y + PADDING_TOP + LINE_HEIGHT);
  }
  else
  {
    tft.drawString(message, 15, STATUS_AREA_Y + PADDING_TOP + (LINE_HEIGHT / 2));
  }
}

void initializeDisplay()
{
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
}

void initButtons()
{
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, INPUT);

  digitalWrite(latchPin, HIGH);
  digitalWrite(clockPin, LOW);
}

void drawBootScreen()
{
  tft.fillScreen(BACKGROUND_COLOR);

  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ESP32 STREAM DECK", tft.width() / 2, tft.height() / 2 - 30);

  tft.drawRect(40, tft.height() / 2, tft.width() - 80, 10, ACCENT_COLOR);
  for (int i = 0; i < 5; i++)
  {
    tft.fillRect(42 + (i * 25), tft.height() / 2 + 2, 20, 6, ACCENT_COLOR);
    delay(300);
  }

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("v2.6 - Multi Interface", tft.width() / 2, tft.height() / 2 + 25);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("github.com/KanekiZLF", tft.width() / 2, tft.height() / 2 + 40);
}

void checkSerialCommands()
{
  if (Serial.available())
  {
    String message = Serial.readStringUntil('\n');
    message.trim();

    Serial.print("📨 Comando recebido: ");
    Serial.println(message);

    if (message.startsWith("LED:") || message.startsWith("ALL_LED:")) {
      processLedCommand(message);
    }
    else if (message == "CONNECTED")
    {
      activeProtocol = USB;
      manualControl = false; // Volta ao controle automático
      Serial.println("✅ Conexão estabelecida com software (USB)");
    }
    else if (message == "DISCONNECT")
    {
      Serial.println("👋 Software se desconectou (USB)");
      if (client.connected())
      {
        activeProtocol = WIFI;
      }
      else
      {
        activeProtocol = NONE;
      }
      manualControl = false; // Volta ao controle automático
    }
    else if (message == "PING")
    {
      Serial.println("PONG");
    }
    else if (message.startsWith("BTN:")) {
      // Processa comandos de botão recebidos
      Serial.print("Botão recebido via Serial: ");
      Serial.println(message);
    }
  }
}

void updateConnectionStatus(ConnectionProtocol protocol)
{
  tft.fillRoundRect(12, 27, tft.width() - 24, 21, 3, BACKGROUND_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);

  // 1. Status de Prontidão (Protocolo USB/Wi-Fi Client)
  if (protocol != NONE)
  {
    tft.setTextColor(CONNECTED_COLOR);
    String protoStr = (protocol == USB) ? "USB" : "WI-FI";
    tft.drawString(String("CONECTADO - PRONTO (") + protoStr + ")", tft.width() / 2, 35);
  }
  else
  {
    tft.setTextColor(DISCONNECTED_COLOR);
    tft.drawString("DESCONECTADO - AGUARDANDO", tft.width() / 2, 35);
  }
}

void drawPanelCompact()
{
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  // Fundo do painel
  tft.fillRoundRect(10, 65, tft.width() - 20, 70, 5, TFT_DARKGREY);
  tft.drawRoundRect(10, 65, tft.width() - 20, 70, 5, ACCENT_COLOR);

  // Título
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("ESP32 DECK v2.6", tft.width() / 2, 70);
  // Informações do DEV
  tft.setTextColor(TFT_CYAN);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("GitHub:", 15, 85);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("github.com/KanekiZLF", 60, 85);

  // Informações técnicas
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Firmware: v2.6", 15, 110);
  tft.drawString("Dev: Luiz F. R. Pimentel", 15, 122);

  // Exibe IP / Configuração AP
  if (WiFi.isConnected())
  {
    tft.setTextColor(TFT_GREEN);
    tft.drawString(String("IP: ") + WiFi.localIP().toString(), 15, 98);
  }
  else
  {
    // Se não conectado, mostra as credenciais do AP
    tft.setTextColor(TFT_ORANGE);
    tft.drawString(String("Wi-Fi Desconectado"), 15, 98);
    tft.setTextColor(TFT_WHITE);
  }
}

void drawMainInterface()
{
  tft.fillScreen(BACKGROUND_COLOR);

  // Cabeçalho
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(1);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("ESP32 STREAM DECK", tft.width() / 2, 8);
  tft.drawFastHLine(10, 20, tft.width() - 20, TFT_DARKGREY);

  // Área de status
  tft.fillRoundRect(10, 25, tft.width() - 20, 25, 5, TFT_DARKGREY);
  updateConnectionStatus(activeProtocol); // Usa o protocolo ativo

  tft.drawFastHLine(10, 55, tft.width() - 20, TFT_DARKGREY);
  // ✅ ESCOLHA SUA INTERFACE PREFERIDA AQUI:
  // ⭐ DESCOMENTE APENAS UMA DAS LINHAS ABAIXO:

  //drawPanelCompact(); // ⭐ Opção 1 - Mais compacta
  // drawPanelModern();    // ⭐ Opção 2 - Estilo moderno
  // drawPanelMinimal(); // ⭐ Opção 3 - Minimalista
  // drawPanelTechnical();  // ⭐ Opção 4 - Técnico
  // drawPanelGaming(); // ⭐ Opção 5 - Estilo gaming
  // drawPanelClassic(); // ⭐ Opção 6 - Clássico
  drawPanelBatteryTest(); // Ative esta para o teste

  // Área de mensagens
  tft.drawFastHLine(10, 150, tft.width() - 20, TFT_DARKGREY);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextDatum(BC_DATUM);
}

void drawPanelModern()
{
  tft.setTextSize(1);
  // Header moderno
  tft.fillRoundRect(10, 65, tft.width() - 20, 25, 3, ACCENT_COLOR);
  tft.setTextColor(BACKGROUND_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("CONTROL PANEL", tft.width() / 2, 71);
  // Corpo do painel
  tft.fillRoundRect(10, 95, tft.width() - 20, 45, 3, TFT_DARKGREY);

  // Status com ícone
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(activeProtocol != NONE ? SUCCESS_COLOR : DISCONNECTED_COLOR);
  tft.drawString(activeProtocol != NONE ? "> ONLINE" : "> OFFLINE", 15, 102);

  // Informações em grid
  tft.setTextColor(TFT_WHITE);
  tft.drawString("FIRMWARE: v2.6", 15, 115);
  tft.drawString(String(numBits) + " Botoes Ativos", 90, 115);
  tft.drawString("Dev: Luiz F. R. Pimentel", 15, 127);
  if (WiFi.isConnected())
  {
    tft.setTextColor(TFT_YELLOW);
    tft.drawString(String("IP: ") + WiFi.localIP().toString(), 90, 127);
  }
  else
  {
    tft.setTextColor(TFT_RED);
    tft.drawString(String("AP: ") + SSID_AP, 90, 127);
  }
}

void drawPanelMinimal()
{
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  // Apenas informações essenciais
  tft.setTextColor(ACCENT_COLOR);
  tft.drawString("ESP32 DECK", 15, 70);

  tft.drawFastHLine(10, 82, tft.width() - 20, ACCENT_COLOR);

  // Status minimalista
  tft.setTextColor(activeProtocol != NONE ? SUCCESS_COLOR : DISCONNECTED_COLOR);
  tft.drawString(activeProtocol != NONE ? "SISTEMA ATIVO" : "AGUARDANDO", 15, 90);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString(String("v2.6 | ") + String(numBits) + " BTNS", 15, 103);
  tft.drawString("Serial: 115200", 15, 115);
  tft.drawString("Luiz F. R. Pimentel", 15, 127);
  if (WiFi.isConnected())
  {
    tft.setTextColor(TFT_YELLOW);
    tft.drawString(String("IP: ") + WiFi.localIP().toString(), 110, 115);
  }
  else
  {
    tft.setTextColor(TFT_RED);
    tft.drawString(String("AP: ") + SSID_AP, 110, 115);
  }
}

void drawPanelTechnical() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  
  // Painel técnico com informações detalhadas
  tft.fillRoundRect(10, 65, tft.width() - 20, 70, 5, TFT_DARKGREY);
  tft.drawRoundRect(10, 65, tft.width() - 20, 70, 5, ACCENT_COLOR);
  
  tft.setTextColor(ACCENT_COLOR);
  tft.drawString("SISTEMA TECNICO", 15, 70);
  
  tft.drawFastHLine(10, 82, tft.width() - 20, ACCENT_COLOR);
  
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("ESP32 Deck v2.6", 15, 90);
  tft.drawString("LEDs WS2812B: " + String(NUM_LEDS), 15, 102);
  tft.drawString("Pino LEDs: " + String(LED_PIN), 90, 102);
  tft.drawString("Shift Reg: " + String(numBits) + " bits", 15, 114);
  
  if (WiFi.isConnected()) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Wi-Fi: " + WiFi.SSID(), 15, 126);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("IP: " + WiFi.localIP().toString(), 90, 126);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("Wi-Fi: OFF", 15, 126);
  }
}

void drawPanelGaming() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  
  // Estilo gaming
  tft.fillRoundRect(10, 65, tft.width() - 20, 70, 5, TFT_NAVY);
  tft.drawRoundRect(10, 65, tft.width() - 20, 70, 5, TFT_CYAN);
  
  tft.setTextColor(TFT_CYAN);
  tft.drawString(">>> ESP32 DECK GAMING <<<", 15, 70);
  
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Status:", 15, 90);
  tft.setTextColor(activeProtocol != NONE ? TFT_GREEN : TFT_RED);
  tft.drawString(activeProtocol != NONE ? "ONLINE" : "OFFLINE", 60, 90);
  
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("LEDs: " + String(NUM_LEDS) + " RGB", 15, 102);
  tft.drawString("Buttons: " + String(numBits), 15, 114);
  
  if (WiFi.isConnected()) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Connected to:", 15, 126);
    tft.drawString(WiFi.SSID().substring(0, 12), 90, 126);
  } else {
    tft.setTextColor(TFT_ORANGE);
    tft.drawString("Gaming Mode", 15, 126);
  }
}

void drawPanelClassic() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  
  // Estilo clássico minimalista
  tft.drawRect(10, 65, tft.width() - 20, 70, TFT_WHITE);
  
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("ESP32 CONTROL PANEL", tft.width() / 2, 70);
  
  tft.drawFastHLine(12, 82, tft.width() - 24, TFT_LIGHTGREY);
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("Firmware: v2.6 (Multi-Interface)", 15, 90);
  
  tft.setTextColor(activeProtocol != NONE ? TFT_GREEN : TFT_RED);
  tft.drawString("Connection: ", 15, 102);
  if (activeProtocol == USB) {
    tft.drawString("USB Serial", 85, 102);
  } else if (activeProtocol == WIFI) {
    tft.drawString("Wi-Fi Client", 85, 102);
  } else {
    tft.drawString("Disconnected", 85, 102);
  }
  
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("Developer: Luiz F. R. Pimentel", 15, 114);
  
  if (WiFi.isConnected()) {
    tft.setTextColor(TFT_CYAN);
    tft.drawString("IP: " + WiFi.localIP().toString(), 15, 126);
  } else {
    tft.setTextColor(TFT_ORANGE);
    tft.drawString("AP Mode: " + String(SSID_AP), 15, 126);
  }
}

// NOVA FUNÇÃO
// NOVA FUNÇÃO
void drawPanelBatteryTest() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  
  // Moldura do Painel de Energia (desenha apenas uma vez)
  tft.fillRoundRect(10, 65, tft.width() - 20, 85, 5, TFT_BLACK); // Aumentei a altura para 85
  tft.drawRoundRect(10, 65, tft.width() - 20, 85, 5, TFT_CYAN);  // Aumentei a altura para 85
  
  tft.setTextColor(ACCENT_COLOR);
  tft.drawString("MONITOR DE ENERGIA", 15, 70);
  tft.drawFastHLine(10, 82, tft.width() - 20, ACCENT_COLOR);

  // Títulos estáticos (desenha apenas uma vez)
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Voltagem:", 15, 90);
  
  // Desenha o quadro da barra de bateria (estático)
  tft.drawRect(120, 90, 30, 10, TFT_WHITE);
  
  tft.drawString("USB Conectado:", 15, 105);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("Status CE:", 15, 120);
  
  // Adiciona linha para o brilho dos LEDs
  tft.drawString("Brilho LEDs:", 15, 135);
  
  // Chama a função para atualizar os valores dinâmicos
  updateBatteryDisplay();
  updateBrightnessDisplay(); // Atualiza o valor do brilho
}

void startConfigPortal()
{
  wifiConfigMode = true;

  drawConfigPortalScreen();

  Serial.println("Iniciando AP para configuracao...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID_AP, PASS_AP);

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/save", handleWiFiSave);
  server.begin();

  Serial.print("AP: ");
  Serial.println(SSID_AP);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Atualiza LEDs para modo configuração
  manualControl = false;
}

void handleRoot()
{
  String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
      <meta name='viewport' content='width=device-width, initial-scale=1'>
      <title>Configurar ESP32 Deck</title>
      <style>
        body { font-family: Arial, sans-serif; text-align: center; background: #222; color: #fff; }
        .container { max-width: 350px; margin: 50px auto; background: #333; padding: 20px; border-radius: 10px; }
        input[type=text], input[type=password] { width: 90%; padding: 10px; margin: 8px 0; display: inline-block; border: 1px solid #555; border-radius: 4px; box-sizing: border-box; background: #444; color: #fff; }
        input[type=submit] { background-color: #007bff; color: white; padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; margin-top: 15px; }
        input[type=submit]:hover { background-color: #0056b3; }
      </style>
    </head>
    <body>
      <div class='container'>
        <h1>ESP32 Deck Wi-Fi</h1>
        <p>Conecte-se à sua rede:</p>
        <form method='get' action='save'>
          <label for='ssid'>SSID da Rede:</label>
          <input type='text' id='ssid' name='ssid' required>
          <label for='pass'>Senha:</label>
          <input type='password' id='pass' name='pass' required>
          <input type='submit' value='Salvar e Conectar'>
        </form>
      </div>
    </body>
    </html>
  )";
  server.send(200, "text/html", html);
}

void handleWiFiSave()
{
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  // 1. SALVA CREDENCIAIS
  preferences.begin(PREFS_KEY, false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();

  String msg = "Credenciais salvas. Tentando conectar a " + ssid + ". Voltando para operação normal...";
  server.send(200, "text/plain", msg);

  // 2. DESLIGA O AP
  server.close();
  wifiConfigMode = false;

  // 3. LIMPA A PILHA DE REDE
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true); // Força a desconexão e limpa a memória (true)

  // 4. MUDA O MODO E INICIA A CONEXÃO STA
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  // 5. Atualiza o Protocolo e Display
  activeProtocol = NONE;
  wifiStatusHandled = false;
  drawMainInterface();
  drawStatusMessage("Wi-Fi salvo. Tentando conectar...");
  
  // Atualiza LEDs
  manualControl = false;
}

void drawConfigPortalScreen()
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("MODO DE CONFIG.", tft.width() / 2, tft.height() / 2 - 40);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("1. Conecte-se a rede Wi-Fi:", tft.width() / 2, tft.height() / 2);

  tft.setTextColor(ACCENT_COLOR);
  tft.drawString(String("SSID: ") + SSID_AP, tft.width() / 2, tft.height() / 2 + 15);
  tft.drawString(String("PASS: ") + PASS_AP, tft.width() / 2, tft.height() / 2 + 27);

  tft.setTextColor(INFO_COLOR);
  tft.drawString("2. Acesse 192.168.4.1 no navegador.", tft.width() / 2, tft.height() / 2 + 50);
}

// NOVO: Tela de Informações do Ponto de Acesso
void drawAccessPointInfo()
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CONFIGURAR WI-FI", tft.width() / 2, tft.height() / 2 - 50);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Conecte seu dispositivo a:", tft.width() / 2, tft.height() / 2 - 10);

  tft.setTextColor(ACCENT_COLOR);
  tft.drawString(String("SSID: ") + SSID_AP, tft.width() / 2, tft.height() / 2 + 10);
  tft.drawString(String("PASS: ") + PASS_AP, tft.width() / 2, tft.height() / 2 + 25);

  tft.setTextColor(INFO_COLOR);
  tft.drawString("Iniciando WebPortal em 5s...", tft.width() / 2, tft.height() / 2 + 50);
}

void initWiFi()
{
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");

  if (ssid.length() > 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++)
    {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("\n✅ Conectado à rede Wi-Fi: " + WiFi.SSID());
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    else
    {
      // Se falhar a conexão, limpa credenciais
      Serial.println("\n❌ Falha ao conectar. Limpando credenciais e operando offline.");
      preferences.clear();
      preferences.end();
    }
  }

  activeProtocol = NONE;
}

void resetWiFiCredentials()
{
  // Sai do menu
  inSettingsMenu = false;

  // Limpa as credenciais salvas
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(WARNING_COLOR);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CONFIGURACAO WI-FI", tft.width() / 2, tft.height() / 2 - 40);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Limpando credenciais anteriores...", tft.width() / 2, tft.height() / 2);

  // Limpa NVS e força a escrita
  preferences.clear();
  preferences.end();

  // Limpa a pilha de Wi-Fi antes de iniciar o AP (Resolve o erro 12308)
  WiFi.disconnect(true);

  // Transição: Mostra as informações do AP (SSID e Senha)
  drawAccessPointInfo();

  delay(5000);

  // Entra no Portal de Configuração
  startConfigPortal();
}

void drawSettingsPanel()
{
  // Desenha o painel de settings (o IP e STATUS Wi-Fi são dinâmicas aqui)
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("CONFIGURACOES", 10, 8);
  tft.drawFastHLine(10, 30, tft.width() - 20, ACCENT_COLOR);

  // Exibição de Informações de Rede Dinâmicas
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("STATUS:", 10, 40);
  tft.drawString(WiFi.isConnected() ? "CONECTADO" : "DESCONECTADO", 70, 40);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("SSID:", 10, 55);
  tft.drawString(WiFi.isConnected() ? WiFi.SSID().c_str() : "N/A", 70, 55);

  tft.setTextColor(TFT_YELLOW);
  tft.drawString("IP:", 10, 70);
  tft.drawString(WiFi.isConnected() ? WiFi.localIP().toString() : "0.0.0.0", 70, 70);

  tft.setTextColor(ACCENT_COLOR);
  tft.drawString("PORTA TCP:", 10, 85);
  tft.drawString(String(TCP_PORT), 70, 85);

  tft.drawFastHLine(10, 100, tft.width() - 20, TFT_DARKGREY);

  // Opções de Menu
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED);
  tft.drawString("BTN 1: CONFIGURAR NOVO WI-FI (AP)", 10, 110);
  tft.setTextColor(SUCCESS_COLOR);
  tft.drawString("BTN 2: SAIR DO MENU", 10, 125);
}

int mapButton(int bit)
{
  int buttonNumber = 0;

  // Mapeamento para 16 botões (0-15)
  switch (bit)
  {
  case 0: buttonNumber = 9; break;
  case 1: buttonNumber = 10; break;
  case 2: buttonNumber = 11; break;
  case 3: buttonNumber = 12; break;
  case 4: buttonNumber = 16; break;
  case 5: buttonNumber = 15; break;
  case 6: buttonNumber = 14; break;
  case 7: buttonNumber = 13; break;
  case 8: buttonNumber = 8; break;
  case 9: buttonNumber = 7; break;
  case 10: buttonNumber = 6; break;
  case 11: buttonNumber = 5; break;
  case 12: buttonNumber = 1; break;
  case 13: buttonNumber = 2; break;
  case 14: buttonNumber = 3; break;
  case 15: buttonNumber = 4; break;
  default: buttonNumber = 0; break; // Caso inválido
  }
  return buttonNumber;
}

int readButtons() {
  digitalWrite(latchPin, LOW);
  delayMicroseconds(5);
  digitalWrite(latchPin, HIGH);

  int data = 0;
  for (int i = 0; i < numBits; i++) {
    // Lê o bit atual e coloca na posição correta
    if (digitalRead(dataPin)) {
      data |= (1 << i); 
    }
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(1);
    digitalWrite(clockPin, LOW);
  }
  return data;
}

void checkButtons()
{
  int currentButtonStates = readButtons();

  // --- Lógica de Sequência Temporizada (1-4-8) para o Menu ---
  if (!inSettingsMenu)
  {
    if (sequenceState > 0 && millis() - sequenceTimer > SEQUENCE_TIMEOUT_MS)
    {
      // Timeout: Reinicia a sequência se passar o tempo limite
      sequenceState = 0;
      Serial.println("🔑 Sequência cancelada por timeout.");
      drawStatusMessage("Sequencia 1-2-3 cancelada.");
    }
  }
  // -------------------------------------------------------------

  for (int i = 0; i < numBits; i++)
  {
    int buttonNumber = mapButton(i);

    // Processa apenas na borda de subida (Press Down)
    if (bitRead(currentButtonStates, i) && !bitRead(lastButtonStates, i))
    {
      if (inSettingsMenu)
      {
        handleButtonPress(buttonNumber);
      }
      else if (!inSettingsMenu)
      {

        // 1. Envia a informação normal (ação do botão)
        handleButtonPress(buttonNumber);

        // 2. Tenta acionar a Sequência 1-2-3 (SEM BLOQUEAR)

        // Verifica se a sequência atual é o botão correto
        if (buttonNumber == SEQUENCE_TARGET[sequenceState])
        {

          // Verifica se o tempo expirou
          if (sequenceState > 0 && millis() - sequenceTimer > SEQUENCE_TIMEOUT_MS)
          {
            sequenceState = 0; // Se expirou, reseta.
            Serial.println("🔑 Sequência falhou por tempo.");
          }

          // Tenta avançar o estado da sequência
          if (buttonNumber == SEQUENCE_TARGET[sequenceState])
          {
            sequenceState++;
            if (sequenceState == 1)
            {
              // Inicia o timer no primeiro clique
              sequenceTimer = millis();
              drawStatusMessage("Sequencia: Pressione 2...");
            }
            else if (sequenceState < SEQUENCE_LENGTH)
            {
              // Continua a sequência
              drawStatusMessage(String("Sequencia: Pressione ") + String(SEQUENCE_TARGET[sequenceState]) + "...");
            }

            if (sequenceState == SEQUENCE_LENGTH)
            {
              // Sequência Completa (1-2-3)
              sequenceState = 0;
              inSettingsMenu = true;
              drawSettingsPanel();
              drawStatusMessage("MENU: Configuracoes Abertas.");
              
              // Feedback visual nos LEDs
              manualControl = true;
              for(int j = 0; j < NUM_LEDS; j++) {
                leds[j] = CRGB::Purple;
              }
              FastLED.show();
              delay(300);
              manualControl = false;
            }
          }
        }
        else
        {
          // Botão errado (se estava no meio da sequência)
          if (sequenceState > 0)
          {
            sequenceState = 0;
            Serial.println("🔑 Sequência incorreta. Reset.");
            drawStatusMessage("Sequencia incorreta. Reset.");
          }
        }
      }
    }
  }
  lastButtonStates = currentButtonStates;
}

void handleButtonPress(int buttonNumber)
{
  // Feedback visual nos LEDs quando um botão é pressionado
  if (buttonNumber >= 1 && buttonNumber <= NUM_LEDS) {  // Verifica se está dentro do range
    manualControl = true;
    leds[buttonNumber - 1] = CRGB::White;  // LED branco no botão pressionado
    FastLED.show();
  }
  
  delay(50);  // Pequeno delay para ver o feedback
  
  // =================================================================
  // === LÓGICA DO MENU DE CONFIGURAÇÕES (Ação de clique rápido) =====
  // =================================================================
  if (inSettingsMenu)
  {
    if (buttonNumber == 1)
    {
      resetWiFiCredentials();
    }
    else if (buttonNumber == 2)
    {
      inSettingsMenu = false;
      drawMainInterface();
      drawStatusMessage("Menu Fechado");
    }
    else
    {
      drawStatusMessage("Pressione BTN 1 ou 2");
    }
    manualControl = false;
    return;
  }

  // =================================================================
  // === LÓGICA DE ENVIO DE COMANDO (TELA PRINCIPAL) ==================
  // =================================================================

  String command = "BTN:" + String(buttonNumber);

  if (activeProtocol == USB) // PRIO 1: USB
  {
    Serial.println(command);
    drawStatusMessage("Botao " + String(buttonNumber) + " enviado (USB)");
  }
  else if (activeProtocol == WIFI) // PRIO 2: WI-FI
  {
    client.println(command);
    drawStatusMessage("Botao " + String(buttonNumber) + " enviado (WiFi)");
  }
  else
  {
    drawStatusMessage("Botao " + String(buttonNumber) + " (offline)");
  }

  delay(250);

  // Restaura o estado dos LEDs
  manualControl = false;

  if (activeProtocol == NONE)
  {
    drawStatusMessage("Aguardando conexao...");
  }
  else
  {
    drawStatusMessage("Pronto");
  }
}

void checkConnectionChange()
{
  bool currentOverallConnection = (activeProtocol != NONE);

  if (inSettingsMenu)
  {
    // Se estiver no menu, RETORNA.
    return;
  }
  else
  {
    if (currentOverallConnection != lastOverallConnectionStatus)
    {
      updateConnectionStatus(activeProtocol);
      drawMainInterface();
      manualControl = false; // Volta ao controle automático
    }
  }

  lastOverallConnectionStatus = currentOverallConnection;
}

void updateBatteryLogic() {
  // A lógica agora depende estritamente do protocolo ativo identificado pelo software
  // 
  isUsbConnected = (activeProtocol == USB);

  if (isUsbConnected) {
    // Só entra aqui se o software do PC enviou "CONNECTED"
    // HIGH liga o transistor -> Aterra o pino 8 -> CARGA BLOQUEADA
    digitalWrite(PIN_TP4056_CE, HIGH); 
  } else {
    // Por padrão (Bateria, Fonte ou WiFi), a carga fica LIBERADA
    // LOW desliga o transistor -> Pull-up de 10k ativa o pino 8 -> CARREGANDO
    digitalWrite(PIN_TP4056_CE, LOW); 
  }

  // Leitura da bateria para o display 
  int rawADC = 0;
  for(int i=0; i<10; i++) rawADC += analogRead(PIN_BATT_ADC);
  rawADC /= 10;
  batteryVoltage = (rawADC / 4095.0) * 3.59 * 2.0;
  batteryPercentage = constrain(map(batteryVoltage * 100, 320, 420, 0, 100), 0, 100);
}

// Implementação da função:
void updateBatteryDisplay() {
  // Atualiza APENAS os valores da bateria, sem redesenhar o painel inteiro
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  
  // Atualiza voltagem
  tft.setTextColor(TFT_WHITE);
  tft.fillRect(80, 90, 40, 8, TFT_BLACK); // Limpa área anterior
  tft.drawString(String(batteryVoltage, 2) + "V", 80, 90);
  
  // Atualiza barra de progresso
  tft.drawRect(120, 90, 30, 10, TFT_WHITE);
  int barWidth = map(batteryPercentage, 0, 100, 0, 28);
  tft.fillRect(121, 91, barWidth, 8, batteryPercentage > 20 ? TFT_GREEN : TFT_RED);
  
  // Atualiza porcentagem
  tft.fillRect(155, 90, 25, 8, TFT_BLACK); // Limpa área anterior
  tft.drawString(String(batteryPercentage) + "%", 155, 90);
  
  // Status do USB
  tft.fillRect(100, 105, 90, 8, TFT_BLACK); // Limpa área anterior
  if (isUsbConnected) {
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("SIM (Carga Bloqueada)", 100, 105);
  } else {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("NAO (Usando Bateria)", 100, 105);
  }
  
  // Status do TP4056
  tft.setTextColor(TFT_LIGHTGREY);
  tft.fillRect(100, 120, 90, 8, TFT_BLACK); // Limpa área anterior
  tft.drawString(isUsbConnected ? "DISABLED (0V)" : "ENABLED (3.3V)", 100, 120);
}

void initEncoder() {
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
  
  lastEncoderState = digitalRead(ENCODER_CLK_PIN);
  
  Serial.println("✅ Encoder EC11 inicializado");
}

void checkEncoder() {
  // Verifica o botão do encoder
  handleEncoderButton();
  
  // Se o modo encoder estiver ativo, verifica a rotação
  if (encoderModeActive) {
    handleEncoderRotation();
  }
}

void handleEncoderButton() {
  int btnState = digitalRead(ENCODER_BTN_PIN);
  
  // Detecção de borda de descida (botão pressionado)
  if (btnState == LOW && encoderBtnLastState == HIGH) {
    unsigned long now = millis();
    
    // Debounce
    if (now - lastEncoderBtnPress > ENCODER_DEBOUNCE_DELAY) {
      // Alterna o modo do encoder
      encoderModeActive = !encoderModeActive;
      
      if (encoderModeActive) {
        // Entrou no modo de ajuste de brilho
        manualControl = true;
        effectActive = false;
        
        // Feedback visual: LEDs em amarelo
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
        FastLED.show();
        
        drawStatusMessage("Modo Brilho: Gire o encoder");
        Serial.println("🎚️ Modo ajuste de brilho ATIVADO");
      } else {
        // Saiu do modo de ajuste de brilho
        manualControl = false;
        
        drawStatusMessage("Modo Brilho Desativado");
        Serial.println("🎚️ Modo ajuste de brilho DESATIVADO");
        
        // Atualiza o display com o brilho atual
        updateBrightnessDisplay();
      }
      
      lastEncoderBtnPress = now;
    }
  }
  
  encoderBtnLastState = btnState;
}

void handleEncoderRotation() {
  int currentState = digitalRead(ENCODER_CLK_PIN);
  
  if (currentState != lastEncoderState) {
    // Houve uma mudança no encoder
    int dtState = digitalRead(ENCODER_DT_PIN);
    
    if (dtState != currentState) {
      // Sentido horário (aumenta brilho)
      if (LED_BRIGHTNESS < 250) {
        LED_BRIGHTNESS += 5;
        FastLED.setBrightness(LED_BRIGHTNESS);
        FastLED.show();
        
        // Feedback visual: LEDs mais brilhantes
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
        FastLED.show();
        
        Serial.print("🔆 Brilho aumentado para: ");
        Serial.println(LED_BRIGHTNESS);
        drawStatusMessage("Brilho: " + String(LED_BRIGHTNESS));
      }
    } else {
      // Sentido anti-horário (diminui brilho)
      if (LED_BRIGHTNESS > 5) {
        LED_BRIGHTNESS -= 5;
        FastLED.setBrightness(LED_BRIGHTNESS);
        FastLED.show();
        
        // Feedback visual: LEDs menos brilhantes
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
        FastLED.show();
        
        Serial.print("🔅 Brilho diminuido para: ");
        Serial.println(LED_BRIGHTNESS);
        drawStatusMessage("Brilho: " + String(LED_BRIGHTNESS));
      }
    }
    
    // Atualiza o display com o brilho atual
    updateBrightnessDisplay();
  }
  
  lastEncoderState = currentState;
}

void updateBrightnessDisplay() {
  // Atualiza apenas a parte do brilho no painel da bateria
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  
  // Limpa a área e desenha o valor do brilho
  tft.fillRect(80, 135, 60, 8, TFT_BLACK);
  tft.drawString(String(LED_BRIGHTNESS), 80, 135);
}

// =========================================================================
// === SETUP e LOOP ========================================================
// =========================================================================

void setup()
{
  Serial.begin(115200);
  preferences.begin(PREFS_KEY, false);

  initializeDisplay();
  initButtons();
  initLEDs();
  initEncoder();
  pinMode(PIN_TP4056_CE, OUTPUT);
  digitalWrite(PIN_TP4056_CE, LOW);
  drawBootScreen();
  
  // Animação dos LEDs durante a inicialização
  manualControl = true;
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Yellow;
    FastLED.show();
    delay(100);
    leds[i] = CRGB::Black;
  }
  FastLED.show();
  manualControl = false;
  
  delay(2500);

  initWiFi();

  // Só tenta iniciar TCP e UDP se o Wi-Fi se conectou
  if (WiFi.status() == WL_CONNECTED)
  {
    serverTCP.begin();
    Serial.print("Servidor TCP iniciado na porta: ");
    Serial.println(TCP_PORT);

    Udp.begin(UDP_SEARCH_PORT);
    Serial.print("Escutando UDP na porta: ");
    Serial.println(UDP_SEARCH_PORT);
  }

  drawMainInterface();

  Serial.println("🎮 Esp32 DECK INICIADO");
  Serial.println("✅ LEDs WS2812B configurados");
  Serial.println("✅ Encoder EC11 configurado");
  Serial.print("📊 Quantidade de LEDs: ");
  Serial.println(NUM_LEDS);
  Serial.print("🎯 Quantidade de Botões: ");
  Serial.println(numBits);
  Serial.print("🔆 Brilho inicial: ");
  Serial.println(LED_BRIGHTNESS);
  Serial.println("==========================================");
  Serial.println("🎚️ CONTROLE DO ENCODER:");
  Serial.println("  • Clique: Ativa/desativa modo brilho");
  Serial.println("  • Gire: Ajusta brilho dos LEDs");
  Serial.println("==========================================");
  Serial.println("📝 COMANDOS LED DISPONÍVEIS:");
  Serial.println("==========================================");
  Serial.println("COMANDOS INDIVIDUAIS:");
  Serial.println("  LED:0:FF0000      // LED 0 vermelho");
  Serial.println("  LED:1:00FF00      // LED 1 verde");
  Serial.println("  LED:2:0000FF      // LED 2 azul");
  Serial.println("  LED:3:FFFF00      // LED 3 amarelo");
  Serial.println("  LED:4:FF00FF      // LED 4 rosa");
  Serial.println("  LED:5:00FFFF      // LED 5 ciano");
  Serial.println("  LED:6:FF8800      // LED 6 laranja");
  Serial.println("  LED:7:8800FF      // LED 7 roxo");
  Serial.println("  LED:8:FFFFFF      // LED 8 branco");
  Serial.println("  LED:9:00FF88      // LED 9 verde água");
  Serial.println("  LED:10:FF0088     // LED 10 rosa escuro");
  Serial.println("  LED:11:88FF00     // LED 11 verde limão");
  Serial.println("  LED:12:0088FF     // LED 12 azul claro");
  Serial.println("  LED:13:880000     // LED 13 vermelho escuro");
  Serial.println("  LED:14:008800     // LED 14 verde escuro");
  Serial.println("  LED:15:000088     // LED 15 azul escuro");
  Serial.println("");
  Serial.println("COMANDOS GLOBAIS:");
  Serial.println("  ALL_LED:ON        // Liga todos branco");
  Serial.println("  ALL_LED:OFF       // Desliga todos");
  Serial.println("  ALL_LED:RAINBOW   // Efeito arco-íris");
  Serial.println("  ALL_LED:BLINK     // Efeito piscante");
  Serial.println("  ALL_LED:WAVE_BLUE // Onda azul");
  Serial.println("  ALL_LED:FIRE      // Efeito fogo");
  Serial.println("  ALL_LED:GRADIENT  // Gradiente");
  Serial.println("  ALL_LED:TWINKLE   // Estrelas");
  Serial.println("  ALL_LED:ALERT     // Alerta");
  Serial.println("==========================================");
  Serial.println("🔧 TESTE: Envie 'LED:0:FF0000' no Monitor Serial");
  Serial.println("🔧 TESTE: Envie 'ALL_LED:OFF' para desligar");
  Serial.println("==========================================");
}

void loop()
{
  // Atualiza a lógica da bateria
  updateBatteryLogic();

  static unsigned long lastScreenUpdate = 0;
  static unsigned long lastEncoderCheck = 0;
  
  // Verifica o encoder mais frequentemente (a cada 10ms)
  if (millis() - lastEncoderCheck > 10) {
    checkEncoder();
    lastEncoderCheck = millis();
  }

  if (millis() - lastScreenUpdate > 1000) { // Atualiza apenas os valores a cada 1 segundo
    updateBatteryDisplay();  // <-- Usa a nova função que atualiza apenas os valores
    lastScreenUpdate = millis();
  }

  // A verificação de timeout da sequência
  if (!wifiConfigMode && sequenceState > 0 && millis() - sequenceTimer > SEQUENCE_TIMEOUT_MS)
  {
    sequenceState = 0;
    Serial.println("🔑 Sequência cancelada por timeout no Loop.");
    drawStatusMessage("Sequencia 1-2-3 cancelada.");
  }

  if (wifiConfigMode)
  {
    dnsServer.processNextRequest();
    server.handleClient();
    // setStatusLEDs() é chamado por updateLEDs() quando necessário
  }
  else
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      if (!wifiStatusHandled)
      {
        // A conexão Wi-Fi acabou de ser estabelecida!
        Serial.println("🌐 Conexão Wi-Fi estabelecida. Atualizando interface.");
        drawMainInterface(); // Redesenha para mostrar o IP e status de rede
        drawStatusMessage(String("IP: ") + WiFi.localIP().toString());
        wifiStatusHandled = true; // Marca como handled (tratado)
        manualControl = false;
      }
    }
    else
    {
      // Se a conexão cair, resetamos a flag para que a próxima conexão dispare a atualização
      wifiStatusHandled = false;
    }
    // =========================================================

    checkButtons();
    checkSerialCommands();

    if (WiFi.status() == WL_CONNECTED)
    {
      checkUdpSearch();

      if (!client || !client.connected())
      {
        WiFiClient newClient = serverTCP.available();
        if (newClient)
        {
          if (client)
            client.stop();
          client = newClient;
          Serial.println("🌐 Cliente Python conectado via Wi-Fi!");
          if (activeProtocol != USB)
          {
            activeProtocol = WIFI; // SÓ SETA WIFI QUANDO O CLIENTE TCP CONECTA
            manualControl = false;
          }
        }
      }

      if (client.connected())
      {
        while (client.available())
        {
          String msg = client.readStringUntil('\n');
          msg.trim();
          
          // Processa comandos LED
          if (msg.startsWith("LED:") || msg.startsWith("ALL_LED:")) {
            processLedCommand(msg);
          }
          else if (msg == "PING") {
            client.println("PONG");
          }
          else if (msg == "DISCONNECT") {
            client.stop();
            Serial.println("🌐 Cliente Wi-Fi desconectado.");
            if (activeProtocol == WIFI) {
              activeProtocol = NONE;
              manualControl = false;
            }
          }
        }
      }
    }

    // Atualiza efeitos de LED contínuos se estiverem ativos
    if (effectActive) {
      updateEffect();
    }
    
    // Atualiza LEDs de status (se não houver controle manual ou efeito)
    updateLEDs();
    
    checkConnectionChange();
  }

  delay(30);
}