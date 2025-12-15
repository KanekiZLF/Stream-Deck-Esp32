# 🎮 ESP32 Deck Controller v2.0.0

<div align="center">

![ESP32 Deck](https://img.shields.io/badge/ESP32-Deck_Controller_2.0-blue?style=for-the-badge&logo=arduino)
![Python](https://img.shields.io/badge/Python-3.8%2B-green?style=for-the-badge&logo=python)
![CustomTkinter](https://img.shields.io/badge/GUI-CustomTkinter-orange?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)

**Sistema completo de automação com ESP32 - Foco, Produtividade e Controle**

[✨ Novidades da v2.0](#-novidades-da-v20) • [🎛️ Funcionalidades](#️-funcionalidades) • [🛠 Tecnologias](#-tecnologias) • [🚀 Instalação](#-instalação) • [🔧 Hardware](#-hardware) • [💾 Configuração](#-configuração) • [🎯 Como Usar](#-como-usar)

</div>

## ✨ Novidades da v2.0

### 🎯 **Sistema Avançado de Macros e Ações**

| Funcionalidade                 | Descrição                                                | Status              |
| :----------------------------- | :------------------------------------------------------- | :------------------ |
| **Editor de Macro Sequencial** | Construa listas ordenadas de comandos com delays         | ✅ **IMPLEMENTADO** |
| **Editor de Ações Unificado**  | Interface centralizada para todos os tipos de ação       | ✅ **IMPLEMENTADO** |
| **Hotkey Profissional**        | Suporte a combinações complexas (`["ctrl","alt","del"]`) | ✅ **IMPLEMENTADO** |
| **Digitação Automática**       | Automação de texto para senhas, URLs e respostas         | ✅ **IMPLEMENTADO** |
| **Execução de Scripts Python** | Execute arquivos `.py` locais como automações            | ✅ **IMPLEMENTADO** |

### 🖥️ **Estabilidade e Experiência do Usuário**

| Melhoria                        | Impacto                                       | Status              |
| :------------------------------ | :-------------------------------------------- | :------------------ |
| **Eliminação de Flickering**    | Técnica de desenho oculto em todas as janelas | ✅ **IMPLEMENTADO** |
| **Gestão de Foco Perfeita**     | Correção de bugs de placeholder persistente   | ✅ **IMPLEMENTADO** |
| **Layout Centralizado**         | Botões organizados e padronizados             | ✅ **IMPLEMENTADO** |
| **Otimização de Inicialização** | Carregamento mais rápido antes da exibição    | ✅ **IMPLEMENTADO** |

---

## 🎛️ Funcionalidades

### 🎮 **Sistema Físico Avançado**

- **8 botões físicos** com leitura via shift register 74HC165
- **Display TFT integrado** 135x240 pixels com múltiplas interfaces
- **Feedback visual** em tempo real do status da conexão
- **6 temas de interface** diferentes para personalização

### 🔌 **Comunicação Bilateral**

- **Protocolo serial** bidirecional ESP32 ↔ Software
- **Conexão automática** com handshake de confirmação
- **Monitoramento em tempo real** do status da comunicação
- **Comandos de controle** (CONNECTED, DISCONNECT, PING)

### ⚡ **Sistema de Ações (COMPLETO)**

- **`open_program`** - Abrir programas/executáveis com toggle de janelas
- **`open_url`** - Abrir URLs no navegador padrão
- **`run_cmd`** - Executar comandos Shell/CMD
- **`type_text`** - Digitação automática de texto
- **`hotkey`** - Combinações de teclas complexas
- **`script`** - Executar scripts Python
- **`macro`** - Sequências de ações automatizadas

### 🎨 **Múltiplas Interfaces Visuais**

- **Interface Compacta** - Informações essenciais organizadas
- **Interface Moderna** - Design limpo com header destacado
- **Interface Minimalista** - Apenas o necessário, máximo de espaço
- **Interface Técnica** - Estilo profissional com especificações
- **Interface Gaming** - Visual estilo stream deck com elementos destacados
- **Interface Clássica** - Design tradicional com bordas arredondadas

---

## 🛠 Tecnologias

### 💻 **Software**

| Tecnologia        | Versão  | Propósito                       | Status v2.0 |
| :---------------- | :------ | :------------------------------ | :---------- |
| **Python**        | 3.8+    | Linguagem principal             | ✅          |
| **CustomTkinter** | 5.2.0+  | Interface gráfica moderna       | ✅          |
| **Pillow (PIL)**  | 10.0.0+ | Manipulação de imagens e ícones | ✅          |
| **PySerial**      | 3.5+    | Comunicação serial com ESP32    | ✅          |
| **PyAutoGUI**     | 0.9.54+ | Automação de teclado/mouse      | ✅          |
| **Requests**      | 2.31.0+ | Verificação de atualizações     | ✅          |
| **PyWin32**       | 306+    | Extração de ícones do Windows   | ✅          |
| **PsUtil**        | 5.9.0+  | Gerenciamento de processos      | ✅          |
| **PyStray**       | 0.19.0+ | System tray integration         | ✅          |

### 🔌 **Hardware**

| Componente         | Especificação       | Função                     |
| :----------------- | :------------------ | :------------------------- |
| **ESP32**          | ESP-WROOM-32        | Microcontrolador principal |
| **Shift Register** | 74HC165             | Expansão de entradas       |
| **Display TFT**    | 1.14" 135x240 SPI   | Interface visual           |
| **Botões**         | 8x Tactile switches | Controles físicos          |

---

## 🚀 Instalação

### 📋 **Pré-requisitos**

- **Python 3.8 ou superior**
- **ESP32 com firmware compatível**
- **Windows 10/11** (recomendado)
- **Conexão USB** para comunicação serial

### 📥 **Instalação Rápida (Executável)**

1. **Baixe o executável:**

   - Acesse [Releases](https://github.com/KanekiZLF/Stream-Deck-Esp32/releases)
   - Baixe `Stream-Deck-Esp32.exe`

2. **Execute diretamente:**
   ```bash
   Stream-Deck-Esp32.exe
   ```

### 🐍 **Para Desenvolvedores**

1. **Clone o repositório:**

   ```bash
   git clone https://github.com/KanekiZLF/Stream-Deck-Esp32.git
   cd "Stream-Deck-Esp32"
   ```

2. **Instale as dependências:**

   ```bash
   pip install -r requirements.txt
   ```

3. **Execute o programa:**
   ```bash
   python "Stream-Deck-Esp32.py"
   ```

### 📋 **Arquivo requirements.txt**

```txt
customtkinter>=5.2.0
pillow>=10.0.0
pyserial>=3.5
pyautogui>=0.9.54
requests>=2.31.0
pywin32>=306
pystray>=0.19.0
psutil>=5.9.0
```

---

## 🔧 Hardware

### 📋 **Lista de Componentes**

| Componente        | Quantidade | Observações                    |
| :---------------- | :--------- | :----------------------------- |
| ESP32             | 1          | Qualquer versão com USB        |
| 74HC165           | 1          | Shift register paralelo-serial |
| Display TFT 1.14" | 1          | SPI, 135x240 pixels            |
| Botões táteis     | 8          | 6x6mm ou similar               |
| Resistores 10K    | 8          | Pull-up para botões            |
| Protoboard        | 1          | Para montagem                  |
| Cabos jumper      | Vários     | Conexões                       |

### 🔌 **Esquema de Ligação**

```
ESP32 → 74HC165 (Shift Register)
================================
GPIO17  → DATA (Q7)
GPIO21  → CLOCK (CP)
GPIO22  → LATCH (PL)

ESP32 → Display TFT
===================
GPIO18  → SCLK
GPIO23  → MOSI
GPIO5   → DC
GPIO4   → RST
GPIO2   → CS
VCC     → 3.3V
GND     → GND

74HC165 → Botões
===============
Q0-Q7   → Botões 1-8 (com resistors pull-up)
VCC     → 3.3V
GND     → GND
```

---

## 💾 Configuração

### ⚙️ **Estrutura JSON para Ações**

```json
{
  "action": {
    "type": "tipo_selecionado",
    "payload": "dados_ou_caminho"
  }
}
```

### 🎯 **Exemplos Práticos de Configuração**

| Tipo de Ação     | Exemplo de Payload                                                              | Descrição                     |
| :--------------- | :------------------------------------------------------------------------------ | :---------------------------- |
| **open_program** | `"C:\\Apps\\Discord.exe"`                                                       | Abrir executável com toggle   |
| **run_cmd**      | `"explorer shell:RecycleBinFolder"`                                             | Executar comandos Shell/CMD   |
| **hotkey**       | `["ctrl", "shift", "z"]`                                                        | Combinação complexa de teclas |
| **type_text**    | `"Meu e-mail é: usuario@exemplo.com"`                                           | Digitação automática de texto |
| **macro**        | `[{"type":"hotkey","payload":"ctrl+a"},{"type":"type_text","payload":"Hello"}]` | Sequência de ações            |
| **script**       | `"C:\\Scripts\\automacao.py"`                                                   | Executar script Python        |

### 🔄 **Protocolo de Comunicação**

#### 📤 **ESP32 → Software**

```
BTN:1        # Botão 1 pressionado
BTN:2        # Botão 2 pressionado
...
BTN:8        # Botão 8 pressionado
```

#### 📥 **Software → ESP32**

```
CONNECTED    # Confirmação de conexão
DISCONNECT   # Solicitação de desconexão
PING         # Teste de comunicação
```

---

## 🎯 Como Usar

### 1. 🎮 **Configuração dos Botões (NOVO)**

1. **Abra o software** ESP32 Deck Controller
2. **Vá para a aba** "🎮 Configurar Botões"
3. **Clique em "Configurar"** em qualquer botão
4. **Use o Editor Unificado**:
   - **Selecione o tipo de ação** no menu suspenso
   - **Configure o payload** dinamicamente
   - **Para macros**: Clique em "🛠️ Editar Macro" para sequenciar ações

### 2. 🛠️ **Criando Macros (NOVO)**

1. **Selecione "Macro"** como tipo de ação
2. **Clique em "Editar Macro"**
3. **Adicione ações sequenciais**:
   - **➕ Adicionar** - Nova ação na sequência
   - **✏️ Editar** - Modificar ação existente
   - **🗑️ Remover** - Excluir ação
   - **▲▼ Mover** - Reordenar sequência

### 3. 🔌 **Conexão com ESP32**

1. **Conecte o ESP32** via USB
2. **Acesse a aba** "🔌 Conexão"
3. **Selecione a porta** COM correspondente
4. **Escolha baud rate** 115200
5. **Clique em "Conectar"**

### 4. 🎨 **Personalização da Interface**

1. **Na aba "⚙️ Configurações"**:
   - **Tema**: Dark, Light ou System
   - **Esquema de cores**: 5 opções disponíveis
   - **Tamanho da fonte**: Pequeno, Médio ou Grande
   - **Transparência**: 50% a 100%
   - **Minimizar para tray**: Mantém app rodando em segundo plano

### 5. 🔄 **Sistema de Atualizações**

1. **Acesse a aba** "🔄 Atualização"
2. **Clique em "Verificar"** para buscar novas versões
3. **Download automático** quando atualização disponível

---

## 🎨 Temas do ESP32

### ⭐ **Escolha no Código do Firmware:**

```cpp
// NO ARQUIVO .ino, LINHA ~380:
drawPanelCompact();    // ⭐ Opção 1 - Mais compacta
// drawPanelModern();     // ⭐ Opção 2 - Estilo moderno
// drawPanelMinimal();    // ⭐ Opção 3 - Minimalista
// drawPanelTechnical();  // ⭐ Opção 4 - Técnico
// drawPanelGaming();     // ⭐ Opção 5 - Estilo gaming
// drawPanelClassic();    // ⭐ Opção 6 - Clássico
```

---

## 🐛 Solução de Problemas

### ❌ **ESP32 Não é Detectado**

- **Verifique a conexão USB**
- **Instale drivers CH340/CP2102** se necessário
- **Teste em outra porta USB**
- **Reinicie o software**

### ❌ **Botões Não Funcionam**

- **Confirme a fiação** do shift register
- **Verifique os resistores** pull-up
- **Teste a comunicação serial** com monitor serial
- **Valide o baud rate** (115200)

### ❌ **Ações Não Executam**

- **Confirme caminhos absolutos** para programas e scripts
- **Teste ações individualmente** antes de criar macros

### ❌ **Display Não Acende**

- **Confirme a alimentação** 3.3V
- **Verifique as conexões SPI**
- **Ajuste as definições** do TFT_eSPI
- **Teste com exemplo** básico da biblioteca

### ❌ **Macros Não Funcionam**

- **Verifique a sintaxe JSON** das sub-ações
- **Teste cada ação** individualmente na macro
- **Ajuste delays** entre ações se necessário

---

## 📄 Licença

Distribuído sob licença MIT. Veja `LICENSE` para mais informações.

## 👨‍💻 Desenvolvedor

**Luiz F. R. Pimentel**

- GitHub: [@KanekiZLF](https://github.com/KanekiZLF)
- LinkedIn: [Luiz F. R. Pimentel](https://www.linkedin.com/in/luiz-fernando-rocha-pimentel)
- Projeto: [ESP32 Deck Controller](https://github.com/KanekiZLF/Stream-Deck-Esp32)

---

<div align="center">

### 🚀 **Sistema Completo: Hardware + Software + Automação**

### ⭐ **Se este projeto te ajudou, deixe uma estrela no repositório!**

**Desenvolvido com muito ☕ por Luiz F. R. Pimentel**

[⬆ Voltar ao topo](#-esp32-deck-controller-v200)

</div>
