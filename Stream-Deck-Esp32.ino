// ===================================================================================
// === STREAM DECK FÍSICO - MÚLTIPLAS INTERFACES =====================================
// ===================================================================================

#include <TFT_eSPI.h>
#include <SPI.h>

// =========================================================================
// === CONFIGURAÇÕES DO SHIFT REGISTER =====================================
// =========================================================================
const int dataPin = 17;  // Q7
const int clockPin = 21; // CP
const int latchPin = 22; // PL
const int numBits = 8;   // 8 botões

// =========================================================================
// === CONFIGURAÇÕES DO DISPLAY ============================================
// =========================================================================
TFT_eSPI tft = TFT_eSPI();

// =========================================================================
// === VARIÁVEIS GLOBAIS ===================================================
// =========================================================================
bool isConnected = false;
byte lastButtonStates = 0;

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
byte readButtons();
int mapButton(int bit);
void checkButtons();
void handleButtonPress(int buttonNumber);
void drawBootScreen();
void drawMainInterface();
void updateConnectionStatus(bool connected);
void checkSerialCommands();

// ✅ NOVAS OPÇÕES DE INTERFACE (ESCOHA UMA DESSAS):
void drawPanelCompact();   // ⭐ Opção 1 - Mais compacta
void drawPanelModern();    // ⭐ Opção 2 - Estilo moderno
void drawPanelMinimal();   // ⭐ Opção 3 - Minimalista
void drawPanelTechnical(); // ⭐ Opção 4 - Técnico
void drawPanelGaming();    // ⭐ Opção 5 - Estilo gaming
void drawPanelClassic();   // ⭐ Opção 6 - Clássico

void drawStatusMessage(const String &message);

// =========================================================================
// === SETUP ===============================================================
// =========================================================================
void setup()
{
  Serial.begin(115200);

  initializeDisplay();
  initButtons();

  // Mostrar tela de inicialização
  drawBootScreen();
  delay(2500);

  // Mostrar interface principal
  drawMainInterface();

  Serial.println("🎮 Esp32 DECK INICIADO");
  Serial.println("📡 Aguardando conexão...");
  Serial.println("✅ Firmware: v2.4 - Multi Interface");
  Serial.println("👨‍💻 Desenvolvedor: Luiz F. R. Pimentel");
}

// =========================================================================
// === LOOP PRINCIPAL ======================================================
// =========================================================================
void loop()
{
  // 1. Verificar botões pressionados
  checkButtons();

  // 2. Verificar comandos serial
  checkSerialCommands();

  delay(30);
}

// =========================================================================
// ✅ SISTEMA DE COMANDOS SERIAL ===========================================
// =========================================================================
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
      updateConnectionStatus(true);
      Serial.println("✅ Conexão estabelecida com software");
    }
    else if (message == "DISCONNECT")
    {
      updateConnectionStatus(false);
      Serial.println("👋 Software se desconectou - aguardando reconexão...");
    }
    else if (message == "PING")
    {
      Serial.println("PONG");
    }
    else if (message.startsWith("BTN:"))
    {
      // Comando de botão
    }
  }
}

// =========================================================================
// === ATUALIZA STATUS DA CONEXÃO ==========================================
// =========================================================================
void updateConnectionStatus(bool connected)
{
  isConnected = connected;

  tft.fillRoundRect(12, 27, tft.width() - 24, 21, 3, BACKGROUND_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);

  if (connected)
  {
    tft.setTextColor(CONNECTED_COLOR);
    tft.drawString("CONECTADO - PRONTO PARA USO", tft.width() / 2, 35);
    Serial.println("🎉 STATUS: Conectado ao software Python");
  }
  else
  {
    tft.setTextColor(DISCONNECTED_COLOR);
    tft.drawString("DESCONECTADO - AGUARDANDO", tft.width() / 2, 35);
    Serial.println("💤 STATUS: Desconectado - aguardando software");
  }
}

// =========================================================================
// ✅ OPÇÃO 1: INTERFACE COMPACTA ==========================================
// =========================================================================
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
  tft.drawString("Firmware: v2.4", 15, 98);
  tft.drawString("8 Botoes Ativos", 15, 110);
  tft.drawString("Dev: Luiz F. R. Pimentel", 15, 122);
}

