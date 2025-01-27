#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>

// Função que executa os comandos
void command(int cmd) {
  switch (cmd) {
    case 1:
      Serial.println("Program1");
      break;
    case 2:
      Serial.println("Program2");
      break;
    default:
      Serial.println("Program3");
  }
}

#endif
