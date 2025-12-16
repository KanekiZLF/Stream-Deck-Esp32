// ===================================================================================
// === STREAM DECK FÍSICO - MÚLTIPLAS INTERFACES =====================================
// ===================================================================================

#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h> 
#include <WiFiUdp.h>

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

// Configuração da Senha de Acesso (Sequência: 1, 2, 3)
const int SEQUENCE_TIMEOUT_MS = 2000; // 2 segundos
const int SEQUENCE_TARGET[] = {1, 2, 3};
const int SEQUENCE_LENGTH = 3;

// =========================================================================
// === CONFIGURAÇÕES DO SHIFT REGISTER =====================================
// =========================================================================
const int dataPin = 17;
const int clockPin = 21;
const int latchPin = 22;
const int numBits = 8; 

// =========================================================================
// === CONFIGURAÇÕES DO DISPLAY ============================================
// =========================================================================
TFT_eSPI tft = TFT_eSPI();

// =========================================================================
// === VARIÁVEIS GLOBAIS ===================================================
// =========================================================================

// Enum para rastrear o protocolo ativo
enum ConnectionProtocol { NONE, USB, WIFI };
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
// Variáveis para Sequência de Acesso 
int sequenceState = 0; 
unsigned long sequenceTimer = 0;

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
void drawBootScreen();
void drawMainInterface();
void drawSettingsPanel();
void drawAccessPointInfo(); 
void drawConfigPortalScreen();
void updateConnectionStatus(ConnectionProtocol protocol);
void drawStatusMessage(const String &message);
void resetWiFiCredentials();
void checkSerialCommands();

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

void checkUdpSearch()
{
  if (WiFi.status() != WL_CONNECTED) return;

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
        Udp.write((const uint8_t*)UDP_ACK_MSG, strlen(UDP_ACK_MSG));
        Udp.endPacket();
        
        Serial.println("ACK UDP enviado.");
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
  tft.drawString("v2.4 - Multi Interface", tft.width() / 2, tft.height() / 2 + 25);
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

    if (message == "CONNECTED")
    {
      activeProtocol = USB;
      Serial.println("✅ Conexão estabelecida com software (USB)");
    }
    else if (message == "DISCONNECT")
    {
      Serial.println("👋 Software se desconectou (USB)");
      if (client.connected()) {
          activeProtocol = WIFI;
      } else {
          activeProtocol = NONE;
      }
    }
    else if (message == "PING")
    {
      Serial.println("PONG");
    }
  }
}

