from __future__ import annotations
import os
import sys
import json
import threading
import time
import subprocess
import webbrowser
import traceback
from dataclasses import dataclass, field
from typing import Dict, Any, Optional, Callable, List

# Windows API
import win32gui

# GUI libs
import customtkinter as ctk
import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageTk

# Serial
import serial
import serial.tools.list_ports

# Optional libs
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

# -----------------------------
# CONSTANTS / DEFAULTS
# -----------------------------
CONFIG_FILE = "Esp32_deck_config.json"
ICON_FOLDER = "icons"
LOG_FILE = "Esp32_deck.log"
APP_VERSION = "1.2.0"
APP_NAME = "Esp32 Deck Controller"
DEVELOPER = "Luiz F. R. Pimentel"
GITHUB_URL = "https://github.com/KanekiZLF"
UPDATE_CHECK_URL = "https://raw.githubusercontent.com/yourrepo/Esp32deck/main/version.json"
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
    "light": "#F8F9FA"
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
# Logger
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
            "appearance": {"theme": "System", "icon_size": ICON_SIZE[0]},
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
                    data['appearance'] = {"theme": "System", "icon_size": ICON_SIZE[0]}
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
            # ✅ CORREÇÃO: Usar resize em vez de thumbnail para manter proporção
            img.thumbnail(self.icon_size, Image.LANCZOS)
            
            # ✅ CORREÇÃO: Criar CTkImage com tamanho correto
            ctk_img = ctk.CTkImage(
                light_image=img,
                dark_image=img,
                size=img.size  # Usar o tamanho real da imagem após thumbnail
            )
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
            import win32con
            import win32ui

            large, small = win32gui.ExtractIconEx(exe_path, 0)
            hicon = None
            if large and len(large) > 0:
                hicon = large[0]
            elif small and len(small) > 0:
                hicon = small[0]
            else:
                return None

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

            try:
                os.remove(temp_bmp)
            except Exception:
                pass

            try:
                for h in large:
                    win32gui.DestroyIcon(h)
            except Exception:
                pass

            return out_png_path
        except Exception:
            return None

# -----------------------------
# Actions
# -----------------------------
@dataclass
class Action:
    type: str
    payload: Any = ''

class ActionManager:
    def __init__(self, logger: Logger):
        self.logger = logger

    def perform(self, action: Action):
        try:
            self.logger.debug(f"Executando ação: {action}")
            if action.type == 'none':
                self.logger.warn('Ação vazia')
            elif action.type == 'open_program':
                path = action.payload
                if os.path.exists(path):
                    os.startfile(path) if sys.platform.startswith('win') else subprocess.Popen([path])
                    self.logger.info(f'Abrindo programa: {path}')
                else:
                    self.logger.error(f'Arquivo não encontrado: {path}')
            elif action.type == 'open_url':
                webbrowser.open(action.payload)
                self.logger.info(f'Abrindo URL: {action.payload}')
            elif action.type == 'run_cmd':
                cmd = action.payload
                subprocess.Popen(cmd, shell=True)
                self.logger.info(f'Rodando comando: {cmd}')
            elif action.type == 'type_text':
                if not PYAUTOGUI_AVAILABLE:
                    self.logger.error('pyautogui não disponível')
                    return
                pyautogui.write(action.payload)
                self.logger.info('Texto digitado via pyautogui')
            elif action.type == 'hotkey':
                if not PYAUTOGUI_AVAILABLE:
                    self.logger.error('pyautogui não disponível')
                    return
                keys = action.payload if isinstance(action.payload, list) else [action.payload]
                pyautogui.hotkey(*keys)
                self.logger.info(f'Hotkey enviada: {keys}')
            elif action.type == 'script':
                script_path = action.payload
                if os.path.exists(script_path):
                    subprocess.Popen([sys.executable, script_path])
                    self.logger.info(f'Executando script: {script_path}')
                else:
                    self.logger.error(f'Script não encontrado: {script_path}')
            elif action.type == 'macro':
                for a in action.payload:
                    sub = Action(a.get('type'), a.get('payload'))
                    self.perform(sub)
                    time.sleep(0.1)
            else:
                self.logger.warn(f'Ação desconhecida: {action.type}')
        except Exception as e:
            self.logger.error(f'Erro ao executar ação: {e}\n{traceback.format_exc()}')

# -----------------------------
# Serial Manager
# -----------------------------
class SerialManager:
    def __init__(self, config: ConfigManager, logger: Logger, on_message: Optional[Callable[[str], None]] = None):
        self.config = config
        self.logger = logger
        self.on_message = on_message
        self._serial: Optional[serial.Serial] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False

    def list_ports(self) -> List[str]:
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self, port: str, baud: int = DEFAULT_SERIAL_BAUD):
        try:
            if self._serial and self._serial.is_open:
                self.disconnect()
            self._serial = serial.Serial(port, baud, timeout=1)
            time.sleep(1)
            self._running = True
            self._thread = threading.Thread(target=self._reader_loop, daemon=True)
            self._thread.start()
            self.logger.info(f'Conectado a {port} @ {baud}')
            return True
        except Exception as e:
            self.logger.error(f'Falha ao conectar serial: {e}')
            return False

    def disconnect(self):
        self._running = False
        try:
            if self._serial and self._serial.is_open:
                self._serial.close()
                self.logger.info('Porta serial fechada')
        except Exception as e:
            self.logger.warn(f'Erro ao fechar serial: {e}')

    def send(self, text: str):
        try:
            if not self._serial or not self._serial.is_open:
                self.logger.warn('Serial não conectada')
                return False
            self._serial.write(text.encode('utf-8') + b'\n')
            return True
        except Exception as e:
            self.logger.error(f'Erro ao enviar serial: {e}')
            return False

    def _reader_loop(self):
        try:
            while self._running and self._serial and self._serial.is_open:
                try:
                    if self._serial.in_waiting > 0:
                        line = self._serial.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            self.logger.debug(f'Recebido serial: {line}')
                            if self.on_message:
                                self.on_message(line)
                    else:
                        time.sleep(0.05)
                except Exception as e:
                    self.logger.error(f'Erro leitura serial: {e}')
                    break
        finally:
            self._running = False
            self.logger.debug('Thread serial encerrada')

