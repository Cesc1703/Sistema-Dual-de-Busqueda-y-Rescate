#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#define SCK_BASE     5  

#define MISO_BASE    19  
#define MOSI_BASE    27  
#define SS_BASE      18  
#define RST_BASE     14  
#define DIO0_BASE    26  
#define BAND         433E6 

IPAddress apIP(192, 168, 1, 1);
const byte DNS_PORT = 53;
DNSServer dnsServer;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String mensajePendienteBase = "";
bool hayMensajeParaEnviar = false;

const char base_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <title>Central Táctica</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: Consolas, monospace; background: #ffffff; color: #333333; margin: 0; padding: 12px; display: flex; flex-direction: column; height: 100vh; }
    h2 { margin: 0 0 10px 0; color: #0055cc; text-align: center; font-size: 20px;}
    .status-bar { background: #e0e0e0; color: #27ae60; padding: 5px; text-align: center; font-weight: bold; margin-bottom: 10px; border: 1px solid #cccccc; border-radius: 4px; }
    .status-offline { color: #c0392b; }
    
    .grid { display: grid; grid-template-columns: 35% 65%; gap: 12px; flex: 1; min-height: 0; }
    .panel { background: #f9f9f9; border-radius: 8px; padding: 12px; border: 1px solid #cccccc; display: flex; flex-direction: column; }
    
    .scroll-container { display: flex; flex-direction: column; gap: 8px; flex: 1; min-height: 0; margin-top: 10px; }
    .scroll-box { background: #ffffff; overflow-y: auto; padding: 10px; border: 1px solid #cccccc; flex: 1; border-radius: 4px; }
    
    .msg { margin: 4px 0; padding: 6px 10px; border-radius: 5px; font-size: 13px; color: #333; }
    .msg-victima { background: #ffe6e6; border-left: 4px solid #ff4d4d; }
    .msg-base { background: #e6f7ff; text-align: right; border-right: 4px solid #00bcd4; }
    .msg-sys { background: #fff5e6; color: #d35400; font-weight: bold; border-left: 3px solid #f39c12; }
    .msg-math { background: #eafaf1; color: #27ae60; font-weight: bold; border-left: 3px solid #27ae60; }
    .msg-ok { background: #eafaf1; color: #27ae60; font-weight: bold; border-left: 3px solid #27ae60; }
    
    .stats-box { background: #ffffff; padding: 8px; margin-top: 5px; border-radius: 4px; border: 1px solid #cccccc; text-align: center; font-size: 14px; color: #333;}
    .stats-num { color: #0055cc; font-size: 18px; font-weight: bold; }
    
    .input-group { display: flex; gap: 5px; margin-bottom: 5px; }
    input[type="text"] { flex: 1; padding: 10px; background: #ffffff; color: #333; border: 1px solid #cccccc; border-radius: 4px;}
    button { padding: 10px; color: white; border: none; cursor: pointer; font-weight: bold; border-radius: 4px; flex: 1; }
    .btn-send { background: #008cba; }
    .btn-start { background: #27ae60; }
    .btn-stop { background: #c0392b; }
    .btn-reset { background: #d35400; }
    
    canvas { background: #ffffff; border: 1px solid #999999; border-radius: 5px; width: 100%; height: 100%; image-rendering: pixelated;}
  </style>
</head>
<body>
  <h2>📡 CENTRAL TÁCTICA</h2>
  <div id="ws-status" class="status-bar status-offline">Conectando Servidor Central...</div>
  
  <div class="grid">
    <div class="panel">
      <strong>⚙️ Controles de Vuelo</strong>
      <div class="input-group">
        <button onclick="enviarComando('CMD_START')" class="btn-start">▶️ Iniciar Vuelo</button>
        <button onclick="enviarComando('CMD_STOP')" class="btn-stop">🛑 Detener</button>
        <button onclick="enviarComando('CMD_RESET_ORIGIN')" class="btn-reset">🔄 Reset</button>
      </div>
      <div class="stats-box">📱 Dispositivos Únicos: <span id="deviceCount" class="stats-num">0</span></div>
      <div class="scroll-container">
        <strong>📜 Panel de Eventos y Coordenadas</strong>
        <div id="events-box" class="scroll-box"></div>
        <strong>💬 Panel de Chat con Víctimas</strong>
        <div id="chat-box" class="scroll-box"></div>
        <div class="input-group" style="margin-top:5px;">
          <input type="text" id="inputMsg" placeholder="Escribir mensaje...">
          <button onclick="enviarMsg()" class="btn-send">Enviar</button>
        </div>
      </div>
    </div>
    <div class="panel">
      <strong>🎯 Radar Algorítmico (Escala: 1 Metro = 30 px)</strong>
      <canvas id="radarCanvas" width="800" height="500"></canvas>
    </div>
  </div>

  <script>
    var ws;
    var canvas = document.getElementById('radarCanvas');
    var ctx = canvas.getContext('2d');
    var scale = 30.0; 
    var route = [{x: 0, y: 0}];
    var mostrarVictimas = false; 
    var vueloActivo = false; 
    var coordenadasCalculadas = []; 
    var lecturasBLE = {}; 

    function logEvent(texto, tipo) {
      var box = document.getElementById('events-box');
      if(tipo === 'sys') box.innerHTML += `<div class="msg msg-sys">${texto}</div>`;
      if(tipo === 'math') box.innerHTML += `<div class="msg msg-math">${texto}</div>`;
      if(tipo === 'ok') box.innerHTML += `<div class="msg msg-ok">${texto}</div>`;
      box.scrollTop = box.scrollHeight;
    }
    
    function logChat(remitente, texto) {
      var box = document.getElementById('chat-box');
      if(remitente === 'Tú') box.innerHTML += `<div class="msg msg-base"><b>Tú:</b> ${texto}</div>`;
      else box.innerHTML += `<div class="msg msg-victima"><b>🚨 ${remitente}:</b> ${texto}</div>`;
      box.scrollTop = box.scrollHeight;
    }

    function drawRadar() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.save(); ctx.translate(canvas.width/2, canvas.height/2);

      // Ejes Cartesianos
      ctx.strokeStyle = '#d0d0d0'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(-400, 0); ctx.lineTo(400, 0); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(0, -250); ctx.lineTo(0, 250); ctx.stroke();

      // Ruta del dron en azul oscuro
      if (route.length > 0) {
        ctx.strokeStyle = '#0055cc'; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(route[0].x * scale, -route[0].y * scale);
        for(var i = 1; i < route.length; i++) ctx.lineTo(route[i].x * scale, -route[i].y * scale);
        ctx.stroke();
      }

      if (mostrarVictimas) {
        coordenadasCalculadas.forEach(obj => {
          var vx = obj.x * scale, vy = -obj.y * scale;

          obj.pts.forEach((d, index) => {
            // Circunferencias 
            ctx.strokeStyle = obj.color; 
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.arc(d.x * scale, -d.y * scale, d.r * scale, 0, 2*Math.PI); 
            ctx.stroke();

            // Líneas de intersección en gris oscuro
            ctx.strokeStyle = 'rgba(0, 0, 0, 0.3)'; 
            ctx.lineWidth = 1.5;
            ctx.beginPath(); 
            ctx.moveTo(d.x * scale, -d.y * scale); 
            ctx.lineTo(vx, vy); 
            ctx.stroke();

            // Rótulos P1, P2, P3 
            ctx.fillStyle = '#666666'; ctx.beginPath(); ctx.arc(d.x * scale, -d.y * scale, 4, 0, 2*Math.PI); ctx.fill();
            ctx.fillStyle = '#000000'; ctx.font = "bold 12px Consolas";
            ctx.fillText(`P${index + 1}`, d.x * scale - 8, -d.y * scale - 8);
          });

          // Marca Central ('X') y texto
          ctx.strokeStyle = '#000000'; ctx.lineWidth = 3;
          ctx.beginPath(); ctx.moveTo(vx - 8, vy - 8); ctx.lineTo(vx + 8, vy + 8); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(vx + 8, vy - 8); ctx.lineTo(vx - 8, vy + 8); ctx.stroke();
          
          ctx.fillStyle = '#000000'; ctx.font = "bold 13px Consolas";
          ctx.fillText(`${obj.mac} (${obj.x.toFixed(2)}, ${obj.y.toFixed(2)})`, vx + 12, vy + 4);
        });
      }
      ctx.restore();
    }

    function iniciarConexion() {
      ws = new WebSocket(`ws://${window.location.hostname}/ws`);
      ws.onopen = function() {
        document.getElementById('ws-status').innerText = "✅ CONEXIÓN LOCAL ACTIVA";
        document.getElementById('ws-status').classList.remove("status-offline");
        drawRadar();
      };
      ws.onclose = function() {
        document.getElementById('ws-status').innerText = "⚠️ Reconectando...";
        document.getElementById('ws-status').classList.add("status-offline");
        setTimeout(iniciarConexion, 1500); 
      };

      ws.onmessage = function(event) {
        var partes = event.data.split("|");
        
        if (partes[0] === "MSG") {
          logChat('Víctima', partes[2]);
          
        } else if (partes[0] === "POS") {
          if (vueloActivo && !mostrarVictimas) {
            route.push({x: parseFloat(partes[1]), y: parseFloat(partes[2])}); 
            drawRadar();
          }
          
        } else if (partes[0] === "VDET") {
          if (vueloActivo) {
            var dx = parseFloat(partes[1]);
            var dy = parseFloat(partes[2]);
            var dist = parseFloat(partes[3]);
            var mac = partes[4];
            
            if(!lecturasBLE[mac]) lecturasBLE[mac] = [];
            lecturasBLE[mac].push({x: dx, y: dy, r: dist});
          
          }
          
        } else if (partes[0] === "SYS") {
          logEvent("✅ " + partes[1], "ok");
        }
      };
    }

    function enviarMsg() {
      var input = document.getElementById('inputMsg');
      if (input.value.trim() !== "" && ws.readyState === 1) {
        ws.send("BASE_MSG|" + input.value);
        logChat('Tú', input.value);
        input.value = "";
      }
    }

    function enviarComando(cmd) {
      if (ws.readyState === 1) {
        ws.send("BASE_MSG|" + cmd);
        
        if (cmd === 'CMD_START') {
          mostrarVictimas = false; 
          vueloActivo = true; 
          coordenadasCalculadas = []; 
          lecturasBLE = {}; 
          document.getElementById('deviceCount').innerText = "0";
          
          // Mensaje de inicio
          logEvent("⏳ Vuelo Iniciado. Calibrando origen (0,0), por favor espere...", "sys");
          
        } else if (cmd === 'CMD_STOP') {
          mostrarVictimas = true; 
          vueloActivo = false; 
          
          // Mensaje de detención
          logEvent("🛑 Vuelo Detenido. Calculando posición de múltiples objetivos...", "sys");
          
          coordenadasCalculadas = [];
          var numVictimas = 0;
          var coloresDisponibles = ['rgba(231, 76, 60, 0.7)', 'rgba(52, 152, 219, 0.7)', 'rgba(46, 204, 113, 0.7)'];

          for (var mac in lecturasBLE) {
            var pts = lecturasBLE[mac];
            
            if (pts.length >= 3) {
              numVictimas++;
              var aliasVictima = "VÍCTIMA " + numVictimas;
              
              // TRILATERACIÓN (Grid Search)
              var minX = pts[0].x, maxX = pts[0].x;
              var minY = pts[0].y, maxY = pts[0].y;
              var maxR = 0;
              for(var i=0; i<pts.length; i++) {
                if(pts[i].x < minX) minX = pts[i].x;
                if(pts[i].x > maxX) maxX = pts[i].x;
                if(pts[i].y < minY) minY = pts[i].y;
                if(pts[i].y > maxY) maxY = pts[i].y;
                if(pts[i].r > maxR) maxR = pts[i].r;
              }
              minX -= maxR; maxX += maxR; minY -= maxR; maxY += maxR;
              
              var bestX = 0, bestY = 0, minError = Infinity;
              for(var tx = minX; tx <= maxX; tx += 0.2) { 
                for(var ty = minY; ty <= maxY; ty += 0.2) {
                  var err = 0;
                  for(var i=0; i<pts.length; i++) {
                    var calcDist = Math.hypot(tx - pts[i].x, ty - pts[i].y);
                    err += Math.pow(calcDist - pts[i].r, 2); 
                  }
                  if(err < minError) {
                    minError = err; bestX = tx; bestY = ty;
                  }
                }
              }

              // SELECCIONAMOS 3 PUNTOS REPRESENTATIVOS PARA LA MATRIZ VISUAL
              var p1 = pts[0];
              var p2 = pts[Math.floor(pts.length / 2)];
              var p3 = pts[pts.length - 1];
              var tripletVisual = [p1, p2, p3];

              // Matriz de datos
              var logPts = `📐 Matriz de Datos [${aliasVictima}]:<br>`;
              logPts += `-> P1: X=${p1.x.toFixed(2)}m | Y=${p1.y.toFixed(2)}m | r=${p1.r.toFixed(2)}m<br>`;
              logPts += `-> P2: X=${p2.x.toFixed(2)}m | Y=${p2.y.toFixed(2)}m | r=${p2.r.toFixed(2)}m<br>`;
              logPts += `-> P3: X=${p3.x.toFixed(2)}m | Y=${p3.y.toFixed(2)}m | r=${p3.r.toFixed(2)}m`;
              logEvent(logPts, "sys");

              // Resultado de localización
              logEvent(`📍 ${aliasVictima} LOCALIZADA -> X: ${bestX.toFixed(2)}m | Y: ${bestY.toFixed(2)}m`, "math");

              var color = coloresDisponibles[(numVictimas - 1) % coloresDisponibles.length];
              
              coordenadasCalculadas.push({x: bestX, y: bestY, mac: aliasVictima, pts: tripletVisual, color: color});
            }
          }
          
          document.getElementById('deviceCount').innerText = numVictimas.toString();
          
        } else if (cmd === 'CMD_RESET_ORIGIN') {
          route = [{x: 0, y: 0}]; 
          mostrarVictimas = false; 
          vueloActivo = false; 
          coordenadasCalculadas = [];
          lecturasBLE = {}; 
          document.getElementById('deviceCount').innerText = "0";
          document.getElementById('events-box').innerHTML = ""; 
          
          // Mensaje de reseteo
          logEvent("🔄 Memoria borrada. Panel y origen reseteados.", "sys");
        }
        drawRadar(); 
      }
    }
    window.addEventListener('load', iniciarConexion);
  </script>
</body>
</html>
)rawliteral";

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len) {
      data[len] = 0; String msg = String((char*)data);
      if (msg.startsWith("BASE_MSG|")) { mensajePendienteBase = msg.substring(9); hayMensajeParaEnviar = true; }
    }
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin(SCK_BASE, MISO_BASE, MOSI_BASE, SS_BASE);
  LoRa.setPins(SS_BASE, RST_BASE, DIO0_BASE);
  if (!LoRa.begin(BAND)) while(1);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("ESTACION_BASE_CONTROL", "12345678");
  dnsServer.start(DNS_PORT, "*", apIP);
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/central", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", base_html); });
  server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("/central"); });
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  if (hayMensajeParaEnviar) { LoRa.beginPacket(); LoRa.print("BASE|" + mensajePendienteBase); LoRa.endPacket(); hayMensajeParaEnviar = false; }
  int packetSize = LoRa.parsePacket();
  if (packetSize) { String p = ""; while (LoRa.available()) p += (char)LoRa.read(); ws.textAll(p); }
  ws.cleanupClients();
  delay(15);
}