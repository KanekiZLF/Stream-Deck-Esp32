from __future__ import annotations
import os
import sys
import atexit
import signal
import json
import threading
import time
import subprocess
import webbrowser
import platform
import traceback
import socket
import select
from dataclasses import dataclass
from typing import Dict, Any, Optional, Callable, List

# Windows API
try:
    import win32gui
    import win32con
    import win32process
    import win32ui
    WINDOWS_AVAILABLE = platform.system() == 'Windows'
except ImportError:
    WINDOWS_AVAILABLE = False
    
# GUI libs
import customtkinter as ctk
import tkinter as tk
from tkinter import filedialog, simpledialog, colorchooser 
# REMOVIDO: from tkinter import messagebox (substituído por CTkMessageDialog)
from PIL import Image, ImageTk, ImageDraw

# Serial
import serial
import serial.tools.list_ports

# -----------------------------
# OPTIONAL IMPORTS CHECK
# -----------------------------
# Para Toggle/Focar Janelas e System Tray
try:
    import requests
    REQUESTS_AVAILABLE = True
except Exception:
    REQUESTS_AVAILABLE = False

try:
    import pyautogui
    PYAUTOGUI_AVAILABLE = True
except Exception:
    REQUESTS_AVAILABLE = False

try:
    import psutil
    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False

try:
    import pystray
    from pystray import MenuItem as item
    PYSTRAY_AVAILABLE = True
except ImportError:
    PYSTRAY_AVAILABLE = False

# -----------------------------
# CONSTANTS / DEFAULTS
# -----------------------------
CONFIG_FILE = "Esp32_deck_config.json"
ICON_FOLDER = "icons"
LOG_FILE = "Esp32_deck.log"
APP_VERSION = "3.2.0" # Versão atualizada
APP_NAME = "Esp32 Deck Controller"
APP_ICON_NAME = "app_icon.ico"
DEVELOPER = "Luiz F. R. Pimentel"
GITHUB_URL = "https://github.com/KanekiZLF/Stream-Deck-Esp32"
UPDATE_CHECK_URL = "https://raw.githubusercontent.com/KanekiZLF/Stream-Deck-Esp32/refs/heads/main/version.txt"
DEFAULT_SERIAL_BAUD = 115200
BUTTON_COUNT = 16
ICON_SIZE = (80, 80) 

# Tipos de Ação e seus nomes amigáveis para a UI
ACTION_TYPES = {
    "none": "Nenhuma Ação",
    "open_program": "Abrir Programa / Executável",
    "open_url": "Abrir URL",
    "run_cmd": "Executar Comando Shell",
    "type_text": "Digitar Texto",
    "hotkey": "Tecla de Atalho (Hotkey)",
    "script": "Executar Script Python (.py)",
    "macro": "Macro (Ações Sequenciais)",
}
ACTION_TYPES_REVERSE = {v: k for k, v in ACTION_TYPES.items()}

# Ensure icons folder exists
os.makedirs(ICON_FOLDER, exist_ok=True)

# Theme colors
COLORS = {
    "primary": "#2B5B84",
    "secondary": "#3D8BC2", 
    "success": "#28A745",
    "warning": "#FFC107",
    "danger": "#DC3545",
    "dark": "#343A40",
    "light": "#F8F9FA",
    "text": "#FFFFFF",
    "default": "#0003AA",
}

# -----------------------------
# Utilities
# -----------------------------
def safe_makedirs(path: str):
    try:
        os.makedirs(path, exist_ok=True)
    except Exception:
        pass

# -----------------------------
# Icon Path (POSICIONADO APÓS AS CONSTANTES)
# -----------------------------
def get_app_icon_path() -> str:
    """Retorna o caminho absoluto para o ícone da aplicação, tratando o ambiente PyInstaller."""
    base_path = os.path.abspath(os.path.dirname(__file__))
    
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        base_path = sys._MEIPASS
    
    # Retorna o caminho completo para o ícone
    return os.path.abspath(os.path.join(base_path, APP_ICON_NAME))
# -----------------------------
# Logger (Filtered)
# -----------------------------
class Logger:
    LEVELS = ("DEBUG", "INFO", "WARN", "ERROR")

    def __init__(self, textbox: Optional[ctk.CTkTextbox] = None, file_path: Optional[str] = LOG_FILE):
        self.textbox = textbox
        self.file_path = file_path
        if file_path:
            try:
                with open(file_path, 'a', encoding='utf-8') as f:
                    f.write(f"\n--- Starting session {time.ctime()} ---\n")
            except Exception:
                pass

    def _write_file(self, message: str):
        if not self.file_path:
            return
        try:
            with open(self.file_path, 'a', encoding='utf-8') as f:
                f.write(message + "\n")
        except Exception:
            pass

    def log(self, msg: str, level: str = "INFO"):
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        entry = f"[{timestamp}] [{level}] {msg}"
        
        print(entry)
        
        should_save = False
        if level in ("WARN", "ERROR"):
            should_save = True
        elif "Fechando aplicação" in msg or "USB Desconectado" in msg or "Wi-Fi Desconectado" in msg:
            should_save = True
        
        # Otimização: Apenas salva log crítico e informações de sessão/serial
        if should_save or level == "ERROR" or "Fechando aplicação" in msg or "USB Desconectado" in msg or "Wi-Fi Desconectado" in msg:
            self._write_file(entry)
            
        if self.textbox:
            try:
                self.textbox.configure(state="normal")
                self.textbox.insert("end", entry + "\n")
                self.textbox.see("end")
                self.textbox.configure(state="disabled")
            except Exception:
                pass

    def debug(self, msg: str):
        self.log(msg, "DEBUG")

    def info(self, msg: str):
        self.log(msg, "INFO")

    def warn(self, msg: str):
        self.log(msg, "WARN")

    def error(self, msg: str):
        self.log(msg, "ERROR")