// =========================================================================
// ✅ OPÇÃO 2: INTERFACE MODERNA ===========================================
// =========================================================================
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
  tft.setTextColor(isConnected ? SUCCESS_COLOR : DISCONNECTED_COLOR);
  tft.drawString(isConnected ? "> ONLINE" : "> OFFLINE", 15, 102);

  // Informações em grid
  tft.setTextColor(TFT_WHITE);
  tft.drawString("FIRMWARE: v2.4", 15, 115);
  tft.drawString("BUTTONS: 8", 90, 115);
  tft.drawString("DEV: LUIZ", 15, 127);
}

// =========================================================================
// ✅ OPÇÃO 3: INTERFACE MINIMALISTA =======================================
// =========================================================================
void drawPanelMinimal()
{
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  // Apenas informações essenciais
  tft.setTextColor(ACCENT_COLOR);
  tft.drawString("ESP32 DECK", 15, 70);

  tft.drawFastHLine(10, 82, tft.width() - 20, ACCENT_COLOR);

  // Status minimalista
  tft.setTextColor(isConnected ? SUCCESS_COLOR : DISCONNECTED_COLOR);
  tft.drawString(isConnected ? "SISTEMA ATIVO" : "AGUARDANDO", 15, 90);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("v2.4 | 8 BTNS", 15, 103);
  tft.drawString("Serial: 115200", 15, 115);
  tft.drawString("Luiz F. R. Pimentel", 15, 127);
}

// =========================================================================
// ✅ OPÇÃO 4: INTERFACE TÉCNICA ===========================================
// =========================================================================
void drawPanelTechnical()
{
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  // Painel estilo técnico
  tft.fillRect(10, 65, tft.width() - 20, 70, TFT_DARKGREY);
  tft.drawRect(10, 65, tft.width() - 20, 70, ACCENT_COLOR);

  // Header
  tft.setTextColor(ACCENT_COLOR);
  tft.drawString("SYSTEM STATUS", 15, 72);
  tft.drawFastHLine(10, 82, tft.width() - 20, ACCENT_COLOR);

  // Status técnico
  tft.setTextColor(isConnected ? SUCCESS_COLOR : WARNING_COLOR);
  tft.drawString(isConnected ? "STATUS: OPERACIONAL" : "STATUS: STAND BY", 15, 90);

  // Especificações
  tft.setTextColor(TFT_WHITE);
  tft.drawString("HW: ESP32+74HC165", 15, 102);
  tft.drawString("PROTO: SERIAL", 15, 114);
  tft.drawString("BAUD: 115200", 15, 126);
}

// =========================================================================
// ✅ OPÇÃO 5: INTERFACE GAMING ============================================
// =========================================================================
void drawPanelGaming()
{
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  // Fundo estilo gaming
  tft.fillRoundRect(10, 65, tft.width() - 20, 70, 2, TFT_DARKGREY);
  tft.drawRoundRect(10, 65, tft.width() - 20, 70, 2, TFT_CYAN);

  // Título gaming
  tft.setTextColor(TFT_CYAN);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(">>> STREAM DECK <<<", tft.width() / 2, 72);

  // Status gaming
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(isConnected ? TFT_GREEN : TFT_RED);
  tft.drawString(isConnected ? "[ LIVE ] SISTEMA ONLINE" : "[ WAIT ] AGUARDANDO", 15, 90);

  // Stats
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("FIRMWARE: v2.4 GAMING", 15, 104);
  tft.drawString("CONTROLES: 8/8 ATIVOS", 15, 116);
  tft.drawString("BY: KANEKIZLF", 15, 128);
}

// =========================================================================
// ✅ OPÇÃO 6: INTERFACE CLÁSSICA ==========================================
// =========================================================================
void drawPanelClassic()
{
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  // Estilo clássico com bordas
  tft.fillRoundRect(10, 65, tft.width() - 20, 70, 8, TFT_DARKGREY);
  tft.drawRoundRect(10, 65, tft.width() - 20, 70, 8, TFT_WHITE);
  tft.drawRoundRect(11, 66, tft.width() - 22, 68, 8, TFT_DARKGREY);

  // Título centralizado
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("ESP32 DECK CONTROLLER", tft.width() / 2, 72);

  // Informações organizadas
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(isConnected ? TFT_GREEN : TFT_ORANGE);
  tft.drawString(isConnected ? "Conectado" : "Desconectado", 20, 90);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString("Versao: 2.4", 20, 103);
  tft.drawString("Botoes: 8", 85, 103);
  tft.drawString("Desenv: Luiz Pimentel", 20, 116);
}

