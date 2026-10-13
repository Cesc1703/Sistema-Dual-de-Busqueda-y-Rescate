#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <math.h>
#include <Wire.h>

#define SCK_DRON     18  
#define MISO_DRON    19  
#define MOSI_DRON    27  
#define SS_DRON      4   
#define RST_DRON     14  
#define DIO0_DRON    26  
#define BAND         433E6 

const int MPU_ADDR = 0x68;
float yaw = 0.0, posX = 0.0, posY = 0.0;
float gyroZ_offset = 0.0;
unsigned long lastMPUTime = 0;
unsigned long lastTelemetryTime = 0;
const float V_CONST = 1.0; 
bool enMovimiento = false; 

#define SERVICE_UUID_VICTIMA "12345678-1234-1234-1234-123456789abc"
const float A_MEDIDO = -72.0; 
const float N_ENTORNO = 2.4;

volatile bool victimaDetectada = false;
volatile float ultimaDistanciaBLE = 0.0;
String ultimaMacBLE = ""; 
bool wifiCreado = false;
unsigned long tiempoUltimoEnvioVictima = 0; 
String mensajePendienteWS = "";
bool hayMensajeParaEnviar = false;

BLEScan* pBLEScan;
TaskHandle_t TaskBLEHandle = NULL;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Portal de Emergencia</title>
  <style>
    body { font-family: Arial, sans-serif; background: #f8f9fa; color: #333333; margin: 0; padding: 15px; text-align: center; }
    h2 { color: #d32f2f; margin-bottom: 20px; }
    .btn { width: 100%; padding: 15px; margin: 8px 0; border: none; border-radius: 8px; font-weight: bold; cursor: pointer; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    .btn-sos { background: #d32f2f; color: white; font-size: 20px; }
    .btn-gps { background: #f57c00; color: white; font-size: 16px; }
    .btn-send { background: #388e3c; color: white; width: 25%; }
    #chat-box { background: #ffffff; height: 160px; border-radius: 8px; padding: 10px; overflow-y: auto; text-align: left; border: 1px solid #cccccc; margin-bottom: 10px; }
    .msg { margin: 5px 0; padding: 8px; border-radius: 5px; font-size: 14px; }
    .msg-victim { background: #e3f2fd; color: #0d47a1; text-align: right; border-right: 3px solid #1976d2; }
    .msg-base { background: #f5f5f5; color: #424242; text-align: left; border-left: 3px solid #00acc1; }
    .input-group { display: flex; gap: 5px; }
    input[type="text"] { width: 75%; padding: 10px; border-radius: 5px; border: 1px solid #cccccc; background: #ffffff; color: #333333; }
  </style>
</head>
<body>
  <h2>🚨 SISTEMA DE RESCATE</h2>
  <button class="btn btn-sos" onclick="enviarSOS()">🆘 BOTÓN DE PÁNICO (SOS)</button>
  <button class="btn btn-gps" onclick="compartirGPS()">📍 COMPARTIR UBICACIÓN GPS</button>
  
  <div id="chat-box" style="margin-top: 15px;"></div>
  <div class="input-group">
    <input type="text" id="chatInput" placeholder="Escriba su situación...">
    <button class="btn btn-send" onclick="enviarMensaje()">Enviar</button>
  </div>
  <script>
    var ws = new WebSocket(`ws://${window.location.hostname}/ws`);
    ws.onmessage = function(e) { 
      document.getElementById('chat-box').innerHTML += `<div class="msg msg-base"><b>Base:</b> ${e.data}</div>`; 
      document.getElementById('chat-box').scrollTop = document.getElementById('chat-box').scrollHeight;
    }

    function enviarSOS() { 
      if(ws.readyState===1){ 
        ws.send("MSG|VICTIMA|SOS"); 
        document.getElementById('chat-box').innerHTML += `<div class="msg msg-victim"><b>TÚ:</b> SOS Enviado</div>`; 
        document.getElementById('chat-box').scrollTop = document.getElementById('chat-box').scrollHeight;
      } 
    }
    
    function compartirGPS() {
      if (navigator.geolocation) {
        navigator.geolocation.getCurrentPosition(function(pos) {
          var lat = pos.coords.latitude.toFixed(6);
          var lon = pos.coords.longitude.toFixed(6);
          var txt = "Mi GPS es: " + lat + ", " + lon;
          if(ws.readyState===1){ 
            ws.send("MSG|VICTIMA|" + txt); 
            document.getElementById('chat-box').innerHTML += `<div class="msg msg-victim"><b>TÚ:</b> ${txt}</div>`;
            document.getElementById('chat-box').scrollTop = document.getElementById('chat-box').scrollHeight;
          }
        }, function(error) {
          alert("⚠️ GPS BLOQUEADO POR EL NAVEGADOR\n\nPor seguridad, Android bloquea la ubicación en esta red local.\n\nCÓMO DESBLOQUEARLO:\n1. Abre Google Chrome normal.\n2. Escribe arriba: chrome://flags\n3. Busca: insecure origin\n4. En el recuadro que aparece escribe: http://192.168.4.1\n5. Ponlo en 'Enabled' y presiona Relaunch (Reiniciar).\n\nVuelve a esta pantalla y el botón funcionará.");
        }, { enableHighAccuracy: true });
      } else {
        alert("Tu navegador no soporta GPS.");
      }
    }

    function enviarMensaje() { 
      var m = document.getElementById('chatInput'); 
      if(m.value && ws.readyState===1){ 
        ws.send("MSG|VICTIMA|"+m.value); 
        document.getElementById('chat-box').innerHTML += `<div class="msg msg-victim"><b>Tú:</b> ${m.value}</div>`; 
        document.getElementById('chat-box').scrollTop = document.getElementById('chat-box').scrollHeight;
        m.value=""; 
      } 
    }
  </script>
</body>
</html>
)rawliteral";

void calibrarMPU() {
  Wire.begin(); 
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0); Wire.endTransmission(true);
  long suma = 0;
  for(int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x47); Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 2, true);
    suma += (Wire.read() << 8 | Wire.read());
    delay(3);
  }
  gyroZ_offset = suma / 500.0;
  lastMPUTime = millis();
}

void actualizarOdometria() {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x47); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 2, true);
  int16_t raw_gz = Wire.read() << 8 | Wire.read();
  unsigned long current = millis();
  float dt = (current - lastMPUTime) / 1000.0;
  lastMPUTime = current;
  float gz_dps = (raw_gz - gyroZ_offset) / 131.0;
  
  if (abs(gz_dps) > 1.0) { yaw += gz_dps * dt; }

  if (enMovimiento) {
    float yaw_rad = yaw * PI / 180.0;
    posX += V_CONST * cos(yaw_rad) * dt;
    posY += V_CONST * sin(yaw_rad) * dt;
  }
}

struct KalmanFast {
  float q = 0.5, r = 3.0, x, p = 1.0; bool iniciado = false;
  float actualizar(float m) {
    if (!iniciado) { x = m; iniciado = true; }
    p = p + q; float k = p / (p + r);
    x = x + k * (m - x); 
    p = (1.0 - k) * p; 
    return x;
  }
} kalman;

float calcularDistancia(float rssi) { 
  return pow(10.0, (A_MEDIDO - rssi) / (10.0 * N_ENTORNO)); 
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len) {
      data[len] = 0; mensajePendienteWS = String((char*)data); hayMensajeParaEnviar = true; 
    }
  }
}

class CallbackBLE : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) {
      String macObj = String(device.getAddress().toString().c_str());
      bool esVictima = false;