# -----------------------------
# Config Manager
# -----------------------------
class ConfigManager:
    def __init__(self, path: str = CONFIG_FILE):
        self.path = path
        self.data = self._load_or_create()

    def _default(self) -> Dict[str, Any]:
        buttons = {}
        for i in range(1, BUTTON_COUNT + 1):
            buttons[str(i)] = {
                "label": "",
                "icon": "",
                "led_color": "#FFFFFF", 
                "action": {"type": "none", "payload": ""}
            }
        return {
            "version": APP_VERSION,
            "buttons": buttons,
            "serial": {
                "type": "Serial", # NOVO: Tipo de conexão padrão
                "port": "", 
                "baud": DEFAULT_SERIAL_BAUD
            },
            "wifi": { # NOVO: Configurações de Wi-Fi
                "ip": "192.168.1.100", 
                "port": 8000
            },
            "appearance": {
                "theme": "System", 
                "icon_size": ICON_SIZE[0],
                "minimize_to_tray": False,
                "font_size": "Médio",
                "color_scheme": "Padrão" 
            },
            "update": {"check_url": UPDATE_CHECK_URL}
        }

    def _load_or_create(self):
        default_config = self._default()
        if os.path.exists(self.path):
            try:
                with open(self.path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                
                # Otimização: Garantir que chaves importantes existam (migração/compatibilidade)
                if 'buttons' not in data:
                    data['buttons'] = default_config['buttons']
                if 'appearance' not in data:
                    data['appearance'] = default_config['appearance']
                if 'serial' not in data:
                    data['serial'] = default_config['serial']
                if 'wifi' not in data: # Adicionando Wi-Fi
                    data['wifi'] = default_config['wifi']
                
                # Sub-chaves de appearance
                for key, default_val in default_config['appearance'].items():
                    if key not in data['appearance']:
                        data['appearance'][key] = default_val
                
                # Sub-chaves de serial
                for key, default_val in default_config['serial'].items():
                    if key not in data['serial']:
                        data['serial'][key] = default_val
                
                # Sub-chaves de wifi
                for key, default_val in default_config['wifi'].items():
                    if key not in data['wifi']:
                        data['wifi'][key] = default_val

                # Adiciona led_color se estiver faltando em botões existentes
                for key in data['buttons']:
                    if 'led_color' not in data['buttons'][key]:
                        data['buttons'][key]['led_color'] = default_config['buttons'][key]['led_color']

                return data
            except Exception:
                return default_config
        else:
            return default_config

    def save(self):
        try:
            with open(self.path, 'w', encoding='utf-8') as f:
                json.dump(self.data, f, indent=4, ensure_ascii=False)
            return True
        except Exception:
            return False

    def backup(self, target_path: Optional[str] = None) -> str:
        if not target_path:
            target_path = filedialog.asksaveasfilename(
                defaultextension='.json', 
                filetypes=[('JSON', '*.json')], 
                title='Salvar backup de configuração'
            )
        if not target_path:
            raise RuntimeError('Backup cancelado')
        with open(target_path, 'w', encoding='utf-8') as f:
            json.dump(self.data, f, indent=4, ensure_ascii=False)
        return target_path

    def restore(self, source_path: Optional[str] = None) -> str:
        if not source_path:
            source_path = filedialog.askopenfilename(
                filetypes=[('JSON', '*.json')], 
                title='Selecionar arquivo de configuração'
            )
        if not source_path:
            raise RuntimeError('Restauração cancelada')
        with open(source_path, 'r', encoding='utf-8') as f:
            self.data = json.load(f)
        self.save()
        return source_path

# -----------------------------
# Icon Loader
# -----------------------------
class IconLoader:
    def __init__(self, icon_size: tuple = ICON_SIZE):
        self.cache: Dict[str, ctk.CTkImage] = {}
        self.icon_size = icon_size

    def clear_cache_for_path(self, path: str):
        """Remove um ícone específico do cache"""
        if path in self.cache:
            del self.cache[path]
    
    def clear_all_cache(self):
        """
        Limpa todo o cache de ícones e tenta liberar memória PIL/Pillow.
        """
        self.cache.clear()
        try:
            import gc
            gc.collect() 
            # Otimização: Tenta fechar objetos PIL não referenciados explicitamente
            for obj in gc.get_objects():
                if isinstance(obj, Image.Image):
                    try:
                        obj.close() 
                    except:
                        pass
            gc.collect()
        except Exception:
            pass

    def load_icon_from_path(self, path: str) -> Optional[ctk.CTkImage]:
        if not path or not os.path.exists(path):
            return None
        if path in self.cache:
            return self.cache[path]
        try:
            img = Image.open(path).convert('RGBA')
            img.thumbnail(self.icon_size, Image.LANCZOS)
            ctk_img = ctk.CTkImage(light_image=img, dark_image=img, size=img.size)
            self.cache[path] = ctk_img
            return ctk_img
        except Exception as e:
            print(f"Erro ao carregar ícone {path}: {e}")
            return None

    def try_load_windows_exe_icon(self, exe_path: str) -> Optional[ctk.CTkImage]:
        # Tenta carregar ícone .ico com o mesmo nome
        ico_candidate = os.path.splitext(exe_path)[0] + '.ico'
        if os.path.exists(ico_candidate):
            return self.load_icon_from_path(ico_candidate)
        return None

    def extract_icon_to_png(self, exe_path: str, out_png_path: str, size: int = 128) -> Optional[str]: 
        """
        Implementação revisada para evitar "Select bitmap object failed" 
        e melhorar o tratamento de transparência (canal alfa) e qualidade.
        """
        if not WINDOWS_AVAILABLE: return None
        
        # Variáveis de limpeza
        large = []
        small = []
        hbmp = None
        hdc_mem = None
        temp_bmp = out_png_path + ".bmp"
        
        try:
            # 1. Extração do Handle do Ícone
            large, small = win32gui.ExtractIconEx(exe_path, 0)
            hicon = large[0] if large and len(large) > 0 else small[0] if small and len(small) > 0 else None
            
            if not hicon: return None

            # 2. Configuração do Device Context (DC) e Bitmap (BMB)
            hdc_screen = win32ui.CreateDCFromHandle(win32gui.GetDC(0))
            hdc_mem = hdc_screen.CreateCompatibleDC()
            
            hbmp = win32ui.CreateBitmap()
            hbmp.CreateCompatibleBitmap(hdc_screen, size, size)
            
            # Seleciona o Bitmap no DC de memória
            hbmp_old = hdc_mem.SelectObject(hbmp)
            
            # 3. Desenho do Ícone
            win32gui.DrawIconEx(hdc_mem.GetSafeHdc(), 0, 0, hicon, size, size, 0, None, win32con.DI_NORMAL)
            
            # 4. Salvamento temporário do Bitmap
            hbmp.SaveBitmapFile(hdc_mem, temp_bmp)
            
            # 5. Processamento com PIL para transparência
            img = Image.open(temp_bmp).convert("RGBA")
            
            # Tenta remover o fundo (geralmente branco/cinza claro do DrawIconEx)
            try:
                bg_color = img.getpixel((0, 0))
                
                if bg_color[0] > 200 and bg_color[1] > 200 and bg_color[2] > 200:
                    data = img.getdata()
                    new_data = []
                    for item in data:
                        if item[0] == bg_color[0] and item[1] == bg_color[1] and item[2] == bg_color[2]:
                            new_data.append((255, 255, 255, 0)) # Transparente
                        else:
                            new_data.append(item)
                    img.putdata(new_data)
                    
            except Exception:
                 pass 

            # Redimensiona e salva como PNG
            img = img.resize((size, size), Image.LANCZOS)
            img.save(out_png_path, "PNG")
            
            return out_png_path
        
        except Exception as e:
            # Captura exceções, incluindo a win32ui.error
            print(f"Erro ao extrair ícone do EXE: {e}\n{traceback.format_exc()}")
            return None
            
        finally:
            # 6. Limpeza de Recursos (IMPORTANTE!)
            try:
                if hdc_mem and 'hbmp_old' in locals():
                    hdc_mem.SelectObject(hbmp_old)
                if hbmp: 
                    win32gui.DeleteObject(hbmp.GetHandle())
                if hdc_mem: 
                    hdc_mem.DeleteDC()
                for h in large: win32gui.DestroyIcon(h)
                for h in small: win32gui.DestroyIcon(h)
            except Exception:
                pass
            
            # Remove o arquivo .bmp temporário
            if os.path.exists(temp_bmp):
                try: os.remove(temp_bmp)
                except: pass

# -----------------------------
# Tray Icon Manager
# -----------------------------
class TrayIconManager:
    def __init__(self, app_reference, logger: Logger):
        self.app = app_reference
        self.logger = logger
        self.icon = None
        self.running = False
        self._thread = None

    def load_tray_icon(self):
        """Tenta carregar o ícone do arquivo, senão cria um padrão."""
        icon_path = get_app_icon_path()
        if os.path.exists(icon_path):
            try:
                return Image.open(icon_path)
            except Exception: 
                pass
        return self.create_fallback_image()

    def create_fallback_image(self):
        """Cria um ícone genérico (quadrado colorido) se não houver arquivo .ico"""
        width = 64
        height = 64
        color1 = COLORS["primary"]
        color2 = COLORS["secondary"]
        image = Image.new('RGB', (width, height), color1)
        dc = ImageDraw.Draw(image)
        dc.rectangle((width // 2, 0, width, height // 2), fill=color2)
        dc.rectangle((0, height // 2, width // 2, height), fill=color2)
        return image

    def on_open_click(self, icon, item):
        self.app.after(0, self.app.restore_from_tray)

    def on_exit_click(self, icon, item):
        self.icon.stop()
        self.app.after(0, self.app.quit_app)

    def run(self):
        if not PYSTRAY_AVAILABLE:
            self.logger.warn("Biblioteca 'pystray' não instalada. Tray icon desativado.")
            return

        image = self.load_tray_icon()
        
        menu = (item('Abrir Esp32Deck', self.on_open_click, default=True), item('Sair', self.on_exit_click))
        self.icon = pystray.Icon("name", image, "Esp32 Deck", menu)
        
        self.running = True
        self._thread = threading.Thread(target=self.icon.run, daemon=True)
        self._thread.start()

    def stop(self):
        if self.icon:
            self.icon.stop()
            self.running = False
            
# -----------------------------
# Window Manager
# -----------------------------
class WindowManager:
    def __init__(self, logger: Logger):
        self.logger = logger
        
    def get_hwnds_for_pid(self, pid: int) -> List[int]:
        if not WINDOWS_AVAILABLE: return []
        def callback(hwnd, hwnds):
            if win32gui.IsWindowVisible(hwnd) and win32gui.IsWindowEnabled(hwnd):
                _, found_pid = win32process.GetWindowThreadProcessId(hwnd)
                if found_pid == pid: hwnds.append(hwnd)
            return True
        hwnds = []
        try: win32gui.EnumWindows(callback, hwnds)
        except Exception: pass
        return hwnds

    def toggle_application(self, exe_path: str):
        if not PSUTIL_AVAILABLE or not WINDOWS_AVAILABLE:
            self.logger.warn(f"Dependência não disponível. Iniciando {os.path.basename(exe_path)}")
            self._start_new(exe_path)
            return

        exe_name = os.path.basename(exe_path).lower()
        target_hwnds = []

        try:
            for proc in psutil.process_iter(['name', 'exe', 'pid']):
                try:
                    if proc.info['exe'] and os.path.samefile(proc.info['exe'], exe_path):
                        p_hwnds = self.get_hwnds_for_pid(proc.info['pid'])
                        target_hwnds.extend(p_hwnds)
                except (psutil.NoSuchProcess, psutil.AccessDenied, FileNotFoundError): continue
        except Exception as e:
            self.logger.error(f"Erro ao listar processos: {e}")
            self._start_new(exe_path)
            return

        target_hwnds = [h for h in target_hwnds if win32gui.GetWindowText(h)]
        target_hwnds.sort() 

        if not target_hwnds:
            self.logger.info(f"Nenhuma instância. Iniciando {exe_name}")
            self._start_new(exe_path)
        else:
            current_fg = win32gui.GetForegroundWindow()
            if current_fg in target_hwnds:
                current_index = target_hwnds.index(current_fg)
                if current_index == len(target_hwnds) - 1:
                    self.logger.info(f"Fim do ciclo. Minimizando {exe_name}")
                    win32gui.ShowWindow(current_fg, win32con.SW_MINIMIZE)
                else:
                    next_hwnd = target_hwnds[current_index + 1]
                    self.logger.info(f"Ciclando para janela {current_index + 2}/{len(target_hwnds)}")
                    self._bring_to_front(next_hwnd)
            else:
                self.logger.info(f"Trazendo {exe_name} para frente")
                self._bring_to_front(target_hwnds[0])

    def _bring_to_front(self, hwnd):
        if not WINDOWS_AVAILABLE: return
        try:
            if win32gui.GetForegroundWindow() == hwnd: return
            if win32gui.IsIconic(hwnd): win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
            win32gui.ShowWindow(hwnd, win32con.SW_SHOW)
            win32gui.SetForegroundWindow(hwnd)
        except Exception as e:
            self.logger.warn(f"Erro ao focar: {e}")
            try:
                if PYAUTOGUI_AVAILABLE:
                    pyautogui.press('alt')
                    win32gui.SetForegroundWindow(hwnd)
            except: pass

    def _start_new(self, path):
        if os.path.exists(path):
            if sys.platform.startswith('win'):
                os.startfile(path)
            else:
                subprocess.Popen([path])
        else:
            self.logger.error(f'Arquivo não encontrado: {path}')

# -----------------------------
# Action Manager
# -----------------------------
@dataclass
class Action:
    type: str
    payload: Any = ''

class ActionManager:
    def __init__(self, logger: Logger):
        self.logger = logger
        self.window_manager = WindowManager(logger)

    def perform(self, action: Action):
        try:
            if action.type == 'none': return # Não faz nada
            
            self.logger.debug(f"Executando ação: {action.type}")
            
            if action.type == 'open_program':
                # O open_program agora faz o 'toggle' no Windows
                self.window_manager.toggle_application(action.payload)
            elif action.type == 'open_url':
                webbrowser.open(action.payload)
                self.logger.info(f'Abrindo URL: {action.payload}')
            elif action.type == 'run_cmd':
                # Executa o comando em um shell separado para não bloquear
                subprocess.Popen(action.payload, shell=True) 
                self.logger.info(f'Rodando comando Shell: {action.payload}')
            elif action.type == 'type_text':
                if PYAUTOGUI_AVAILABLE:
                    pyautogui.write(action.payload)
                    self.logger.info('Texto digitado via pyautogui')
                else:
                    self.logger.warn('pyautogui não disponível para digitar texto.')
            elif action.type == 'hotkey':
                if PYAUTOGUI_AVAILABLE:
                    # O payload pode ser uma string (Ex: 'ctrl+a') ou lista (Ex: ['ctrl', 'alt', 'del'])
                    keys = action.payload if isinstance(action.payload, list) else action.payload.split('+')
                    pyautogui.hotkey(*keys)
                    self.logger.info(f'Hotkey enviada: {keys}')
                else:
                    self.logger.warn('pyautogui não disponível para hotkey.')
            elif action.type == 'script':
                # Executa um script Python (.py)
                if os.path.exists(action.payload):
                    # Inicia o script Python com o interpretador atual
                    subprocess.Popen([sys.executable, action.payload]) 
                else:
                    self.logger.error(f'Script não encontrado: {action.payload}')
            elif action.type == 'macro':
                # Macro: Sequência de sub-ações
                if isinstance(action.payload, list):
                    self.logger.info(f'Iniciando Macro de {len(action.payload)} passos.')
                    for i, a in enumerate(action.payload):
                        # Garantir que cada elemento da macro é um dicionário Action válido
                        if isinstance(a, dict) and 'type' in a:
                            sub = Action(a.get('type'), a.get('payload', ''))
                            self.perform(sub)
                            # Pequeno delay entre ações para estabilidade
                            time.sleep(0.1) 
                        else:
                            self.logger.error(f'Erro no passo {i+1} da macro: Estrutura inválida.')
                else:
                    self.logger.error('Macro com payload inválido (não é uma lista).')
                    
        except Exception as e:
            self.logger.error(f'Erro ao executar ação: {e}\n{traceback.format_exc()}')


# -----------------------------
# Dialogs (Custom Messagebox) - MELHORADA E ADAPTÁVEL
# -----------------------------
class CTkMessageDialog(ctk.CTkToplevel):
    """Substitui o tkinter.messagebox com o estilo CustomTkinter, com tamanho adaptável."""
    
    def __init__(self, parent, title: str, message: str, type: str, logger: Logger, icon: Optional[str] = None):
        super().__init__(parent)
        
        self.withdraw()
        self.result = None
        self.type = type
        self.logger = logger
        self.parent = parent
        self.message = message
        self.icon_text = icon

        # 1. Configurações base
        self.title(title)
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        
        # Mapeamento de Cores/Ícones
        if self.type == "info":
            self.color = COLORS["primary"]
            self.icon_text = "ℹ️"
        elif self.type == "warning":
            self.color = COLORS["warning"]
            self.icon_text = "⚠️"
        elif self.type == "error":
            self.color = COLORS["danger"]
            self.icon_text = "❌"
        elif self.type == "confirm":
            self.color = COLORS["secondary"]
            self.icon_text = "❓"
        else:
            self.color = COLORS["primary"]
            self.icon_text = "💬"
            
        if icon: self.icon_text = icon

        # 2. Constrói UI para obter as dimensões (Temporário/Final)
        self._build_ui()

        # 3. Calcula o tamanho e centraliza
        self._calculate_and_resize()
        
        self.deiconify()
        self.lift() 
        
    def _calculate_and_resize(self):
        """Calcula o tamanho ideal da janela baseado nos widgets e aplica centralização."""
        
        # O cálculo precisa ser feito após a inserção do texto na label e no frame
        self.update_idletasks()
        
        # Largura: Largura do frame de conteúdo + margens laterais (20+20)
        content_width = self.content_frame.winfo_reqwidth()
        window_width = max(400, content_width + 40) # Mínimo de 400px
        
        # Altura: (1) Altura do frame de conteúdo + (2) Altura do frame de botões + margens
        content_height = self.content_frame.winfo_reqheight()
        button_height = self.btn_frame.winfo_reqheight()
        
        # Altura total = Top Padding (20) + Content Height + Spacing (20) + Button Height + Bottom Padding (20)
        # Usando margens de 20px no frame principal (pack(padx=20, pady=20))
        window_height = content_height + button_height + 60
        
        # Aplica o novo tamanho
        self.geometry(f'{window_width}x{window_height}')
        
        # Centraliza na tela
        screen_w = self.winfo_screenwidth()
        screen_h = self.winfo_screenheight()
        x = (screen_w // 2) - (window_width // 2)
        y = (screen_h // 2) - (window_height // 2)
        self.geometry(f'+{x}+{y}')


    def _build_ui(self):
        # 1. Frame principal (Padding)
        main_frame = ctk.CTkFrame(self, fg_color="transparent")
        # Usamos fill/expand e padding para que o cálculo de tamanho funcione melhor
        main_frame.pack(fill='both', expand=True, padx=20, pady=20)
        
        # 2. Conteúdo (Ícone e Mensagem)
        self.content_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        self.content_frame.pack(fill='x', pady=(0, 20))
        self.content_frame.grid_columnconfigure(0, weight=0) # Ícone
        self.content_frame.grid_columnconfigure(1, weight=1) # Mensagem (Expansível)

        # Ícone (Coluna 0, Centralizado Verticalmente)
        ctk.CTkLabel(
            self.content_frame, 
            text=self.icon_text, 
            font=ctk.CTkFont(size=30, weight="bold"), 
            text_color=self.color
        ).grid(row=0, column=0, padx=(0, 15), pady=5, sticky="nsw")
        
        # Mensagem (Coluna 1, Centralizado)
        ctk.CTkLabel(
            self.content_frame, 
            text=self.message, 
            # Define uma largura máxima de quebra de linha para limitar o quão larga a janela pode ficar
            wraplength=300, 
            # 💡 CENTRALIZAÇÃO DO TEXTO AQUI
            justify="center", 
            font=ctk.CTkFont(size=14),
            # Usa 'ew' para que a label se expanda na coluna e 'n' para alinhar ao topo se for quebrar linha
            anchor='center' 
        ).grid(row=0, column=1, padx=(5, 0), pady=5, sticky="ew")

        # 3. Botões
        self.btn_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        self.btn_frame.pack(side='bottom', fill='x')
        
        # 💡 CENTRALIZAÇÃO DOS BOTÕES AQUI: Usamos grid para centralizar o frame interno.
        self.btn_frame.columnconfigure(0, weight=1) 
        inner_buttons_frame = ctk.CTkFrame(self.btn_frame, fg_color="transparent")
        inner_buttons_frame.grid(row=0, column=0, pady=0, padx=0) 

        if self.type == "confirm":
            # Botões Sim/Não para askyesno
            ctk.CTkButton(
                inner_buttons_frame, 
                text="✅ Sim", 
                command=lambda: self._set_result(True), 
                fg_color=COLORS["success"],
                width=80 
            ).pack(side='left', padx=(0, 20))
            
            ctk.CTkButton(
                inner_buttons_frame, 
                text="🚫 Não", 
                command=lambda: self._set_result(False), 
                fg_color=COLORS["danger"],
                width=80 
            ).pack(side='left', padx=(20, 0)) # Adicionado espaçamento à esquerda (5px)
        else:
            # Botão OK para showinfo, showwarning, showerror
            ctk.CTkButton(
                inner_buttons_frame, 
                text="OK", 
                command=lambda: self._set_result(True), 
                fg_color=COLORS["default"],
                width=80 
            ).pack(side='left', padx=(5, 0))

    def _set_result(self, res):
        self.result = res
        self.destroy()

    @staticmethod
    def showinfo(parent, title: str, message: str, logger: Logger):
        dlg = CTkMessageDialog(parent, title, message, "info", logger)
        parent.wait_window(dlg)
        return True

    @staticmethod
    def showwarning(parent, title: str, message: str, logger: Logger):
        dlg = CTkMessageDialog(parent, title, message, "warning", logger, icon="⚠️")
        parent.wait_window(dlg)
        return True

    @staticmethod
    def showerror(parent, title: str, message: str, logger: Logger):
        dlg = CTkMessageDialog(parent, title, message, "error", logger, icon="❌")
        parent.wait_window(dlg)
        return True

    @staticmethod
    def askyesno(parent, title: str, message: str, logger: Logger):
        dlg = CTkMessageDialog(parent, title, message, "confirm", logger)
        parent.wait_window(dlg)
        return dlg.result if dlg.result is not None else False


# -----------------------------
# Dialogs (Sub Action Config)
# -----------------------------
class SubActionConfigDialog(ctk.CTkToplevel):
    """Novo diálogo CustomTkinter para configurar uma única ação dentro de uma macro."""
    def __init__(self, parent, initial_type: str, initial_payload: Any, logger: Logger):
        super().__init__(parent)
        
        self.withdraw()
        
        self.parent = parent
        self.logger = logger
        self.title("Configurar Ação da Macro")
        self.geometry("400x250")
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._center_window()
        
        self.result: Optional[Dict[str, Any]] = None
        self.action_type_var = tk.StringVar(value=ACTION_TYPES.get(initial_type, ACTION_TYPES['open_program']))
        self.initial_payload = initial_payload
        
        self._build_ui()
        self.deiconify()
        self.lift()
        
    def _center_window(self):
        self.update_idletasks()
        # Garante que a janela abre centralizada em relação à tela ou ao parent.
        # Aqui, centralizamos em relação ao parent (MacroEditorDialog).
        parent_x = self.parent.winfo_x()
        parent_y = self.parent.winfo_y()
        parent_w = self.parent.winfo_width()
        parent_h = self.parent.winfo_height()
        
        x = parent_x + (parent_w // 2) - (400 // 2)
        y = parent_y + (parent_h // 2) - (250 // 2)
        
        self.geometry(f'+{x}+{y}')

    def _build_ui(self):
        main_frame = ctk.CTkFrame(self, fg_color="transparent")
        main_frame.pack(fill='both', expand=True, padx=20, pady=20)
        
        # Tipo de Ação
        ctk.CTkLabel(main_frame, text='Tipo de Ação:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', pady=(0, 5))
        
        self.action_type_menu = ctk.CTkOptionMenu(
            main_frame, 
            values=[v for k, v in ACTION_TYPES.items() if k != 'macro'], # Macros não podem conter macros
            variable=self.action_type_var,
            command=self._on_type_change,
            height=35
        )
        self.action_type_menu.pack(fill='x', pady=(0, 10))
        
        # Payload
        self.payload_label = ctk.CTkLabel(main_frame, text='Payload (Valor / Caminho):', font=ctk.CTkFont(weight="bold"))
        self.payload_label.pack(anchor='w', pady=(5, 5))
        
        self.payload_entry = ctk.CTkEntry(main_frame, height=35)
        self.payload_entry.pack(fill='x', pady=(0, 15))
        
        # Preenche com o valor inicial
        if isinstance(self.initial_payload, (dict, list)):
            try:
                # Hotkey/Macro payload pode ser complexo, tenta serializar se aplicável
                val = json.dumps(self.initial_payload, ensure_ascii=False)
            except:
                 val = str(self.initial_payload)
        else:
            val = str(self.initial_payload)
            
        self.payload_entry.insert(0, val)
        self.payload_entry.bind("<FocusIn>", self._on_focus_in) # Garante que o texto seja tratado como valor

        self._on_type_change(self.action_type_var.get())
        
        # Botões
        btn_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        btn_frame.pack(side='bottom', fill='x')
        
        # Botões centralizados
        inner_buttons_frame = ctk.CTkFrame(btn_frame, fg_color="transparent")
        inner_buttons_frame.pack(expand=True, padx=5)
        
        ctk.CTkButton(inner_buttons_frame, text='🚫 Cancelar', command=self.destroy, fg_color="#6c757d").pack(side='right', padx=(10, 0))
        ctk.CTkButton(inner_buttons_frame, text='💾 Salvar Ação', command=self._save_action, fg_color=COLORS["success"]).pack(side='right')

    def _on_focus_in(self, event):
        # Apenas garante que a janela permaneça em foco
        self.lift()

    def _on_type_change(self, friendly_type: str):
        selected_type = ACTION_TYPES_REVERSE.get(friendly_type, 'none')
        
        # Dicas de placeholder
        placeholders = {
            'open_program': 'Ex: C:\\Program Files\\app.exe',
            'open_url': 'Ex: https://google.com',
            'run_cmd': 'Ex: start explorer',
            'type_text': 'O texto será digitado na janela ativa.',
            'hotkey': 'Ex: ctrl+c ou ["ctrl", "alt", "f1"]',
            'script': 'Ex: C:\\Scripts\\meu_script.py',
            'none': ''
        }
        
        # Define o placeholder_text
        self.payload_entry.configure(placeholder_text=placeholders.get(selected_type, ''))
        
        self.focus()

    def _save_action(self):
        raw_payload = self.payload_entry.get().strip()
        selected_type = ACTION_TYPES_REVERSE.get(self.action_type_var.get(), 'none')
        payload = raw_payload
        
        if selected_type in ['hotkey']:
            try:
                # Tenta desserializar o payload como JSON (lista de teclas)
                payload = json.loads(raw_payload)
            except json.JSONDecodeError:
                 if raw_payload:
                    payload = raw_payload
                 else:
                     payload = []
        
        self.result = {'type': selected_type, 'payload': payload}
        self.destroy()

# -----------------------------
# Dialogs (Macro Editor)
# -----------------------------
class MacroEditorDialog(ctk.CTkToplevel):
    def __init__(self, parent, initial_macro: List[Dict[str, Any]], logger: Logger):
        super().__init__(parent)
        
        self.withdraw()
        
        self.logger = logger
        self.title("Editor de Macro")
        self.geometry("600x580")
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        
        self._center_window()
        
        # A macro é uma lista de dicionários de ação (type, payload)
        self.macro_list = initial_macro or [] 
        self.result = None
        
        self._build_ui()
        self._populate_list()
        
        self.deiconify()
        self.lift() 
        
    def _center_window(self):
        self.update_idletasks()
        x = (self.winfo_screenwidth() // 2) - (self.winfo_width() // 2)
        y = (self.winfo_screenheight() // 2) - (self.winfo_height() // 2)
        self.geometry(f'+{x}+{y}')

    def _build_ui(self):
        # Frame de Listagem
        list_frame = ctk.CTkFrame(self, corner_radius=8)
        list_frame.pack(fill='both', expand=True, padx=15, pady=(15, 5))
        
        ctk.CTkLabel(list_frame, text="Sequência de Ações (Executada em Ordem)", font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=(10, 5))

        # Determina as cores com base no modo de aparência atual
        if ctk.get_appearance_mode().lower() in ["dark", "system"]:
            bg_color = "#343638"  # Fundo escuro (CTK Dark BG)
            fg_color = "white"    # Texto claro
        else:
            bg_color = "white"    # Fundo claro
            fg_color = "black"    # Texto escuro

        self.listbox = tk.Listbox(
            list_frame, 
            height=15, 
            background=bg_color,
            foreground=fg_color,
            selectbackground=COLORS["secondary"],
            selectforeground="white",
            font=("Arial", 12),
            bd=0,
            highlightthickness=0 
        )
        self.listbox.pack(fill='both', expand=True, padx=10, pady=(0, 10))
        self.listbox.bind('<<ListboxSelect>>', self._on_select)
        
        # Frame de Ações (Adicionar, Editar, Mover)
        action_frame = ctk.CTkFrame(self, fg_color="transparent")
        action_frame.pack(fill='x', padx=15, pady=(5, 15))
        
        # Botões de controle da lista centralizados
        control_buttons_frame = ctk.CTkFrame(action_frame, fg_color="transparent")
        control_buttons_frame.pack(expand=True)
        
        self.add_btn = ctk.CTkButton(control_buttons_frame, text="➕ Adicionar", command=lambda: self._edit_action(), fg_color=COLORS["success"])
        self.add_btn.pack(side='left', padx=(0, 5))
        
        self.edit_btn = ctk.CTkButton(control_buttons_frame, text="✏️ Editar", command=lambda: self._edit_action(selected=True), state='disabled', fg_color=COLORS["primary"])
        self.edit_btn.pack(side='left', padx=5)
        
        self.remove_btn = ctk.CTkButton(control_buttons_frame, text="🗑️ Remover", command=self._remove_action, state='disabled', fg_color=COLORS["danger"])
        self.remove_btn.pack(side='left', padx=5)
        
        self.up_btn = ctk.CTkButton(control_buttons_frame, text="▲", command=lambda: self._move_action(-1), state='disabled', width=30)
        self.up_btn.pack(side='left', padx=(10, 2))

        self.down_btn = ctk.CTkButton(control_buttons_frame, text="▼", command=lambda: self._move_action(1), state='disabled', width=30)
        self.down_btn.pack(side='left')
        
        # Frame de Salvar/Cancelar (também centralizado)
        save_frame = ctk.CTkFrame(self, fg_color="transparent")
        save_frame.pack(fill='x', padx=15, pady=(0, 15))

        inner_save_buttons_frame = ctk.CTkFrame(save_frame, fg_color="transparent")
        inner_save_buttons_frame.pack(expand=True)
        
        ctk.CTkButton(inner_save_buttons_frame, text='🚫 Cancelar', command=self.destroy, fg_color="#6c757d").pack(side='left', padx=(10, 0))
        ctk.CTkButton(inner_save_buttons_frame, text='💾 Salvar Macro', command=self._save_and_close, fg_color=COLORS["success"]).pack(side='left')

    def _populate_list(self):
        self.listbox.delete(0, tk.END)
        for action in self.macro_list:
            action_type = action.get('type', 'none')
            payload = action.get('payload', '')
            display_type = ACTION_TYPES.get(action_type, action_type)
            
            # Formatação de exibição
            display_payload = str(payload)
            if action_type in ['hotkey']:
                display_payload = json.dumps(payload)
            elif len(display_payload) > 40:
                display_payload = display_payload[:37] + "..."
                
            entry = f"{display_type}: {display_payload}"
            self.listbox.insert(tk.END, entry)
            
        self._on_select() # Atualiza o estado dos botões

    def _on_select(self, event=None):
        selected_indices = self.listbox.curselection()
        if selected_indices:
            idx = selected_indices[0]
            self.edit_btn.configure(state='normal')
            self.remove_btn.configure(state='normal')
            self.up_btn.configure(state='normal' if idx > 0 else 'disabled')
            self.down_btn.configure(state='normal' if idx < len(self.macro_list) - 1 else 'disabled')
        else:
            self.edit_btn.configure(state='disabled')
            self.remove_btn.configure(state='disabled')
            self.up_btn.configure(state='disabled')
            self.down_btn.configure(state='disabled')
            
    def _edit_action(self, selected=False):
        
        index = self.listbox.curselection()[0] if selected else None
        
        initial_type = "open_program"
        initial_payload = ""
        
        if index is not None:
            initial_type = self.macro_list[index].get('type', 'none')
            initial_payload = self.macro_list[index].get('payload', '')

        dlg = SubActionConfigDialog(self, initial_type, initial_payload, self.logger)
        self.wait_window(dlg)
        
        if dlg.result:
            new_action = dlg.result
            if index is not None:
                self.macro_list[index] = new_action
            else:
                self.macro_list.append(new_action)
            
            self._populate_list()
        
    def _remove_action(self):
        selected_indices = self.listbox.curselection()
        if selected_indices:
            idx = selected_indices[0]
            del self.macro_list[idx]
            self._populate_list()

    def _move_action(self, direction: int):
        selected_indices = self.listbox.curselection()
        if selected_indices:
            idx = selected_indices[0]
            new_idx = idx + direction
            if 0 <= new_idx < len(self.macro_list):
                # Troca os itens na lista
                self.macro_list[idx], self.macro_list[new_idx] = self.macro_list[new_idx], self.macro_list[idx]
                self._populate_list()
                # Mantém o item selecionado
                self.listbox.select_clear(0, tk.END)
                self.listbox.select_set(new_idx)
                self.listbox.event_generate("<<ListboxSelect>>")

    def _save_and_close(self):
        self.result = self.macro_list
        self.destroy()

# -----------------------------
# Dialogs (Button Config)
# -----------------------------
class ButtonConfigDialog(ctk.CTkToplevel):
    def __init__(self, parent: Esp32DeckApp, button_key: str, conf: Dict[str, Any], icon_loader: IconLoader, logger: Logger):
        super().__init__(parent)
        
        self.withdraw()
        
        self.parent = parent
        self.button_key = button_key
        self.conf = conf
        self.icon_loader = icon_loader
        self.logger = logger
        self._newly_created_icon = None
        self.title(f'Configurar Botão {button_key}')
        self.geometry('550x580') # Aumentado para acomodar o menu de ação
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._center_window()
        self.protocol("WM_DELETE_WINDOW", self._on_cancel)
        initial_label = self.conf.get('label', f'Botão {button_key}')
        if initial_label == f'Botão {button_key}':
            initial_label = ""
        self.label_var = tk.StringVar(value=self.conf.get('label', f'Botão {button_key}'))
        self.icon_path = self.conf.get('icon', '')
        self.led_color_var = tk.StringVar(value=self.conf.get('led_color', '#FFFFFF')) # Cor do LED
        self._initial_action_type = self.conf.get('action', {}).get('type', 'none')
        self._initial_payload = self.conf.get('action', {}).get('payload', '')
        
        self._build()
        self.deiconify()
        self.lift() 
        
    def _center_window(self):
        self.update_idletasks()
        x = (self.winfo_screenwidth() // 2) - (self.winfo_width() // 2)
        y = (self.winfo_screenheight() // 2) - (self.winfo_height() // 2)
        self.geometry(f'+{x}+{y}')
        
    def _build(self):
        main_frame = ctk.CTkFrame(self, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=15, pady=15)
        
        # Nome do Botão
        ctk.CTkLabel(main_frame, text='Nome do Botão (Opcional):', font=ctk.CTkFont(weight="bold")).pack(anchor='w', pady=(5, 0), padx=5)
        def on_name_change(*args):
            if len(self.label_var.get()) > 16: self.label_var.set(self.label_var.get()[:16])
        self.label_var.trace('w', on_name_change)
        # MODIFICAÇÃO: Adiciona um placeholder mais útil
        ctk.CTkEntry(main_frame, textvariable=self.label_var, width=400, placeholder_text="Deixe vazio para mostrar apenas o ícone").pack(fill='x', pady=5, padx=5)
        
        # Ícone e Programa (Frame)
        icon_frame = ctk.CTkFrame(main_frame, corner_radius=8, border_width=1, border_color="#555")
        icon_frame.pack(fill='x', pady=(10, 5))
        ctk.CTkLabel(icon_frame, text='Ícone e Programa:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=(10, 5))
        
        icon_content = ctk.CTkFrame(icon_frame, fg_color="transparent")
        icon_content.pack(fill='x', padx=10, pady=(0, 10))
        
        self.icon_preview = ctk.CTkLabel(icon_content, text='📱', width=64, height=64, font=ctk.CTkFont(size=20), text_color=COLORS["primary"])
        self.icon_preview.pack(side='left', padx=10)
        
        btn_frame = ctk.CTkFrame(icon_content, fg_color="transparent")
        btn_frame.pack(side='left', padx=10)
        ctk.CTkButton(btn_frame, text='Escolher Ícone', width=140, command=self._choose_icon).pack(side='top', padx=5, pady=(0, 5))
        
        # --- Configuração do LED ---
        led_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        led_frame.pack(fill='x', pady=5)
        
        ctk.CTkLabel(led_frame, text='Cor do LED do Botão:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=(10, 5))
        
        led_input_frame = ctk.CTkFrame(led_frame, fg_color="transparent")
        led_input_frame.pack(fill='x', padx=10, pady=(0, 10))

        # Adiciona o seletor de cor nativo (tk.colorchooser)
        self.color_entry = ctk.CTkEntry(led_input_frame, textvariable=self.led_color_var, width=150, height=35, placeholder_text="#RRGGBB")
        self.color_entry.pack(side='left', padx=(0, 10))
        
        # Pré-visualização de Cor
        self.color_preview = ctk.CTkLabel(led_input_frame, text="  ", width=30, height=30, corner_radius=5, fg_color=self.led_color_var.get())
        self.color_preview.pack(side='left')
        
        # O tkinter é necessário para o seletor de cor nativo
        ctk.CTkButton(led_input_frame, text="Seletor de Cor", command=self._open_color_picker, width=120).pack(side='left', padx=10)
        
        # Ação (Tipo e Payload)
        action_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        action_frame.pack(fill='x', pady=5)
        
        # Tipo de Ação
        ctk.CTkLabel(action_frame, text='Tipo de Ação:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=(10, 5))
        
        type_options = list(ACTION_TYPES.values())
        self.action_type_menu = ctk.CTkOptionMenu(
            action_frame, 
            values=type_options, 
            command=self._on_action_type_change,
            height=35
        )
        self.action_type_menu.pack(fill='x', padx=10, pady=(0, 5))
        
        friendly_type = ACTION_TYPES.get(self._initial_action_type, ACTION_TYPES['open_program'])
        self.action_type_menu.set(friendly_type)
        
        # Payload / Botão de Seleção
        self.payload_label = ctk.CTkLabel(action_frame, text='Comando / Caminho:', font=ctk.CTkFont(weight="bold"))
        self.payload_label.pack(anchor='w', padx=10, pady=(5, 5))
        
        payload_input_frame = ctk.CTkFrame(action_frame, fg_color="transparent")
        payload_input_frame.pack(fill='x', padx=10, pady=(0, 10))
        
        self.payload_entry = ctk.CTkEntry(payload_input_frame, height=35)
        self.payload_entry.pack(side='left', fill='x', expand=True, padx=(0, 5))
        
        self.select_btn = ctk.CTkButton(payload_input_frame, text="...", width=40, command=self._select_file_or_macro)
        self.select_btn.pack(side='right')

        # Popula o payload inicial
        try:
            val = json.dumps(self._initial_payload, ensure_ascii=False) if isinstance(self._initial_payload, (dict, list)) else str(self._initial_payload)
            self.payload_entry.insert(0, val)
        except: self.payload_entry.insert(0, str(self._initial_payload))
        
        # Chama o handler para configurar os widgets baseados na ação inicial
        self._on_action_type_change(friendly_type)
        
        # Botões de Ação
        buttons_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        buttons_frame.pack(fill='x', pady=10)
        inner_buttons_frame = ctk.CTkFrame(buttons_frame, fg_color="transparent")
        inner_buttons_frame.pack(expand=True)
        
        ctk.CTkButton(inner_buttons_frame, text='▶️ Testar', command=self._test_action, fg_color=COLORS["primary"], width=80).pack(side='left', padx=5)
        ctk.CTkButton(inner_buttons_frame, text='🗑️ Excluir', command=self._on_delete, fg_color=COLORS["danger"], width=80).pack(side='left', padx=5)
        ctk.CTkButton(inner_buttons_frame, text='🚫 Cancelar', command=self._on_cancel, fg_color="#6c757d", width=80).pack(side='left', padx=5)
        ctk.CTkButton(inner_buttons_frame, text='💾 Salvar', command=self._save_and_close, fg_color=COLORS["success"], width=80).pack(side='left', padx=5)
        
        self._refresh_icon_preview()

    def _on_action_type_change(self, friendly_type: str):
        selected_type = ACTION_TYPES_REVERSE.get(friendly_type, 'none')
        
        # 1. Limpa o campo para garantir que o placeholder correto apareça
        self.payload_entry.delete(0, 'end')
        
        # 2. Se o novo tipo for o mesmo que o tipo inicial, restaura o payload.
        # Caso contrário, o campo fica vazio (e exibe o placeholder).
        if self._initial_action_type == selected_type:
             try:
                 val = json.dumps(self._initial_payload, ensure_ascii=False) if isinstance(self._initial_payload, (dict, list)) else str(self._initial_payload)
                 if str(val).strip(): # Apenas insere se for um valor real (não vazio)
                    self.payload_entry.insert(0, val)
             except:
                 self.payload_entry.insert(0, str(self._initial_payload))

        if selected_type == 'none':
            self.payload_label.configure(text='Nenhuma ação configurada')
            self.payload_entry.configure(placeholder_text="", state='disabled')
            self.select_btn.configure(text="...", state='disabled')
        elif selected_type == 'open_program':
            self.payload_label.configure(text='Caminho do Executável (.exe):')
            self.payload_entry.configure(placeholder_text="Ex: C:\\Program Files\\app.exe", state='normal')
            self.select_btn.configure(text="...", state='normal', command=lambda: self._select_file_or_macro('open_program'))
        elif selected_type == 'open_url':
            self.payload_label.configure(text='URL (Link Web):')
            self.payload_entry.configure(placeholder_text="Ex: https://google.com", state='normal')
            self.select_btn.configure(text="...", state='disabled')
        elif selected_type == 'run_cmd':
            self.payload_label.configure(text='Comando Shell:')
            self.payload_entry.configure(placeholder_text="Ex: start explorer", state='normal')
            self.select_btn.configure(text="...", state='disabled')
        elif selected_type == 'type_text':
            self.payload_label.configure(text='Texto para Digitar:')
            self.payload_entry.configure(placeholder_text="O texto será digitado na janela ativa.", state='normal')
            self.select_btn.configure(text="...", state='disabled')
        elif selected_type == 'hotkey':
            self.payload_label.configure(text='Tecla de Atalho (Separado por "+"):')
            self.payload_entry.configure(placeholder_text="Ex: ctrl+c ou ['ctrl', 'alt', 'del']", state='normal')
            self.select_btn.configure(text="...", state='disabled')
        elif selected_type == 'script':
            self.payload_label.configure(text='Caminho do Script Python (.py):')
            self.payload_entry.configure(placeholder_text="Ex: C:\\Scripts\\meu_script.py", state='normal')
            self.select_btn.configure(text="...", state='normal', command=lambda: self._select_file_or_macro('script'))
        elif selected_type == 'macro':
            self.payload_label.configure(text='Configuração da Macro:')
            self.payload_entry.configure(placeholder_text="Clique em 'Editar Macro' para configurar a sequência.", state='disabled')
            self.select_btn.configure(text="🛠️ Editar Macro", state='normal', command=lambda: self._select_file_or_macro('macro'))
            
        # Atualiza a referência de ação
        self._initial_action_type = selected_type
        
        self.focus()

    def _select_file_or_macro(self, action_type: str):
        if action_type == 'open_program':
            filetypes = [("Executáveis", "*.exe"), ("Todos", "*.*")]
            path = filedialog.askopenfilename(filetypes=filetypes)
            if not path: return
            self.payload_entry.delete(0, 'end')
            self.payload_entry.insert(0, path)
            
            # Tenta extrair ícone automaticamente
            if path.lower().endswith('.exe'):
                basename = os.path.splitext(os.path.basename(path))[0]
                safe_makedirs(ICON_FOLDER)
                out_png = os.path.join(ICON_FOLDER, f"btn{self.button_key}_{basename}.png")
                # Usa o novo tamanho de 128 para melhor qualidade antes de redimensionar
                extracted = self.icon_loader.extract_icon_to_png(path, out_png, size=128) 
                if extracted:
                    self._newly_created_icon = extracted
                    self.icon_path = extracted
                    self._refresh_icon_preview()
                    self.logger.info(f"Ícone extraído: {extracted}")

        elif action_type == 'script':
            filetypes = [("Scripts Python", "*.py"), ("Todos", "*.*")]
            path = filedialog.askopenfilename(filetypes=filetypes)
            if not path: return
            self.payload_entry.delete(0, 'end')
            self.payload_entry.insert(0, path)

        elif action_type == 'macro':
            # Abre o editor de macro
            current_payload = self._initial_payload if self._initial_action_type == 'macro' and isinstance(self._initial_payload, list) else []
            dlg = MacroEditorDialog(self, current_payload, self.logger) # Passa 'self' (ButtonConfigDialog) como parent
            self.wait_window(dlg)
            
            if dlg.result is not None:
                macro_data = dlg.result
                self._initial_payload = macro_data # Atualiza a referência interna
                try:
                    # Serializa a macro como JSON para o campo de texto
                    self.payload_entry.configure(state='normal')
                    self.payload_entry.delete(0, 'end')
                    self.payload_entry.insert(0, json.dumps(macro_data, ensure_ascii=False, indent=2)) 
                    self.payload_entry.configure(state='disabled')
                    self.logger.info(f"Macro de {len(macro_data)} passos configurada.")
                except Exception as e:
                    self.logger.error(f"Erro ao serializar macro: {e}")

    def _choose_icon(self):
        path = filedialog.askopenfilename(filetypes=[('Images', '*.png *.jpg *.ico'), ('All', '*.*')])
        if not path: return
        # Se um ícone foi extraído automaticamente, ele é removido
        if self._newly_created_icon and os.path.exists(self._newly_created_icon):
            try: os.remove(self._newly_created_icon)
            except: pass
        self._newly_created_icon = None
        
        self.icon_path = path
        self.conf['icon'] = self.icon_path
        self._refresh_icon_preview()

    def _refresh_icon_preview(self):
        ctk_img = self.icon_loader.load_icon_from_path(self.icon_path) if self.icon_path else None
        if ctk_img:
            self.icon_preview.configure(image=ctk_img, text='')
            self.icon_preview.image = ctk_img 
        else:
            self.icon_preview.configure(image=None, text='📱')
            self.icon_preview.image = None

    def _open_color_picker(self):
        """Abre o seletor de cores nativo e atualiza o campo e a pré-visualização no diálogo de configuração."""
        color_code = colorchooser.askcolor(title="Escolha a Cor do LED")
        
        if color_code and color_code[1]:
            hex_color = color_code[1].upper()
            self.led_color_var.set(hex_color)
            self.color_entry.delete(0, 'end')
            self.color_entry.insert(0, hex_color)
            self.color_preview.configure(fg_color=hex_color) # Atualiza a pré-visualização

    def _on_cancel(self):
        if self._newly_created_icon and os.path.exists(self._newly_created_icon):
            try: 
                os.remove(self._newly_created_icon)
                self.logger.info(f"Ícone extraído temporário removido: {self._newly_created_icon}")
            except: 
                pass
        self.destroy()

    def _on_delete(self):
        """
        Garante a remoção da referência da imagem, recria o IconLoader E
        TENTA DELETAR O ARQUIVO DO ÍCONE DA PASTA ICONS.
        """
        # SUBSTITUIÇÃO: Usando CTkMessageDialog.askyesno
        if not CTkMessageDialog.askyesno(
            self, 
            "Excluir Botão", 
            "Deseja remover o programa, o ícone e DELETAR o arquivo do ícone?", 
            self.logger
        ):
            return

        try:
            icon_path_to_delete = self.conf.get('icon', '')
            nome_do_botao = self.label_var.get()
            
            # 1. Limpa a configuração do botão no ConfigManager
            self.parent.config.data['buttons'][self.button_key] = {
                "label": "", 
                "icon": "",
                "led_color": "#FFFFFF", # RESETANDO A COR DO LED
                "action": {"type": "none", "payload": ""}
            }
            
            # 2. Reinicia o loader e Recria a tela para garantir a limpeza do fantasma.
            self.parent._reset_icon_loader()
            
            # 3. Limpa a referência interna do diálogo e salva
            self.icon_path = ""
            self.conf['icon'] = ""
            self.parent.config.save()

            # 4. Atualiza a interface
            self.parent.refresh_all_buttons()
            
            # 5. Tenta deletar o arquivo do ícone
            if icon_path_to_delete:
                abs_path_to_delete = os.path.abspath(icon_path_to_delete)
                icons_folder_abs = os.path.abspath(ICON_FOLDER)
                
                # Regra de segurança: Apenas exclui se o arquivo estiver DENTRO da pasta 'icons'
                if abs_path_to_delete.startswith(icons_folder_abs) and os.path.exists(abs_path_to_delete):
                    try: 
                        os.remove(abs_path_to_delete)
                    except Exception as e:
                        self.parent.logger.error(f"❌ Erro ao deletar arquivo de ícone: {e}")
                else:
                    self.parent.logger.info(f"Ícone não está na pasta 'icons', pulando exclusão do disco: {icon_path_to_delete}")

            self.parent.logger.info(f"🗑️ Botão excluído com sucesso: {nome_do_botao} (ID: {self.button_key})")
            # 6. Fecha o diálogo
            self.destroy()
            
        except Exception as e:
            self.parent.logger.error(f"❌ Erro durante exclusão: {e}\n{traceback.format_exc()}")
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showerror
            CTkMessageDialog.showerror(self, "Erro", f"Ocorreu um erro durante a exclusão: {e}", self.logger)
                
    def _save_and_close(self):
        raw_payload = self.payload_entry.get().strip()
        selected_type_friendly = self.action_type_menu.get()
        action_type = ACTION_TYPES_REVERSE.get(selected_type_friendly, 'none')
        
        payload = raw_payload
        
        if action_type == 'macro':
            if isinstance(self._initial_payload, list):
                 payload = self._initial_payload
            else:
                try:
                    payload = json.loads(raw_payload)
                except:
                    payload = []
        elif action_type == 'hotkey':
            try:
                payload = json.loads(raw_payload)
            except json.JSONDecodeError:
                 if raw_payload:
                    payload = raw_payload
                 else:
                     payload = []
        
        self.conf['label'] = self.label_var.get()
        self.conf['icon'] = self.icon_path
        self.conf['action'] = {'type': action_type, 'payload': payload}
        self.conf['led_color'] = self.led_color_var.get() # SALVANDO A COR DO LED
        
        self.parent.config.data['buttons'][self.button_key] = self.conf
        self.parent.config.save()
        
        # REINICIA O LOADER e atualiza
        self.parent._reset_icon_loader()
        self.parent.refresh_all_buttons()
        
        # Envia a cor do LED (se conectado)
        self.parent._send_led_color_command(self.button_key, self.conf['led_color'])
            
        self.destroy()

    def _test_action(self):
        raw_payload = self.payload_entry.get().strip()
        selected_type_friendly = self.action_type_menu.get()
        action_type = ACTION_TYPES_REVERSE.get(selected_type_friendly, 'none')
        
        payload = raw_payload
        
        if action_type == 'macro':
            if isinstance(self._initial_payload, list):
                 payload = self._initial_payload
            else:
                try:
                    payload = json.loads(raw_payload)
                except:
                    payload = []
        elif action_type == 'hotkey':
             try:
                payload = json.loads(raw_payload)
             except json.JSONDecodeError:
                if raw_payload:
                    payload = raw_payload
                else:
                    payload = []
                     
        if action_type == 'macro' and not payload:
             payload = []

        self.parent.action_manager.perform(Action(action_type, payload))

# -----------------------------
# Serial Manager
# -----------------------------
class SerialManager:
    def __init__(self, config: ConfigManager, logger: Logger, 
                 on_message: Optional[Callable[[str], None]] = None,
                 on_status_change: Optional[Callable[[bool, str], None]] = None):
        self.config = config
        self.logger = logger
        self.on_message = on_message
        # Atualizado para aceitar 'connection_type'
        self.on_status_change = on_status_change 
        self._serial: Optional[serial.Serial] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False
        self._is_connected = False
        self.connection_type = "Serial"
        # O self.connection_type não será alterado, pois ele define a lógica interna
        # se é serial (COM) ou Wi-Fi. A mudança para 'USB' é apenas cosmética na UI.

    @property
    def is_connected(self):
        return self._is_connected
        
    def send_command(self, command: str):
        if self._is_connected and self._serial and self._serial.is_open:
            try:
                self._serial.write((command + "\n").encode('utf-8')) # Adicionado \n
                return True
            except Exception as e:
                self.logger.error(f"Erro ao enviar comando serial: {e}")
                return False
        return False

    def is_port_available(self, port: str) -> bool:
        try:
            test_serial = serial.Serial(port=port, baudrate=DEFAULT_SERIAL_BAUD, timeout=0.1)
            test_serial.close()
            return True
        except serial.SerialException: return False
        except Exception: return False

    def list_ports(self) -> List[str]:
        return [p.device for p in serial.tools.list_ports.comports()]

    def disconnect(self):
        try:
            self._running = False
            self._is_connected = False
            if self._serial and self._serial.is_open:
                # Envia um comando de desconexão antes de fechar a porta
                try: self._serial.write(b"DISCONNECT\n")
                except Exception: pass 
                self._serial.close()
                self.logger.info('🔌 Porta serial fechada')
            if self.on_status_change: self.on_status_change(False, self.connection_type)
        except Exception as e: self.logger.warn(f'⚠️ Erro ao fechar serial: {e}')

    def connect(self, port: str, baud: int = DEFAULT_SERIAL_BAUD):
        try:
            if not self.is_port_available(port): return False
            if self._serial and self._serial.is_open: self.disconnect()
            
            self._serial = serial.Serial(port=port, baudrate=baud, timeout=1, write_timeout=1)
            time.sleep(2)
            self._running = True
            self._is_connected = True
            self._thread = threading.Thread(target=self._reader_loop, daemon=True)
            self._thread.start()
            
            self.send_command("CONNECTED")
            self.logger.info(f'✅ Conectado a {port} @ {baud}')
            if self.on_status_change: self.on_status_change(True, self.connection_type)
            return True
        except Exception as e:
            self.logger.error(f'❌ Falha ao conectar serial: {e}')
            return False

    def _reader_loop(self):
        try:
            while self._running and self._serial and self._serial.is_open:
                try:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            self.logger.debug(f'📨 Recebido serial: {line}')
                            if self.on_message: self.on_message(line)
                    else: time.sleep(0.05)
                except Exception: break
        finally:
            self._running = False
            self._is_connected = False
            if self.on_status_change: self.on_status_change(False, self.connection_type)

# -----------------------------
# Wifi Manager
# -----------------------------
class WifiManager:
    def __init__(self, logger: Logger, 
                 on_message: Optional[Callable[[str], None]] = None,
                 on_status_change: Optional[Callable[[bool, str], None]] = None):
        
        self.logger = logger
        self.on_message = on_message
        self.on_status_change = on_status_change
        self._socket: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False
        self._is_connected = False
        self.host = ""
        self.port = 0
        self.connection_type = "Wi-Fi"
        self.read_buffer = ""
        
    @property
    def is_connected(self):
        return self._is_connected

    def send_command(self, command: str) -> bool:
        # CORREÇÃO: Verifica se o socket existe E se ainda está conectado (flag)
        if self._is_connected and self._socket:
            try:
                self._socket.sendall((command + "\n").encode('utf-8'))
                return True
            except Exception as e:
                # O WinError 10038 é capturado aqui, e a desconexão é chamada para limpeza
                self.logger.error(f"Erro ao enviar comando Wi-Fi: {e}")
                self.disconnect() 
                return False
        return False

    def connect(self, host: str, port: int) -> bool:
        self.disconnect() 
        self.host = host
        self.port = port
        
        self.logger.info(f"Conectando via Wi-Fi em {host}:{port}...")
        
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(3) 
            self._socket.connect((host, port))
            self._socket.settimeout(None) 
            
            self._running = True
            self._is_connected = True
            self._thread = threading.Thread(target=self._reader_loop, daemon=True)
            self._thread.start()
            
            # Envia mensagem de conexão
            self.send_command("CONNECTED") 
            
            self.logger.info(f'✅ Conectado via Wi-Fi a {host}:{port}')
            if self.on_status_change: self.on_status_change(True, self.connection_type)
            return True
            
        except Exception as e:
            self.logger.error(f'❌ Falha ao conectar Wi-Fi: {e}')
            self._is_connected = False
            if self.on_status_change: self.on_status_change(False, self.connection_type)
            return False

    def disconnect(self):
        # Passo 1: Sinaliza a thread para parar
        self._running = False
        self._is_connected = False
        
        # Passo 2: Fecha o socket de forma limpa (protegido contra a thread de leitura)
        if self._socket:
            # Tenta enviar DISCONNECT uma última vez, caso a thread principal chame disconnect
            try: self._socket.sendall(b"DISCONNECT\n")
            except Exception: pass
            
            try:
                # CORREÇÃO: Fechar o socket aqui fará com que o select na thread falhe,
                # permitindo que a thread termine naturalmente.
                self._socket.shutdown(socket.SHUT_RDWR)
                self._socket.close()
                self.logger.info('🔌 Conexão Wi-Fi fechada')
            except Exception: pass
            
            # Passo 3: Limpa a referência
            self._socket = None

        if self.on_status_change: self.on_status_change(False, self.connection_type)

    def _reader_loop(self):
        try:
            # CORREÇÃO: Loop verifica self._running e self._socket
            while self._running and self._socket:
                try:
                    # Usa 'select' para non-blocking read
                    ready_to_read, _, _ = select.select([self._socket], [], [], 0.1)
                    
                    if ready_to_read:
                        # Verifica novamente self._socket antes de usar
                        if not self._socket: break
                        
                        data = self._socket.recv(1024)
                        if not data:
                            self.logger.warn("Conexão Wi-Fi fechada pelo host remoto.")
                            break 
                            
                        self.read_buffer += data.decode('utf-8', errors='ignore')
                        
                        while '\n' in self.read_buffer:
                            line, self.read_buffer = self.read_buffer.split('\n', 1)
                            line = line.strip()
                            if line:
                                self.logger.debug(f'📨 Recebido Wi-Fi: {line}')
                                if self.on_message: self.on_message(line)

                    time.sleep(0.01)
                        
                except socket.timeout:
                    continue
                # Captura de erro 10038 (Bad file descriptor) ou fechamento forçado
                except socket.error as e:
                    self.logger.error(f"Erro de socket Wi-Fi no loop: {e}")
                    break
                except Exception as e:
                    self.logger.error(f"Erro inesperado no loop Wi-Fi: {e}")
                    break
        finally:
            # Garante que o estado seja limpo quando a thread morre
            self.disconnect() # Chama disconnect para limpar o estado

    def search_device(self) -> Optional[str]:
        """
        Tenta encontrar o ESP32 Deck usando UDP Broadcast.
        O ESP32 deve responder a uma mensagem UDP específica.
        """
        UDP_PORT = 4210 # Porta de broadcast (a ser definida no ESP32)
        BROADCAST_IP = '255.255.255.255'
        MESSAGE = b"ESP32_DECK_DISCOVER"
        RESPONSE_TIMEOUT = 2 

        try:
            # 1. Cria socket UDP
            client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            client_socket.settimeout(RESPONSE_TIMEOUT)
            
            self.logger.info(f"Enviando broadcast UDP para {BROADCAST_IP}:{UDP_PORT}")
            
            # 2. Envia a mensagem de broadcast
            client_socket.sendto(MESSAGE, (BROADCAST_IP, UDP_PORT))
            
            # 3. Espera pela resposta
            while True:
                try:
                    data, server_address = client_socket.recvfrom(1024)
                    
                    if data.decode('utf-8', errors='ignore').strip() == "ESP32_DECK_ACK":
                        client_ip = server_address[0]
                        self.logger.info(f"Resposta recebida de {client_ip}")
                        return client_ip
                    
                except socket.timeout:
                    self.logger.info("Tempo limite de busca Wi-Fi atingido.")
                    return None
                except Exception as e:
                    self.logger.error(f"Erro durante a busca UDP: {e}")
                    return None
                    
        except Exception as e:
            self.logger.error(f"Falha ao inicializar a busca UDP: {e}")
            return None
        finally:
            try: client_socket.close()
            except: pass


# -----------------------------
# Update Checker
# -----------------------------
class UpdateChecker:
    def __init__(self, config: ConfigManager, logger: Logger):
        self.config = config
        self.logger = logger

    def check_update(self) -> Dict[str, Any]:
        """Verifica a versão mais recente no servidor remoto, lendo um arquivo TXT simples."""
        url = self.config.data.get('update', {}).get('check_url', UPDATE_CHECK_URL)

        if not REQUESTS_AVAILABLE:
            return {"ok": False, "error": "Biblioteca 'requests' não instalada"}
        
        try:
            response = requests.get(url, timeout=10)
            response.raise_for_status()
            
            latest_version_raw = response.text.strip()
            download_url = GITHUB_URL 
            
            if not latest_version_raw:
                return {"ok": False, "error": "Conteúdo da versão não encontrado no TXT"}
            
            latest = latest_version_raw
            is_new = self._version_greater(latest, APP_VERSION)
            
            return {
                "ok": True, 
                "latest": latest, 
                "download_url": download_url, 
                "is_new": is_new
            }
            
        except requests.exceptions.HTTPError as e:
            if e.response.status_code == 429:
                return {"ok": False, "error": "Muitas tentativas (429). Aguarde alguns minutos."}
            return {"ok": False, "error": f"Erro HTTP: {e.response.status_code}"}
            
        except requests.exceptions.ConnectionError:
            return {"ok": False, "error": "Sem conexão com a internet"}
            
        except Exception as e:
            return {"ok": False, "error": str(e)}

    @staticmethod
    def _version_greater(a: str, b: str) -> bool:
        try:
            pa = [int(x) for x in (''.join(c for c in a if c.isdigit() or c == '.') or '0').split('.')]
            pb = [int(x) for x in (''.join(c for c in b if c.isdigit() or c == '.') or '0').split('.')]
            max_len = max(len(pa), len(pb))
            pa.extend([0] * (max_len - len(pa)))
            pb.extend([0] * (max_len - len(pb)))
            return pa > pb
        except Exception:
            return False


# -----------------------------
# Dialogs (Improved About)
# -----------------------------
class AboutDialog(ctk.CTkToplevel):
    def __init__(self, parent):
        super().__init__(parent)
        self.title('Sobre')
        self.geometry('420x480')
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._center_window()
        self._build_ui()
        self.protocol("WM_DELETE_WINDOW", self.destroy)

    def _center_window(self):
        self.update_idletasks()
        x = (self.winfo_screenwidth() // 2) - (self.winfo_width() // 2)
        y = (self.winfo_screenheight() // 2) - (self.winfo_height() // 2)
        self.geometry(f'+{x}+{y}')

    def _build_ui(self):
        header_frame = ctk.CTkFrame(self, height=110, corner_radius=0, fg_color=COLORS["primary"])
        header_frame.pack(fill='x', padx=0, pady=0)
        header_frame.pack_propagate(False)
        ctk.CTkLabel(header_frame, text=APP_NAME, font=ctk.CTkFont(size=24, weight="bold"), text_color="white").pack(pady=(25, 0))
        ctk.CTkLabel(header_frame, text=f"v{APP_VERSION}", font=ctk.CTkFont(size=14), text_color="#E0E0E0").pack()
        content_frame = ctk.CTkFrame(self, fg_color="transparent")
        content_frame.pack(fill='both', expand=True, padx=20, pady=20)
        ctk.CTkLabel(content_frame, text="Desenvolvido por", font=ctk.CTkFont(size=12, weight="normal"), text_color="gray").pack(pady=(5,0))
        ctk.CTkLabel(content_frame, text=DEVELOPER, font=ctk.CTkFont(size=18, weight="bold"), text_color=COLORS["secondary"]).pack(pady=(0, 15))
        desc = "Software controlador para ESP32 Deck.\nGerencie atalhos, macros e automação."
        ctk.CTkLabel(content_frame, text=desc, font=ctk.CTkFont(size=13), justify="center").pack(pady=5)
        tech_frame = ctk.CTkFrame(content_frame, fg_color="transparent")
        tech_frame.pack(pady=15)
        def create_badge(parent, text, color):
            f = ctk.CTkFrame(parent, fg_color=color, corner_radius=6)
            f.pack(side='left', padx=4)
            ctk.CTkLabel(f, text=text, font=ctk.CTkFont(size=11, weight="bold"), text_color="white").pack(padx=8, pady=2)
        create_badge(tech_frame, "Python 3", "#3776AB")
        create_badge(tech_frame, "ESP32", "#E7352C")
        create_badge(tech_frame, "CustomTkinter", "#2B5B84")
        sys_info = f"Python: {platform.python_version()} | OS: {platform.system()}"
        ctk.CTkLabel(content_frame, text=sys_info, font=ctk.CTkFont(size=10), text_color="gray").pack(pady=(15, 5))
        action_frame = ctk.CTkFrame(self, fg_color="transparent")
        action_frame.pack(side='bottom', fill='x', padx=20, pady=20)
        gh_btn = ctk.CTkButton(action_frame, text="Ver projeto no GitHub", command=lambda: webbrowser.open(GITHUB_URL), fg_color="transparent", border_width=2, border_color=COLORS["primary"], text_color=COLORS["primary"], hover_color=COLORS["light"])
        gh_btn.pack(fill='x', pady=(0, 10))
        ctk.CTkButton(action_frame, text="Fechar", command=self.destroy, fg_color=COLORS["primary"], hover_color=COLORS["secondary"]).pack(fill='x')


# -----------------------------
# GUI / App
# -----------------------------
class Esp32DeckApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        
        self.withdraw()
        
        ctk.deactivate_automatic_dpi_awareness()
        self.title(f'{APP_NAME} v{APP_VERSION}')
        self.geometry('750x700')
        self.resizable(False, False)
        
        self._setup_app_icon()
        self._setup_theme()
        
        self.config = ConfigManager()
        
        # 1. Inicializa o Logger
        self.logger = Logger(file_path=LOG_FILE) 
        
        # 2. Inicializa o IconLoader
        self._reset_icon_loader()
        
        self.action_manager = ActionManager(self.logger)
        
        # 3. Inicializa os managers de conexão
        self.serial_manager = SerialManager(
            self.config, 
            self.logger, 
            on_message=self._on_serial_message,
            on_status_change=self._update_header_status
        )
        self.wifi_manager = WifiManager(
            self.logger,
            on_message=self._on_wifi_message,
            on_status_change=self._update_header_status
        )
        
        self.update_checker = UpdateChecker(self.config, self.logger)
        
        self.tray_manager = TrayIconManager(self, self.logger)
        if PYSTRAY_AVAILABLE:
            self.tray_manager.run()

        self.colors = COLORS.copy()
        self.current_font_size = 14
        
        # Inicializa a lista de widgets de botões
        self.button_frames: Dict[str, Dict[str, Any]] = {}
        
        ctk.set_appearance_mode(self.config.data.get('appearance', {}).get('theme', 'System'))
        
        # Componentes referenciados em _build_header e _build_connection_tab
        self.header_status_dot = None
        self.header_status_text = None
        self.status_card = None
        self.dash_icon = None
        self.dash_status_text = None
        self.dash_sub_text = None
        self.details_frame = None
        self.lbl_detail_port = None
        self.lbl_detail_baud = None
        self.lbl_detail_proto = None
        self.connect_btn = None
        self.disconnect_btn = None
        self.port_option = None
        self.baud_option = None
        self.refresh_ports_btn = None
        self.ip_entry = None
        self.port_entry = None
        self.search_btn = None
        self.centering_frame = None
        self.remote_version_var = tk.StringVar(value=APP_VERSION)
        self.remote_card_frame = None
        self.last_check_label = None
        self.check_update_btn = None
        self.status_icon_label = None
        self.status_title_label = None
        self.status_detail_label = None
        self.connection_type_var = tk.StringVar(value=self.config.data.get('serial', {}).get('type', 'Serial'))
        self.connection_details_textbox = None
        
        self._build_ui()
        self._load_appearance_settings() 
        
        self.logger.textbox = self.log_textbox
        
        self._center_window()
        self.refresh_all_buttons()
        self.update_serial_ports()
        
        self.bind("<Unmap>", self._on_minimize_event)

        # Determina o estado de conexão inicial
        initial_conn_type = self.connection_type_var.get()
        is_connected = self.serial_manager.is_connected or self.wifi_manager.is_connected # Verifica o estado real

        # A chamada visual é feita aqui no final do __init__
        self._set_header_visuals(is_connected, initial_conn_type)

        atexit.register(self._cleanup_on_exit)
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
        self.logger.info(f"--- {APP_NAME} v{APP_VERSION} Iniciado ---")
        self.logger.info(f"  💻 Dev: {DEVELOPER}")
        self.logger.info(f"🔗 GitHub: {GITHUB_URL}")

    def _reset_icon_loader(self):
        """Destrói e recria o IconLoader para forçar a limpeza total da memória de imagens."""
        size = self.config.data.get('appearance', {}).get('icon_size', ICON_SIZE[0])
        
        if hasattr(self, 'icon_loader'):
            self.icon_loader.clear_all_cache()
            del self.icon_loader 
            
        self.icon_loader = IconLoader(icon_size=(size, size))

    def _setup_app_icon(self):
        icon_path = get_app_icon_path()
        if os.path.exists(icon_path):
            try:
                self.iconbitmap(default=icon_path)
            except Exception as e:
                print(f"Aviso: Não foi possível definir ícone da janela: {e}")

    def _make_clickable(self, widget, command):
        """Liga o evento de clique e define o cursor para o widget e todos os seus filhos."""
        
        # 1. Configura o cursor (Crucial para a UX)
        widget.configure(cursor="hand2")
        
        # 2. Liga o comando de clique
        widget.bind('<Button-1>', lambda event: self.after(0, command))
        
        # 3. Recorre para os filhos, garantindo cursor e clique
        for child in widget.winfo_children():
            
            child.configure(cursor="hand2")
            child.bind('<Button-1>', lambda event, w=widget: self.after(0, command))
        
            self._make_clickable(child, command)

    def _on_minimize_event(self, event):
        if event.widget == self and self.state() == 'iconic':
            minimize_to_tray = self.config.data.get('appearance', {}).get('minimize_to_tray', False)
            if minimize_to_tray:
                self.withdraw()
                self.logger.info("Aplicativo minimizado para o System Tray.")

    def restore_from_tray(self):
        self.deiconify()
        self.state('normal')
        self.logger.info("Aplicativo restaurado do System Tray.")
        self.lift()
        self.focus_force()

    def quit_app(self):
        self._cleanup_on_exit()
        self.quit()

    def _cleanup_on_exit(self):
        try:
            self.logger.info("🚪 Fechando aplicação...")
            if hasattr(self, 'serial_manager'): self.serial_manager.disconnect()
            if hasattr(self, 'wifi_manager'): self.wifi_manager.disconnect()
            
            if hasattr(self, 'config'): 
                self.config.data['version'] = APP_VERSION # Garante que a versão atual está salva
                self.config.save()
            if hasattr(self, 'tray_manager'): self.tray_manager.stop()
        except Exception: pass

    def _signal_handler(self, signum, frame):
        self._cleanup_on_exit()
        sys.exit(0)

    def on_closing(self):
        self._cleanup_on_exit()
        self.destroy() 
    
    def _recursive_update_widgets(self, widget):
        try:
            if isinstance(widget, ctk.CTkButton):
                text = widget.cget("text")

                # Botões de Sucesso (Verde)
                if text in ["💾 Salvar", "🔗 Conectar", "💾 Salvar Macro", "💾 Salvar Ação", "⚡ Aplicar Cor", "Ligar Todos"]: 
                    fg = self.colors["success"]
                    # Usa 'success_hover' ou volta para a cor 'success' se a chave não existir
                    hv = self.colors.get("success_hover", fg) 
                    widget.configure(fg_color=fg, hover_color=hv)
                
                # Botões de Perigo (Vermelho)
                elif text in ["🗑️ Excluir", "🔓 Desconectar", "Desligar Todos"]: 
                    fg = self.colors["danger"]
                    # Usa 'danger_hover' ou volta para a cor 'danger' se a chave não existir
                    hv = self.colors.get("danger_hover", fg)
                    widget.configure(fg_color=fg, hover_color=hv)
                
                # Botões de Aviso (Amarelo)
                elif text in ["🔄 Atualizar", "🔄 Restaurar Configurações Padrão", "🔄 Atualizar Portas", "⬇️ Baixar Atualização"]: 
                    fg = self.colors["warning"]
                    hv = self.colors.get("warning_hover", fg)
                    # Força text_color para garantir contraste
                    txt_color = "black" if ctk.get_appearance_mode() == "Light" else "white"
                    widget.configure(fg_color=fg, hover_color=hv, text_color=txt_color)

                # Botões Primários (Azul - Inclui Testar, Efeitos e Paleta de Cores)
                elif text in ["▶️ Testar", "Efeito Arco-Íris 🌈", "Efeito Piscante ✨", "🎨", "ℹ️ Sobre", "🔍 Buscar"]:
                    fg = self.colors["primary"]
                    # Usa 'primary_hover' ou volta para a cor 'secondary' (como padrão)
                    hv = self.colors.get("primary_hover", self.colors.get("secondary", fg)) 
                    widget.configure(fg_color=fg, hover_color=hv)
                
                # Botões de Cancelar (Cinza)
                elif text in ["🚫 Cancelar"]: 
                    # Usando cores fixas, pois não estão na paleta principal (opcional: criar 'cancel' na paleta)
                    widget.configure(fg_color="#6c757d", hover_color="#5a6268")
                
                # Fallback para botões não listados
                else: 
                    fg = self.colors["primary"]
                    hv = self.colors.get("secondary", fg)
                    widget.configure(fg_color=fg, hover_color=hv)
            
            # Atualização de Frames e Fontes (Mantido)
            elif isinstance(widget, ctk.CTkFrame):
                if widget.cget("border_width") > 0: widget.configure(border_color=self.colors["secondary"])
            
            try:
                if hasattr(widget, 'configure') and hasattr(self, 'current_font_size'):
                    try: current_font = widget.cget("font")
                    except: current_font = None
                    if isinstance(current_font, ctk.CTkFont):
                        new_font = ctk.CTkFont(family=current_font.cget("family"), size=self.current_font_size, weight=current_font.cget("weight"))
                    else:
                        new_font = ctk.CTkFont(size=self.current_font_size)
                    widget.configure(font=new_font)
            except Exception: pass
            
            for child in widget.winfo_children(): self._recursive_update_widgets(child)
        except: pass

    def _load_appearance_settings(self):
        app = self.config.data.get('appearance', {})
        ctk.set_appearance_mode(app.get('theme', 'System'))
        transparency = app.get('transparency', 1.0)
        self.attributes('-alpha', transparency)
        if hasattr(self, 'transparency_var'):
            self.transparency_var.set(transparency)
        
        self._on_color_scheme_change(app.get('color_scheme', 'Padrão'))
        self._on_font_size_change(app.get('font_size', 'Médio'))

    def _setup_theme(self): ctk.set_default_color_theme("dark-blue")
        
    def _center_window(self):
        self.update_idletasks()
        x = (self.winfo_screenwidth() // 2) - (900 // 2)
        y = (self.winfo_screenheight() // 2) - (700 // 2)
        self.geometry(f'+{x}+{y}')

    def _on_transparency_change(self, value):
        value = float(value)
        self.attributes('-alpha', value)
        self.transparency_label.configure(text=f"{int(value * 100)}%")
        self.config.data.setdefault('appearance', {})['transparency'] = value
        self.config.save()

    def _on_color_scheme_change(self, value):
        # Paletas de cores, incluindo hover_color para botões críticos e primários.
        palettes = {
            "Padrão": {
                "primary": "#2B5B84", "primary_hover": "#22496b", # Hover Primary
                "secondary": "#3D8BC2", "secondary_hover": "#3174a6", # Hover Secondary
                "success": "#28A745", "success_hover": "#1e7e34", # Hover Success
                "warning": "#FFC107", "warning_hover": "#d9a100", # Hover Warning
                "danger": "#DC3545", "danger_hover": "#c82333",    # Hover Danger
                "dark": "#343A40", "light": "#F8F9FA", "text": "#FFFFFF"
            },
            "Moderno": {
                "primary": "#000981", "primary_hover": "#000760",
                "secondary": "#4527A0", "secondary_hover": "#3a2082",
                "success": "#0D8040", "success_hover": "#0a6030", 
                "warning": "#915E00", "warning_hover": "#7a4e00",
                "danger": "#C62828", "danger_hover": "#a22020",    
                "dark": "#343A40", "light": "#F8F9FA", "text": "#FFFFFF"
            },
            "Vibrante": {
                "primary": "#FF007F", "primary_hover": "#cc0066",
                "secondary": "#00D4FF", "secondary_hover": "#00aed4",
                "success": "#39FF14", "success_hover": "#2eaf0e", 
                "warning": "#FFE600", "warning_hover": "#d9c300",
                "danger": "#FF0033", "danger_hover": "#c8002a",    
                "dark": "#343A40", "light": "#F8F9FA", "text": "#FFFFFF"
            },
            "Suave": {
                "primary": "#C0A9F7", "primary_hover": "#9a85c8",
                "secondary": "#6C7BB1", "secondary_hover": "#576492",
                "success": "#69F9A6", "success_hover": "#54c885", 
                "warning": "#FAF48B", "warning_hover": "#d5d074",
                "danger": "#FF6C6C", "danger_hover": "#cc5656",    
                "dark": "#343A40", "light": "#F8F9FA", "text": "#FFFFFF"
            },
            "Escuro Total": {
                "primary": "#3C4043", "primary_hover": "#2a2d30",
                "secondary": "#191C1F", "secondary_hover": "#0a0a0a",
                "success": "#2E7D32", "success_hover": "#225e27", 
                "warning": "#EF6C00", "warning_hover": "#c95800",
                "danger": "#C62828", "danger_hover": "#a22020",    
                "dark": "#343A40", "light": "#F8F9FA", "text": "#FFFFFF"
            }
        }
        
        if value in palettes: self.colors.update(palettes[value])

        self._recursive_update_widgets(self)
        
        self.config.data.setdefault('appearance', {})['color_scheme'] = value
        self.config.save()
        self.update()

    def _on_font_size_change(self, value):
        size_map = {'Pequeno': 12, 'Médio': 14, 'Grande': 18}
        self.current_font_size = size_map.get(value, 14)
        self._recursive_update_widgets(self)
        self.config.data.setdefault('appearance', {})['font_size'] = value
        self.config.save()

    def _on_minimize_tray_change(self):
        val = self.minimize_tray_switch.get()
        self.config.data.setdefault('appearance', {})['minimize_to_tray'] = bool(val)
        self.config.save()
        status = "ativado" if val else "desativado"
        self.logger.info(f"Minimizar para Tray {status}")

    def _reset_appearance(self):
        # SUBSTITUIÇÃO: Usando CTkMessageDialog.askyesno
        if CTkMessageDialog.askyesno(self, "Confirmar Reset", "Restaurar padrões de aparência?", self.logger):
            self.config.data['appearance'] = {'theme': 'System', 'transparency': 1.0, 'color_scheme': 'Padrão', 'font_size': 'Médio', 'minimize_to_tray': False}
            
            ctk.set_appearance_mode('System')
            self.attributes('-alpha', 1.0)
            self.update()
            
            self.minimize_tray_switch.deselect()
            self.theme_menu.set('System')
            self.transparency_var.set(1.0)
            self.color_scheme_menu.set('Padrão')
            self.font_size_menu.set('Médio')
            
            self._on_color_scheme_change('Padrão')
            self._on_font_size_change('Médio')
            
            self.config.save()
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showinfo
            CTkMessageDialog.showinfo(self, "Reset", "Configurações restauradas!", self.logger)
    
    def _build_ui(self):
        self._build_header()
        
        self.tabview = ctk.CTkTabview(self, width=860, height=500, corner_radius=10)
        self.tabview.pack(expand=True, fill='both', padx=20, pady=(0, 20))
        
        self.tab_buttons = self.tabview.add('🎮 Configurar Botões')
        self.tab_connection = self.tabview.add('🔌 Conexão')
        self.tab_settings = self.tabview.add('⚙️ Configurações')
        self.tab_update = self.tabview.add('🔄 Atualização')
        
        # A ordem de chamada das abas é importante para garantir que as referências existam
        self._build_buttons_tab(self.tab_buttons) 
        self._build_connection_tab(self.tab_connection)
        self._build_settings_tab(self.tab_settings)
        self._build_update_tab(self.tab_update)
        
        log_frame = ctk.CTkFrame(self, corner_radius=10)
        log_frame.pack(side='bottom', fill='x', padx=20, pady=(0, 10))
        log_header = ctk.CTkFrame(log_frame, fg_color="transparent")
        log_header.pack(fill='x', padx=10, pady=(5, 0))
        ctk.CTkLabel(log_header, text="📋 Log de Eventos", font=ctk.CTkFont(weight="bold")).pack(side='left')
        ctk.CTkButton(log_header, text="Limpar", width=60, height=24, command=self._clear_log).pack(side='right')
        self.log_textbox = ctk.CTkTextbox(log_frame, height=120, state='disabled')
        self.log_textbox.pack(fill='x', padx=10, pady=(0, 10))
        
    def _build_header(self):
        header = ctk.CTkFrame(self, height=60, corner_radius=0)
        header.pack(fill='x', padx=0, pady=0)
        header.pack_propagate(False)
        
        title_frame = ctk.CTkFrame(header, fg_color="transparent")
        title_frame.pack(side='left', padx=20, pady=10)
        ctk.CTkLabel(title_frame, text=APP_NAME, font=ctk.CTkFont(size=20, weight="bold")).pack(anchor='w')
        ctk.CTkLabel(title_frame, text=f"v{APP_VERSION} - Controller para ESP32", font=ctk.CTkFont(size=12), text_color=self.colors["secondary"]).pack(anchor='w')
        
        status_frame = ctk.CTkFrame(header, fg_color="transparent")
        status_frame.place(relx=0.5, rely=0.5, anchor='center')
        self.header_status_dot = ctk.CTkLabel(status_frame, text="●", font=ctk.CTkFont(size=20), text_color=self.colors["danger"])
        self.header_status_dot.pack(side='left', padx=(0, 5))
        self.header_status_text = ctk.CTkLabel(status_frame, text="Desconectado", font=ctk.CTkFont(size=14, weight="bold"), text_color=self.colors["danger"])
        self.header_status_text.pack(side='left')
        
        btn_frame = ctk.CTkFrame(header, fg_color="transparent")
        btn_frame.pack(side='right', padx=20, pady=10)
        
        ctk.CTkButton(btn_frame, text="ℹ️ Sobre", width=80, command=self._show_about).pack(side='right', padx=(5, 0))
        ctk.CTkButton(btn_frame, text="💾 Salvar", width=80, command=self._save_all, fg_color=self.colors["success"]).pack(side='right', padx=5)
        
        # O botão 'Atualizar' agora funciona corretamente
        ctk.CTkButton(btn_frame, text="🔄 Atualizar", width=80, command=self.refresh_all).pack(side='right', padx=5)
    
    def _update_header_status(self, connected: bool, connection_type: str = 'Serial'):
        self.after(0, lambda: self._set_header_visuals(connected, connection_type))
        
    def _set_header_visuals(self, connected: bool, connection_type: str = 'Serial'):
        
        # Verifica se os widgets do cabeçalho e conexão foram construídos (evita o erro no __init__)
        if not self.header_status_dot or not self.connect_btn:
             return 

        status_color = self.colors["success"] if connected else self.colors["danger"]
        
        # Usa 'USB' no lugar de 'Serial' apenas na interface visual
        visual_connection_type = "USB" if connection_type == "Serial" else connection_type
        status_text = f"Conectado ({visual_connection_type})" if connected else "Desconectado"
        
        self.header_status_dot.configure(text_color=status_color)
        self.header_status_text.configure(text=status_text, text_color=status_color)

        # Ajuste o estado do botão Connect/Disconnect
        state_conn = 'disabled' if connected else 'normal'
        state_disc = 'normal' if connected else 'disabled'
        
        self.connect_btn.configure(state=state_conn)
        self.disconnect_btn.configure(state=state_disc)

        # --- Lógica de ativação/desativação da aba de configurações de conexão ---
        # Verifica se os widgets de configuração existem (Serial e Wi-Fi)
        if self.port_option and self.ip_entry:
            if connected:
                # Desativa a edição da aba ativa
                # Desativa todos os campos de configuração
                self.port_option.configure(state='disabled')
                self.baud_option.configure(state='disabled')
                self.refresh_ports_btn.configure(state='disabled')
                self.ip_entry.configure(state='disabled')
                self.port_entry.configure(state='disabled')
                self.search_btn.configure(state='disabled')
            else:
                # Se não estiver conectado, reativa apenas o modo selecionado
                conn_type_ui = self.connection_type_var.get()
                
                # Desativa todos para garantir que apenas o correto seja ativado
                self.port_option.configure(state='disabled')
                self.baud_option.configure(state='disabled')
                self.refresh_ports_btn.configure(state='disabled')
                self.ip_entry.configure(state='disabled')
                self.port_entry.configure(state='disabled')
                self.search_btn.configure(state='disabled')

                if conn_type_ui == 'USB': # Usa a string atualizada para o check
                    self.port_option.configure(state='normal')
                    self.baud_option.configure(state='normal')
                    self.refresh_ports_btn.configure(state='normal')
                elif conn_type_ui == 'Wi-Fi':
                    self.ip_entry.configure(state='normal')
                    self.port_entry.configure(state='normal')
                    self.search_btn.configure(state='normal')


        # --- Status Card Detalhes (NOVA LÓGICA) ---
        
        # Garante que os widgets do novo Status Card existam
        if not self.dash_icon or not self.status_card or not self.connection_details_textbox:
            return
            
        self.dash_icon.configure(text_color="white")
        self.dash_status_text.configure(text_color=status_color)
        self.status_card.configure(border_color=status_color)
        
        # Limpa e prepara o Textbox para receber detalhes
        self.connection_details_textbox.configure(state="normal")
        self.connection_details_textbox.delete('1.0', 'end')

        if connected:
            self.dash_icon.configure(text="⚡")
            self.dash_status_text.configure(text="CONECTADO")
            self.dash_sub_text.configure(text=f"Pronto para receber comandos via {visual_connection_type}.")
            
            detail_log = f"--- Detalhes da Conexão ---\n"
            
            if connection_type == 'Serial':
                current_port = self.config.data.get('serial', {}).get('port', '-')
                current_baud = self.config.data.get('serial', {}).get('baud', '-')
                detail_log += f"Protocolo: USB/UART\n"
                detail_log += f"Porta Ativa: {current_port}\n"
                detail_log += f"Velocidade: {current_baud} bps"
            
            elif connection_type == 'Wi-Fi':
                detail_log += f"Protocolo: TCP/IP\n"
                detail_log += f"IP Ativo: {self.wifi_manager.host}\n"
                detail_log += f"Porta: {self.wifi_manager.port}"

            self.connection_details_textbox.insert("end", detail_log)
            
        else:
            self.dash_icon.configure(text="🔌") 
            self.dash_status_text.configure(text="DESCONECTADO")
            self.dash_sub_text.configure(text="Selecione um método de conexão e clique em conectar.")
            self.connection_details_textbox.insert("end", "Aguardando conexão...\nDetalhes aparecerão aqui após conectar.")
        
        self.connection_details_textbox.configure(state="disabled")

    def _build_buttons_tab(self, parent):
        """Constrói a aba de configuração de botões (Painel Lateral LED + Grid 4x4)."""

        parent.grid_columnconfigure(0, weight=0, minsize=20) # Coluna Esquerda: Largura fixa (LED Control)
        parent.grid_columnconfigure(1, weight=3) # Coluna Central: Expansível (Grid de Botões)
        parent.grid_rowconfigure(0, weight=1)

        # --- Painel Esquerdo: Controle de LED e Funções ---
        left_panel = ctk.CTkFrame(parent, corner_radius=10)
        left_panel.grid(row=0, column=0, sticky="nsew", padx=(10, 5), pady=10)
        self._build_led_control_panel(left_panel) 

        # --- Painel Central: Grid de Botões ---
        grid_container = ctk.CTkFrame(parent, fg_color="transparent")
        grid_container.grid(row=0, column=1, sticky="nsew", padx=5, pady=10)
        
        # O centering_frame é importante para manter o grid centralizado mesmo em telas grandes
        self.centering_frame = ctk.CTkFrame(grid_container, fg_color="transparent")
        self.centering_frame.place(relx=0.5, rely=0.5, anchor='center')
        
        cols = 4
        rows = BUTTON_COUNT // cols

        for c in range(cols):
            self.centering_frame.grid_columnconfigure(c, weight=1)

        for r in range(rows):
            self.centering_frame.grid_rowconfigure(r, weight=1)

        btn_id = 1
        for row in range(4):
            for col in range(4):
                key = str(btn_id)
                self._create_button_frame(self.centering_frame, key, row, col)
                btn_id += 1

    def _create_button_frame(self, parent, key, row, col):
        """Cria o frame de visualização de um único botão, tornando-o clicável e adicionando hover."""
        
        # Se o botão já existe, destrói o frame antigo
        if key in self.button_frames and 'frame' in self.button_frames[key]:
            self.button_frames[key]['frame'].destroy()
            
        # 1. Cria o novo Frame do Botão - DIMENSÃO QUADRADA (100x100)
        btn_frame = ctk.CTkFrame(parent, width=100, height=100, corner_radius=10, border_width=2, border_color=self.colors["secondary"]) 
        btn_frame.grid_propagate(False)
        btn_frame.grid(row=row, column=col, padx=3, pady=3, sticky="nsew")
        
        # --- DEFINE COR DE FUNDO PADRÃO ---
        default_fg_color = parent.cget("fg_color")
        btn_frame.configure(fg_color=default_fg_color) 

        # 2. Widgets internos
        # ... (código que cria icon_label e title_label)
        icon_label = ctk.CTkLabel(btn_frame, text='📱', width=80, height=80, font=ctk.CTkFont(size=20), text_color=self.colors["primary"]) 
        icon_label.pack(pady=(10, 10), expand=True) 
        btn_frame.pack_propagate(False)

        title_label = ctk.CTkLabel(btn_frame, text="", font=ctk.CTkFont(size=1, weight="bold")) 
        title_label.pack_forget() 
        # ... (fim dos widgets internos)
        
        # 3. Adiciona o efeito Hover
        
        def set_hover_on(widget):
            widget.configure(border_color=self.colors["primary"]) 
            widget.configure(fg_color=self.colors["secondary"])

        def set_hover_off(widget):
            widget.configure(border_color=self.colors["secondary"])
            widget.configure(fg_color=default_fg_color)
        
        # --- LIGAÇÃO DIRETA DOS EVENTOS DE HOVER ---
        
        # HOVER NO FRAME PAI
        btn_frame.bind('<Enter>', lambda e: set_hover_on(btn_frame))
        btn_frame.bind('<Leave>', lambda e: set_hover_off(btn_frame))
        icon_label.bind('<Enter>', lambda e: set_hover_on(btn_frame))
        icon_label.bind('<Leave>', lambda e: set_hover_off(btn_frame))


        # 4. Torna o FRAME inteiro clicável (O _make_clickable agora SÓ LIGA O CURSOR E O CLIQUE)
        self._make_clickable(btn_frame, lambda i=key: self.open_button_config(i))

        # 5. Armazena a referência
        self.button_frames[key] = {'frame': btn_frame, 'icon_label': icon_label, 'title_label': title_label, 'grid_row': row, 'grid_col': col}

    def _build_led_control_panel(self, parent):
        """Constrói o painel lateral de controle de LED."""
        # Título Principal
        ctk.CTkLabel(
            parent, 
            text="💡 Controle de LED", 
            font=ctk.CTkFont(size=16, weight="bold")
        ).pack(pady=(10, 10), padx=5)

        parent.columnconfigure(0, weight=1) 
        
        # --- 1. Controle de Cor Individual (Ajuste Rápido) ---
        individual_frame = ctk.CTkFrame(
            parent, 
            corner_radius=8, 
            border_width=1, 
            border_color=self.colors["secondary"]
        )
        individual_frame.pack(fill='x', padx=10, pady=(5, 10)) 
        
        # Configurando colunas para alinhar ComboBox, Preview e Botão
        individual_frame.columnconfigure((0, 2), weight=0)
        individual_frame.columnconfigure(1, weight=1)      
        
        # Título do Subpainel
        ctk.CTkLabel(
            individual_frame, 
            text="Ajuste Rápido de Cor",
            font=ctk.CTkFont(weight="bold", size=13)
        ).grid(row=0, column=0, columnspan=3, sticky='w', padx=10, pady=(8, 5))
        
        # Linha de Seleção e Paleta
        
        # 1. Seleção do Botão (ComboBox)
        button_keys = [str(i) for i in range(1, BUTTON_COUNT + 1)] 
        self.led_button_select = ctk.CTkComboBox(
            individual_frame, 
            values=button_keys,
            command=self._update_quick_led_preview,
            width=60,
            state='readonly'
        )
        self.led_button_select.set(button_keys[0])
        self.led_button_select.grid(row=1, column=0, padx=(10, 5), pady=5, sticky='w')
        
        # 2. Pré-visualização da Cor
        self.quick_led_color_var = tk.StringVar(value="#FFFFFF")
        self.quick_color_preview = ctk.CTkLabel(
            individual_frame, 
            text=" ", 
            width=25, 
            height=25, 
            corner_radius=5, 
            fg_color="#FFFFFF",
            bg_color=individual_frame.cget("fg_color") 
        )
        self.quick_color_preview.grid(row=1, column=1, padx=5, pady=5, sticky='w')
        
        # 3. Botão Paleta
        ctk.CTkButton(
            individual_frame, 
            text="🎨", 
            command=self._open_quick_color_picker, 
            width=30, 
            fg_color=self.colors["primary"]
        ).grid(row=1, column=2, padx=(5, 10), pady=5, sticky='e')

        # 4. Botão Aplicar (Ocupa toda a linha)
        ctk.CTkButton(
            individual_frame, 
            text="⚡ Aplicar Cor", 
            command=self._send_quick_led_color_command,
            fg_color=self.colors["primary"] 
        ).grid(row=2, column=0, columnspan=3, sticky='ew', padx=10, pady=(5, 10))
        
        # ----------------------------------------------
        # --- 2. Controles Globais (Empilhados) ---
        # ----------------------------------------------
        global_frame = ctk.CTkFrame(
            parent, 
            corner_radius=8, 
            border_width=1, 
            border_color=self.colors["secondary"]
        )
        global_frame.pack(fill='x', padx=10, pady=(5, 15))
        
        # Configura o grid para ter apenas UMA coluna que se expande
        global_frame.columnconfigure(0, weight=1) 
        
        # Título do Subpainel
        ctk.CTkLabel(
            global_frame, 
            text="Comandos Globais", 
            font=ctk.CTkFont(weight="bold", size=13)
        ).grid(row=0, column=0, sticky='w', padx=10, pady=(8, 5), columnspan=1)
        
        row_idx = 1
        
        # Botão Ligar (Verde / Success)
        ctk.CTkButton(
            global_frame, 
            text="Ligar Todos",
            command=lambda: self._send_all_led_command("ON"),
            fg_color=self.colors["success"],
            hover_color="#1e7e34"
        ).grid(row=row_idx, column=0, sticky='ew', padx=10, pady=(5, 5)) # padx total de 10
        row_idx += 1
        
        # Botão Desligar (Vermelho / Danger)
        ctk.CTkButton(
            global_frame, 
            text="Desligar Todos",
            command=lambda: self._send_all_led_command("OFF"),
            fg_color=self.colors["danger"],
            hover_color="#c82333"
        ).grid(row=row_idx, column=0, sticky='ew', padx=10, pady=5)
        row_idx += 1

        # Botão Efeito Arco-Íris
        ctk.CTkButton(
            global_frame, 
            text="Efeito Arco-Íris 🌈", 
            command=lambda: self._send_all_led_command("RAINBOW"),
            fg_color=self.colors["primary"] 
        ).grid(row=row_idx, column=0, sticky='ew', padx=10, pady=5)
        row_idx += 1
        
        # Botão Efeito Piscante
        ctk.CTkButton(
            global_frame, 
            text="Efeito Piscante ✨", 
            command=lambda: self._send_all_led_command("BLINK"),
            fg_color=self.colors["primary"] 
        ).grid(row=row_idx, column=0, sticky='ew', padx=10, pady=(5, 10)) # pady final maior
        row_idx += 1

        self._update_quick_led_preview()

    def _update_quick_led_preview(self, *args):
        """Atualiza a pré-visualização de cor rápida baseada na cor salva do botão selecionado."""
        selected_key = self.led_button_select.get()
        btn_conf = self.config.data.get('buttons', {}).get(selected_key, {})
        color = btn_conf.get('led_color', '#FFFFFF')
        
        self.quick_led_color_var.set(color)
        self.quick_color_preview.configure(fg_color=color)

    def _open_quick_color_picker(self):
        """Abre o seletor de cores nativo e atualiza a cor rápida e o conf.data."""
        color_code = colorchooser.askcolor(title="Escolha a Cor do LED")
        
        if color_code and color_code[1]:
            hex_color = color_code[1].upper()
            
            # 1. Atualiza a UI e a variável
            self.quick_led_color_var.set(hex_color)
            self.quick_color_preview.configure(fg_color=hex_color)
            
            selected_key = self.led_button_select.get()
            
            # 2. Salva a cor na configuração do botão selecionado
            if selected_key in self.config.data['buttons']:
                self.config.data['buttons'][selected_key]['led_color'] = hex_color
                self.config.save()
                self.logger.info(f"Cor do LED do Botão {selected_key} atualizada para {hex_color} (localmente).")
            
            # 3. Envia imediatamente via serial/wifi
            self._send_quick_led_color_command()

    def _send_quick_led_color_command(self):
        """Envia o comando de cor do LED via conexão ativa (usando a cor rápida selecionada)."""
        selected_key = self.led_button_select.get()
        color = self.quick_led_color_var.get() 
        self._send_led_color_command(selected_key, color)

    def _send_led_color_command(self, button_key: str, color_hex: str):
        """Envia o comando de cor do LED via conexão ativa (Serial ou Wi-Fi)."""
        
        is_serial = self.serial_manager.is_connected
        is_wifi = self.wifi_manager.is_connected
        
        if not is_serial and not is_wifi:
            self.logger.warn("❌ Não conectado: Conecte ao USB ou Wi-Fi para enviar comandos de LED.")
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showwarning
            CTkMessageDialog.showwarning(self, "Aviso", "Não é possível enviar comandos de LED. Conecte primeiro.", self.logger)
            return

        # Formato de Comando Sugerido para WS2812B: LED:<ID>:<RRGGBB>
        try:
            led_index = int(button_key) - 1 
            hex_payload = color_hex.lstrip('#') 
            
            command = f"LED:{led_index}:{hex_payload}" # Sem o '\n' para ser adicionado pelo send_command
            
            if is_serial:
                self.serial_manager.send_command(command)
                self.logger.info(f"⚡ Enviado serial: {command}")
            elif is_wifi:
                self.wifi_manager.send_command(command)
                self.logger.info(f"⚡ Enviado Wi-Fi: {command}")
            
        except Exception as e:
            self.logger.error(f"Erro ao formatar/enviar comando LED: {e}")

    def _send_all_led_command(self, command: str):
        """Envia um comando para todos os LEDs (Ex: ON, OFF, RAINBOW)"""
        is_serial = self.serial_manager.is_connected
        is_wifi = self.wifi_manager.is_connected
        
        if not is_serial and not is_wifi:
            self.logger.warn("❌ Não conectado: Conecte à porta serial ou Wi-Fi para enviar comandos de LED.")
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showwarning
            CTkMessageDialog.showwarning(self, "Aviso", "Não é possível enviar comandos de LED. Conecte primeiro.", self.logger)
            return

        # Formato de Comando Sugerido: ALL_LED:<COMMAND>
        try:
            cmd = f"ALL_LED:{command}"
            
            if is_serial:
                self.serial_manager.send_command(cmd)
                self.logger.info(f"⚡ Enviado serial (Comando Global LED): {cmd}")
            elif is_wifi:
                self.wifi_manager.send_command(cmd)
                self.logger.info(f"⚡ Enviado Wi-Fi (Comando Global LED): {cmd}")
            
        except Exception as e:
            self.logger.error(f"Erro ao enviar comando global LED: {e}")


    def _backup_config(self): 
        try:
            path = self.config.backup()
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showinfo
            CTkMessageDialog.showinfo(self, "Backup", f"Configuração salva em:\n{path}", self.logger)
        except RuntimeError:
            pass # Cancelado
        except Exception as e:
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showerror
            CTkMessageDialog.showerror(self, "Erro", f"Falha ao salvar backup: {e}", self.logger)

    def _restore_config(self):
        # SUBSTITUIÇÃO: Usando CTkMessageDialog.askyesno
        if not CTkMessageDialog.askyesno(self, "Restaurar Configuração", "Isto substituirá a configuração atual. Continuar?", self.logger):
            return
        try:
            path = self.config.restore()
            self.refresh_all()
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showinfo
            CTkMessageDialog.showinfo(self, "Restauração", f"Configuração carregada de:\n{path}", self.logger)
        except RuntimeError:
            pass # Cancelado
        except Exception as e:
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showerror
            CTkMessageDialog.showerror(self, "Erro", f"Falha ao restaurar: {e}", self.logger)

    def _build_connection_status_card(self, parent):
        """
        Cria o novo Status Card compacto e informativo.
        Substitui o Status Card anterior (self.status_card, self.dash_icon, etc.).
        """
        
        # Este card é menor e usa um fundo 'dark' para contraste, 
        # independentemente do tema Light/Dark.
        self.status_card = ctk.CTkFrame(
            parent, 
            corner_radius=10, 
            fg_color=self.colors["dark"], # Fundo escuro para contraste
            border_width=2, 
            border_color=self.colors["danger"] # Cor de borda inicial (Desconectado)
        )
        self.status_card.pack(fill='x', padx=10, pady=(20, 10))
        
        # Configurações do Grid interno (Ícone + Título + Subtítulo)
        self.status_card.columnconfigure(0, weight=0) # Ícone
        self.status_card.columnconfigure(1, weight=1) # Texto
        
        # Ícone de Status (Coluna 0)
        self.dash_icon = ctk.CTkLabel(
            self.status_card, 
            text="🔌", 
            font=ctk.CTkFont(size=30), 
            text_color="white"
        )
        self.dash_icon.grid(row=0, column=0, rowspan=2, padx=15, pady=10, sticky='nsew')

        # Status Principal (Coluna 1, Linha 0)
        self.dash_status_text = ctk.CTkLabel(
            self.status_card, 
            text="DESCONECTADO", 
            font=ctk.CTkFont(size=16, weight="bold"), 
            text_color=self.colors["danger"],
            anchor='w'
        )
        self.dash_status_text.grid(row=0, column=1, padx=(0, 15), pady=(10, 0), sticky='w')

        # Detalhe / Subtítulo (Coluna 1, Linha 1)
        self.dash_sub_text = ctk.CTkLabel(
            self.status_card, 
            text="Selecione um método de conexão e clique em conectar.", 
            font=ctk.CTkFont(size=12), 
            text_color="gray",
            anchor='w'
        )
        self.dash_sub_text.grid(row=1, column=1, padx=(0, 15), pady=(0, 10), sticky='w')
        
        self.details_frame = self.status_card
        
        # Remove a construção dos antigos detalhes:
        self.lbl_detail_port = None
        self.lbl_detail_baud = None
        self.lbl_detail_proto = None

    def _build_connection_tab(self, parent):
        
        # O estado inicial do Connection Type é lido do config.data
        initial_conn_type = self.connection_type_var.get()

        # Configurações do Grid: 2 Colunas de peso igual
        parent.grid_columnconfigure((0, 1), weight=1) 
        parent.grid_rowconfigure(0, weight=1)
        
        # --- COLUNA 0: Configurações de Conexão (Serial/Wi-Fi) ---
        config_frame = ctk.CTkFrame(parent, corner_radius=10)
        config_frame.grid(row=0, column=0, sticky="nsew", padx=(10, 5), pady=10)
        
        ctk.CTkLabel(config_frame, text="⚙️ Configuração", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=(20, 15))
        
        # 1. Segmented Button para escolher o tipo de conexão
        self.connection_type_switcher = ctk.CTkSegmentedButton(
            config_frame, 
            values=['USB', 'Wi-Fi'], 
            command=self._on_connection_type_change,
            variable=self.connection_type_var
        )
        self.connection_type_switcher.pack(fill='x', padx=15, pady=(0, 20))
        
        # Se a config antiga for 'Serial', atualiza a variável para 'USB' no início para a UI
        if initial_conn_type == 'Serial':
            self.connection_type_var.set('USB')
        
        # 2. Frame Container para as configurações específicas
        self.connection_settings_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        self.connection_settings_frame.pack(fill='both', expand=True, padx=15, pady=(0, 10))
        
        # 3. Frames de Serial e Wi-Fi (construídos)
        self._build_serial_settings_frame()
        self._build_wifi_settings_frame()
        
        # 4. Ação Frame
        action_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        action_frame.pack(fill='x', padx=15, pady=(0, 20))

        self.connect_btn = ctk.CTkButton(
            action_frame, 
            text="🔗 Conectar", 
            command=self._connect_any, 
            fg_color=self.colors["success"]
        )
        self.connect_btn.pack(fill='x', pady=(0, 10))

        self.disconnect_btn = ctk.CTkButton(
            action_frame, 
            text="🔓 Desconectar", 
            command=self._disconnect_any, 
            state='disabled', 
            fg_color=self.colors["danger"]
        )
        self.disconnect_btn.pack(fill='x')
        
        self._on_connection_type_change(self.connection_type_var.get())
        
        # --- COLUNA 1: Status e Detalhes ---
        status_and_details_frame = ctk.CTkFrame(parent, corner_radius=10)
        status_and_details_frame.grid(row=0, column=1, sticky="nsew", padx=(5, 10), pady=10)
        
        self._build_connection_status_card(status_and_details_frame) 
        
        log_card = ctk.CTkFrame(status_and_details_frame, corner_radius=10)
        log_card.pack(fill='both', expand=True, padx=10, pady=(10, 20))
        
        ctk.CTkLabel(log_card, text="Detalhes da Conexão", font=ctk.CTkFont(size=14, weight="bold")).pack(anchor='w', padx=15, pady=(10, 5))
        
        self.connection_details_textbox = ctk.CTkTextbox(log_card, height=100, state='disabled')
        self.connection_details_textbox.pack(fill='both', expand=True, padx=15, pady=(0, 15))

        self._set_header_visuals(self.serial_manager.is_connected or self.wifi_manager.is_connected, 'Serial' if self.connection_type_var.get() == 'USB' else self.connection_type_var.get())


    def _build_serial_settings_frame(self):
        self.serial_frame = ctk.CTkFrame(self.connection_settings_frame, fg_color="transparent")
        
        port_frame = ctk.CTkFrame(self.serial_frame, fg_color="transparent")
        port_frame.pack(fill='x', pady=5)
        
        ctk.CTkLabel(port_frame, text="Porta USB (COM):", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        self.port_option = ctk.CTkOptionMenu(port_frame, values=['Nenhuma'], width=200)
        self.port_option.pack(fill='x', pady=(5, 5))
        
        self.refresh_ports_btn = ctk.CTkButton(
            port_frame, 
            text="🔄 Atualizar Portas", 
            command=self.update_serial_ports,
            fg_color=COLORS["dark"],
            height=24
        )
        self.refresh_ports_btn.pack(fill='x')

        ctk.CTkFrame(self.serial_frame, height=2, fg_color=self.colors["secondary"]).pack(fill='x', padx=5, pady=20)

        baud_frame = ctk.CTkFrame(self.serial_frame, fg_color="transparent")
        baud_frame.pack(fill='x')
        
        ctk.CTkLabel(baud_frame, text="Velocidade (Baud):", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        baud_rates = ['9600', '19200', '38400', '57600', '115200', '230400']
        self.baud_option = ctk.CTkOptionMenu(baud_frame, values=baud_rates, command=self._on_baud_change)
        self.baud_option.set(str(self.config.data.get('serial', {}).get('baud', DEFAULT_SERIAL_BAUD)))
        self.baud_option.pack(fill='x', pady=5)

    def _build_wifi_settings_frame(self):
        # Valor padrão do IP
        DEFAULT_IP = '192.168.1.100'
        
        # 1. Tenta carregar o IP salvo. Se não houver, retorna o DEFAULT_IP.
        initial_ip = self.config.data.get('wifi', {}).get('ip', DEFAULT_IP)
        initial_port = self.config.data.get('wifi', {}).get('port', 8000)

        self.wifi_frame = ctk.CTkFrame(self.connection_settings_frame, fg_color="transparent")
        
        ip_frame = ctk.CTkFrame(self.wifi_frame, fg_color="transparent")
        ip_frame.pack(fill='x', pady=5)
        
        ctk.CTkLabel(ip_frame, text="Endereço IP do ESP32:", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        ip_input_frame = ctk.CTkFrame(ip_frame, fg_color="transparent")
        ip_input_frame.pack(fill='x')
        
        # 2. Define o placeholder_text usando o IP padrão como exemplo.
        self.ip_entry = ctk.CTkEntry(ip_input_frame, placeholder_text=f"Ex: {DEFAULT_IP}")
        
        # 3. Só insere o valor salvo se ele for diferente do valor padrão ou se for vazio.
        if initial_ip and initial_ip != DEFAULT_IP:
            self.ip_entry.insert(0, initial_ip)
            
        self.ip_entry.pack(side='left', fill='x', expand=True, pady=5, padx=(0, 5))
        
        self.search_btn = ctk.CTkButton(
            ip_input_frame, 
            text="🔍 Buscar", 
            command=self._search_wifi_device,
            fg_color=self.colors["primary"],
            width=80
        )
        self.search_btn.pack(side='right', pady=5)

        ctk.CTkFrame(self.wifi_frame, height=2, fg_color=self.colors["secondary"]).pack(fill='x', padx=5, pady=20)
        
        port_frame = ctk.CTkFrame(self.wifi_frame, fg_color="transparent")
        port_frame.pack(fill='x', pady=5)
        
        ctk.CTkLabel(port_frame, text="Porta TCP:", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        self.port_entry = ctk.CTkEntry(port_frame, placeholder_text="Ex: 8000")
        self.port_entry.insert(0, str(initial_port))
        self.port_entry.pack(fill='x', pady=5)

    def _on_connection_type_change(self, value):
        for widget in self.connection_settings_frame.winfo_children():
            widget.pack_forget()
            
        if value == 'USB' or value == 'Serial':
            self.serial_frame.pack(fill='both', expand=True)
            saved_value = 'Serial'
        
            if value == 'USB':
                 self.connection_type_var.set('USB') 
            
        elif value == 'Wi-Fi':
            self.wifi_frame.pack(fill='both', expand=True)
            saved_value = 'Wi-Fi'

        # Salva o tipo de conexão preferido (usando 'Serial' ou 'Wi-Fi' internamente)
        self.config.data.setdefault('serial', {})['type'] = saved_value
        self.config.save()
        
        # Se um estiver conectado, o outro deve ser desconectado.
        is_serial_selected = saved_value == 'Serial'
        
        if is_serial_selected and self.wifi_manager.is_connected:
            self._disconnect_any()
        elif not is_serial_selected and self.serial_manager.is_connected:
            self._disconnect_any()
            
        # Atualiza o estado dos botões de conectar/desconectar na UI
        self._set_header_visuals(self.serial_manager.is_connected or self.wifi_manager.is_connected, saved_value)

    def _connect_any(self):
        # Mapeia a string da UI de volta para a lógica interna
        conn_type_ui = self.connection_type_var.get()
        conn_type_logic = 'Serial' if conn_type_ui == 'USB' else conn_type_ui
        
        # Desconecta o manager não selecionado primeiro
        if conn_type_logic == 'Serial' and self.wifi_manager.is_connected:
            self.wifi_manager.disconnect()
        elif conn_type_logic == 'Wi-Fi' and self.serial_manager.is_connected:
            self.serial_manager.disconnect()

        # Tenta conectar
        if conn_type_logic == 'Serial':
            self._connect_serial()
        elif conn_type_logic == 'Wi-Fi':
            self._connect_wifi()
            
    def _disconnect_any(self):
        self.serial_manager.disconnect()
        self.wifi_manager.disconnect()
        
    def _connect_serial(self):
        port = self.port_option.get()
        if not port or port == 'Nenhuma': 
            self.logger.warn("Selecione uma porta válida para conectar.")
            return
            
        baud = int(self.baud_option.get())
        if self.serial_manager.connect(port, baud):
            self.config.data['serial']['port'] = port
            self.config.data['serial']['baud'] = baud
            self.config.save()

    def _connect_wifi(self):
        host = self.ip_entry.get().strip()
        try:
            port = int(self.port_entry.get().strip())
        except ValueError:
            self.logger.error("Porta inválida.")
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showerror
            CTkMessageDialog.showerror(self, "Erro", "A porta deve ser um número válido (Ex: 8000).", self.logger)
            return

        if not host or port <= 0:
            self.logger.warn("Preencha o IP e a Porta.")
            return

        # Tenta conectar o Wi-Fi
        if self.wifi_manager.connect(host, port):
            # Salva a configuração Wi-Fi
            self.config.data.setdefault('wifi', {})['ip'] = host
            self.config.data.setdefault('wifi', {})['port'] = port
            self.config.save()
        
    def _search_wifi_device(self):
        self.search_btn.configure(state='disabled', text="Buscando...")
        self.ip_entry.configure(state='disabled')
        
        t = threading.Thread(target=self._run_search_device, daemon=True)
        t.start()
        
    def _run_search_device(self):
        found_ip = self.wifi_manager.search_device()
        self.after(0, lambda: self._process_search_result(found_ip))

    def _process_search_result(self, ip: Optional[str]):
        self.search_btn.configure(state='normal', text="🔍 Buscar")
        self.ip_entry.configure(state='normal')
        
        if ip:
            self.ip_entry.delete(0, 'end')
            self.ip_entry.insert(0, ip)
            self.logger.info(f"Dispositivo Wi-Fi encontrado em: {ip}")
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showinfo
            CTkMessageDialog.showinfo(self, "Sucesso", f"Dispositivo encontrado em: {ip}", self.logger)
        else:
            self.logger.warn("Dispositivo Wi-Fi não encontrado.")
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showwarning
            CTkMessageDialog.showwarning(self, "Aviso", "Dispositivo não encontrado. Verifique o Wi-Fi do ESP32.", self.logger)

    def update_serial_ports(self):
        ports = self.serial_manager.list_ports() or ['Nenhuma']
        self.port_option.configure(values=ports)
        try:
            curr = self.config.data.get('serial', {}).get('port', '')
            self.port_option.set(curr if curr in ports else ports[0])
        except: self.port_option.set(ports[0])

    def _clear_log(self):
        self.log_textbox.configure(state='normal')
        self.log_textbox.delete('1.0', 'end')
        self.log_textbox.configure(state='disabled')
        
    def _on_baud_change(self, value):
        self.config.data['serial']['baud'] = int(value)
        self.config.save()
        
    def _save_all(self):
        if self.config.save(): 
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showinfo
            CTkMessageDialog.showinfo(self, "Sucesso", "Configurações salvas!", self.logger)
        else: 
            # SUBSTITUIÇÃO: Usando CTkMessageDialog.showerror
            CTkMessageDialog.showerror(self, "Erro", "Erro ao salvar!", self.logger)
            
    def _show_about(self): AboutDialog(self)

    def _build_settings_tab(self, parent):
        parent.grid_columnconfigure(0, weight=1)
        parent.grid_columnconfigure(1, weight=1)
        parent.grid_rowconfigure(0, weight=1)
        parent.grid_rowconfigure(1, weight=0)

        visual_frame = ctk.CTkFrame(parent, corner_radius=10)
        visual_frame.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        
        ctk.CTkLabel(visual_frame, text="🎨 Aparência Visual", font=ctk.CTkFont(size=18, weight="bold"), text_color=self.colors["secondary"]).pack(pady=(20, 15))
        ctk.CTkLabel(visual_frame, text="Personalize as cores e o tema", font=ctk.CTkFont(size=12), text_color="gray").pack(pady=(0, 20))

        theme_box = ctk.CTkFrame(visual_frame, fg_color="transparent")
        theme_box.pack(fill='x', padx=20, pady=10)
        ctk.CTkLabel(theme_box, text="Modo do Tema", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        self.theme_menu = ctk.CTkOptionMenu(
            theme_box, 
            values=['System', 'Claro', 'Escuro'], 
            command=self._on_theme_change,
            height=35
        )
        self.theme_menu.pack(fill='x', pady=(5, 0))
        curr_theme = self.config.data.get('appearance', {}).get('theme', 'System')
        display_theme = {'System': 'System', 'Light': 'Claro', 'Dark': 'Escuro'}.get(curr_theme, 'System')
        self.theme_menu.set(display_theme)

        color_box = ctk.CTkFrame(visual_frame, fg_color="transparent")
        color_box.pack(fill='x', padx=20, pady=10)
        ctk.CTkLabel(color_box, text="Esquema de Cores (Botões)", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        self.color_scheme_menu = ctk.CTkOptionMenu(
            color_box, 
            values=['Padrão', 'Moderno', 'Vibrante', 'Suave', 'Escuro Total'], 
            command=self._on_color_scheme_change,
            height=35
        )
        self.color_scheme_menu.pack(fill='x', pady=(5, 0))
        self.color_scheme_menu.set(self.config.data.get('appearance', {}).get('color_scheme', 'Padrão'))

        font_box = ctk.CTkFrame(visual_frame, fg_color="transparent")
        font_box.pack(fill='x', padx=20, pady=10)
        ctk.CTkLabel(font_box, text="Tamanho do Texto", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        self.font_size_menu = ctk.CTkOptionMenu(
            font_box, 
            values=['Pequeno', 'Médio', 'Grande'], 
            command=self._on_font_size_change,
            height=35
        )
        self.font_size_menu.pack(fill='x', pady=(5, 0))
        self.font_size_menu.set(self.config.data.get('appearance', {}).get('font_size', 'Médio'))

        system_frame = ctk.CTkFrame(parent, corner_radius=10)
        system_frame.grid(row=0, column=1, sticky="nsew", padx=10, pady=10)

        ctk.CTkLabel(system_frame, text="⚙️ Sistema & Janela", font=ctk.CTkFont(size=18, weight="bold"), text_color=self.colors["secondary"]).pack(pady=(20, 15))
        ctk.CTkLabel(system_frame, text="Comportamento e opacidade", font=ctk.CTkFont(size=12), text_color="gray").pack(pady=(0, 20))

        tray_box = ctk.CTkFrame(system_frame, fg_color="transparent", border_width=1, border_color=self.colors["secondary"], corner_radius=8)
        tray_box.pack(fill='x', padx=20, pady=10, ipady=5)
        
        tray_label_frame = ctk.CTkFrame(tray_box, fg_color="transparent")
        tray_label_frame.pack(side='left', padx=15)
        
        ctk.CTkLabel(tray_label_frame, text="Minimizar para Bandeja", font=ctk.CTkFont(weight="bold"), anchor="w").pack(anchor="w")
        ctk.CTkLabel(tray_label_frame, text="O app continua rodando ao fechar", font=ctk.CTkFont(size=11), text_color="gray", anchor="w").pack(anchor="w")

        self.minimize_tray_switch = ctk.CTkSwitch(
            tray_box, 
            text="", 
            command=self._on_minimize_tray_change,
            onvalue=True, 
            offvalue=False,
            width=50
        )
        self.minimize_tray_switch.pack(side='right', padx=15)
        
        if self.config.data.get('appearance', {}).get('minimize_to_tray', False):
            self.minimize_tray_switch.select()
        else:
            self.minimize_tray_switch.deselect()

        trans_box = ctk.CTkFrame(system_frame, fg_color="transparent")
        trans_box.pack(fill='x', padx=20, pady=20)
        
        header_trans = ctk.CTkFrame(trans_box, fg_color="transparent")
        header_trans.pack(fill='x', pady=(0, 5))
        ctk.CTkLabel(header_trans, text="Transparência da Janela", font=ctk.CTkFont(weight="bold")).pack(side='left')
        self.transparency_label = ctk.CTkLabel(header_trans, text="100%", font=ctk.CTkFont(weight="bold"), text_color=self.colors["secondary"])
        self.transparency_label.pack(side='right')

        self.transparency_var = tk.DoubleVar(value=self.config.data.get('appearance', {}).get('transparency', 1.0))
        
        def update_label(val):
            self.transparency_label.configure(text=f"{int(float(val)*100)}%")
            self._on_transparency_change(val)

        ctk.CTkSlider(
            trans_box, 
            from_=0.5, 
            to=1.0, 
            number_of_steps=20, 
            variable=self.transparency_var, 
            command=update_label,
            height=20
        ).pack(fill='x', pady=5)
        
        update_label(self.transparency_var.get())

        reset_frame = ctk.CTkFrame(parent, fg_color="transparent")
        reset_frame.grid(row=1, column=0, columnspan=2, sticky="ew", padx=20, pady=10)
        
        ctk.CTkLabel(reset_frame, text="Problemas com a interface?", text_color="gray", font=ctk.CTkFont(size=12)).pack(side='left')
        
        ctk.CTkButton(
            reset_frame, 
            text="🔄 Restaurar Configurações Padrão", 
            command=self._reset_appearance,
            fg_color=COLORS["dark"], 
            border_width=1,
            border_color=self.colors["secondary"],
            height=32
        ).pack(side='right')
        
    def _on_theme_change(self, value):
        theme_map = {'System': 'System', 'Claro': 'Light', 'Escuro': 'Dark'}
        english = theme_map.get(value, 'System')
        ctk.set_appearance_mode(english)
        self.config.data.setdefault('appearance', {})['theme'] = english
        self.config.save()
        self._on_color_scheme_change(self.color_scheme_menu.get())

    def _build_update_tab(self, parent):
        parent.grid_columnconfigure(0, weight=1)
        parent.grid_rowconfigure(0, weight=1)
        
        main_container = ctk.CTkFrame(parent, corner_radius=10, fg_color="transparent")
        main_container.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        
        cards_frame = ctk.CTkFrame(main_container, fg_color="transparent")
        cards_frame.pack(fill='x', pady=(10, 20))
        
        self._create_version_card(cards_frame, "Sua Versão", APP_VERSION, self.colors["secondary"], "left")
        
        arrow_label = ctk.CTkLabel(cards_frame, text="➜", font=ctk.CTkFont(size=24), text_color="gray")
        arrow_label.pack(side='left', padx=10)
        
        # self.remote_version_var é inicializado no __init__
        self.remote_card_frame = self._create_version_card(cards_frame, "Versão no Servidor", self.remote_version_var, self.colors["dark"], "right")

        action_frame = ctk.CTkFrame(main_container, corner_radius=8, border_width=1, border_color=self.colors["secondary"])
        action_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        self.status_icon_label = ctk.CTkLabel(action_frame, text="☁️", font=ctk.CTkFont(size=40))
        self.status_icon_label.pack(pady=(20, 5))
        
        self.status_title_label = ctk.CTkLabel(action_frame, text="Sistema Pronto", font=ctk.CTkFont(size=16, weight="bold"))
        self.status_title_label.pack(pady=(0, 5))
        
        self.status_detail_label = ctk.CTkLabel(action_frame, text="Clique em 'Verificar' para buscar atualizações.", text_color="gray")
        self.status_detail_label.pack(pady=(0, 20))
        
        self.check_update_btn = ctk.CTkButton(
            action_frame, 
            text="🔍 Verificar Agora", 
            command=self._check_update_thread,
            font=ctk.CTkFont(size=14, weight="bold"),
            height=40,
            fg_color=self.colors["primary"]
        )
        self.check_update_btn.pack(pady=(0, 20))

        self.last_check_label = ctk.CTkLabel(main_container, text="Última verificação: Nunca", font=ctk.CTkFont(size=10), text_color="gray")
        self.last_check_label.pack(side='bottom', pady=5)

    def _create_version_card(self, parent, title, value_var, color, side):
        card = ctk.CTkFrame(parent, corner_radius=10, border_width=2, border_color=color)
        card.pack(side=side, expand=True, fill='both', padx=5)
        
        ctk.CTkLabel(card, text=title, font=ctk.CTkFont(size=12, weight="bold"), text_color="gray").pack(pady=(15, 5))
        
        if isinstance(value_var, tk.StringVar):
            lbl = ctk.CTkLabel(card, textvariable=value_var, font=ctk.CTkFont(size=22, weight="bold"), text_color=color)
        else:
            lbl = ctk.CTkLabel(card, text=value_var, font=ctk.CTkFont(size=22, weight="bold"), text_color=color)
        
        lbl.pack(pady=(0, 15))
        return card

    def refresh_all(self):
        # Reinicia o loader e recria o painel de botões
        self._reset_icon_loader() 
        self.refresh_all_buttons()
        self.update_serial_ports()
        self.logger.info("Interface atualizada")
        
    def refresh_all_buttons(self):
        """
        Recria completamente todos os frames de botão para forçar o redesenho da tela
        e eliminar referências de ícones fantasmas.
        """
        
        if not self.centering_frame:
            # Se o frame principal ainda não existe, sai.
            return

        btn_id = 1
        for row in range(4):
            for col in range(4):
                key = str(btn_id)
                
                self._create_button_frame(self.centering_frame, key, row, col)

                btn_conf = self.config.data.get('buttons', {}).get(key, {})
                icon_path = btn_conf.get('icon', '')
                
                # Tenta carregar o ícone
                ctk_img = None
                if icon_path and os.path.exists(icon_path):
                    ctk_img = self.icon_loader.load_icon_from_path(icon_path)

                widget_map = self.button_frames[key]
                widget_map['title_label'].configure(text=btn_conf.get('label', ''))
                
                if ctk_img:
                    widget_map['icon_label'].configure(image=ctk_img, text='')
                    widget_map['icon_label'].image = ctk_img
                else:
                    widget_map['icon_label'].configure(image=None, text='📱')
                    widget_map['icon_label'].image = None
                
                btn_id += 1

        self.update()

    def open_button_config(self, button_key: str):
        if 'buttons' not in self.config.data:
            self.config.data['buttons'] = self.config._default()['buttons']
            self.config.save()

        if button_key not in self.config.data['buttons']:
            default_conf = self.config._default()['buttons'].get(button_key, {
                "label": "",
                "icon": "",
                "led_color": "#FFFFFF", 
                "action": {"type": "none", "payload": ""}
            })
            self.config.data['buttons'][button_key] = default_conf
            self.config.save()

        conf = self.config.data['buttons'][button_key]

        dlg = ButtonConfigDialog(self, button_key, conf.copy(), self.icon_loader, self.logger) 
        self.wait_window(dlg)
        
        self.config.save()

    def _check_update_thread(self):
        self.check_update_btn.configure(state='disabled', text="Conectando...")
        self.status_title_label.configure(text="Verificando...", text_color=self.colors["primary"])
        self.status_detail_label.configure(text="Aguarde, contatando servidor...")
        self.status_icon_label.configure(text="⏳")
        
        t = threading.Thread(target=self._check_update, daemon=True)
        t.start()

    def _check_update(self):
        time.sleep(0.5) 
        self.logger.info('Iniciando verificação de atualização...')
        res = self.update_checker.check_update()
        self.after(0, lambda: self._process_update_result(res))

    def _process_update_result(self, res):
        timestamp = time.strftime("%H:%M:%S")
        self.last_check_label.configure(text=f"Última verificação: Hoje às {timestamp}")
        self.check_update_btn.configure(state='normal', text="🔍 Verificar Novamente")
        self.check_update_btn.configure(
            command=self._check_update_thread, 
            fg_color=self.colors["primary"],
            text_color="white"
        )

        if not res.get('ok'):
            error_msg = res.get("error", "Desconhecido")
            self.logger.error(f'Falha na atualização: {error_msg}')
            self.status_icon_label.configure(text="❌")
            self.status_title_label.configure(text="Falha na Conexão", text_color=self.colors["danger"])
            if len(error_msg) > 60: error_msg = error_msg[:57] + "..."
            self.status_detail_label.configure(text=error_msg)
            self.remote_version_var.set(APP_VERSION)
            self.remote_card_frame.configure(border_color=self.colors["danger"])
            
        else:
            latest = res.get('latest')
            self.remote_version_var.set(latest)
            
            if res.get('is_new'):
                self.logger.info(f'Atualização encontrada: {latest}')
                self.status_icon_label.configure(text="🎉")
                self.status_title_label.configure(text="Nova Versão Disponível!", text_color=self.colors["warning"])
                self.status_detail_label.configure(text=f"A versão {latest} está pronta para download.")
                self.remote_card_frame.configure(border_color=self.colors["warning"])
                
                self.check_update_btn.configure(
                    text="⬇️ Baixar Atualização",
                    fg_color=self.colors["warning"],
                    text_color="black",
                    command=lambda: webbrowser.open(res.get('download_url'))
                )
                
                # SUBSTITUIÇÃO: Usando CTkMessageDialog.askyesno
                if CTkMessageDialog.askyesno(self, 'Atualização', f'Nova versão {latest} encontrada!\nDeseja abrir a página de download agora?', self.logger):
                    webbrowser.open(res.get('download_url'))
            else:
                self.logger.info('Sistema já está atualizado.')
                self.status_icon_label.configure(text="✅")
                self.status_title_label.configure(text="Você está atualizado", text_color=self.colors["success"])
                self.status_detail_label.configure(text=f"A versão {latest} é a mais recente.")
                self.remote_card_frame.configure(border_color=self.colors["success"])
                self.check_update_btn.configure(fg_color=self.colors["success"], text="🔍 Verificar Novamente")

    def _on_serial_message(self, text: str):
        self.logger.info(f'<- ESP (Serial): {text}')
        self._process_button_message(text)

    def _on_wifi_message(self, text: str):
        self.logger.info(f'<- ESP (Wi-Fi): {text}')
        self._process_button_message(text)

    def _process_button_message(self, text: str):
        if text.startswith('BTN:'):
            # Formato: BTN:<ID>
            try:
                key = text.split(':')[1]
                btn_conf = self.config.data['buttons'].get(key)
                if btn_conf: 
                    self.action_manager.perform(
                        Action(
                            btn_conf.get('action', {}).get('type', 'none'), 
                            btn_conf.get('action', {}).get('payload', '')
                        )
                    )
            except IndexError:
                self.logger.error(f"Mensagem BTN inválida: {text}")

def main():
    ctk.set_appearance_mode('System')
    app = Esp32DeckApp()

    app.deiconify() 
    
    app.protocol('WM_DELETE_WINDOW', app.on_closing)
    app.mainloop()

if __name__ == '__main__':
    main()