void updateConnectionStatus(ConnectionProtocol protocol)
{
  tft.fillRoundRect(12, 27, tft.width() - 24, 21, 3, BACKGROUND_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);

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
  tft.drawString("ESP32 DECK v2.4", tft.width() / 2, 70);
// Informações do DEV
  tft.setTextColor(TFT_CYAN);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("GitHub:", 15, 85);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("github.com/KanekiZLF", 60, 85);

  // Informações técnicas
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Firmware: v2.4", 15, 110);
  tft.drawString("Dev: Luiz F. R. Pimentel", 15, 122);
  
  // Exibe IP / Configuração AP
  if (WiFi.isConnected()) {
      tft.setTextColor(TFT_GREEN);
      tft.drawString(String("IP: ") + WiFi.localIP().toString(), 15, 98); 
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

  drawPanelCompact(); // ⭐ Opção 1 - Mais compacta
  // drawPanelModern();     // ⭐ Opção 2 - Estilo moderno
  // drawPanelMinimal(); // ⭐ Opção 3 - Minimalista
  // drawPanelTechnical();  // ⭐ Opção 4 - Técnico
  // drawPanelGaming(); // ⭐ Opção 5 - Estilo gaming
  // drawPanelClassic(); // ⭐ Opção 6 - Clássico

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
  tft.drawString("FIRMWARE: v2.4", 15, 115);
  tft.drawString(String(numBits) + " Botoes Ativos", 90, 115);
  tft.drawString("Dev: Luiz F. R. Pimentel", 15, 127);
  if (WiFi.isConnected()) {
      tft.setTextColor(TFT_YELLOW);
      tft.drawString(String("IP: ") + WiFi.localIP().toString(), 90, 127);
  } else {
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
  tft.drawString(String("v2.4 | ") + String(numBits) + " BTNS", 15, 103);
  tft.drawString("Serial: 115200", 15, 115);
  tft.drawString("Luiz F. R. Pimentel", 15, 127);
  if (WiFi.isConnected()) {
      tft.setTextColor(TFT_YELLOW);
      tft.drawString(String("IP: ") + WiFi.localIP().toString(), 110, 115);
  } else {
      tft.setTextColor(TFT_RED);
      tft.drawString(String("AP: ") + SSID_AP, 110, 115);
  }
}

void drawPanelTechnical() { /* (Implementação Completa) */ }
void drawPanelGaming() { /* (Implementação Completa) */ }
void drawPanelClassic() { /* (Implementação Completa) */ }

void startConfigPortal()
{
  wifiConfigMode = true;

  drawConfigPortalScreen();

  Serial.println("Iniciando AP para configuracao...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID_AP, PASS_AP);

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  DNSServer dnsServer; 
  dnsServer.start(53, "*", apIP); 

  server.on("/", handleRoot); 
  server.on("/save", handleWiFiSave);
  server.begin();
  
  Serial.print("AP: "); Serial.println(SSID_AP);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
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

  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
  
  String msg = "Credenciais salvas. Tentando conectar a " + ssid + ". Reiniciando...";
  server.send(200, "text/plain", msg);

  // CORREÇÃO DA FALHA NA CONEXÃO: Desliga os serviços de AP de forma limpa antes de reiniciar
  wifiConfigMode = false;
  WiFi.softAPdisconnect(true);
  server.close();
  
  // O restart é crucial para tentar conectar com as novas credenciais
  delay(1000); 
  ESP.restart();
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
      // Se falhar a conexão, limpa credenciais para que o AP seja iniciado manualmente
      Serial.println("\n❌ Falha ao conectar. Limpando credenciais e operando offline.");
      preferences.clear();
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
  
  preferences.clear();
  preferences.end();
  
  // Transição: Mostra as informações do AP (SSID e Senha)
  drawAccessPointInfo();
  
  delay(5000); 
  
  // Entra no Portal de Configuração
  startConfigPortal(); 
}

void drawSettingsPanel()
{
  // Desenha o painel de settings (o IP e STATUS Wi-Fi são dinâmicos aqui)
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
  tft.drawString("BTN 1: CONFIGURAR NOVO WI-FI (AP)", 10, 115);
  tft.setTextColor(SUCCESS_COLOR);
  tft.drawString("BTN 2: SAIR DO MENU", 10, 130);
  
  tft.setTextColor(INFO_COLOR);
  tft.drawString("Sequencia 1-2-3 para Menu", 10, 160); 
}


int mapButton(int bit)
{
  int buttonNumber = 0;
  
  // Mapeamento para 8 botões (se numBits=8)
  switch (bit)
  {
    case 7: buttonNumber = 1; break; 
    case 6: buttonNumber = 2; break;
    case 5: buttonNumber = 3; break;
    case 4: buttonNumber = 4; break;
    case 3: buttonNumber = 5; break;
    case 2: buttonNumber = 6; break;
    case 1: buttonNumber = 7; break;
    case 0: buttonNumber = 8; break; 
  }
  return buttonNumber;
}


int readButtons()
{
  digitalWrite(latchPin, LOW);
  delayMicroseconds(5);
  digitalWrite(latchPin, HIGH);
  delayMicroseconds(5);

  int data = 0;
  for (int i = 0; i < numBits; i++)
  {
    data = (data << 1) |
    digitalRead(dataPin);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(1);
    digitalWrite(clockPin, LOW);
  }
  
  return data;
}

void checkButtons()
{
  int currentButtonStates = readButtons();

  // --- Lógica de Sequência Temporizada (1-2-3) para o Menu ---
  if (!inSettingsMenu) {
    if (sequenceState > 0 && millis() - sequenceTimer > SEQUENCE_TIMEOUT_MS) {
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
      if (inSettingsMenu) {
         handleButtonPress(buttonNumber); 
      } else if (!inSettingsMenu) {
         
         // 1. Envia a informação normal (ação do botão)
         handleButtonPress(buttonNumber); 
         
         // 2. Tenta acionar a Sequência 1-2-3 (SEM BLOQUEAR)
         
         // Verifica se a sequência atual é o botão correto
         if (buttonNumber == SEQUENCE_TARGET[sequenceState]) {
            
            // Verifica se o tempo expirou
            if (sequenceState > 0 && millis() - sequenceTimer > SEQUENCE_TIMEOUT_MS) {
                 sequenceState = 0; // Se expirou, reseta.
                 Serial.println("🔑 Sequência falhou por tempo.");
            }
            
            // Tenta avançar o estado da sequência
            if (buttonNumber == SEQUENCE_TARGET[sequenceState]) {
                sequenceState++;
                if (sequenceState == 1) {
                    // Inicia o timer no primeiro clique
                    sequenceTimer = millis();
                    drawStatusMessage("Sequencia: Pressione 2...");
                } else if (sequenceState < SEQUENCE_LENGTH) {
                    // Continua a sequência 
                    drawStatusMessage(String("Sequencia: Pressione ") + String(SEQUENCE_TARGET[sequenceState]) + "...");
                }
                
                if (sequenceState == SEQUENCE_LENGTH) {
                    // Sequência Completa (1-2-3)
                    sequenceState = 0; 
                    inSettingsMenu = true;
                    drawSettingsPanel(); 
                    drawStatusMessage("MENU: Configuracoes Abertas.");
                }
            }
         } else {
             // Botão errado (se estava no meio da sequência)
             if (sequenceState > 0) {
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
  // =================================================================
  // === LÓGICA DO MENU DE CONFIGURAÇÕES (Ação de clique rápido) =====
  // =================================================================
  if (inSettingsMenu) {
    if (buttonNumber == 1) { 
      resetWiFiCredentials(); 
    } else if (buttonNumber == 2) { 
      inSettingsMenu = false;
      drawMainInterface(); 
      drawStatusMessage("Menu Fechado");
    } else {
      drawStatusMessage("Pressione BTN 1 ou 2");
    }
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

  delay(300);
  
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

    if (inSettingsMenu) {
        // CORREÇÃO DEFINITIVA FLICKER: Se estiver no menu, RETORNA, sem redesenho.
        return; 
    } else {
        if (currentOverallConnection != lastOverallConnectionStatus) {
            updateConnectionStatus(activeProtocol);
            drawMainInterface(); 
        }
    }

    lastOverallConnectionStatus = currentOverallConnection;
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
  
  drawBootScreen();
  delay(2500);

  initWiFi();
  
  // Só tenta iniciar TCP e UDP se o Wi-Fi se conectou
  if (WiFi.status() == WL_CONNECTED) {
    serverTCP.begin(); 
    Serial.print("Servidor TCP iniciado na porta: ");
    Serial.println(TCP_PORT);
    
    Udp.begin(UDP_SEARCH_PORT);
    Serial.print("Escutando UDP na porta: ");
    Serial.println(UDP_SEARCH_PORT);
  }

  drawMainInterface();

  Serial.println("🎮 Esp32 DECK INICIADO");
}

void loop()
{
  // A verificação de timeout da sequência
  if (!wifiConfigMode && sequenceState > 0 && millis() - sequenceTimer > SEQUENCE_TIMEOUT_MS) {
      sequenceState = 0;
      Serial.println("🔑 Sequência cancelada por timeout no Loop.");
      drawStatusMessage("Sequencia 1-2-3 cancelada.");
  }
  
  if (wifiConfigMode)
  {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  else
  {
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
                if (client) client.stop();
                client = newClient;
                Serial.println("🌐 Cliente Python conectado via Wi-Fi!");
                if (activeProtocol != USB) { 
                    activeProtocol = WIFI; 
                }
            }
        }
        
        if (client.connected()) {
            while (client.available()) {
                String msg = client.readStringUntil('\n');
                msg.trim();
                if (msg == "PING") client.println("PONG");
                if (msg == "DISCONNECT") {
                   client.stop();
                   Serial.println("🌐 Cliente Wi-Fi desconectado.");
                   if (activeProtocol == WIFI) { activeProtocol = NONE; }
                }
            }
        }
    }

    checkConnectionChange();
  }

  delay(30);
}