      // Buscar UUID principal
      if (device.haveServiceUUID()) {
        String uuidDetectado = String(device.getServiceUUID().toString().c_str());
        if (uuidDetectado.equalsIgnoreCase(SERVICE_UUID_VICTIMA)) {
          esVictima = true;
        }
      }
      
      // Buscar UUID en Service Data (común en Android)
      if (!esVictima && device.haveServiceData()) {
         String uuidData = String(device.getServiceDataUUID().toString().c_str());
         if (uuidData.equalsIgnoreCase(SERVICE_UUID_VICTIMA)) {
            esVictima = true;
         }
      }

      if (esVictima) {
        float rssiF = kalman.actualizar((float)device.getRSSI()); 
        ultimaDistanciaBLE = calcularDistancia(rssiF);
        ultimaMacBLE = macObj; 
        victimaDetectada = true;
        
        Serial.print(">>> ¡OBJETIVO DETECTADO! MAC: ");
        Serial.print(macObj);
        Serial.print(" | RSSI: ");
        Serial.print(device.getRSSI());
        Serial.print(" | Dist. Aprox: ");
        Serial.println(ultimaDistanciaBLE);
      }
    }
};

void TaskBLE(void * pvParameters) {
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new CallbackBLE());
  pBLEScan->setActiveScan(true); 
  pBLEScan->setInterval(100); 
  pBLEScan->setWindow(99); 
  
  for (;;) { 
    pBLEScan->start(1, false); 
    pBLEScan->stop();          
    pBLEScan->clearResults();  
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

void arrancarPortalCautivo() {
  WiFi.mode(WIFI_AP); WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); WiFi.softAP("RESCATE_WIFI");
  dnsServer.start(DNS_PORT, "*", apIP);
  ws.onEvent(onEvent); server.addHandler(&ws);
  server.onNotFound([](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  calibrarMPU();
  SPI.begin(SCK_DRON, MISO_DRON, MOSI_DRON, SS_DRON);
  LoRa.setPins(SS_DRON, RST_DRON, DIO0_DRON);
  if (!LoRa.begin(BAND)) while(1);
  xTaskCreatePinnedToCore(TaskBLE, "TaskBLE", 10000, NULL, 1, &TaskBLEHandle, 0);
}

void loop() {
  actualizarOdometria();
  
  if (millis() - lastTelemetryTime > 1000) {
    LoRa.beginPacket(); LoRa.print("POS|" + String(posX, 2) + "|" + String(posY, 2)); LoRa.endPacket();
    lastTelemetryTime = millis();
  }
  
  if (hayMensajeParaEnviar) {
    LoRa.beginPacket(); LoRa.print(mensajePendienteWS); LoRa.endPacket();
    hayMensajeParaEnviar = false; 
  }
  
  if (victimaDetectada) {
    if (millis() - tiempoUltimoEnvioVictima > 2000) {
      LoRa.beginPacket();
      LoRa.print("VDET|" + String(posX, 2) + "|" + String(posY, 2) + "|" + String(ultimaDistanciaBLE, 2) + "|" + ultimaMacBLE);
      LoRa.endPacket();
      tiempoUltimoEnvioVictima = millis();
      victimaDetectada = false;
    }
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String p = ""; while (LoRa.available()) p += (char)LoRa.read();
    if (p.startsWith("BASE|")) {
      String payload = p.substring(5);
      if (payload == "CMD_START") {
        calibrarMPU();
        posX = 0.0; posY = 0.0; yaw = 0.0; enMovimiento = true; 
        LoRa.beginPacket(); LoRa.print("SYS|Punto (0,0) calibrado. ¡Ya puede iniciar el recorrido!"); LoRa.endPacket();
      } else if (payload == "CMD_STOP") {
        enMovimiento = false;
        if (!wifiCreado) { wifiCreado = true; arrancarPortalCautivo(); }
      } else if (payload == "CMD_RESET_ORIGIN") {
        posX = 0.0; posY = 0.0; yaw = 0.0; enMovimiento = false;
      } else {
        ws.textAll(payload);
      }
    }
  }
  
  if (wifiCreado) { dnsServer.processNextRequest(); ws.cleanupClients(); }
  delay(15);
}