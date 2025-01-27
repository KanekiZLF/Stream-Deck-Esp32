#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>

// Função que executa os comandos
void command(int cmd) {
  switch (cmd) {
    case 1:
      Serial.println("Comando 1 recebido!");
      break;
    case 2:
      Serial.println("Comando 2 recebido!");
      break;
    default:
      Serial.println("Comando desconhecido.");
  }
}

#endif