# -----------------------------
# Update Checker
# -----------------------------
class UpdateChecker:
    def __init__(self, config: ConfigManager, logger: Logger):
        self.config = config
        self.logger = logger

    def check_update(self) -> Dict[str, Any]:
        url = self.config.data.get('update', {}).get('check_url', UPDATE_CHECK_URL)
        self.logger.debug(f'Verificando atualização em {url}')
        
        if not REQUESTS_AVAILABLE:
            return {"ok": False, "error": "Biblioteca 'requests' não instalada"}
        
        try:
            response = requests.get(url, timeout=10)  # Aumentei o timeout
            response.raise_for_status()
            
            info = response.json()
            latest = info.get('latest_version')
            download = info.get('download_url')
            
            # Valida se a versão foi retornada
            if not latest:
                return {
                    "ok": False, 
                    "error": "Resposta do servidor inválida: versão não encontrada"
                }
            
            is_new = self._version_greater(latest, APP_VERSION)
            
            return {
                "ok": True, 
                "latest": latest, 
                "download_url": download, 
                "is_new": is_new
            }
            
        except requests.exceptions.Timeout:
            return {
                "ok": False, 
                "error": "Tempo limite excedido ao verificar atualização"
            }
        except requests.exceptions.ConnectionError:
            return {
                "ok": False, 
                "error": "Erro de conexão - verifique sua internet"
            }
        except requests.exceptions.HTTPError as e:
            status_code = e.response.status_code if e.response else "desconhecido"
            if status_code == 404:
                return {
                    "ok": False, 
                    "error": f"URL não encontrada (404)\n{url}"
                }
            else:
                return {
                    "ok": False, 
                    "error": f"Erro HTTP {status_code} - Servidor não disponível"
                }
        except requests.exceptions.RequestException as e:
            return {
                "ok": False, 
                "error": f"Erro de rede: {str(e)}"
            }
        except ValueError as e:
            return {
                "ok": False, 
                "error": "Resposta do servidor em formato JSON inválido"
            }
        except Exception as e:
            return {
                "ok": False, 
                "error": f"Erro inesperado: {str(e)}"
            }

    @staticmethod
    def _version_greater(a: str, b: str) -> bool:
        try:
            # Remove possíveis caracteres não numéricos
            a_clean = ''.join(c for c in a if c.isdigit() or c == '.')
            b_clean = ''.join(c for c in b if c.isdigit() or c == '.')
            
            pa = [int(x) for x in (a_clean or '0').split('.')]
            pb = [int(x) for x in (b_clean or '0').split('.')]
            
            # Preenche com zeros para ter o mesmo tamanho
            max_len = max(len(pa), len(pb))
            pa.extend([0] * (max_len - len(pa)))
            pb.extend([0] * (max_len - len(pb)))
            
            return pa > pb
        except Exception:
            return False

