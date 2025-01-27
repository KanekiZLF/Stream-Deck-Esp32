import serial
import psutil
import subprocess
import pygetwindow as gw
import win32gui
import win32process
import time

programName = ""  # Inicializa a variável programName
windowName = ""  # Nome da janela
buttonCount = 0  # Número de vezes que botão foi pressionado
targetProcess = programName  # Programa que foi indicado

# Configurar a porta serial do Arduino (ajuste conforme necessário)
arduino_port = "COM3"  # Verifique a porta correta no Gerenciador de Dispositivos
baud_rate = 115200

def programIsRunning(programName):
    for proc in psutil.process_iter(['pid', 'name']):
        try:
            if programName.lower() in proc.info['name'].lower():
                return True
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            pass
    return False

def monitorProgram(programName):
    if programIsRunning(programName):
        if programName == "":
            return False
        else:
            print(f"{programName} está aberto.")
            return True
    else:
        print(f"{programName} não está aberto.")
        return False

def getWindowName(targetProcess):
    def enum_windows_callback(hwnd, windows):
        _, pid = win32process.GetWindowThreadProcessId(hwnd)
        try:
            process = psutil.Process(pid)
            executable_name = process.name()
            if targetProcess.lower() == executable_name.lower():
                window_title = win32gui.GetWindowText(hwnd)
                if window_title.strip():
                    windows.append((hwnd, window_title))
        except psutil.NoSuchProcess:
            pass
        return True

    windows = []
    win32gui.EnumWindows(enum_windows_callback, windows)
    return windows

try:
    esp = serial.Serial(arduino_port, baud_rate, timeout=1)
except Exception as e:
    print(f"Erro ao conectar ao ESP: {e}")
    print("Verifique se nenhum outro programa está usando o ESP!")
    exit()

print("Aguardando comandos do Arduino...")

try:
    while True:
        command = esp.readline().decode('utf-8').strip()
        if command:
            print(f"Comando recebido: {command}")
            match command:
                case "Program1":
                    programName = "notepad.exe"
                    targetProcess = programName
                    if not monitorProgram(programName):
                        subprocess.Popen([programName])
                    else:
                        result = getWindowName(targetProcess)
                        if result:
                            hwnd, title = result[0]
                            print(f"Janela encontrada: {title}")
                            window = gw.getWindowsWithTitle(title)[0]
                            if buttonCount == 0:
                                window.minimize()
                                buttonCount = 1
                            elif buttonCount == 1:
                                window.maximize()
                                buttonCount = 0
                        else:
                            print("Nenhuma janela encontrada.")
                    print("Abrindo bloco de notas...")
                case "Program2":
                    programName = "calc.exe"
                    targetProcess = programName
                    if not monitorProgram(programName):
                        subprocess.Popen([programName])
                    print("Abrindo Calculadora...")
                case _:
                    print(f"Comando desconhecido: {command}")
        monitorProgram(programName)

except KeyboardInterrupt:
    print("\nEncerrando...")
finally:
    esp.close()
