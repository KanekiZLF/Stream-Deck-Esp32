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
from dataclasses import dataclass, field
from typing import Dict, Any, Optional, Callable, List

# Windows API
try:
    import win32gui
    import win32con
    import win32process
    WINDOWS_AVAILABLE = platform.system() == 'Windows'
except ImportError:
    WINDOWS_AVAILABLE = False
    
# GUI libs
import customtkinter as ctk
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog 
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
    PYAUTOGUI_AVAILABLE = False

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
APP_VERSION = "2.0.0"
APP_NAME = "Esp32 Deck Controller"
APP_ICON_NAME = "app_icon.ico"
DEVELOPER = "Luiz F. R. Pimentel"
GITHUB_URL = "https://github.com/KanekiZLF/Stream-Deck-Esp32"
UPDATE_CHECK_URL = "https://raw.githubusercontent.com/KanekiZLF/Stream-Deck-Esp32/refs/heads/main/version.txt"
DEFAULT_SERIAL_BAUD = 115200
BUTTON_COUNT = 8
ICON_SIZE = (64, 64)

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
    "text": "#FFFFFF"
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
        if level == "ERROR":
            should_save = True
        elif "Fechando aplicação" in msg or "Porta serial fechada" in msg:
            should_save = True
        
        # Otimização: Apenas salva log crítico e informações de sessão/serial
        if should_save or level == "ERROR" or "Fechando aplicação" in msg or "Porta serial fechada" in msg:
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
                "label": f"Botão {i}",
                "icon": "",
                "action": {"type": "none", "payload": ""}
            }
        return {
            "version": APP_VERSION,
            "buttons": buttons,
            "serial": {"port": "", "baud": DEFAULT_SERIAL_BAUD},
            "appearance": {
                "theme": "System", 
                "icon_size": ICON_SIZE[0],
                "minimize_to_tray": False,
                "font_size": "Médio",
                "color_scheme": "Padrão" # Adicionando default color scheme
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
                # Sub-chaves de appearance
                for key, default_val in default_config['appearance'].items():
                    if key not in data['appearance']:
                        data['appearance'][key] = default_val
                
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

    def extract_icon_to_png(self, exe_path: str, out_png_path: str, size: int = 256) -> Optional[str]:
        if not WINDOWS_AVAILABLE: return None
        try:
            import win32ui # type: ignore
            large, small = win32gui.ExtractIconEx(exe_path, 0)
            hicon = large[0] if large and len(large) > 0 else small[0] if small and len(small) > 0 else None
            
            if not hicon: return None

            hdc = win32ui.CreateDCFromHandle(win32gui.GetDC(0))
            hbmp = win32ui.CreateBitmap()
            hbmp.CreateCompatibleBitmap(hdc, size, size)
            hdc_mem = hdc.CreateCompatibleDC()
            hdc_mem.SelectObject(hbmp)
            
            win32gui.DrawIconEx(hdc_mem.GetSafeHdc(), 0, 0, hicon, size, size, 0, None, win32con.DI_NORMAL)
            
            temp_bmp = out_png_path + ".bmp"
            hbmp.SaveBitmapFile(hdc_mem, temp_bmp)
            
            img = Image.open(temp_bmp).convert("RGBA")
            img = img.resize((size, size), Image.LANCZOS)
            img.save(out_png_path, "PNG")
            
            try: os.remove(temp_bmp)
            except Exception: pass
            
            try: 
                for h in large: win32gui.DestroyIcon(h)
                for h in small: win32gui.DestroyIcon(h)
            except Exception: pass
            
            return out_png_path
        except Exception:
            return None

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
        self.geometry("600x500")
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
        
        ctk.CTkButton(inner_save_buttons_frame, text="🚫 Cancelar", command=self.destroy, fg_color="#6c757d").pack(side='left', padx=(10, 0))
        ctk.CTkButton(inner_save_buttons_frame, text="💾 Salvar Macro", command=self._save_and_close, fg_color=COLORS["success"]).pack(side='left')

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
        self.geometry('550x500') # Aumentado para acomodar o menu de ação
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._center_window()
        self.protocol("WM_DELETE_WINDOW", self._on_cancel)
        
        self.label_var = tk.StringVar(value=self.conf.get('label', f'Botão {button_key}'))
        self.icon_path = self.conf.get('icon', '')
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
        ctk.CTkLabel(main_frame, text='Nome do Botão:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', pady=(5, 0), padx=5)
        def on_name_change(*args):
            if len(self.label_var.get()) > 16: self.label_var.set(self.label_var.get()[:16])
        self.label_var.trace('w', on_name_change)
        ctk.CTkEntry(main_frame, textvariable=self.label_var, width=400, placeholder_text="Máximo 16 caracteres").pack(fill='x', pady=5, padx=5)
        
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
        if not messagebox.askyesno("Excluir", "Deseja remover o programa, o ícone e DELETAR o arquivo do ícone?"):
            return

        try:
            icon_path_to_delete = self.conf.get('icon', '')
            nome_do_botao = self.label_var.get()
            
            # 1. Limpa a configuração do botão no ConfigManager
            self.parent.config.data['buttons'][self.button_key] = {
                "label": f"Botão {self.button_key}",
                "icon": "",
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
            messagebox.showerror("Erro", f"Ocorreu um erro durante a exclusão: {e}")
                
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
        
        self.parent.config.data['buttons'][self.button_key] = self.conf
        self.parent.config.save()
        
        # REINICIA O LOADER e atualiza
        self.parent._reset_icon_loader()
        self.parent.refresh_all_buttons()
        
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
                 on_status_change: Optional[Callable[[bool], None]] = None):
        self.config = config
        self.logger = logger
        self.on_message = on_message
        self.on_status_change = on_status_change
        self._serial: Optional[serial.Serial] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False
        self._is_connected = False

    def is_port_available(self, port: str) -> bool:
        try:
            test_serial = serial.Serial(port=port, baudrate=DEFAULT_SERIAL_BAUD, timeout=0.1)
            test_serial.close()
            return True
        except serial.SerialException: return False
        except Exception: return False

    def list_ports(self) -> List[str]:
        return [p.device for p in serial.tools.list_ports.comports()]

    def send_disconnect_command(self):
        try:
            if self._serial and self._serial.is_open:
                self._serial.write(b"DISCONNECT\n")
                time.sleep(0.3)
                return True
        except Exception: pass
        return False

    def disconnect(self):
        try:
            if self._is_connected: self.send_disconnect_command()
            self._running = False
            self._is_connected = False
            if self._serial and self._serial.is_open:
                self._serial.close()
                self.logger.info('🔌 Porta serial fechada')
            if self.on_status_change: self.on_status_change(False)
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
            self._serial.write(b"CONNECTED\n")
            self.logger.info(f'✅ Conectado a {port} @ {baud}')
            if self.on_status_change: self.on_status_change(True)
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
            if self.on_status_change: self.on_status_change(False)
            self.logger.warn("Loop de leitura serial interrompido.")

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
        self.geometry('900x700')
        self.resizable(False, False)
        
        self._setup_app_icon()
        self._setup_theme()
        
        self.config = ConfigManager()
        
        # 1. Inicializa o Logger
        self.logger = Logger(file_path=LOG_FILE) 
        
        # 2. Inicializa o IconLoader
        self._reset_icon_loader()
        
        self.action_manager = ActionManager(self.logger)
        
        self.serial_manager = SerialManager(
            self.config, 
            self.logger, 
            on_message=self._on_serial_message,
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
        
        self._build_ui()
        self._load_appearance_settings() # Deve vir após _build_ui
        
        self.logger.textbox = self.log_textbox
        
        self._center_window()
        self.refresh_all_buttons()
        self.update_serial_ports()
        
        self.bind("<Unmap>", self._on_minimize_event)

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
                if text in ["💾 Salvar", "🔗 Conectar", "💾 Salvar Macro", "💾 Salvar Ação"]: 
                    widget.configure(fg_color=self.colors["success"], hover_color=self.colors["success"] if self.colors["success"] != COLORS["success"] else "#1e7a30")
                elif text in ["🗑️ Excluir", "🔓 Desconectar"]: 
                    widget.configure(fg_color=self.colors["danger"], hover_color=self.colors["danger"] if self.colors["danger"] != COLORS["danger"] else "#a92a39")
                elif text in ["▶️ Testar"]:
                    widget.configure(fg_color=self.colors["primary"], hover_color=self.colors["secondary"])
                elif text in ["🚫 Cancelar"]: 
                    widget.configure(fg_color="#6c757d", hover_color="#5a6268")
                elif text in ["🔄 Atualizar", "🔄 Restaurar Configurações Padrão", "🔄 Atualizar Portas", "⬇️ Baixar Atualização"]: 
                    widget.configure(fg_color=self.colors["warning"], text_color="black" if ctk.get_appearance_mode() == "Light" else "white", hover_color=self.colors["warning"] if self.colors["warning"] != COLORS["warning"] else "#d9a100")
                else: 
                    widget.configure(fg_color=self.colors["primary"], hover_color=self.colors["secondary"])
            
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
        palettes = {
            "Padrão": {"primary": "#2B5B84", "secondary": "#3D8BC2", "success": "#28A745", "danger": "#DC3545", "warning": "#FFC107"},
            "Moderno": {"primary": "#000981", "secondary": "#4527A0", "success": "#0D8040", "danger": "#C62828", "warning": "#915E00"},
            "Vibrante": {"primary": "#FF007F", "secondary": "#00D4FF", "success": "#39FF14", "danger": "#FF0033", "warning": "#FFE600"},
            "Suave": {"primary": "#C0A9F7", "secondary": "#6C7BB1", "success": "#69F9A6", "danger": "#FF6C6C", "warning": "#FAF48B"},
            "Escuro Total": {"primary": "#3C4043", "secondary": "#191C1F", "success": "#2E7D32", "danger": "#C62828", "warning": "#EF6C00"}
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
        if messagebox.askyesno("Confirmar Reset", "Restaurar padrões de aparência?"):
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
            messagebox.showinfo("Reset", "Configurações restauradas!")
    
    def _build_ui(self):
        self._build_header()
        self.tabview = ctk.CTkTabview(self, width=860, height=500, corner_radius=10)
        self.tabview.pack(expand=True, fill='both', padx=20, pady=(0, 20))
        self.tab_buttons = self.tabview.add('🎮 Configurar Botões')
        self.tab_connection = self.tabview.add('🔌 Conexão')
        self.tab_settings = self.tabview.add('⚙️ Configurações')
        self.tab_update = self.tabview.add('🔄 Atualização')
        
        # Container principal dos botões
        self.grid_buttons_parent = ctk.CTkFrame(self.tab_buttons, fg_color="transparent")
        self.grid_buttons_parent.pack(expand=True, fill='both', padx=10, pady=10)
        
        self._build_buttons_tab(self.grid_buttons_parent) 

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
        ctk.CTkButton(btn_frame, text="🔄 Atualizar", width=80, command=self.refresh_all).pack(side='right', padx=5)
    
    def _update_header_status(self, connected: bool):
        self.after(0, lambda: self._set_header_visuals(connected))
        
    def _set_header_visuals(self, connected: bool):
            status_color = self.colors["success"] if connected else self.colors["danger"]
            status_text = "Conectado" if connected else "Desconectado"
            
            self.header_status_dot.configure(text_color=status_color)
            self.header_status_text.configure(text=status_text, text_color=status_color)

            state_conn = 'disabled' if connected else 'normal'
            state_disc = 'normal' if connected else 'disabled'
            
            if hasattr(self, 'connect_btn'): # Garante que os widgets existem
                self.connect_btn.configure(state=state_conn)
                self.port_option.configure(state=state_conn)
                self.baud_option.configure(state=state_conn)
                self.refresh_ports_btn.configure(state=state_conn)
                self.disconnect_btn.configure(state=state_disc)

                if connected:
                    self.status_card.configure(border_color=self.colors["success"])
                    self.dash_icon.configure(text="⚡")
                    self.dash_status_text.configure(text="CONECTADO", text_color=self.colors["success"])
                    self.dash_sub_text.configure(text="O sistema está pronto para receber comandos.")
                    
                    current_port = self.port_option.get()
                    current_baud = self.baud_option.get()
                    self.lbl_detail_port.configure(text=f"Porta Ativa: {current_port}")
                    self.lbl_detail_baud.configure(text=f"Velocidade:  {current_baud} bps")
                    self.details_frame.pack(fill='x', ipadx=20, ipady=10)
                    
                else:
                    self.status_card.configure(border_color=self.colors["secondary"])
                    self.dash_icon.configure(text="🔌") 
                    self.dash_status_text.configure(text="DESCONECTADO", text_color=self.colors["danger"])
                    self.dash_sub_text.configure(text="Selecione uma porta e clique em conectar.")
                    self.details_frame.pack_forget()

    def _build_buttons_tab(self, grid_frame):
        """Constrói a aba de configuração de botões (Grid 4x2)."""
        
        # Configura o grid para 4 colunas e 2 linhas (8 botões)
        for i in range(4): grid_frame.grid_columnconfigure(i, weight=1)
        for i in range(2): grid_frame.grid_rowconfigure(i, weight=1)
        
        btn_id = 1
        for row in range(2):
            for col in range(4):
                key = str(btn_id)
                self._create_button_frame(grid_frame, key, row, col)
                btn_id += 1
                
    def _create_button_frame(self, parent, key, row, col):
        """Cria o frame de visualização de um único botão, destruindo o antigo se existir."""
        
        # Se o botão já existe, destrói o frame antigo
        if key in self.button_frames and 'frame' in self.button_frames[key]:
            self.button_frames[key]['frame'].destroy()
            
        # 1. Cria o novo Frame do Botão
        btn_frame = ctk.CTkFrame(parent, width=180, height=180, corner_radius=12, border_width=2, border_color=self.colors["secondary"])
        btn_frame.grid(row=row, column=col, padx=8, pady=8, sticky='nsew')
        btn_frame.grid_propagate(False)
        
        # 2. Widgets internos
        icon_label = ctk.CTkLabel(btn_frame, text='📱', width=64, height=64, font=ctk.CTkFont(size=24), text_color=self.colors["primary"])
        icon_label.pack(pady=(15, 5))
        
        btn_conf = self.config.data.get('buttons', {}).get(key, {})
        title_label = ctk.CTkLabel(btn_frame, text=btn_conf.get('label', f'Botão {key}'), font=ctk.CTkFont(size=14, weight="bold"))
        title_label.pack(pady=(0, 5))
        
        ctk.CTkButton(btn_frame, text='Configurar', width=120, height=28, command=lambda i=key: self.open_button_config(i), fg_color=self.colors["primary"]).pack(pady=(0, 15))
        
        # 3. Armazena a referência dos novos widgets
        self.button_frames[key] = {'frame': btn_frame, 'icon_label': icon_label, 'title_label': title_label, 'grid_row': row, 'grid_col': col}

    def _build_connection_tab(self, parent):
        parent.grid_columnconfigure(0, weight=1) 
        parent.grid_columnconfigure(1, weight=2) 
        parent.grid_rowconfigure(0, weight=1)
        
        config_frame = ctk.CTkFrame(parent, corner_radius=10)
        config_frame.grid(row=0, column=0, sticky="nsew", padx=(10, 5), pady=10)
        
        ctk.CTkLabel(config_frame, text="⚙️ Configuração", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=(20, 15))
        
        port_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        port_frame.pack(fill='x', padx=15, pady=5)
        
        ctk.CTkLabel(port_frame, text="Porta Serial (COM):", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
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

        ctk.CTkFrame(config_frame, height=2, fg_color=self.colors["secondary"]).pack(fill='x', padx=20, pady=20)

        baud_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        baud_frame.pack(fill='x', padx=15, pady=5)
        
        ctk.CTkLabel(baud_frame, text="Velocidade (Baud):", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        baud_rates = ['9600', '19200', '38400', '57600', '115200', '230400']
        self.baud_option = ctk.CTkOptionMenu(baud_frame, values=baud_rates, command=self._on_baud_change)
        self.baud_option.set(str(self.config.data.get('serial', {}).get('baud', DEFAULT_SERIAL_BAUD)))
        self.baud_option.pack(fill='x', pady=5)

        ctk.CTkLabel(config_frame, text="").pack(expand=True)

        action_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        action_frame.pack(fill='x', padx=15, pady=20)

        self.connect_btn = ctk.CTkButton(
            action_frame, 
            text="🔗 Conectar", 
            command=self._connect_serial, 
            fg_color=self.colors["success"]
        )
        self.connect_btn.pack(fill='x', pady=(0, 10))

        self.disconnect_btn = ctk.CTkButton(
            action_frame, 
            text="🔓 Desconectar", 
            command=self._disconnect_serial, 
            state='disabled', 
            fg_color=self.colors["danger"]
        )
        self.disconnect_btn.pack(fill='x')

        self.status_card = ctk.CTkFrame(parent, corner_radius=10, border_width=2, border_color=self.colors["secondary"])
        self.status_card.grid(row=0, column=1, sticky="nsew", padx=(5, 10), pady=10)
        
        status_content = ctk.CTkFrame(self.status_card, fg_color="transparent")
        status_content.place(relx=0.5, rely=0.5, anchor='center')

        self.dash_icon = ctk.CTkLabel(status_content, text="🔌", font=ctk.CTkFont(size=80))
        self.dash_icon.pack(pady=(0, 10))

        self.dash_status_text = ctk.CTkLabel(status_content, text="DESCONECTADO", font=ctk.CTkFont(size=24, weight="bold"), text_color=self.colors["danger"])
        self.dash_status_text.pack(pady=5)

        self.dash_sub_text = ctk.CTkLabel(status_content, text="O dispositivo não está comunicando.", font=ctk.CTkFont(size=14), text_color="gray")
        self.dash_sub_text.pack(pady=(0, 30))

        self.details_frame = ctk.CTkFrame(status_content, fg_color=self.colors["dark"], corner_radius=8)
        self.details_frame.pack(fill='x', ipadx=20, ipady=10)
        self.details_frame.pack_forget() 

        self.lbl_detail_port = ctk.CTkLabel(self.details_frame, text="Porta: -", font=ctk.CTkFont(family="Consolas", size=12))
        self.lbl_detail_port.pack(anchor='w')
        
        self.lbl_detail_baud = ctk.CTkLabel(self.details_frame, text="Baud: -", font=ctk.CTkFont(family="Consolas", size=12))
        self.lbl_detail_baud.pack(anchor='w')
        
        self.lbl_detail_proto = ctk.CTkLabel(self.details_frame, text="Protocolo: Serial/UART", font=ctk.CTkFont(family="Consolas", size=12))
        self.lbl_detail_proto.pack(anchor='w')

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
        
        self.remote_version_var = tk.StringVar(value=APP_VERSION)
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
        
        btn_id = 1
        for row in range(2):
            for col in range(4):
                key = str(btn_id)
                
                # Passo 1: RECRIA O FRAME DO ZERO
                self._create_button_frame(self.grid_buttons_parent, key, row, col)
                
                # Passo 2: Configura o novo frame com as informações atualizadas
                btn_conf = self.config.data.get('buttons', {}).get(key, {})
                icon_path = btn_conf.get('icon', '')
                
                # Tenta carregar o ícone
                ctk_img = None
                if icon_path and os.path.exists(icon_path):
                    ctk_img = self.icon_loader.load_icon_from_path(icon_path)

                widget_map = self.button_frames[key]
                widget_map['title_label'].configure(text=btn_conf.get('label', f'Botão {key}'))
                
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
            
        conf = self.config.data['buttons'][button_key]

        dlg = ButtonConfigDialog(self, button_key, conf.copy(), self.icon_loader, self.logger) 
        self.wait_window(dlg)
        
        self.config.save()

    def update_serial_ports(self):
        ports = self.serial_manager.list_ports() or ['Nenhuma']
        self.port_option.configure(values=ports)
        try:
            curr = self.config.data.get('serial', {}).get('port', '')
            self.port_option.set(curr if curr in ports else ports[0])
        except: self.port_option.set(ports[0])

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

    def _disconnect_serial(self): self.serial_manager.disconnect()
        
    def _clear_log(self):
        self.log_textbox.configure(state='normal')
        self.log_textbox.delete('1.0', 'end')
        self.log_textbox.configure(state='disabled')
        
    def _on_baud_change(self, value):
        self.config.data['serial']['baud'] = int(value)
        self.config.save()
        
    def _save_all(self):
        if self.config.save(): 
            messagebox.showinfo("Sucesso", "Configurações salvas!")
        else: 
            messagebox.showerror("Erro", "Erro ao salvar!")
            
    def _show_about(self): AboutDialog(self)

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
                
                if messagebox.askyesno('Atualização', f'Nova versão {latest} encontrada!\nDeseja abrir a página de download agora?'):
                    webbrowser.open(res.get('download_url'))
            else:
                self.logger.info('Sistema já está atualizado.')
                self.status_icon_label.configure(text="✅")
                self.status_title_label.configure(text="Você está atualizado", text_color=self.colors["success"])
                self.status_detail_label.configure(text=f"A versão {latest} é a mais recente.")
                self.remote_card_frame.configure(border_color=self.colors["success"])
                self.check_update_btn.configure(fg_color=self.colors["success"], text="🔍 Verificar Novamente")

    def _on_serial_message(self, text: str):
        self.logger.info(f'<- ESP: {text}')
        
        if text.startswith('BTN:'):
            key = text.split(':')[1]
            btn_conf = self.config.data['buttons'].get(key)
            if btn_conf: 
                self.action_manager.perform(
                    Action(
                        btn_conf.get('action', {}).get('type', 'none'), 
                        btn_conf.get('action', {}).get('payload', '')
                    )
                )

def main():
    ctk.set_appearance_mode('System')
    app = Esp32DeckApp()

    app.deiconify() 
    
    app.protocol('WM_DELETE_WINDOW', app.on_closing)
    app.mainloop()

if __name__ == '__main__':
    main()