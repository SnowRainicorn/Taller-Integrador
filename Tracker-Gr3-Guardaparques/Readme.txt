# Tracker APRS LoRa - T-Beam V1.2

Este proyecto implementa un dispositivo tracker APRS basado en una placa **LilyGO T-Beam V1.2**, utilizando **GPS**, **LoRa**, pantalla **OLED SSD1306**, botón físico y gestión de energía mediante **AXP2101**.

El sistema permite transmitir la ubicación del dispositivo en formato APRS a través de LoRa, mostrando información básica en pantalla y permitiendo cambiar entre perfiles de uso.

## Características principales

* Lectura de posición GPS en tiempo real.
* Transmisión de tramas APRS mediante LoRa.
* Visualización en pantalla OLED de:

  * Callsign.
  * Fecha y hora.
  * Latitud y longitud.
  * Altitud.
  * Velocidad.
  * Estado de batería.
  * Último callsign recibido.
* Dos perfiles de operación:

  * **Modo caminando**.
  * **Modo bote**.
* Envío de alerta **SOS** mediante pulsación larga.
* Detección de batería baja.
* Modo de ahorro de pantalla.
* Recepción básica de paquetes LoRa APRS.

## Hardware utilizado

* LilyGO T-Beam V1.2.
* Módulo GPS integrado.
* Módulo LoRa integrado.
* Pantalla OLED SSD1306 128x64.
* PMU AXP2101.
* Botón físico integrado.
* Batería Li-Ion/LiPo compatible.

## Librerías necesarias

Instalar las siguientes librerías en Arduino IDE o PlatformIO:

* `Arduino.h`
* `Wire.h`
* `SPI.h`
* `Adafruit_GFX`
* `Adafruit_SSD1306`
* `TinyGPS++`
* `LoRa`
* `OneButton`
* `XPowersLib`

## Funcionamiento general

Al iniciar, el dispositivo configura la pantalla OLED, el módulo GPS, el módulo LoRa y la unidad de gestión de energía.

Luego entra en una máquina de estados que realiza el siguiente flujo:

1. Leer datos del GPS.
2. Evaluar si debe enviarse un mensaje normal, SOS o batería baja.
3. Construir una trama APRS.
4. Transmitir la trama por LoRa.
5. Entrar en espera hasta la siguiente transmisión.

## Controles del botón

* **Click simple:** enciende o actualiza la pantalla.
* **Doble click:** cambia entre modo caminando y modo bote.
* **Pulsación larga:** activa una alerta SOS.

## Configuración APRS

Dentro del código se pueden modificar los siguientes valores:

```
const String CALLSIGN = "TI0TEC3-7";
const String APRS_COMMENT = "Grupo 3: G. Mejia, E. Valle.";
const String TOCALL = "APRS";
```

Estos valores definen el identificador del dispositivo, el comentario enviado en la trama y el destino APRS.

## Frecuencia LoRa

El dispositivo está configurado para transmitir en:

```
433775000 Hz
```

Antes de usar el dispositivo, verificar que la frecuencia sea permitida según la regulación local.

## Perfiles de transmisión

El intervalo de transmisión depende del perfil seleccionado:

* **Modo caminando:** cada 5 minutos.
* **Modo bote:** cada 2 minutos.

## Formato de trama

El dispositivo construye tramas APRS con información de posición, altitud, velocidad, tipo de símbolo y comentario.