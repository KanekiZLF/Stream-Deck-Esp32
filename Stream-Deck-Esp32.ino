#include <TFT_eSPI.h>  // Biblioteca TFT_eSPI
#include <SPI.h>
#include "commands.h" // Inclui o arquivo de comandos

// Cria um objeto do display
TFT_eSPI tft = TFT_eSPI(); 

const int dataPin = 17;   /* Q7 */
const int clockPin = 21;  /* CP */
const int latchPin = 22;  /* PL */

const int numBits = 8;    /* Número de bits (botões) */

// Estado dos botões
bool buttonPressed[numBits] = {false}; // Controla se o botão foi "clicado"
bool lastButtonState[numBits] = {false}; // Estado anterior dos botões

void setup() {
  Serial.begin(115200);  // Inicializa o monitor serial
  pinMode(dataPin, INPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin, OUTPUT);

  // Inicia o display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  
  // Texto para centralizar
  const char* text = " Welcome to my Device !";

  // Calcula a posição central
  int16_t x = (tft.width() - tft.textWidth(text)) / 2;  // Centraliza no eixo X
  int16_t y = (tft.height() - tft.fontHeight()) / 2;    // Centraliza no eixo Y

  // Exibe o texto no centro
  tft.setCursor(x, y);
  tft.print(text);
}

void loop() {
  // Atualizar os estados dos botões do registrador
  digitalWrite(latchPin, LOW);   // Carregar os dados no registrador
  digitalWrite(latchPin, HIGH);

  for (int i = 0; i < numBits; i++) {
    int currentBit = digitalRead(dataPin); // Ler o estado atual do botão

    // Verifica se houve uma mudança de estado
    if (currentBit == HIGH && lastButtonState[i] == LOW && !buttonPressed[i]) {
      // O botão foi pressionado
      buttonPressed[i] = true;
      Serial.print("Botão pressionado: ");
      Serial.println(i + 1);

      // Define a função do botão/ação
      int btN = 0;
      switch (i) {
        case 4:
          btN = 1;
          command(1); // Chama o comando 1
          break;

        case 5:
          btN = 2;
          command(2); // Chama o comando 2
          break;

        case 6:
          btN = 3;
          break;

        case 7:
          btN = 4;
          break;

        case 3:
          btN = 5;
          break;

        case 2:
          btN = 6;
          break;

        case 1:
          btN = 7;
          break;

        case 0:
          btN = 8;
          break;
      }

      // Atualizar o display com o botão pressionado
      tft.fillScreen(TFT_BLACK);
      String message = "Botao: " + String(btN);
      tft.drawString(message, 10, 20); // Exibe a mensagem
    }

    // Verifica se o botão foi solto
    if (currentBit == LOW && buttonPressed[i]) {
      buttonPressed[i] = false;
    }

    // Gera o clock para o próximo bit
    digitalWrite(clockPin, HIGH);
    digitalWrite(clockPin, LOW);

    // Atualizar o estado anterior
    lastButtonState[i] = currentBit;
  }

  delay(10); // Pausa pequena para estabilidade
}
