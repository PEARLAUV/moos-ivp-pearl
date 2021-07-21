import serial
import time
from pymodbus.client.sync import ModbusSerialClient as ModbusClient

# SCC reading initialization
client = ModbusClient(method = 'rtu', port = '/dev/ttyXRUSB0', baudrate = 115200)

# Autonomy RPi serial port initialization
moos_serial = serial.Serial(port='/dev/ttyS0', baudrate=115200)

# Set string IDs
scc_ID = "PLSCC,"
veml_ID = "PLLUX,"
wind_ID = "PLWND,"

# Get SCC data
client.connect()
try:
    result = client.read_input_registers(0x3100,8,unit=1)
    pvVoltage = float(result.registers[0] / 100.0)
    pvCurrent = float(result.registers[1] / 100.0)
    pvPowerL = float(result.registers[2] / 100.0)
    pvPowerH = float(result.registers[3] / 100.0)
    batteryVoltage = float(result.registers[4] / 100.0)
    batteryCurrent = float(result.registers[5] / 100.0)
    batteryPowerL = float(result.registers[6] / 100.0)
    batteryPowerH = float(result.registers[7] / 100.0)
except:
    pvVoltage = "No SCC"
    pvCurrent = "No SCC"
    pvPowerL = "No SCC"
    pvPowerH = "No SCC"
    batteryVoltage = "No SCC"
    batteryCurrent = "No SCC"
    batteryPowerL = "No SCC"
    batteryPowerH = "No SCC"
try:
    result = client.read_input_registers(0x310C,6,unit=1)
    loadVoltage = float(result.registers[0] / 100.0)
    loadCurrent = float(result.registers[1] / 100.0)
    loadPowerL = float(result.registers[2] / 100.0)
    loadPowerH = float(result.registers[3] / 100.0)
    batteryTemp = float(result.registers[4] / 100.0)
    deviceTemp = float(result.registers[5] / 100.0)
except:
    loadVoltage = "No SCC"
    loadCurrent = "No SCC"
    loadPowerL = "No SCC"
    loadPowerH = "No SCC"
    batteryTemp = "No SCC"
    deviceTemp = "No SCC"
try:
    result = client.read_input_registers(0x311A,1,unit=1)
    batterySOC = float(result.registers[0] / 100.0)
    #print("Battery SOC: ",batterySOC);
except:
    batterySOC = "No SCC"

try:
    result = client.read_input_registers(0x331B,1,unit=1)
    batterynetCurrent = float(result.registers[0] / 100.0)
    #print("Battery Net Current (A): ",batterynetCurrent);
except:
    batterynetCurrent = "No SCC"
    
client.close()
scc_datastring = "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}".format(
    pvVoltage,
    pvCurrent,
    pvPowerL,
    pvPowerH,
    batteryVoltage,
    batteryCurrent,
    batterynetCurrent,
    batteryPowerL,
    batteryPowerH,
    loadVoltage,
    loadCurrent,
    loadPowerL,
    loadPowerH,
    batterySOC,
    batteryTemp,
    deviceTemp)

veml_datastring = "No VEML7700,No VEML7700,No VEML7700"
wind_datastring = "No Wind,No Wind"

if scc_datastring[0:2]=="No":
    scc_datastring = ",,,,,,,,,,,,,,,"
    
if veml_datastring[0:2]=="No":
    veml_datastring = ",,"
    
if wind_datastring[0:2]=="No":
    wind_datastring = ","

# Format data strings
scc_moos_string = "$" + scc_ID + scc_datastring + "*"
veml_moos_string = "$" + veml_ID + veml_datastring + "*"
wind_moos_string = "$" + wind_ID + wind_datastring + "*"

while True:
#     print(scc_moos_string)
#     print(veml_moos_string)
#     print(wind_moos_string)
    moos_serial.write(scc_moos_string.encode())
    moos_serial.write(veml_moos_string.encode())
    moos_serial.write(wind_moos_string.encode())
    time.sleep(1)
    