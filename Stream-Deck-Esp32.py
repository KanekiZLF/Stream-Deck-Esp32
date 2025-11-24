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
import win32gui
import win32con
import win32process

# GUI libs
import customtkinter as ctk
import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageTk, ImageDraw

# Serial
import serial
import serial.tools.list_ports

# -----------------------------
# OPTIONAL IMPORTS CHECK
# -----------------------------
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
APP_VERSION = "1.5.6" # Versão: Filtro de Log Otimizado
APP_NAME = "Esp32 Deck Controller"
APP_ICON_NAME = "app_icon.ico"
DEVELOPER = "Luiz F. R. Pimentel"
GITHUB_URL = "https://github.com/KanekiZLF"
UPDATE_CHECK_URL = "https://raw.githubusercontent.com/KanekiZLF/PrismaFX---Gerador-ImageFX-em-Lote/refs/heads/master/version.txt"
DEFAULT_SERIAL_BAUD = 115200
BUTTON_COUNT = 8
ICON_SIZE = (64, 64)

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
# Icon Path
# -----------------------------
def get_app_icon_path() -> str:
    """Retorna o caminho absoluto para o ícone da aplicação"""
    return os.path.abspath(os.path.join(os.path.dirname(__file__), APP_ICON_NAME))

# -----------------------------
# Logger (Filtered)
# -----------------------------
class Logger:
    LEVELS = ("DEBUG", "INFO", "WARN", "ERROR")

    def __init__(self, textbox: Optional[ctk.CTkTextbox] = None, file_path: Optional[str] = LOG_FILE):
        self.textbox = textbox
        self.file_path = file_path
        # Inicialização sempre grava no arquivo
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
        
        # 1. Sempre mostrar no Console
        print(entry)
        
        # 2. Lógica de Filtro para o Arquivo de Log
        # Salva apenas se for ERRO ou mensagens críticas de ciclo de vida
        should_save = False
        if level == "ERROR":
            should_save = True
        elif "Fechando aplicação" in msg:
            should_save = True
        elif "Porta serial fechada" in msg:
            should_save = True
        
        if should_save:
            self._write_file(entry)

        if level == "DEBUG":
            self._write_file(entry)
            
        # 3. GUI sempre recebe tudo (Visualização em tempo real)
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
                "font_size": "Médio"
            },
            "update": {"check_url": UPDATE_CHECK_URL}
        }

    def _load_or_create(self):
        if os.path.exists(self.path):
            try:
                with open(self.path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                if 'buttons' not in data:
                    data = self._default()
                if 'appearance' not in data:
                    data['appearance'] = self._default()['appearance']
                if 'minimize_to_tray' not in data['appearance']:
                    data['appearance']['minimize_to_tray'] = False
                if 'font_size' not in data['appearance']:
                    data['appearance']['font_size'] = "Médio"
                return data
            except Exception:
                return self._default()
        else:
            return self._default()

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
        ico_candidate = os.path.splitext(exe_path)[0] + '.ico'
        if os.path.exists(ico_candidate):
            return self.load_icon_from_path(ico_candidate)
        return None

    def extract_icon_to_png(self, exe_path: str, out_png_path: str, size: int = 256) -> Optional[str]:
        try:
            import win32api
            import win32ui # type: ignore
            large, small = win32gui.ExtractIconEx(exe_path, 0)
            hicon = None
            if large and len(large) > 0: hicon = large[0]
            elif small and len(small) > 0: hicon = small[0]
            else: return None

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

        # Carrega o ícone (do arquivo ou fallback)
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
        if not PSUTIL_AVAILABLE:
            self.logger.warn("Biblioteca 'psutil' não encontrada. Instalando apenas nova instância.")
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
            os.startfile(path) if sys.platform.startswith('win') else subprocess.Popen([path])
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
            self.logger.debug(f"Executando ação: {action.type}")
            if action.type == 'open_program':
                self.window_manager.toggle_application(action.payload)
            elif action.type == 'open_url':
                webbrowser.open(action.payload)
                self.logger.info(f'Abrindo URL: {action.payload}')
            elif action.type == 'run_cmd':
                subprocess.Popen(action.payload, shell=True)
                self.logger.info(f'Rodando comando: {action.payload}')
            elif action.type == 'type_text':
                if PYAUTOGUI_AVAILABLE:
                    pyautogui.write(action.payload)
                    self.logger.info('Texto digitado via pyautogui')
            elif action.type == 'hotkey':
                if PYAUTOGUI_AVAILABLE:
                    keys = action.payload if isinstance(action.payload, list) else [action.payload]
                    pyautogui.hotkey(*keys)
                    self.logger.info(f'Hotkey enviada: {keys}')
            elif action.type == 'script':
                if os.path.exists(action.payload):
                    subprocess.Popen([sys.executable, action.payload])
            elif action.type == 'macro':
                for a in action.payload:
                    sub = Action(a.get('type'), a.get('payload'))
                    self.perform(sub)
                    time.sleep(0.1)
        except Exception as e:
            self.logger.error(f'Erro ao executar ação: {e}\n{traceback.format_exc()}')

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

# -----------------------------
# Update Checker
# -----------------------------
class UpdateChecker:
    def __init__(self, config: ConfigManager, logger: Logger):
        self.config = config
        self.logger = logger

    def check_update(self) -> Dict[str, Any]:
        url = self.config.data.get('update', {}).get('check_url', UPDATE_CHECK_URL)
        
        if not REQUESTS_AVAILABLE:
            return {"ok": False, "error": "Biblioteca 'requests' não instalada"}
        
        try:
            response = requests.get(url, timeout=10)
            response.raise_for_status()
            
            info = response.json()
            latest = info.get('latest_version')
            download = info.get('download_url')
            
            if not latest:
                return {"ok": False, "error": "Versão não encontrada no servidor"}
            
            is_new = self._version_greater(latest, APP_VERSION)
            
            return {
                "ok": True, 
                "latest": latest, 
                "download_url": download, 
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

class ButtonConfigDialog(ctk.CTkToplevel):
    def __init__(self, parent: Esp32DeckApp, button_key: str, conf: Dict[str, Any], icon_loader: IconLoader, logger: Logger):
        super().__init__(parent)
        self.parent = parent
        self.button_key = button_key
        self.conf = conf
        self.icon_loader = icon_loader
        self.logger = logger
        self._newly_created_icon = None
        self.title(f'Configurar Botão {button_key}')
        self.geometry('550x400') 
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._center_window()
        self.label_var = tk.StringVar(value=self.conf.get('label', f'Botão {button_key}'))
        self.icon_path = self.conf.get('icon', '')
        self._initial_payload = self.conf.get('action', {}).get('payload', '')
        self._build()
    def _center_window(self):
        self.update_idletasks()
        x = (self.winfo_screenwidth() // 2) - (self.winfo_width() // 2)
        y = (self.winfo_screenheight() // 2) - (self.winfo_height() // 2)
        self.geometry(f'+{x}+{y}')
    def _build(self):
        main_frame = ctk.CTkFrame(self, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=15, pady=15)
        ctk.CTkLabel(main_frame, text='Nome do Botão:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', pady=(5, 0))
        def on_name_change(*args):
            if len(self.label_var.get()) > 16: self.label_var.set(self.label_var.get()[:16])
        self.label_var.trace('w', on_name_change)
        ctk.CTkEntry(main_frame, textvariable=self.label_var, width=400, placeholder_text="Máximo 16 caracteres").pack(fill='x', pady=5)
        icon_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        icon_frame.pack(fill='x', pady=10)
        ctk.CTkLabel(icon_frame, text='Programa:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=10)
        icon_content = ctk.CTkFrame(icon_frame, fg_color="transparent")
        icon_content.pack(fill='x', padx=10, pady=5)
        self.icon_preview = ctk.CTkLabel(icon_content, text='📱', width=64, height=64, font=ctk.CTkFont(size=20), text_color=COLORS["primary"])
        self.icon_preview.pack(side='left', padx=10)
        btn_frame = ctk.CTkFrame(icon_content, fg_color="transparent")
        btn_frame.pack(side='left', padx=10)
        ctk.CTkButton(btn_frame, text='Escolher Ícone', width=140, command=self._choose_icon).pack(side='left', padx=5)
        ctk.CTkButton(btn_frame, text='Selecionar Programa', width=140, command=self._select_program_for_button).pack(side='left', padx=5)
        payload_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        payload_frame.pack(fill='x', pady=10)
        ctk.CTkLabel(payload_frame, text='Caminho:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=(10, 5))
        self.payload_entry = ctk.CTkEntry(payload_frame, height=35)
        self.payload_entry.pack(fill='x', padx=10, pady=(0, 10))
        try:
            val = json.dumps(self._initial_payload, ensure_ascii=False) if isinstance(self._initial_payload, (dict, list)) else str(self._initial_payload)
            self.payload_entry.insert(0, val)
        except: self.payload_entry.insert(0, str(self._initial_payload))
        buttons_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        buttons_frame.pack(fill='x', pady=10)
        inner_buttons_frame = ctk.CTkFrame(buttons_frame, fg_color="transparent")
        inner_buttons_frame.pack(expand=True)
        ctk.CTkButton(inner_buttons_frame, text='▶️ Testar', command=self._test_action, fg_color=COLORS["primary"], width=80).pack(side='left', padx=5)
        ctk.CTkButton(inner_buttons_frame, text='🗑️ Excluir', command=self._on_delete, fg_color=COLORS["danger"], width=80).pack(side='left', padx=5)
        ctk.CTkButton(inner_buttons_frame, text='🚫 Cancelar', command=self._on_cancel, fg_color="#6c757d", width=80).pack(side='left', padx=5)
        ctk.CTkButton(inner_buttons_frame, text='💾 Salvar', command=self._save_and_close, fg_color=COLORS["success"], width=80).pack(side='left', padx=5)
        self._refresh_icon_preview()

    def _select_program_for_button(self):
        exe_path = filedialog.askopenfilename(filetypes=[("Executáveis", "*.exe"), ("Todos", "*.*")])
        if not exe_path: return
        self.payload_entry.delete(0, 'end')
        self.payload_entry.insert(0, exe_path)
        basename = os.path.splitext(os.path.basename(exe_path))[0]
        safe_makedirs(ICON_FOLDER)
        out_png = os.path.join(ICON_FOLDER, f"btn{self.button_key}_{basename}.png")
        extracted = self.icon_loader.extract_icon_to_png(exe_path, out_png, size=128)
        self.conf['action'] = {'type': 'open_program', 'payload': exe_path}
        if extracted:
            self._newly_created_icon = extracted
            self.icon_path = extracted
            self.conf['icon'] = self.icon_path
            self._refresh_icon_preview()
            messagebox.showinfo("Ícone", f"Ícone extraído: {extracted}")

    def _choose_icon(self):
        path = filedialog.askopenfilename(filetypes=[('Images', '*.png *.jpg *.ico'), ('All', '*.*')])
        if not path: return
        self.icon_path = path
        self.conf['icon'] = self.icon_path
        self._refresh_icon_preview()

    def _refresh_icon_preview(self):
        ctk_img = self.icon_loader.load_icon_from_path(self.icon_path) if self.icon_path else None
        if ctk_img:
            self.icon_preview.configure(image=ctk_img, text='')
        else:
            self.icon_preview.configure(image=None, text='📱')

    def _on_cancel(self):
        if self._newly_created_icon and os.path.exists(self._newly_created_icon):
            try: os.remove(self._newly_created_icon)
            except: pass
        self.destroy()

    def _on_delete(self):
        if messagebox.askyesno("Excluir", "Deseja remover o programa e o ícone deste botão?"):
            # Apagar arquivo do ícone se existir
            if self.conf.get('icon') and os.path.exists(self.conf['icon']):
                try: os.remove(self.conf['icon'])
                except: pass
                
            self.icon_path = ""
            self.conf['action'] = {'type': 'none', 'payload': ''}
            self.conf['icon'] = ""
            self.conf['label'] = f"Botão {self.button_key}"
            
            # Forçar atualização direta na config pai
            self.parent.config.data['buttons'][self.button_key] = self.conf
            self.parent.config.save()
            self.parent.logger.info(f"Configuração do Botão {self.button_key} removida.")
            self.destroy()

    def _save_and_close(self):
        raw = self.payload_entry.get().strip()
        payload = raw
        try: payload = json.loads(raw) if raw else ''
        except: pass
        self.conf['label'] = self.label_var.get()
        if 'icon' not in self.conf: self.conf['icon'] = self.icon_path
        self.conf['action'] = {'type': 'open_program', 'payload': payload}
        self.parent.config.save()
        self.destroy()

    def _test_action(self):
        raw = self.payload_entry.get().strip()
        self.parent.action_manager.perform(Action('open_program', raw))

# -----------------------------
# GUI / App
# -----------------------------
class Esp32DeckApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title(f'{APP_NAME} v{APP_VERSION}')
        self.geometry('900x700')
        self.resizable(False, False)
        self._setup_app_icon()
        self._setup_theme()
        
        self.config = ConfigManager()
        self.icon_loader = IconLoader(icon_size=(self.config.data.get('appearance', {}).get('icon_size', ICON_SIZE[0]),) * 2)
        self.logger = Logger(file_path=LOG_FILE)
        self.action_manager = ActionManager(self.logger)
        
        self.serial_manager = SerialManager(
            self.config, 
            self.logger, 
            on_message=self._on_serial_message,
            on_status_change=self._update_header_status
        )
        self.update_checker = UpdateChecker(self.config, self.logger)
        
        # Tray Icon
        self.tray_manager = TrayIconManager(self, self.logger)
        self.tray_manager.run()

        self.colors = COLORS.copy()
        self.current_font_size = 14
        saved_font = self.config.data.get('appearance', {}).get('font_size', 'Médio')
        if saved_font == 'Pequeno': self.current_font_size = 12
        elif saved_font == 'Grande': self.current_font_size = 18

        self.button_frames: Dict[str, Dict[str, Any]] = {}
        
        ctk.set_appearance_mode(self.config.data.get('appearance', {}).get('theme', 'System'))
        self._build_ui()
        self._load_appearance_settings()
        self.logger.textbox = self.log_textbox
        self._center_window()
        self.refresh_all_buttons()
        self.update_serial_ports()
        
        self.bind("<Unmap>", self._on_minimize_event)

        atexit.register(self._cleanup_on_exit)
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

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
            if hasattr(self, 'config'): self.config.save()
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
                if text in ["💾 Salvar", "🔗 Conectar", "▶️ Testar"]: widget.configure(fg_color=self.colors["success"])
                elif text in ["🗑️ Excluir", "🔓 Desconectar", "Fechar"]: widget.configure(fg_color=self.colors["danger"])
                elif text in ["🚫 Cancelar"]: widget.configure(fg_color="#6c757d")
                elif text in ["🔄 Atualizar", "🔄 Restaurar Configurações Padrão", "🔄 Atualizar Portas"]: widget.configure(fg_color=self.colors["warning"])
                else: widget.configure(fg_color=self.colors["primary"])
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
        self.attributes('-alpha', app.get('transparency', 1.0))
        self._on_color_scheme_change(app.get('color_scheme', 'Padrão'))
        self._on_font_size_change(app.get('font_size', 'Médio'))

    def _setup_theme(self): ctk.set_default_color_theme("dark-blue")
    def _center_window(self):
        self.update_idletasks()
        x = (self.winfo_screenwidth() // 2) - (900 // 2)
        y = (self.winfo_screenheight() // 2) - (700 // 2)
        self.geometry(f'+{x}+{y}')

    def _on_transparency_change(self, value):
        self.attributes('-alpha', value)
        self.transparency_label.configure(text=f"{int(value * 100)}%")
        self.config.data.setdefault('appearance', {})['transparency'] = value
        self.config.save()

    def _on_color_scheme_change(self, value):
        palettes = {
            "Padrão": {"primary": "#510DCF", "secondary": "#1E719E", "success": "#007430", "danger": "#EB2711", "warning": "#FC6703"},
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
            self._on_font_size_change('Médio')
            self.config.save()
            messagebox.showinfo("Reset", "Configurações restauradas!")

    # -----------------------------
    # UI Construction
    # -----------------------------
    def _build_ui(self):
        self._build_header()
        self.tabview = ctk.CTkTabview(self, width=860, height=500, corner_radius=10)
        self.tabview.pack(expand=True, fill='both', padx=20, pady=(0, 20))
        self.tab_buttons = self.tabview.add('🎮 Configurar Botões')
        self.tab_connection = self.tabview.add('🔌 Conexão')
        self.tab_settings = self.tabview.add('⚙️ Configurações')
        self.tab_update = self.tabview.add('🔄 Atualização')
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
        ctk.CTkLabel(title_frame, text=f"v{APP_VERSION} - Controller para ESP32", font=ctk.CTkFont(size=12), text_color=COLORS["secondary"]).pack(anchor='w')
        status_frame = ctk.CTkFrame(header, fg_color="transparent")
        status_frame.place(relx=0.5, rely=0.5, anchor='center')
        self.header_status_dot = ctk.CTkLabel(status_frame, text="●", font=ctk.CTkFont(size=20), text_color=COLORS["danger"])
        self.header_status_dot.pack(side='left', padx=(0, 5))
        self.header_status_text = ctk.CTkLabel(status_frame, text="Desconectado", font=ctk.CTkFont(size=14, weight="bold"), text_color=COLORS["danger"])
        self.header_status_text.pack(side='left')
        btn_frame = ctk.CTkFrame(header, fg_color="transparent")
        btn_frame.pack(side='right', padx=20, pady=10)
        ctk.CTkButton(btn_frame, text="ℹ️ Sobre", width=80, command=self._show_about).pack(side='right', padx=(5, 0))
        ctk.CTkButton(btn_frame, text="💾 Salvar", width=80, command=self._save_all, fg_color=COLORS["success"]).pack(side='right', padx=5)
        ctk.CTkButton(btn_frame, text="🔄 Atualizar", width=80, command=self.refresh_all).pack(side='right', padx=5)
    
    def _update_header_status(self, connected: bool):
        self.after(0, lambda: self._set_header_visuals(connected))
        
    def _set_header_visuals(self, connected: bool):
            # 1. Atualiza Header (Topo da Janela)
            if connected:
                self.header_status_dot.configure(text_color=COLORS["success"])
                self.header_status_text.configure(text="Conectado", text_color=COLORS["success"])
            else:
                self.header_status_dot.configure(text_color=COLORS["danger"])
                self.header_status_text.configure(text="Desconectado", text_color=COLORS["danger"])

            # 2. Atualiza Botões de Ação
            state_conn = 'disabled' if connected else 'normal'
            state_disc = 'normal' if connected else 'disabled'
            
            self.connect_btn.configure(state=state_conn)
            self.port_option.configure(state=state_conn)
            self.baud_option.configure(state=state_conn)
            self.refresh_ports_btn.configure(state=state_conn)
            self.disconnect_btn.configure(state=state_disc)

            # 3. Atualiza o Novo Dashboard (Aba Conexão)
            if connected:
                # Visual Conectado
                self.status_card.configure(border_color=COLORS["success"])
                self.dash_icon.configure(text="⚡") # Raio ou Tomada ligada
                self.dash_status_text.configure(text="CONECTADO", text_color=COLORS["success"])
                self.dash_sub_text.configure(text="O sistema está pronto para receber comandos.")
                
                # Preenche e mostra detalhes
                current_port = self.port_option.get()
                current_baud = self.baud_option.get()
                self.lbl_detail_port.configure(text=f"Porta Ativa: {current_port}")
                self.lbl_detail_baud.configure(text=f"Velocidade:  {current_baud} bps")
                self.details_frame.pack(fill='x', ipadx=20, ipady=10) # Mostra o painel
                
            else:
                # Visual Desconectado
                self.status_card.configure(border_color=COLORS["secondary"])
                self.dash_icon.configure(text="🔌") # Tomada desligada
                self.dash_status_text.configure(text="DESCONECTADO", text_color=COLORS["danger"])
                self.dash_sub_text.configure(text="Selecione uma porta e clique em conectar.")
                self.details_frame.pack_forget() # Esconde detalhes

    def _build_buttons_tab(self, parent):
        grid_frame = ctk.CTkFrame(parent)
        grid_frame.pack(expand=True, fill='both', padx=10, pady=10)
        for i in range(4): grid_frame.grid_columnconfigure(i, weight=1)
        for i in range(2): grid_frame.grid_rowconfigure(i, weight=1)
        btn_id = 1
        for row in range(2):
            for col in range(4):
                key = str(btn_id)
                self._create_button_frame(grid_frame, key, row, col)
                btn_id += 1
    def _create_button_frame(self, parent, key, row, col):
        btn_frame = ctk.CTkFrame(parent, width=180, height=180, corner_radius=12, border_width=2, border_color=COLORS["secondary"])
        btn_frame.grid(row=row, column=col, padx=8, pady=8, sticky='nsew')
        btn_frame.grid_propagate(False)
        icon_label = ctk.CTkLabel(btn_frame, text='📱', width=80, height=80, font=ctk.CTkFont(size=24), text_color=COLORS["primary"])
        icon_label.pack(pady=(15, 5))
        btn_conf = self.config.data.get('buttons', {}).get(key, {})
        title_label = ctk.CTkLabel(btn_frame, text=btn_conf.get('label', f'Botão {key}'), font=ctk.CTkFont(size=14, weight="bold"))
        title_label.pack(pady=(0, 5))
        ctk.CTkButton(btn_frame, text='Configurar', width=120, height=28, command=lambda i=key: self.open_button_config(i), fg_color=COLORS["primary"]).pack(pady=(0, 15))
        self.button_frames[key] = {'frame': btn_frame, 'icon_label': icon_label, 'title_label': title_label}

    # ---------------------------------------------------------
    # CORREÇÃO: ABA CONEXÃO (CORES E TAMANHOS ORIGINAIS)
    # ---------------------------------------------------------
    def _build_connection_tab(self, parent):
        # Configuração do Grid Principal (2 Colunas)
        parent.grid_columnconfigure(0, weight=1) # Coluna Config
        parent.grid_columnconfigure(1, weight=2) # Coluna Status (Mais larga)
        parent.grid_rowconfigure(0, weight=1)
        
        # ================== COLUNA ESQUERDA: CONFIGURAÇÕES ==================
        config_frame = ctk.CTkFrame(parent, corner_radius=10)
        config_frame.grid(row=0, column=0, sticky="nsew", padx=(10, 5), pady=10)
        
        # Título
        ctk.CTkLabel(config_frame, text="⚙️ Configuração", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=(20, 15))
        
        # --- Seção Porta ---
        port_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        port_frame.pack(fill='x', padx=15, pady=5)
        
        ctk.CTkLabel(port_frame, text="Porta Serial (COM):", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        self.port_option = ctk.CTkOptionMenu(port_frame, values=['Nenhuma'], width=200)
        self.port_option.pack(fill='x', pady=(5, 5))
        
        # Botão Atualizar
        self.refresh_ports_btn = ctk.CTkButton(
            port_frame, 
            text="🔄 Atualizar Portas", 
            command=self.update_serial_ports,
            fg_color=COLORS["dark"],
            height=24 # Levemente menor apenas para diferenciar
        )
        self.refresh_ports_btn.pack(fill='x')

        # Separador Visual
        ctk.CTkFrame(config_frame, height=2, fg_color=COLORS["secondary"]).pack(fill='x', padx=20, pady=20)

        # --- Seção Baud Rate ---
        baud_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        baud_frame.pack(fill='x', padx=15, pady=5)
        
        ctk.CTkLabel(baud_frame, text="Velocidade (Baud):", font=ctk.CTkFont(weight="bold"), anchor="w").pack(fill='x')
        
        baud_rates = ['9600', '19200', '38400', '57600', '115200', '230400']
        self.baud_option = ctk.CTkOptionMenu(baud_frame, values=baud_rates, command=self._on_baud_change)
        self.baud_option.set(str(self.config.data.get('serial', {}).get('baud', DEFAULT_SERIAL_BAUD)))
        self.baud_option.pack(fill='x', pady=5)

        # Espaçador
        ctk.CTkLabel(config_frame, text="").pack(expand=True)

        # --- Botões de Ação (Volta ao Padrão) ---
        action_frame = ctk.CTkFrame(config_frame, fg_color="transparent")
        action_frame.pack(fill='x', padx=15, pady=20)

        self.connect_btn = ctk.CTkButton(
            action_frame, 
            text="🔗 Conectar",  # ✅ Texto corrigido para o sistema de cores reconhecer
            command=self._connect_serial, 
            fg_color=COLORS["success"] # ✅ Verde forçado
        )
        self.connect_btn.pack(fill='x', pady=(0, 10))

        self.disconnect_btn = ctk.CTkButton(
            action_frame, 
            text="🔓 Desconectar", # ✅ Texto corrigido
            command=self._disconnect_serial, 
            state='disabled', 
            fg_color=COLORS["danger"] # ✅ Vermelho forçado
        )
        self.disconnect_btn.pack(fill='x')

        # ================== COLUNA DIREITA: STATUS DASHBOARD ==================
        self.status_card = ctk.CTkFrame(parent, corner_radius=10, border_width=2, border_color=COLORS["secondary"])
        self.status_card.grid(row=0, column=1, sticky="nsew", padx=(5, 10), pady=10)
        
        # Centralizador
        status_content = ctk.CTkFrame(self.status_card, fg_color="transparent")
        status_content.place(relx=0.5, rely=0.5, anchor='center')

        # Ícone Gigante
        self.dash_icon = ctk.CTkLabel(status_content, text="🔌", font=ctk.CTkFont(size=80))
        self.dash_icon.pack(pady=(0, 10))

        # Status Texto Principal
        self.dash_status_text = ctk.CTkLabel(status_content, text="DESCONECTADO", font=ctk.CTkFont(size=24, weight="bold"), text_color=COLORS["danger"])
        self.dash_status_text.pack(pady=5)

        # Subtexto
        self.dash_sub_text = ctk.CTkLabel(status_content, text="O dispositivo não está comunicando.", font=ctk.CTkFont(size=14), text_color="gray")
        self.dash_sub_text.pack(pady=(0, 30))

        # --- Painel de Detalhes Técnicos ---
        self.details_frame = ctk.CTkFrame(status_content, fg_color=COLORS["dark"], corner_radius=8)
        self.details_frame.pack(fill='x', ipadx=20, ipady=10)
        self.details_frame.pack_forget() 

        # Labels de Detalhes
        self.lbl_detail_port = ctk.CTkLabel(self.details_frame, text="Porta: -", font=ctk.CTkFont(family="Consolas", size=12))
        self.lbl_detail_port.pack(anchor='w')
        
        self.lbl_detail_baud = ctk.CTkLabel(self.details_frame, text="Baud: -", font=ctk.CTkFont(family="Consolas", size=12))
        self.lbl_detail_baud.pack(anchor='w')
        
        self.lbl_detail_proto = ctk.CTkLabel(self.details_frame, text="Protocolo: Serial/UART", font=ctk.CTkFont(family="Consolas", size=12))
        self.lbl_detail_proto.pack(anchor='w')

# ---------------------------------------------------------
    # NOVA ABA DE CONFIGURAÇÕES (ESTILIZADA)
    # ---------------------------------------------------------
    def _build_settings_tab(self, parent):
        # Configuração do Grid Principal (2 Colunas iguais)
        parent.grid_columnconfigure(0, weight=1)
        parent.grid_columnconfigure(1, weight=1)
        parent.grid_rowconfigure(0, weight=1) # Conteúdo principal
        parent.grid_rowconfigure(1, weight=0) # Rodapé (Reset)

        # ================== CARD 1: APARÊNCIA VISUAL (ESQUERDA) ==================
        visual_frame = ctk.CTkFrame(parent, corner_radius=10)
        visual_frame.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        
        # Título do Card
        ctk.CTkLabel(visual_frame, text="🎨 Aparência Visual", font=ctk.CTkFont(size=18, weight="bold"), text_color=COLORS["secondary"]).pack(pady=(20, 15))
        ctk.CTkLabel(visual_frame, text="Personalize as cores e o tema", font=ctk.CTkFont(size=12), text_color="gray").pack(pady=(0, 20))

        # --- Seção Tema ---
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
        # Setar valor atual
        curr_theme = self.config.data.get('appearance', {}).get('theme', 'System')
        display_theme = {'System': 'System', 'Light': 'Claro', 'Dark': 'Escuro'}.get(curr_theme, 'System')
        self.theme_menu.set(display_theme)

        # --- Seção Cores ---
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

        # --- Seção Fonte ---
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

        # ================== CARD 2: SISTEMA & COMPORTAMENTO (DIREITA) ==================
        system_frame = ctk.CTkFrame(parent, corner_radius=10)
        system_frame.grid(row=0, column=1, sticky="nsew", padx=10, pady=10)

        # Título do Card
        ctk.CTkLabel(system_frame, text="⚙️ Sistema & Janela", font=ctk.CTkFont(size=18, weight="bold"), text_color=COLORS["secondary"]).pack(pady=(20, 15))
        ctk.CTkLabel(system_frame, text="Comportamento e opacidade", font=ctk.CTkFont(size=12), text_color="gray").pack(pady=(0, 20))

        # --- Seção System Tray ---
        tray_box = ctk.CTkFrame(system_frame, fg_color="transparent", border_width=1, border_color=COLORS["secondary"], corner_radius=8)
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

        # --- Seção Transparência ---
        trans_box = ctk.CTkFrame(system_frame, fg_color="transparent")
        trans_box.pack(fill='x', padx=20, pady=20)
        
        header_trans = ctk.CTkFrame(trans_box, fg_color="transparent")
        header_trans.pack(fill='x', pady=(0, 5))
        ctk.CTkLabel(header_trans, text="Transparência da Janela", font=ctk.CTkFont(weight="bold")).pack(side='left')
        self.transparency_label = ctk.CTkLabel(header_trans, text="100%", font=ctk.CTkFont(weight="bold"), text_color=COLORS["secondary"])
        self.transparency_label.pack(side='right')

        self.transparency_var = tk.DoubleVar(value=self.config.data.get('appearance', {}).get('transparency', 1.0))
        
        # Callback para atualizar o label em tempo real enquanto arrasta
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

        # ================== RODAPÉ: ZONA DE RESET ==================
        reset_frame = ctk.CTkFrame(parent, fg_color="transparent")
        reset_frame.grid(row=1, column=0, columnspan=2, sticky="ew", padx=20, pady=10)
        
        ctk.CTkLabel(reset_frame, text="Problemas com a interface?", text_color="gray", font=ctk.CTkFont(size=12)).pack(side='left')
        
        ctk.CTkButton(
            reset_frame, 
            text="🔄 Restaurar Configurações Padrão", 
            command=self._reset_appearance,
            fg_color=COLORS["dark"], 
            border_width=1,
            border_color=COLORS["secondary"],
            height=32
        ).pack(side='right')
        
    def _on_theme_change(self, value):
        theme_map = {'System': 'System', 'Claro': 'Light', 'Escuro': 'Dark'}
        english = theme_map.get(value, 'System')
        ctk.set_appearance_mode(english)
        self.config.data.setdefault('appearance', {})['theme'] = english
        self.config.save()

    def _build_update_tab(self, parent):
            # Layout Principal da Aba
            parent.grid_columnconfigure(0, weight=1)
            parent.grid_rowconfigure(0, weight=1) # Conteúdo
            
            main_container = ctk.CTkFrame(parent, corner_radius=10, fg_color="transparent")
            main_container.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
            
            # --- 1. Área de Cards (Comparação) ---
            cards_frame = ctk.CTkFrame(main_container, fg_color="transparent")
            cards_frame.pack(fill='x', pady=(10, 20))
            
            # Card Esquerda: Versão Local
            self._create_version_card(cards_frame, "Sua Versão", APP_VERSION, COLORS["secondary"], "left")
            
            # Separador (Seta)
            arrow_label = ctk.CTkLabel(cards_frame, text="➜", font=ctk.CTkFont(size=24), text_color="gray")
            arrow_label.pack(side='left', padx=10)
            
            # Card Direita: Versão Remota (Inicia igual a local conforme pedido)
            self.remote_version_var = tk.StringVar(value=APP_VERSION)
            self.remote_card_frame = self._create_version_card(cards_frame, "Versão no Servidor", self.remote_version_var, COLORS["dark"], "right")

            # --- 2. Área de Status e Ação ---
            action_frame = ctk.CTkFrame(main_container, corner_radius=8, border_width=1, border_color=COLORS["secondary"])
            action_frame.pack(fill='both', expand=True, padx=10, pady=10)
            
            # Ícone de Status Grande
            self.status_icon_label = ctk.CTkLabel(action_frame, text="☁️", font=ctk.CTkFont(size=40))
            self.status_icon_label.pack(pady=(20, 5))
            
            # Título do Status
            self.status_title_label = ctk.CTkLabel(action_frame, text="Sistema Pronto", font=ctk.CTkFont(size=16, weight="bold"))
            self.status_title_label.pack(pady=(0, 5))
            
            # Detalhes do Status (Log visual)
            self.status_detail_label = ctk.CTkLabel(action_frame, text="Clique em 'Verificar' para buscar atualizações.", text_color="gray")
            self.status_detail_label.pack(pady=(0, 20))
            
            # Botão de Ação
            self.check_update_btn = ctk.CTkButton(
                action_frame, 
                text="🔍 Verificar Agora", 
                command=self._check_update_thread,
                font=ctk.CTkFont(size=14, weight="bold"),
                height=40,
                fg_color=COLORS["primary"]
            )
            self.check_update_btn.pack(pady=(0, 20))

            # Rodapé com data
            self.last_check_label = ctk.CTkLabel(main_container, text="Última verificação: Nunca", font=ctk.CTkFont(size=10), text_color="gray")
            self.last_check_label.pack(side='bottom', pady=5)
    
    def _create_version_card(self, parent, title, value_var, color, side):
        card = ctk.CTkFrame(parent, corner_radius=10, border_width=2, border_color=color)
        card.pack(side=side, expand=True, fill='both', padx=5)
        
        ctk.CTkLabel(card, text=title, font=ctk.CTkFont(size=12, weight="bold"), text_color="gray").pack(pady=(15, 5))
        
        # Aceita tanto string direta quanto StringVar
        if isinstance(value_var, tk.StringVar):
            lbl = ctk.CTkLabel(card, textvariable=value_var, font=ctk.CTkFont(size=22, weight="bold"), text_color=color)
        else:
            lbl = ctk.CTkLabel(card, text=value_var, font=ctk.CTkFont(size=22, weight="bold"), text_color=color)
        
        lbl.pack(pady=(0, 15))
        return card

    def refresh_all(self):
        self.refresh_all_buttons()
        self.update_serial_ports()
        self.logger.info("Interface atualizada")
        
    def refresh_all_buttons(self):
        for key, widget_map in self.button_frames.items():
            btn_conf = self.config.data.get('buttons', {}).get(key, {})
            widget_map['title_label'].configure(text=btn_conf.get('label', f'Botão {key}'))
            icon_path = btn_conf.get('icon', '')
            ctk_img = self.icon_loader.load_icon_from_path(icon_path) if icon_path else None
            if not ctk_img and icon_path and icon_path.lower().endswith('.exe'):
                ctk_img = self.icon_loader.try_load_windows_exe_icon(icon_path)
            if ctk_img: widget_map['icon_label'].configure(image=ctk_img, text='')
            else: widget_map['icon_label'].configure(image=None, text='📱')

    def open_button_config(self, button_key: str):
        if 'buttons' not in self.config.data:
            self.config.data['buttons'] = self.config._default()['buttons']
            self.config.save()
        conf = self.config.data['buttons'][button_key]
        dlg = ButtonConfigDialog(self, button_key, conf, self.icon_loader, self.logger)
        self.wait_window(dlg)
        self.config.save()
        self.refresh_all_buttons()

    def update_serial_ports(self):
        ports = self.serial_manager.list_ports() or ['Nenhuma']
        self.port_option.configure(values=ports)
        try:
            curr = self.config.data.get('serial', {}).get('port', '')
            self.port_option.set(curr if curr in ports else ports[0])
        except: self.port_option.set(ports[0])

    def _connect_serial(self):
        port = self.port_option.get()
        if not port or port == 'Nenhuma': return
        if self.serial_manager.connect(port, int(self.baud_option.get())):
            self.config.data['serial']['port'] = port
            self.config.data['serial']['baud'] = int(self.baud_option.get())
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
        if self.config.save(): messagebox.showinfo("Sucesso", "Configurações salvas!")
        else: messagebox.showerror("Erro", "Erro ao salvar!")
    def _show_about(self): AboutDialog(self)

    def _check_update_thread(self):
        self.check_update_btn.configure(state='disabled', text="Conectando...")
        self.status_title_label.configure(text="Verificando...", text_color=COLORS["primary"])
        self.status_detail_label.configure(text="Aguarde, contatando servidor...")
        self.status_icon_label.configure(text="⏳")
        
        t = threading.Thread(target=self._check_update, daemon=True)
        t.start()

    def _check_update(self):
        # Simula um pequeno delay para UX (opcional, remove sensação de 'piscar')
        time.sleep(0.5) 
        
        self.logger.info('Iniciando verificação de atualização...')
        res = self.update_checker.check_update()
        
        # Atualiza GUI na thread principal
        self.after(0, lambda: self._process_update_result(res))

    def _process_update_result(self, res):
        timestamp = time.strftime("%H:%M:%S")
        self.last_check_label.configure(text=f"Última verificação: Hoje às {timestamp}")
        self.check_update_btn.configure(state='normal', text="🔍 Verificar Novamente")

        if not res.get('ok'):
            # --- ERRO ---
            error_msg = res.get("error", "Desconhecido")
            self.logger.error(f'Falha na atualização: {error_msg}')
            
            self.status_icon_label.configure(text="❌")
            self.status_title_label.configure(text="Falha na Conexão", text_color=COLORS["danger"])
            
            # Quebra o texto se for muito longo
            if len(error_msg) > 60: error_msg = error_msg[:57] + "..."
            self.status_detail_label.configure(text=error_msg)
            
            # Reseta a versão remota para a atual (fallback)
            self.remote_version_var.set(APP_VERSION)
            self.remote_card_frame.configure(border_color=COLORS["danger"])
            
        else:
            latest = res.get('latest')
            self.remote_version_var.set(latest)
            
            if res.get('is_new'):
                # --- NOVA VERSÃO DISPONÍVEL ---
                self.logger.info(f'Atualização encontrada: {latest}')
                
                self.status_icon_label.configure(text="🎉")
                self.status_title_label.configure(text="Nova Versão Disponível!", text_color=COLORS["warning"])
                self.status_detail_label.configure(text=f"A versão {latest} está pronta para download.")
                self.remote_card_frame.configure(border_color=COLORS["warning"])
                
                # Muda o botão para Download
                self.check_update_btn.configure(
                    text="⬇️ Baixar Atualização",
                    fg_color=COLORS["warning"],
                    text_color="black", # Melhor contraste no amarelo
                    command=lambda: webbrowser.open(res.get('download_url'))
                )
                
                if messagebox.askyesno('Atualização', f'Nova versão {latest} encontrada!\nDeseja abrir a página de download agora?'):
                    webbrowser.open(res.get('download_url'))
            else:
                # --- TUDO ATUALIZADO ---
                self.logger.info('Sistema já está atualizado.')
                
                self.status_icon_label.configure(text="✅")
                self.status_title_label.configure(text="Você está atualizado", text_color=COLORS["success"])
                self.status_detail_label.configure(text=f"A versão {latest} é a mais recente.")
                self.remote_card_frame.configure(border_color=COLORS["success"])
                self.check_update_btn.configure(fg_color=COLORS["success"], text="🔍 Verificar Novamente")


    def _on_serial_message(self, text: str):
        self.logger.info(f'<- ESP: {text}')
        if text.startswith('BTN:'):
            key = text.split(':')[1]
            btn_conf = self.config.data['buttons'].get(key)
            if btn_conf: self.action_manager.perform(Action(btn_conf.get('action', {}).get('type', 'none'), btn_conf.get('action', {}).get('payload', '')))

def main():
    ctk.set_appearance_mode('System')
    app = Esp32DeckApp()
    app.protocol('WM_DELETE_WINDOW', app.on_closing)
    app.mainloop()

if __name__ == '__main__':
    main()