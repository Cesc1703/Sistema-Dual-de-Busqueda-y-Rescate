# Sistema-Dual-de-Busqueda-y-Rescate
Sistema dual para búsqueda y rescate basado en RSSI y GPS sobre una red local con interfaz gráfica 

Este repositorio contiene el código fuente y las instrucciones de uso para el proyecto de búsqueda y rescate mediante un dron (UAV) equipado con ESP32. El sistema utiliza comunicación LoRa para la telemetría y Bluetooth Low Energy (BLE) para la trilateración de víctimas.

1. Conexiones de Hardware

Pines para el Módulo LoRa del Dron

SCK_DRON: 18
MISO_DRON: 19
MOSI_DRON: 27
SS_DRON: 4
RST_DRON: 14
DIO0_DRON: 26
BAND: 433E6

Pines MPU6050

SDA: 21
SCL: 22

Pines para Módulo LoRa de la Base

SCK_BASE: 5
MISO_BASE: 19
MOSI_BASE: 27
SS_BASE: 18
RST_BASE: 14
DIO0_BASE: 26
BAND: 433E6

2. Configuración de Nrf Connect para los móviles

Se le agregará un UUID a los móviles que queramos detectar para evitar que las MAC's aleatorias interfieran en la detección.
Una vez activado el bluetooth desde la aplicación, los móviles mantendrán una MAC única que los identificará.

3. Librerías Necesarias

Instalar las siguientes librerías en tu entorno de desarrollo:
ESP32 by Espressif Systems Version 3.3.12
LoRa by Sandeep Mistry 0.8.0
ESPAsyncWebServer by lacamera 3.1.0
AsyncTCP by dvarrel 1.1.4

4. Instrucciones de Operación

Acceso a la Estación Base:
Una vez se conecta el ESP32 de la base, seleccionas desde la laptop la red wifi "Estacion Base Control" y te redirecciona mediante el portal cautivo a la interfaz de la Base la clave es "12345678"

Vuelo y Trilateración

En primera instancia se debe mantener el dron quieto para que al seleccionar iniciar vuelo se calibre el punto de origen en (0,0).
Luego recién desplazar el dron haciendo recorridos que no sean en línea recta (en el trabajo se optó por hacer los recorridos en forma de una U pero también se puede realizar en zigzag).
Después de completar la trayectoria seleccionamos detener, en este paso se dibujarán las circunferencias de trilateración, ubicando las coordenadas de las víctimas con respecto al punto de partida.

Comunicación de Emergencia:
Cuando el recorrido finaliza también el ESP32 del dron despliega su red wifi a través de un portal cautivo donde permitirá a las víctimas intercambiar mensajes con la estación base.
