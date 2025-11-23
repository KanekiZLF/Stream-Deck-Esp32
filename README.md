# 🎮 ESP32 Deck Controller

<div align="center">

![ESP32 Deck](https://img.shields.io/badge/ESP32-Deck_Controller-blue?style=for-the-badge&logo=arduino)
![Python](https://img.shields.io/badge/Python-3.8%2B-green?style=for-the-badge&logo=python)
![CustomTkinter](https://img.shields.io/badge/GUI-CustomTkinter-orange?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)

**Um controlador personalizável para ESP32 Deck com interface moderna e intuitiva**

[✨ Funcionalidades](#-funcionalidades) • [🛠 Tecnologias](#-tecnologias) • [🚀 Instalação](#-instalação) • [🎯 Como Usar](#-como-usar) • [📁 Estrutura](#-estrutura)

</div>

## ✨ Funcionalidades

### 🎛️ Controle de Botões Personalizáveis

- **8 botões configuráveis** com ícones e ações personalizadas
- **Limite de 20 caracteres** nos nomes dos botões para manter a interface organizada
- **Sistema de ícones** automático a partir de executáveis ou imagens personalizadas
- **Preview visual** em tempo real das configurações

### 🔌 Comunicação Serial Avançada

- **Conexão automática** com ESP32 via porta serial
- **Detecção automática** de portas disponíveis
- **Configuração de baud rate** (9600, 19200, 38400, 57600, 115200)
- **Monitoramento em tempo real** da comunicação

### 🎨 Interface Moderna

- **Theme dark/light/system** com CustomTkinter
- **Layout responsivo** e intuitivo
- **Abas organizadas** para diferentes funcionalidades
- **Sistema de log** integrado para debug

### ⚡ Ações Automatizadas

- **Execução de programas** (.exe, .bat, etc.)
- **Abertura de URLs** no navegador padrão
- **Execução de comandos** do sistema
- **Digitação automática** de texto
- **Hotkeys** e combinações de teclas
- **Macros** com múltiplas ações sequenciais

### 🔧 Recursos Avançados

- **Sistema de backup/restore** das configurações
- **Verificação de atualizações** automática
- **Configuração salva automaticamente** em JSON
- **Tratamento elegante** de erros e exceções

## 🛠 Tecnologias

| Tecnologia        | Versão | Propósito                       |
| ----------------- | ------ | ------------------------------- |
| **Python**        | 3.8+   | Linguagem principal             |
| **CustomTkinter** | Latest | Interface gráfica moderna       |
| **Pillow (PIL)**  | Latest | Manipulação de imagens e ícones |
| **PySerial**      | Latest | Comunicação serial com ESP32    |
| **PyAutoGUI**     | Latest | Automação de teclado/mouse      |
| **Requests**      | Latest | Verificação de atualizações     |
| **PyWin32**       | Latest | Extração de ícones do Windows   |

## 🚀 Instalação

### Pré-requisitos

- **Python 3.8 ou superior**
- **ESP32 com firmware compatível**
- **Windows 10/11** (recomendado)

### 📥 Instalação Rápida

1. **Clone o repositório:**

```bash
git clone https://github.com/KanekiZLF/Stream-Deck-Esp32.git
cd Stream-Deck-Esp32
```

2. **Instale as dependências:**

```bash
pip install -r requirements.txt
```

3. **Execute o programa:**

```bash
python Stream-Deck-Esp32.py
```

### 📋 requirements.txt

```txt
customtkinter>=5.2.0
pillow>=10.0.0
pyserial>=3.5
pyautogui>=0.9.54
requests>=2.31.0
pywin32>=306
```

## 🎯 Como Usar

### 1. 🎮 Configurando Botões

1. Acesse a aba **"Configurar Botões"**
2. Clique em **"Configurar"** em qualquer botão
3. Defina o **nome** (máximo 20 caracteres)
4. Selecione o **programa** ou **ícone**
5. Clique em **"Salvar"**

### 2. 🔌 Conectando ao ESP32

1. Vá para a aba **"Conexão"**
2. Selecione a **porta serial** do ESP32
3. Escolha o **baud rate** (geralmente 115200)
4. Clique em **"Conectar"**

### 3. ⚙️ Personalizando Aparência

1. Na aba **"Configurações"**
2. Escolha entre temas **Dark, Light ou System**
3. Ajuste o **tamanho dos ícones**
4. Faça **backup** das suas configurações

### 4. 🔄 Atualizando o Software

1. Acesse a aba **"Atualização"**
2. Clique em **"Verificar Atualizações"**
3. Siga as instruções para atualizar

## 📁 Estrutura do Projeto

```
esp32-deck-controller/
├── 📄 Stream-Deck-Esp32.py        # Arquivo principal
├── 📄 Esp32_deck_config.json      # Configurações salvas
├── 📁 icons/                      # Pasta de ícones
├── 📄 Esp32_deck.log              # Arquivo de log
└── 📄 README.md                   # Este arquivo
```

## 🔧 Configuração do ESP32

### Código Exemplo para ESP32

```cpp
// Exemplo básico para ESP32
#include <Arduino.h>

const int buttonPins[] = {2, 3, 4, 5, 6, 7, 8, 9};
const int numButtons = 8;

void setup() {
  Serial.begin(115200);

  for(int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
}

void loop() {
  for(int i = 0; i < numButtons; i++) {
    if(digitalRead(buttonPins[i]) == LOW) {
      Serial.print("BTN:");
      Serial.println(i + 1);
      delay(300); // Debounce
    }
  }
  delay(50);
}
```

## 🎨 Capturas de Tela

### Interface Principal

```
┌─────────────────────────────────────────────┐
│              ESP32 DECK CONTROLLER          │
├─────────────────────────────────────────────┤
│  [🎮] [🎮] [🎮] [🎮]     [🔌] [⚙️] [🔄]     │
│  [🎮] [🎮] [🎮] [🎮]                       │
├─────────────────────────────────────────────┤
│ 📋 Log de Eventos                           │
│ [2024-01-01 12:00:00] Conectado à COM3      │
└─────────────────────────────────────────────┘
```

## 🔄 Protocolo de Comunicação

### Comandos do ESP32 → Software

```
BTN:1    # Botão 1 pressionado
BTN:2    # Botão 2 pressionado
...
BTN:8    # Botão 8 pressionado
```

### Comandos do Software → ESP32

```python
# Envio de comandos (futuras implementações)
serial_manager.send("LED:ON")
serial_manager.send("BEEP:1")
```

## 🐛 Solução de Problemas

### ❌ Porta Serial Não Aparece

- Verifique se o ESP32 está conectado via USB
- Instale os drivers CH340/CP2102 se necessário
- Reinicie o programa

### ❌ Botões Não Funcionam

- Confirme a conexão serial (status "Conectado")
- Verifique o baud rate (geralmente 115200)
- Teste o botão com "Testar" na configuração

### ❌ Ícones Não Carregam

- Verifique se o arquivo de ícone existe
- Formatos suportados: PNG, JPG, ICO
- Tente extrair ícone do executável

## 📄 Licença

Distribuído sob licença MIT. Veja `LICENSE` para mais informações.

## 👨‍💻 Desenvolvedor

**Luiz F. R. Pimentel**

- GitHub: [@KanekiZLF](https://github.com/KanekiZLF)

---

<div align="center">

### ⭐ Se este projeto te ajudou, deixe uma estrela no repositório!

**Feito com muito ☕ por Luiz F. R. Pimentel**
[⬆ Voltar ao topo](#-esp32-deck-controller)
</div>