// =========================================================================
// === FUNÇÕES PRINCIPAIS (MANTIDAS) ======================================
// =========================================================================
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

byte readButtons()
{
  digitalWrite(latchPin, LOW);
  delayMicroseconds(5);
  digitalWrite(latchPin, HIGH);
  delayMicroseconds(5);

  byte data = 0;
  for (int i = 0; i < numBits; i++)
  {
    data = (data << 1) | digitalRead(dataPin);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(1);
    digitalWrite(clockPin, LOW);
  }

  return data;
}

int mapButton(int bit)
{
  int buttonNumber = 0;
  switch (bit)
  {
  case 0:
    buttonNumber = 4;
    break;
  case 1:
    buttonNumber = 3;
    break;
  case 2:
    buttonNumber = 2;
    break;
  case 3:
    buttonNumber = 1;
    break;
  case 4:
    buttonNumber = 5;
    break;
  case 5:
    buttonNumber = 6;
    break;
  case 6:
    buttonNumber = 7;
    break;
  case 7:
    buttonNumber = 8;
    break;
  }
  return buttonNumber;
}

void checkButtons()
{
  byte currentButtonStates = readButtons();

  for (int i = 0; i < numBits; i++)
  {
    if (bitRead(currentButtonStates, i) && !bitRead(lastButtonStates, i))
    {
      int buttonNumber = mapButton(i);

      Serial.print("🎮 BOTÃO PRESSIONADO: ");
      Serial.println(buttonNumber);

      handleButtonPress(buttonNumber);
    }
  }
  lastButtonStates = currentButtonStates;
}

void handleButtonPress(int buttonNumber)
{
  if (isConnected)
  {
    Serial.print("BTN:");
    Serial.println(buttonNumber);
    drawStatusMessage("Botao " + String(buttonNumber) + " enviado");
  }
  else
  {
    Serial.println("❌ Não conectado - comando não enviado");
    drawStatusMessage("Botao " + String(buttonNumber) + " (offline)");
  }

  delay(300);

  if (!isConnected)
  {
    drawStatusMessage("Aguardando conexao...");
  }
  else
  {
    drawStatusMessage("Pronto");
  }
}

void drawBootScreen()
{
  tft.fillScreen(BACKGROUND_COLOR);

  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ESP32 STREAM DECK", tft.width() / 2, tft.height() / 2 - 30);

  // Loading bar
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
  updateConnectionStatus(false);

  tft.drawFastHLine(10, 55, tft.width() - 20, TFT_DARKGREY);

  // ✅ ESCOLHA SUA INTERFACE PREFERIDA AQUI:
  // ⭐ DESCOMENTE APENAS UMA DAS LINHAS ABAIXO:

  drawPanelCompact(); // ⭐ Opção 1 - Mais compacta
  // drawPanelModern();     // ⭐ Opção 2 - Estilo moderno
  // drawPanelMinimal();    // ⭐ Opção 3 - Minimalista
  // drawPanelTechnical();  // ⭐ Opção 4 - Técnico
  // drawPanelGaming();     // ⭐ Opção 5 - Estilo gaming
  // drawPanelClassic();    // ⭐ Opção 6 - Clássico

  // Área de mensagens
  tft.drawFastHLine(10, 150, tft.width() - 20, TFT_DARKGREY);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextDatum(BC_DATUM);
}

void drawStatusMessage(const String &message)
{
  tft.fillRect(10, 155, tft.width() - 20, 30, BACKGROUND_COLOR);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  if (message.length() > 20)
  {
    tft.drawString(message.substring(0, 20), 15, 160);
    tft.drawString(message.substring(20), 15, 172);
  }
  else
  {
    tft.drawString(message, 15, 165);
  }
}