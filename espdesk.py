import serial
import os

# Configurar a porta serial do Arduino (ajuste conforme necessário)
arduino_port = "COM3"  # Verifique a porta correta no Gerenciador de Dispositivos
baud_rate = 115200

# Conectar ao Arduino
try:
    esp = serial.Serial(arduino_port, baud_rate, timeout=1)  # Adicionado timeout de 1 segundo
except Exception as e:
    print(f"Erro ao conectar ao ESP: {e}")
    print("Verifique se nenhum outro programa está usando o ESP!")
    exit()

print("Aguardando comandos do Arduino...")

try:
    while True:
        # Ler o comando do Arduino
        command = esp.readline().decode('utf-8').strip()  # Decodifica e remove espaços extras
        
        if command:
            print(f"Comando recebido: {command}")

            if command == "Navegador":
                print("Abrindo navegador...")
                #os.system("start chrome")  # Substitua 'chrome' por outro navegador, se necessário
            
            elif command == "open_notepad":
                print("Abrindo Bloco de Notas...")
                #os.system("start notepad")
            
            else:
                print(f"Comando desconhecido: {command}")
                
except KeyboardInterrupt:
    print("\nEncerrando...")
    esp.close()