# -----------------------------
# About Dialog
# -----------------------------
class AboutDialog(ctk.CTkToplevel):
    def __init__(self, parent):
        super().__init__(parent)
        self.title(f'Sobre - {APP_NAME}')
        self.geometry('400x320')  # Aumentei a altura para o link
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        
        # Center the dialog
        self._center_window()
        
        self._build_ui()
        
    def _center_window(self):
        self.update_idletasks()
        width = self.winfo_width()
        height = self.winfo_height()
        x = (self.winfo_screenwidth() // 2) - (width // 2)
        y = (self.winfo_screenheight() // 2) - (height // 2)
        self.geometry(f'{width}x{height}+{x}+{y}')
        
    def _build_ui(self):
        main_frame = ctk.CTkFrame(self, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=20, pady=20)
        
        # App icon/logo placeholder
        logo_frame = ctk.CTkFrame(main_frame, width=64, height=64, corner_radius=15, fg_color=COLORS["primary"])
        logo_frame.pack(pady=(20, 10))
        logo_label = ctk.CTkLabel(logo_frame, text="Esp32Deck", font=ctk.CTkFont(size=24, weight="bold"), text_color="white")
        logo_label.pack(expand=True)
        
        # App info
        ctk.CTkLabel(main_frame, text=APP_NAME, font=ctk.CTkFont(size=20, weight="bold")).pack(pady=(0, 5))
        ctk.CTkLabel(main_frame, text=f"Versão {APP_VERSION}", font=ctk.CTkFont(size=14)).pack(pady=(0, 10))
        
        # Description
        desc_text = "Software de controle para Esp32 Deck com ESP32\n\nGerencie e execute ações com botões personalizáveis"
        desc_label = ctk.CTkLabel(main_frame, text=desc_text, font=ctk.CTkFont(size=12), justify="center")
        desc_label.pack(pady=(0, 15), padx=20)
        
        # Developer info
        info_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        info_frame.pack(fill='x', padx=20, pady=10)
        
        ctk.CTkLabel(info_frame, text=f"Desenvolvido por: {DEVELOPER}", font=ctk.CTkFont(size=12)).pack(anchor='w', pady=2)
        
        # ✅ GITHUB CLICÁVEL
        github_frame = ctk.CTkFrame(info_frame, fg_color="transparent")
        github_frame.pack(anchor='w', pady=2)
        
        ctk.CTkLabel(
            github_frame, 
            text="GitHub: ", 
            font=ctk.CTkFont(size=12)
        ).pack(side='left')
        
        # Botão com aparência de link
        github_btn = ctk.CTkButton(
            github_frame,
            text=GITHUB_URL,
            font=ctk.CTkFont(size=12, underline=True),
            fg_color="transparent",
            hover_color=COLORS["light"],
            text_color=COLORS["primary"],
            height=20,
            width=len(GITHUB_URL) * 7,
            command=self._open_github
        )
        github_btn.pack(side='left')
        
        # Close button
        ctk.CTkButton(main_frame, text="Fechar", command=self.destroy, 
                      fg_color=COLORS["primary"], hover_color=COLORS["secondary"]).pack(pady=20)

    def _open_github(self):
        """Abre o link do GitHub no navegador"""
        try:
            import webbrowser
            webbrowser.open(GITHUB_URL)
        except Exception as e:
            import tkinter.messagebox as messagebox
            messagebox.showerror("Erro", f"Não foi possível abrir o GitHub:\n{str(e)}")

# -----------------------------
# Button Config Dialog
# -----------------------------
class ButtonConfigDialog(ctk.CTkToplevel):
    def __init__(self, parent: Esp32DeckApp, button_key: str, conf: Dict[str, Any], icon_loader: IconLoader, logger: Logger):
        super().__init__(parent)
        self.parent = parent
        self.button_key = button_key
        self.conf = conf
        self.icon_loader = icon_loader
        self.logger = logger
        
        self.title(f'Configurar Botão {button_key}')
        self.geometry('520x400')  # Aumentei um pouco para acomodar melhor
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        
        self._center_window()
        
        self.label_var = tk.StringVar(value=self.conf.get('label', f'Botão {button_key}'))
        self.icon_path = self.conf.get('icon', '')
        self.action_type = tk.StringVar(value=self.conf.get('action', {}).get('type', 'none'))
        self._initial_payload = self.conf.get('action', {}).get('payload', '')
        
        try:
            self.action_payload = tk.StringVar(value=json.dumps(self._initial_payload, ensure_ascii=False) if isinstance(self._initial_payload, (dict, list)) else str(self._initial_payload))
        except Exception:
            self.action_payload = tk.StringVar(value=str(self._initial_payload))

        self._build()

    def _center_window(self):
        self.update_idletasks()
        width = self.winfo_width()
        height = self.winfo_height()
        x = (self.winfo_screenwidth() // 2) - (width // 2)
        y = (self.winfo_screenheight() // 2) - (height // 2)
        self.geometry(f'{width}x{height}+{x}+{y}')

    def _build(self):
        main_frame = ctk.CTkFrame(self, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=15, pady=15)

        # Button label
        ctk.CTkLabel(main_frame, text='Nome do Botão:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', pady=(5, 0))

        # ✅ FUNÇÃO DE VALIDAÇÃO
        def on_name_change(*args):
            current_text = self.label_var.get()
            if len(current_text) > 16:
                # Corta o texto e atualiza a variável
                self.label_var.set(current_text[:16])
                # Opcional: mostrar tooltip ou mensagem
                name_entry.configure(border_color=COLORS["warning"])
                self.after(1000, lambda: name_entry.configure(border_color=ctk.ThemeManager.theme["CTkEntry"]["border_color"]))
            else:
                name_entry.configure(border_color=ctk.ThemeManager.theme["CTkEntry"]["border_color"])

        # ✅ ENTRY com limite
        name_entry = ctk.CTkEntry(
            main_frame, 
            textvariable=self.label_var, 
            width=400, 
            placeholder_text="Máximo 16 caracteres"
        )
        name_entry.pack(fill='x', pady=5)

        # ✅ VINCULAR VALIDAÇÃO
        self.label_var.trace('w', on_name_change)

        # Program section
        icon_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        icon_frame.pack(fill='x', pady=10)

        ctk.CTkLabel(icon_frame, text='Programa a ser executado:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=10)

        icon_content = ctk.CTkFrame(icon_frame, fg_color="transparent")
        icon_content.pack(fill='x', padx=10, pady=5)
        
        self.icon_preview = ctk.CTkLabel(icon_content, text='📱', width=64, height=64, 
                                       font=ctk.CTkFont(size=20), text_color=COLORS["primary"])
        self.icon_preview.pack(side='left', padx=10)

        btn_frame = ctk.CTkFrame(icon_content, fg_color="transparent")
        btn_frame.pack(side='left', padx=10)

        # Botões lado a lado
        ctk.CTkButton(btn_frame, text='Escolher Ícone', width=140,
                    command=self._choose_icon).pack(side='left', padx=5)
        ctk.CTkButton(btn_frame, text='Selecionar Programa', width=140,
                    command=self._select_program_for_button).pack(side='left', padx=5)

        # Action type
        '''
        action_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        action_frame.pack(fill='x', pady=10)
        
        action_content = ctk.CTkFrame(action_frame, fg_color="transparent")
        action_content.pack(fill='x', padx=10, pady=10)
        
        ctk.CTkLabel(action_content, text='Tipo de Ação:', font=ctk.CTkFont(weight="bold")).pack(side='left')
        action_menu = ctk.CTkOptionMenu(action_content, 
                                      values=['none','open_program','open_url','run_cmd','type_text','hotkey','script','macro'], 
                                      variable=self.action_type, width=200)
        action_menu.pack(side='left', padx=10)
        '''

        # Localização do programa / payload
        payload_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        payload_frame.pack(fill='x', pady=10)

        ctk.CTkLabel(payload_frame, text='Localização do programa:', font=ctk.CTkFont(weight="bold")).pack(anchor='w', padx=10, pady=(10, 5))
        
        # ✅ CORREÇÃO: Usar Entry em vez de Textbox para evitar scroll e campo grande
        self.payload_entry = ctk.CTkEntry(payload_frame, height=35)
        self.payload_entry.pack(fill='x', padx=10, pady=(0, 10))
        
        if isinstance(self._initial_payload, (dict, list)):
            try:
                self.payload_entry.insert(0, json.dumps(self._initial_payload, ensure_ascii=False))
            except Exception:
                self.payload_entry.insert(0, str(self._initial_payload))
        else:
            self.payload_entry.insert(0, str(self._initial_payload))

        # Buttons - ✅ CORREÇÃO: Botões centralizados e organizados
        buttons_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        buttons_frame.pack(fill='x', pady=10)

        # Frame interno para centralização
        inner_buttons_frame = ctk.CTkFrame(buttons_frame, fg_color="transparent")
        inner_buttons_frame.pack(expand=True)

        # Botão Testar - ESQUERDA
        test_btn = ctk.CTkButton(inner_buttons_frame, text='▶️ Testar', command=self._test_action,
                                fg_color=COLORS["primary"], width=100)
        test_btn.pack(side='left', padx=5)

        # Botão Cancelar - CENTRO
        cancel_btn = ctk.CTkButton(inner_buttons_frame, text='🚫 Cancelar', command=self.destroy,
                                fg_color=COLORS["danger"], width=100)
        cancel_btn.pack(side='left', padx=20)

        # Botão Salvar - DIREITA
        save_btn = ctk.CTkButton(inner_buttons_frame, text='💾 Salvar', command=self._save_and_close,
                                fg_color=COLORS["success"], width=100)
        save_btn.pack(side='left', padx=5)

        self._refresh_icon_preview()

    def _select_program_for_button(self):
        exe_path = filedialog.askopenfilename(
            title=f"Selecione o executável para o Botão {self.button_key}", 
            filetypes=[("Executáveis", "*.exe"), ("Todos", "*.*")]
        )
        if not exe_path:
            return
        
        self.payload_entry.delete(0, 'end')
        self.payload_entry.insert(0, exe_path)

        basename = os.path.splitext(os.path.basename(exe_path))[0]
        safe_makedirs(ICON_FOLDER)
        out_png = os.path.join(ICON_FOLDER, f"btn{self.button_key}_{basename}.png")
        extracted = self.icon_loader.extract_icon_to_png(exe_path, out_png, size=128)
        
        if extracted:
            self.icon_path = extracted
            self.conf['icon'] = self.icon_path
            self.conf['action'] = {'type': 'open_program', 'payload': exe_path}
            try:
                self.parent.config.save()
            except Exception:
                pass
            self._refresh_icon_preview()
            messagebox.showinfo("Ícone extraído", f"Ícone extraído e salvo em:\n{extracted}")
        else:
            self.conf['action'] = {'type': 'open_program', 'payload': exe_path}
            try:
                self.parent.config.save()
            except Exception:
                pass
            messagebox.showwarning("Extração falhou", "Não foi possível extrair o ícone do executável.")

    def _choose_icon(self):
        path = filedialog.askopenfilename(
            title='Escolher ícone', 
            filetypes=[('Images', '*.png *.jpg *.ico'), ('All', '*.*')]
        )
        if not path:
            return
        self.icon_path = path
        self.conf['icon'] = self.icon_path
        try:
            self.parent.config.save()
        except Exception:
            pass
        self._refresh_icon_preview()

    def _refresh_icon_preview(self):
        """Update icon preview display"""
        ctk_img = self.icon_loader.load_icon_from_path(self.icon_path) if self.icon_path else None
        
        if ctk_img:
            # ✅ CORREÇÃO: Configurar CTkImage corretamente
            self.icon_preview.configure(image=ctk_img, text='')
        else:
            # ✅ CORREÇÃO: Não usar string como imagem
            self.icon_preview.configure(image='', text='📱')

    def _save_and_close(self):
        """✅ NOVA FUNÇÃO: Salva e fecha a janela"""
        try:
            raw = self.payload_entry.get().strip()
            try:
                payload = json.loads(raw) if raw else ''
            except Exception:
                payload = raw

            self.conf['label'] = self.label_var.get()
            if 'icon' not in self.conf:
                self.conf['icon'] = self.icon_path

            self.conf['action'] = {'type': 'open_program', 'payload': payload}

            self.conf['action'] = {'type': self.action_type.get(), 'payload': payload}
            self.parent.config.save()
            self.logger.info(f'Botão {self.button_key} salvo: {self.conf}')
            self.destroy()  # ✅ Fecha a janela após salvar
        except Exception as e:
            messagebox.showerror('Erro', str(e))

    def _save(self):
        """Função original mantida para compatibilidade"""
        self._save_and_close()

    def _test_action(self):
        raw = self.payload_entry.get().strip()
        try:
            payload = json.loads(raw) if raw else ''
        except Exception:
            payload = raw
        
        # ✅ DEFINIR AUTOMATICAMENTE COMO 'open_program'
        action = Action('open_program', payload)
        self.parent.action_manager.perform(action)

# -----------------------------
# GUI / App
# -----------------------------
class Esp32DeckApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title(f'{APP_NAME} v{APP_VERSION}')
        self.geometry('900x700')
        self.resizable(False, False)  # Desabilita redimensionamento
        
        # Configure custom theme
        self._setup_theme()
        
        # Core objects
        self.config = ConfigManager()
        self.icon_loader = IconLoader(icon_size=(self.config.data.get('appearance', {}).get('icon_size', ICON_SIZE[0]),) * 2)
        self.logger_widget = None
        self.logger = Logger(file_path=LOG_FILE)
        self.action_manager = ActionManager(self.logger)
        self.serial_manager = SerialManager(self.config, self.logger, on_message=self._on_serial_message)
        self.update_checker = UpdateChecker(self.config, self.logger)
        
        # UI state
        self.button_frames: Dict[str, Dict[str, Any]] = {}

        # Style
        ctk.set_appearance_mode(self.config.data.get('appearance', {}).get('theme', 'System'))

        # Build UI
        self._build_ui()

        # ✅ CARREGAR CONFIGURAÇÕES DE APARÊNCIA
        self._load_appearance_settings()

        # Hook logger to textbox after it's created
        self.logger.textbox = self.log_textbox

        # Centraliza a janela
        self._center_window()

        # Refresh display
        self.refresh_all_buttons()
        self.update_serial_ports()
        # Aplicar transparência salva
        try:
            transparency = self.config.data.get('appearance', {}).get('transparency', 1.0)
            self.attributes('-alpha', transparency)
        except Exception:
            pass

    def _load_appearance_settings(self):
        """Carrega as configurações de aparência salvas"""
        try:
            appearance = self.config.data.get('appearance', {})
            
            # Aplicar tema
            theme = appearance.get('theme', 'System')
            ctk.set_appearance_mode(theme)
            
            # Aplicar transparência
            transparency = appearance.get('transparency', 1.0)
            self.attributes('-alpha', transparency)
            
            # Aplicar esquema de cores
            color_scheme = appearance.get('color_scheme', 'Padrão')
            self._on_color_scheme_change(color_scheme)
            
        except Exception as e:
            self.logger.error(f"Erro ao carregar configurações de aparência: {e}")

    def _setup_theme(self):
        """Configure custom theme colors"""
        ctk.set_default_color_theme("dark-blue")  # Base theme

    def _center_window(self):
        """Centraliza a janela na tela de forma simples"""
        self.update_idletasks()
        
        # Calcula posição central
        x = (self.winfo_screenwidth() // 2) - (900 // 2)
        y = (self.winfo_screenheight() // 2) - (700 // 2)
        
        # Define a posição
        self.geometry(f'+{x}+{y}')

    def _apply_custom_theme(self, theme_config):
        """Aplica um tema customizado ao CustomTkinter"""
        try:
            # Obter o gerenciador de tema
            from customtkinter import ThemeManager
            
            # Aplicar configurações do tema
            for widget_type, styles in theme_config.items():
                for style_name, style_value in styles.items():
                    ThemeManager.theme[widget_type][style_name] = style_value
            
            # Forçar atualização de todos os widgets
            self._update_all_widgets(self)
            
        except Exception as e:
            self.logger.error(f"Erro ao aplicar tema customizado: {e}")

    def _update_all_widgets(self, widget):
        """Atualiza recursivamente todos os widgets"""
        try:
            widget.configure()
            for child in widget.winfo_children():
                self._update_all_widgets(child)
        except:
            pass

    def _on_transparency_change(self, value):
        """Altera a transparência da janela"""
        try:
            self.attributes('-alpha', value)
            self.transparency_label.configure(text=f"{int(value * 100)}%")
            
            if 'appearance' not in self.config.data:
                self.config.data['appearance'] = {}
            self.config.data['appearance']['transparency'] = value
            self.config.save()
        except Exception as e:
            self.logger.error(f"Erro ao alterar transparência: {e}")

    def _on_color_scheme_change(self, value):
        """Altera o esquema de cores"""
        try:
            # ✅ IMPLEMENTAÇÃO REAL - Criar temas customizados
            if value == 'Padrão':
                ctk.set_default_color_theme("dark-blue")
            elif value == 'Moderno':
                # Tema azul moderno
                self._apply_custom_theme({
                    "CTk": {"fg_color": ["#2B2B2B", "#1E1E1E"]},
                    "CTkButton": {"fg_color": ["#3B8ED0", "#1F6AA5"], "hover_color": ["#36719F", "#144870"]},
                    "CTkFrame": {"fg_color": ["#2B2B2B", "#1E1E1E"]}
                })
            elif value == 'Vibrante':
                # Tema verde vibrante
                self._apply_custom_theme({
                    "CTk": {"fg_color": ["#2B2B2B", "#1E1E1E"]},
                    "CTkButton": {"fg_color": ["#28A745", "#1E7E34"], "hover_color": ["#218838", "#155724"]},
                    "CTkFrame": {"fg_color": ["#2B2B2B", "#1E1E1E"]}
                })
            elif value == 'Suave':
                # Tema roxo suave
                self._apply_custom_theme({
                    "CTk": {"fg_color": ["#2B2B2B", "#1E1E1E"]},
                    "CTkButton": {"fg_color": ["#6F42C1", "#5A2D91"], "hover_color": ["#5A2D91", "#4A2378"]},
                    "CTkFrame": {"fg_color": ["#2B2B2B", "#1E1E1E"]}
                })
            elif value == 'Escuro Total':
                # Tema completamente escuro
                self._apply_custom_theme({
                    "CTk": {"fg_color": ["#1A1A1A", "#0D0D0D"]},
                    "CTkButton": {"fg_color": ["#333333", "#222222"], "hover_color": ["#444444", "#2A2A2A"]},
                    "CTkFrame": {"fg_color": ["#1A1A1A", "#0D0D0D"]}
                })
            
            # ✅ FORÇAR ATUALIZAÇÃO
            self.update()
            
            if 'appearance' not in self.config.data:
                self.config.data['appearance'] = {}
            self.config.data['appearance']['color_scheme'] = value
            self.config.save()
            
            self.logger.info(f"Esquema de cores alterado para: {value}")
        except Exception as e:
            self.logger.error(f"Erro ao alterar esquema de cores: {e}")

    def _on_font_size_change(self, value):
        """Altera o tamanho da fonte global"""
        try:
            size_map = {
                'Pequeno': 12,
                'Médio': 14, 
                'Grande': 16
            }
            
            new_size = size_map.get(value, 14)
            
            # ✅ APLICAR EM ALGUNS ELEMENTOS CHAVE (simplificado)
            try:
                # Atualizar fonte do header
                for widget in self.winfo_children():
                    if hasattr(widget, 'winfo_children'):
                        for child in widget.winfo_children():
                            if isinstance(child, ctk.CTkLabel):
                                current_font = child.cget("font")
                                if isinstance(current_font, ctk.CTkFont):
                                    new_font = ctk.CTkFont(
                                        family=current_font._family,
                                        size=new_size,
                                        weight=current_font._weight
                                    )
                                    child.configure(font=new_font)
            except Exception as font_error:
                self.logger.debug(f"Ajuste de fonte parcial: {font_error}")
            
            if 'appearance' not in self.config.data:
                self.config.data['appearance'] = {}
            self.config.data['appearance']['font_size'] = value
            self.config.save()
            
            self.logger.info(f"Tamanho da fonte alterado para: {value}")
            messagebox.showinfo("Configuração Aplicada", 
                            f"Tamanho da fonte alterado para '{value}'. Alguns elementos podem requerer reinício para aplicação completa.")
            
        except Exception as e:
            self.logger.error(f"Erro ao alterar tamanho da fonte: {e}")

    def _reset_appearance(self):
        """Restaura todas as configurações de aparência para os padrões"""
        try:
            if messagebox.askyesno("Confirmar Reset", 
                                "Deseja restaurar todas as configurações de aparência para os valores padrão?"):
                
                # Resetar configurações
                self.config.data['appearance'] = {
                    'theme': 'System',
                    'transparency': 1.0,
                    'color_scheme': 'Padrão',
                    'font_size': 'Médio'
                }
                
                # ✅ APLICAR RESET COMPLETO
                ctk.set_appearance_mode('System')
                ctk.set_default_color_theme("dark-blue")
                self.attributes('-alpha', 1.0)
                
                # ✅ FORÇAR ATUALIZAÇÃO
                self.update()
                
                # Atualizar UI
                self.theme_menu.set('System')
                self.transparency_var.set(1.0)
                self.transparency_label.configure(text="100%")
                self.color_scheme_menu.set('Padrão')
                self.font_size_menu.set('Médio')
                
                self.config.save()
                self.logger.info("Configurações de aparência restauradas para padrão")
                messagebox.showinfo("Reset Concluído", "Configurações de aparência restauradas com sucesso!")
                
        except Exception as e:
            self.logger.error(f"Erro ao resetar aparência: {e}")
            
    # -----------------------------
    # UI Construction
    # -----------------------------
    def _build_ui(self):
        # Header
        self._build_header()
        
        # Main content with tabs
        self.tabview = ctk.CTkTabview(self, width=860, height=500, corner_radius=10)
        self.tabview.pack(expand=True, fill='both', padx=20, pady=(0, 20))
        
        self.tab_buttons = self.tabview.add('🎮 Configurar Botões')
        self.tab_connection = self.tabview.add('🔌 Conexão')
        self.tab_settings = self.tabview.add('⚙️ Configurações')
        self.tab_update = self.tabview.add('🔄 Atualização')

        # Build tabs
        self._build_buttons_tab(self.tab_buttons)
        self._build_connection_tab(self.tab_connection)
        self._build_settings_tab(self.tab_settings)
        self._build_update_tab(self.tab_update)

        # Logger (bottom area)
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
        
        # Title
        title_frame = ctk.CTkFrame(header, fg_color="transparent")
        title_frame.pack(side='left', padx=20, pady=10)
        ctk.CTkLabel(title_frame, text=APP_NAME, font=ctk.CTkFont(size=20, weight="bold")).pack(anchor='w')
        ctk.CTkLabel(title_frame, text=f"v{APP_VERSION} - Controller para ESP32", 
                    font=ctk.CTkFont(size=12), text_color=COLORS["secondary"]).pack(anchor='w')
        
        # Header buttons
        btn_frame = ctk.CTkFrame(header, fg_color="transparent")
        btn_frame.pack(side='right', padx=20, pady=10)
        ctk.CTkButton(btn_frame, text="ℹ️ Sobre", width=80, command=self._show_about).pack(side='right', padx=(5, 0))
        ctk.CTkButton(btn_frame, text="💾 Salvar", width=80, command=self._save_all, 
                     fg_color=COLORS["success"]).pack(side='right', padx=5)
        ctk.CTkButton(btn_frame, text="🔄 Atualizar", width=80, command=self.refresh_all).pack(side='right', padx=5)
    
    def _build_buttons_tab(self, parent):
        # Instructions
        info_frame = ctk.CTkFrame(parent, corner_radius=8)
        info_frame.pack(fill='x', padx=10, pady=10)
        ctk.CTkLabel(info_frame, 
                    text="💡 Clique em 'Configurar' para definir a ação do botão.",
                    font=ctk.CTkFont(size=12)).pack(padx=10, pady=8)
        
        # Buttons Grid
        grid_frame = ctk.CTkFrame(parent)
        grid_frame.pack(expand=True, fill='both', padx=10, pady=10)
        
        # Configure grid layout
        for i in range(4):
            grid_frame.grid_columnconfigure(i, weight=1)
        for i in range(2):
            grid_frame.grid_rowconfigure(i, weight=1)

        # ✅ CORREÇÃO: Usar a nova função _create_button_frame
        btn_id = 1
        for row in range(2):
            for col in range(4):
                key = str(btn_id)
                self._create_button_frame(grid_frame, key, row, col)
                btn_id += 1

    def _create_button_frame(self, parent, key, row, col):
        """Create individual button frame"""
        btn_frame = ctk.CTkFrame(
            parent, 
            width=180, 
            height=180, 
            corner_radius=12, 
            border_width=2, 
            border_color=COLORS["secondary"]
        )
        btn_frame.grid(row=row, column=col, padx=8, pady=8, sticky='nsew')
        btn_frame.grid_propagate(False)

        # Icon
        icon_label = ctk.CTkLabel(
            btn_frame, 
            text='📱', 
            width=80, 
            height=80, 
            font=ctk.CTkFont(size=24), 
            text_color=COLORS["primary"]
        )
        icon_label.pack(pady=(15, 5))
        
        # Title (apenas label, não editável)
        btn_conf = self.config.data.get('buttons', {}).get(key, {})
        saved_label = btn_conf.get('label', f'Botão {key}')
        title_label = ctk.CTkLabel(
            btn_frame, 
            text=saved_label, 
            font=ctk.CTkFont(size=14, weight="bold")
        )
        title_label.pack(pady=(0, 5))
        
        # Configure button
        config_btn = ctk.CTkButton(
            btn_frame, 
            text='Configurar', 
            width=120, 
            height=28,
            command=lambda i=key: self.open_button_config(i),
            fg_color=COLORS["primary"], 
            hover_color=COLORS["secondary"]
        )
        config_btn.pack(pady=(0, 15))

        self.button_frames[key] = {
            'frame': btn_frame, 
            'icon_label': icon_label, 
            'title_label': title_label
        }

    def _build_connection_tab(self, parent):
        main_frame = ctk.CTkFrame(parent, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        # Connection status
        status_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        status_frame.pack(fill='x', padx=15, pady=15)
        
        self.status_label = ctk.CTkLabel(status_frame, text="🔴 Desconectado", 
                                       font=ctk.CTkFont(size=16, weight="bold"),
                                       text_color=COLORS["danger"])
        self.status_label.pack(pady=10)
        
        # Connection controls - usando pack para consistência
        controls_frame = ctk.CTkFrame(main_frame, fg_color="transparent")
        controls_frame.pack(fill='x', padx=15, pady=10)
        
        # Linha 1 - Porta Serial
        port_frame = ctk.CTkFrame(controls_frame, fg_color="transparent")
        port_frame.pack(fill='x', pady=5)
        
        ctk.CTkLabel(port_frame, text="Porta Serial:", font=ctk.CTkFont(weight="bold")).pack(side='left', padx=5)
        self.port_option = ctk.CTkOptionMenu(port_frame, values=['Nenhuma'], width=200)
        self.port_option.pack(side='left', padx=5)
        
        self.refresh_ports_btn = ctk.CTkButton(port_frame, text="🔄 Atualizar Portas", width=120,
                                             command=self.update_serial_ports)
        self.refresh_ports_btn.pack(side='left', padx=5)
        
        # Linha 2 - Baud Rate
        baud_frame = ctk.CTkFrame(controls_frame, fg_color="transparent")
        baud_frame.pack(fill='x', pady=5)
        
        ctk.CTkLabel(baud_frame, text="Baud Rate:", font=ctk.CTkFont(weight="bold")).pack(side='left', padx=5)
        baud_rates = ['9600', '19200', '38400', '57600', '115200']
        self.baud_option = ctk.CTkOptionMenu(baud_frame, values=baud_rates, width=200,
                                           command=self._on_baud_change)
        self.baud_option.set(str(self.config.data.get('serial', {}).get('baud', DEFAULT_SERIAL_BAUD)))
        self.baud_option.pack(side='left', padx=5)
        
        # Connection buttons
        btn_frame = ctk.CTkFrame(baud_frame, fg_color="transparent")
        btn_frame.pack(side='left', padx=20)
        
        self.connect_btn = ctk.CTkButton(btn_frame, text="🔗 Conectar", width=120,
                                       command=self._connect_serial,
                                       fg_color=COLORS["success"])
        self.connect_btn.pack(side='left', padx=5)
        
        self.disconnect_btn = ctk.CTkButton(btn_frame, text="🔓 Desconectar", width=120,
                                          command=self._disconnect_serial, 
                                          state='disabled',
                                          fg_color=COLORS["danger"])
        self.disconnect_btn.pack(side='left', padx=5)

    def _build_settings_tab(self, parent):
        main_frame = ctk.CTkFrame(parent, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        # 🎨 APARÊNCIA AVANÇADA
        appearance_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        appearance_frame.pack(fill='x', padx=15, pady=15)
        
        ctk.CTkLabel(appearance_frame, text="🎨 Personalização da Interface", 
                    font=ctk.CTkFont(size=16, weight="bold")).pack(anchor='w', pady=(10, 15))
        
        # ✅ TEMA
        theme_frame = ctk.CTkFrame(appearance_frame, fg_color="transparent")
        theme_frame.pack(fill='x', padx=10, pady=8)
        ctk.CTkLabel(theme_frame, text="Tema de Cores:", font=ctk.CTkFont(weight="bold")).pack(side='left')
        theme_values = ['System', 'Claro', 'Escuro']
        self.theme_menu = ctk.CTkOptionMenu(theme_frame, values=theme_values, 
                                        command=self._on_theme_change, width=150)
        self.theme_menu.pack(side='left', padx=10)

        # Configurar tema atual
        current_theme = self.config.data.get('appearance', {}).get('theme', 'System')
        theme_display = {'System': 'System', 'Light': 'Claro', 'Dark': 'Escuro'}
        self.theme_menu.set(theme_display.get(current_theme, 'System'))
        
        # Configurar tema atual
        current_theme = self.config.data.get('appearance', {}).get('theme', 'System')
        theme_display = {'System': 'System', 'Light': 'Claro', 'Dark': 'Escuro'}
        self.theme_menu.set(theme_display.get(current_theme, 'System'))
        
        # ✅ TRANSPARÊNCIA DA JANELA
        transparency_frame = ctk.CTkFrame(appearance_frame, fg_color="transparent")
        transparency_frame.pack(fill='x', padx=10, pady=8)
        ctk.CTkLabel(transparency_frame, text="Transparência:", font=ctk.CTkFont(weight="bold")).pack(side='left')
        
        self.transparency_var = tk.DoubleVar(value=self.config.data.get('appearance', {}).get('transparency', 1.0))
        transparency_slider = ctk.CTkSlider(
            transparency_frame, 
            from_=0.5, 
            to=1.0, 
            number_of_steps=10,
            variable=self.transparency_var,
            command=self._on_transparency_change,
            width=150
        )
        transparency_slider.pack(side='left', padx=10)
        
        self.transparency_label = ctk.CTkLabel(transparency_frame, text=f"{int(self.transparency_var.get() * 100)}%")
        self.transparency_label.pack(side='left', padx=5)
        
        # ✅ CORES DOS BOTÕES
        colors_frame = ctk.CTkFrame(appearance_frame, fg_color="transparent")
        colors_frame.pack(fill='x', padx=10, pady=8)
        ctk.CTkLabel(colors_frame, text="Esquema de Cores:", font=ctk.CTkFont(weight="bold")).pack(side='left')
        
        color_schemes = ['Padrão', 'Moderno', 'Vibrante', 'Suave', 'Escuro Total']
        self.color_scheme_menu = ctk.CTkOptionMenu(
            colors_frame, 
            values=color_schemes, 
            command=self._on_color_scheme_change, 
            width=150
        )
        self.color_scheme_menu.pack(side='left', padx=10)
        self.color_scheme_menu.set(self.config.data.get('appearance', {}).get('color_scheme', 'Padrão'))
        
        # ✅ TAMANHO DA FONTE
        font_frame = ctk.CTkFrame(appearance_frame, fg_color="transparent")
        font_frame.pack(fill='x', padx=10, pady=8)
        ctk.CTkLabel(font_frame, text="Tamanho da Fonte:", font=ctk.CTkFont(weight="bold")).pack(side='left')
        
        font_sizes = ['Pequeno', 'Médio', 'Grande']
        self.font_size_menu = ctk.CTkOptionMenu(
            font_frame, 
            values=font_sizes, 
            command=self._on_font_size_change, 
            width=150
        )
        self.font_size_menu.pack(side='left', padx=10)
        self.font_size_menu.set(self.config.data.get('appearance', {}).get('font_size', 'Médio'))
        
        # ✅ BOTÃO DE RESET
        reset_frame = ctk.CTkFrame(appearance_frame, fg_color="transparent")
        reset_frame.pack(fill='x', padx=10, pady=15)
        ctk.CTkButton(
            reset_frame, 
            text="🔄 Restaurar Padrões", 
            command=self._reset_appearance,
            fg_color=COLORS["warning"],
            hover_color="#E0A800",
            width=180
        ).pack(side='left')
        
        # ✅ PREVIEW AO VIVO
        preview_frame = ctk.CTkFrame(appearance_frame, corner_radius=6)
        preview_frame.pack(fill='x', padx=10, pady=10)
        ctk.CTkLabel(preview_frame, text="👀 Preview - As alterações são aplicadas instantaneamente", 
                    font=ctk.CTkFont(size=12, weight="bold")).pack(pady=8)

    def _build_update_tab(self, parent):
        main_frame = ctk.CTkFrame(parent, corner_radius=10)
        main_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        # Current version info
        version_frame = ctk.CTkFrame(main_frame, corner_radius=8)
        version_frame.pack(fill='x', padx=15, pady=15)
        
        ctk.CTkLabel(version_frame, text="🔄 Atualizações", font=ctk.CTkFont(size=16, weight="bold")).pack(anchor='w', pady=(10, 15))
        
        self.current_version_label = ctk.CTkLabel(version_frame, text=f"Versão atual: {APP_VERSION}", 
                                                font=ctk.CTkFont(size=14))
        self.current_version_label.pack(anchor='w', pady=5)
        
        self.latest_version_label = ctk.CTkLabel(version_frame, text="Última versão: Verificando...", 
                                               font=ctk.CTkFont(size=14))
        self.latest_version_label.pack(anchor='w', pady=5)
        
        # Update button
        self.check_update_btn = ctk.CTkButton(version_frame, text="🔍 Verificar Atualizações", 
                                            command=self._check_update_thread,
                                            fg_color=COLORS["primary"])
        self.check_update_btn.pack(pady=15)

    # -----------------------------
    # UI Actions
    # -----------------------------
    def refresh_all(self):
        """Refresh all UI elements"""
        self.refresh_all_buttons()
        self.update_serial_ports()
        self.logger.info("Interface atualizada")
        
    def refresh_all_buttons(self):
        for key, widget_map in self.button_frames.items():
            btn_conf = self.config.data.get('buttons', {}).get(key, {})
            label = btn_conf.get('label', f'Botão {key}')
            icon_path = btn_conf.get('icon', '')
            
            # ✅ ATUALIZAR: Atualizar o título do label
            widget_map['title_label'].configure(text=label)
            
            # Carregar ícone
            ctk_img = self.icon_loader.load_icon_from_path(icon_path) if icon_path else None
            if not ctk_img and icon_path and icon_path.lower().endswith('.exe'):
                ctk_img = self.icon_loader.try_load_windows_exe_icon(icon_path)
            
            # Atualizar UI
            if ctk_img:
                widget_map['icon_label'].configure(image=ctk_img, text='')
            else:
                widget_map['icon_label'].configure(image='', text='📱')

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
        ports = self.serial_manager.list_ports()
        if not ports:
            ports = ['Nenhuma']
        self.port_option.configure(values=ports)
        try:
            current = self.config.data.get('serial', {}).get('port', '')
            self.port_option.set(current if current in ports else ports[0])
        except Exception:
            self.port_option.set(ports[0])

    def _connect_serial(self):
        port = self.port_option.get()
        if port == 'Nenhuma' or not port:
            self.logger.warn('Nenhuma porta selecionada')
            return
        baud = int(self.baud_option.get())
        ok = self.serial_manager.connect(port, baud)
        if ok:
            self.connect_btn.configure(state='disabled')
            self.disconnect_btn.configure(state='normal')
            self.status_label.configure(text="🟢 Conectado", text_color=COLORS["success"])
            self.config.data['serial']['port'] = port
            self.config.data['serial']['baud'] = baud
            self.config.save()

    def _disconnect_serial(self):
        self.serial_manager.disconnect()
        self.connect_btn.configure(state='normal')
        self.disconnect_btn.configure(state='disabled')
        self.status_label.configure(text="🔴 Desconectado", text_color=COLORS["danger"])

    def _clear_log(self):
        try:
            self.log_textbox.configure(state='normal')
            self.log_textbox.delete('1.0', 'end')
            self.log_textbox.configure(state='disabled')
        except Exception:
            pass

    def _on_theme_change(self, value):
        """Altera o tema da aplicação"""
        try:
            # Mapear valores em português para inglês
            theme_map = {
                'System': 'System',
                'Claro': 'Light', 
                'Escuro': 'Dark',
                'Azul': 'Dark',
                'Verde': 'Dark',
                'Roxo': 'Dark'
            }
            
            english_theme = theme_map.get(value, 'System')
            ctk.set_appearance_mode(english_theme)
            
            if 'appearance' not in self.config.data:
                self.config.data['appearance'] = {}
            self.config.data['appearance']['theme'] = english_theme
            self.config.save()
            
            self.logger.info(f"Tema alterado para: {value}")
        except Exception as e:
            self.logger.error(f"Erro ao alterar tema: {e}")

    '''def _on_icon_size_change(self, value):
        try:
            v = int(value)
            if 'appearance' not in self.config.data:
                self.config.data['appearance'] = {}
            self.config.data['appearance']['icon_size'] = v
            self.icon_loader.icon_size = (v, v)
            self.config.save()
            self.refresh_all_buttons()
        except Exception:
            pass'''
            
    def _on_baud_change(self, value):
        try:
            baud = int(value)
            self.config.data['serial']['baud'] = baud
            self.config.save()
        except Exception:
            pass

    def _backup_config(self):
        try:
            path = self.config.backup()
            messagebox.showinfo('Backup', f'Backup salvo em: {path}')
            self.logger.info(f'Backup criado: {path}')
        except Exception as e:
            messagebox.showerror('Erro', str(e))

    def _restore_config(self):
        try:
            path = self.config.restore()
            messagebox.showinfo('Restaurado', f'Config restaurada de: {path}')
            self.refresh_all_buttons()
            self.logger.info(f'Configuração restaurada: {path}')
        except Exception as e:
            messagebox.showerror('Erro', str(e))
            
    def _save_all(self):
        if self.config.save():
            self.logger.info("Configurações salvas com sucesso")
            messagebox.showinfo("Sucesso", "Configurações salvas com sucesso!")
        else:
            self.logger.error("Erro ao salvar configurações")
            messagebox.showerror("Erro", "Não foi possível salvar as configurações")

    def _show_about(self):
        AboutDialog(self)

    def _check_update_thread(self):
        self.check_update_btn.configure(state='disabled', text="Verificando...")
        t = threading.Thread(target=self._check_update, daemon=True)
        t.start()

    def _check_update(self):
        """Check for updates"""
        self.logger.info('Verificando atualizações...')
        res = self.update_checker.check_update()
        
        # Update UI in main thread
        self.after(0, lambda: self.check_update_btn.configure(
            state='normal', 
            text="🔍 Verificar Atualizações"
        ))
        
        if not res.get('ok'):
            error_msg = res.get("error", "Erro desconhecido")
            self.logger.error(f'Erro ao checar atualização: {error_msg}')
            
            # ✅ CORREÇÃO: Quebrar linha se o texto for muito grande
            display_error = error_msg
            if len(error_msg) > 50:
                # Encontrar um ponto natural para quebrar a linha
                break_point = error_msg.find(' ', 40)  # Tenta quebrar após 40 caracteres
                if break_point != -1:
                    display_error = error_msg[:break_point] + '\n' + error_msg[break_point+1:]
            
            self.after(0, lambda: self.latest_version_label.configure(
                text=f'Última versão: erro\n{display_error}'
            ))
            return
            
        latest = res.get('latest')
        self.after(0, lambda: self.latest_version_label.configure(
            text=f'Última versão: {latest}'
        ))
        
        if res.get('is_new'):
            self.logger.info(f'Nova versão disponível: {latest}. Download: {res.get("download_url")}')
            if messagebox.askyesno(
                'Atualização disponível', 
                f'Versão {latest} disponível. Deseja abrir a página de download?'
            ):
                webbrowser.open(res.get('download_url'))
        else:
            self.logger.info('Você já está na versão mais recente')
            messagebox.showinfo('Atualização', 'Você já está na versão mais recente!')

    # -----------------------------
    # Serial events
    # -----------------------------
    def _on_serial_message(self, text: str):
        self.logger.info(f'<- ESP: {text}')
        if text.startswith('BTN:'):
            key = text.split(':')[1]
            btn_conf = self.config.data['buttons'].get(key)
            if btn_conf:
                action = Action(btn_conf.get('action', {}).get('type', 'none'), btn_conf.get('action', {}).get('payload', ''))
                self.action_manager.perform(action)
            else:
                self.logger.warn(f'Botão {key} desconhecido')

    # -----------------------------
    # Closing
    # -----------------------------
    def on_closing(self):
        try:
            self.serial_manager.disconnect()
            self.config.save()
        except Exception:
            pass
        self.destroy()

# -----------------------------
# Main
# -----------------------------
def main():
    ctk.set_appearance_mode('System')
    app = Esp32DeckApp()
    app.protocol('WM_DELETE_WINDOW', app.on_closing)
    app.mainloop()

if __name__ == '__main__':
    main()