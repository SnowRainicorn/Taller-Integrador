/*
  Este programa lee la posicion GPS del dispositivo, calcula el estado de bateria,
  muestra informacion basica en una pantalla OLED y transmite tramas APRS mediante
  LoRa. Ademas permite cambiar el perfil de operacion con el boton fisico y enviar
  una alerta SOS mediante una pulsacion larga
  Componentes
  - ESP32 / LilyGO T-Beam V1.2
  - Modulo GPS integrado
  - Radio LoRa SX127x
  - Pantalla OLED SSD1306 por I2C
  - PMU AXP2101 para lectura y gestion de energia
  - Boton de usuario gestionado con OneButton
*/

#include <Arduino.h>             // Funciones base del framework Arduino
#include <Wire.h>                // Comunicacion I2C, usada por OLED y PMU
#include <SPI.h>                 // Comunicacion SPI, usada por el modulo LoRa
#include <Adafruit_GFX.h>        // Libreria grafica base para la pantalla OLED
#include <Adafruit_SSD1306.h>    // Controlador especifico para OLED SSD1306
#include <TinyGPS++.h>           // Decodificacion de sentencias NMEA del GPS
#include <LoRa.h>                // Comunicacion LoRa
#include <OneButton.h>           // Manejo de click, doble click y pulsacion larga

// Se indica a XPowersLib que el chip de energia usado es el AXP2101
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

// ==========================================
// CONFIGURACIoN ESTaTICA Y LEGAL DE LA RED
// ==========================================

// Identificador APRS de la estacion
const String CALLSIGN = "TI0TEC3-7";

// Comentario agregado al final de cada trama APRS transmitida
const String APRS_COMMENT = "Grupo 3: G. Mejia, E. Valle.";

// Destino APRS generico. Identificador
const String TOCALL = "APRS";

// ==========================================
// ASIGNACIoN DE PINES HARDWARE (T-BEAM V1.2)
// ==========================================

// Pines SPI conectados al modulo LoRa
#define SCK_LORA     5
#define MISO_LORA    19
#define MOSI_LORA    27
#define NSS_LORA     18
#define RST_LORA     23
#define DIO0_LORA    26

// Pines UART usados para recibir datos del GPS
#define GPS_RX_PIN   34ii
#define GPS_TX_PIN   12

// Pin del boton fisico de usuario
#define BUTTON_PIN   38

// Pines I2C usados por la pantalla OLED y el PMU
#define OLED_SDA     21
#define OLED_SCL     22

// Resolucion de la pantalla OLED SSD1306
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ==========================================
// INSTANCIACIoN DE OBJETOS
// ==========================================

// Objeto de control para la pantalla OLED. El parametro -1 indica que no se usa pin de reset dedicado
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Objeto encargado de interpretar los datos NMEA recibidos del GPS
TinyGPSPlus gps;

// Se usa el puerto serial hardware 1 del ESP32 para comunicarse con el GPS
HardwareSerial gpsSerial(1);

// Configuracion del boton: pin, logica activa en bajo y resistencia pull-up interna
OneButton userButton(BUTTON_PIN, true, true);

// Objeto para gestionar el PMU AXP2101: bateria, carga y salidas reguladas
XPowersPMU PMU;

// ==========================================
// MaQUINA DE ESTADOS Y BANDERAS FINITAS
// ==========================================

// Estados principales del tracker. El loop() avanza entre estos estados segun eventos y temporizadores
// TRANSMITIR  en este programa la transmision se realiza dentro de EMPAQUETAR
enum EstadosTracker { INIT, LEER_GPS, EVALUAR_EVENTO, EMPAQUETAR, TRANSMITIR, DORMIR };
EstadosTracker estado_actual = INIT;

// Perfiles de uso cambian el simbolo APRS y el intervalo de transmision
enum PerfilUso { MODO_CAMINANDO, MODO_BOTE };
PerfilUso perfil_actual = MODO_CAMINANDO;

// Simbolos APRS para representar el tracker en el mapa
const char SYMBOL_WALKING = '[';
const char SYMBOL_BOAT    = 'Y';
const char SYMBOL_TABLE   = '/';

// Banderas de eventos relevantes
bool bandera_sos = false;              // Se activa con pulsacion larga para enviar un mensaje de emergencia
bool bandera_bateria_baja = false;     // Se activa si la bateria cae por debajo del umbral establecido
bool modo_ahorro_pantalla = false;     // Apaga logicamente la OLED para reducir consumo

// Temporizadores no bloqueantes basados en millis()
uint32_t tiempo_ultima_tx = 0;           // Marca del ultimo envio APRS
uint32_t tiempo_pantalla_encendida = 0;  // Marca desde la ultima interaccion que encendio pantalla
uint32_t tiempo_ultima_lectura_pmu = 0;  // Marca de la ultima actualizacion de bateria/energia
uint32_t timeout_gps_ms = 120000;        // Tiempo maximo esperando una actualizacion GPS valida

// Variables de estado visibles en pantalla o usadas en la trama
String tipo_mensaje_actual = "RUTINA";   // Puede ser RUTINA, LOW_BATT o SOS
String ultima_rx_callsign = "Ninguno";   // ultimo callsign detectado en recepcion LoRa
int porcentaje_bateria = 0;              // Porcentaje estimado a partir del voltaje de bateria

// ==========================================
// VECTORES GRaFICOS (16x16 px)
// ==========================================

// icono mostrado cuando el perfil activo es caminando
// PROGMEM almacena el arreglo en memoria de programa para ahorrar RAM
const unsigned char icon_walk[] PROGMEM = {
  0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x03, 0xc0, 0x07, 0x00, 0x0e, 0x00,
  0x18, 0x00, 0x0f, 0xc0, 0x01, 0x80, 0x03, 0x00, 0x06, 0x00, 0x0c, 0x00,
  0x18, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00
};

// icono mostrado cuando el perfil activo es bote
const unsigned char icon_boat[] PROGMEM = {
  0x00, 0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x80, 0x0f, 0x80, 0x01, 0x80,
  0x01, 0x80, 0x01, 0x80, 0xff, 0xff, 0x7f, 0xfe, 0x3f, 0xfc, 0x1f, 0xf8,
  0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ==========================================
// RUTINAS DE CONVERSIoN TOPOGRaFICA APRS
// ==========================================

/*
  paddingCeros()
  Convierte un numero a String y agrega ceros a la izquierda hasta alcanzar
  el ancho solicitado. Construye fechas, horas y coordenadas APRS
*/
String paddingCeros(int numero, int ancho) {
    String res = String(numero);
    while (res.length() < ancho) res = "0" + res;
    return res;
}

/*
  conversionLatitudAPRS()
  Convierte latitud decimal a formato APRS: DDMM.mmN o DDMM.mmS
*/
String conversionLatitudAPRS(double lat) {
    char direccion = (lat >= 0) ? 'N' : 'S';  // Norte para positivo, Sur para negativo
    lat = fabs(lat);                          // APRS usa la direccion como letra, por eso se trabaja con valor absoluto

    int grados = (int)lat;
    double minutos = (lat - grados) * 60.0;

    return paddingCeros(grados, 2) +
           paddingCeros((int)minutos, 2) +
           "." +
           paddingCeros((int)((minutos - (int)minutos) * 100.0), 2) +
           direccion;
}

/*
  conversionLongitudAPRS()
  Convierte longitud decimal a formato APRS: DDDMM.mmE o DDDMM.mmW

  La longitud usa tres digitos para grados, a diferencia de la latitud
*/
String conversionLongitudAPRS(double lon) {
    char direccion = (lon >= 0) ? 'E' : 'W';  // Este para positivo, Oeste para negativo
    lon = fabs(lon);

    int grados = (int)lon;
    double minutos = (lon - grados) * 60.0;

    return paddingCeros(grados, 3) +
           paddingCeros((int)minutos, 2) +
           "." +
           paddingCeros((int)((minutos - (int)minutos) * 100.0), 2) +
           direccion;
}

// ==========================================
// CONTROL DE UI Y EVENTOS DE INTERRUPCION
// ==========================================

/*
  renderizarPantallaPrincipal()
  Actualiza la pantalla OLED con la informacion principal del tracker:
  - Callsign
  - icono del perfil activo
  - Fecha y hora GPS ajustada a UTC-6
  - Coordenadas
  - Altitud y velocidad
  - ultimo callsign recibido
  - Estado de carga o porcentaje de bateria
*/
void renderizarPantallaPrincipal() {
    // Si el modo de ahorro esta activo, se limpia la pantalla y se sale
    if (modo_ahorro_pantalla) {
        display.clearDisplay();
        display.display();
        return;
    }

    display.clearDisplay();
    display.setTextColor(WHITE);

    // Encabezado con el callsign de la estacion
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(CALLSIGN);

    // Dibuja el icono segun el perfil de uso seleccionado
    if (perfil_actual == MODO_CAMINANDO) display.drawBitmap(112, 0, icon_walk, 16, 16, WHITE);
    else display.drawBitmap(112, 0, icon_boat, 16, 16, WHITE);

    display.setTextSize(1);

    // Fecha y hora. La hora GPS viene en UTC se resta 6 para Costa Rica/Centroamerica
    display.setCursor(0, 18);
    if (gps.time.isValid() && gps.date.isValid()) {
        int hora_local = gps.time.hour() - 6;
        if (hora_local < 0) hora_local += 24;

        display.print(paddingCeros(gps.date.year(), 4) + "-" +
                      paddingCeros(gps.date.month(), 2) + "-" +
                      paddingCeros(gps.date.day(), 2));
        display.print("  " + paddingCeros(hora_local, 2) + ":" +
                      paddingCeros(gps.time.minute(), 2) + ":" +
                      paddingCeros(gps.time.second(), 2));
    } else {
        display.print("Esperando Satelites...");
    }

    // Coordenadas GPS si ya existe fix valido
    display.setCursor(0, 30);
    if (gps.location.isValid()) display.print(String(gps.location.lat(), 4) + "  " + String(gps.location.lng(), 4));
    else display.print("No Fix");

    // Altitud en metros y velocidad en km/h
    display.setCursor(0, 42);
    display.print("A=" + paddingCeros((int)gps.altitude.meters(), 4) + "m    " + String((int)gps.speed.kmph()) + "km/h");

    // ultima estacion recibida por LoRa
    display.setCursor(0, 54);
    display.print("Rx:" + ultima_rx_callsign);

    // Estado de alimentacion: cargando por VBUS o porcentaje de bateria
    display.setCursor(68, 54);
    if (PMU.isVbusIn()) display.print("Cargando...");
    else display.print("B: " + String(porcentaje_bateria) + "%");

    display.display();
}

/*
  actualizarEnergia()
  Lee el PMU para estimar el porcentaje de bateria y decidir si se debe activar
  la bandera de bateria baja. Tambien refresca la OLED si la pantalla esta activa
*/
void actualizarEnergia() {
    // Si hay bateria conectada, se estima el porcentaje usando el voltaje
    if (PMU.isBatteryConnect()) {
        uint16_t vol_mv = PMU.getBattVoltage();

        if (vol_mv > 0) {
            float voltaje = vol_mv / 1000.0;

            // Aproximacion lineal: 3.2 V = 0 %, 4.2 V = 100 %
            porcentaje_bateria = (int)(((voltaje - 3.2) / (4.2 - 3.2)) * 100.0);

            // Saturacion para evitar valores menores a 0 o mayores a 100
            if (porcentaje_bateria > 100) porcentaje_bateria = 100;
            if (porcentaje_bateria < 0) porcentaje_bateria = 0;
        } else {
            porcentaje_bateria = 0;
        }
    } else {
        porcentaje_bateria = 0;
    }

    // Activa alerta de bateria baja solo si no esta cargando y hay bateria conectada
    if (porcentaje_bateria < 15 && !PMU.isVbusIn() && PMU.isBatteryConnect()) {
        bandera_bateria_baja = true;
    } else {
        bandera_bateria_baja = false;
    }

    // Actualiza pantalla si no esta en ahorro y no se esta mostrando SOS
    if (!modo_ahorro_pantalla && !bandera_sos) {
        renderizarPantallaPrincipal();
    }
}

/*
  clickSimpleBoton()
  Despierta la pantalla y actualiza informacion de energia
*/
void clickSimpleBoton() {
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    actualizarEnergia();
}

/*
  dobleClickBoton()
  Cambia entre modo caminando y modo bote
  El perfil afecta el simbolo APRS y el intervalo de transmision
*/
void dobleClickBoton() {
    perfil_actual = (perfil_actual == MODO_CAMINANDO) ? MODO_BOTE : MODO_CAMINANDO;
    clickSimpleBoton();
}

/*
  presionLargaBoton()
  Activa una alerta SOS. Se fuerza el estado EVALUAR_EVENTO para que el siguiente
  ciclo prepare una trama de emergencia
*/
void presionLargaBoton() {
    bandera_sos = true;
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    estado_actual = EVALUAR_EVENTO;

    // Mensaje visual inmediato para confirmar al usuario que se activo SOS
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(15, 25);
    display.print("SOS !!!");
    display.display();
}

// ==========================================
// INICIALIZACION DE HARDWARE
// ==========================================

/*
  setup()
  Se ejecuta una sola vez al iniciar el ESP32. Configura comunicacion serial, I2C, PMU, OLED, boton, GPS y radio LoRa
*/
void setup() {
    Serial.begin(115200);

    // Inicializa el bus I2C compartido por la OLED y el PMU
    Wire.begin(OLED_SDA, OLED_SCL);

    // Inicializacion del PMU AXP2101
    if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, OLED_SDA, OLED_SCL)) {
        // Habilita salidas reguladas necesarias para perifericos del T-Beam.
        PMU.setALDO2Voltage(3300);
        PMU.enableALDO2();
        PMU.setALDO3Voltage(3300);
        PMU.enableALDO3();

        // Activa mediciones de bateria y VBUS
        PMU.enableBattDetection();
        PMU.enableVbusVoltageMeasure();
        PMU.enableBattVoltageMeasure();

        // Configura parametros de carga de bateria
        PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
        PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    }

    // Inicializa la pantalla OLED en la direccion I2C 0x3C
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay();
        display.display();
    }

    // Asocia las acciones del boton con las funciones de evento
    userButton.attachClick(clickSimpleBoton);
    userButton.attachDoubleClick(dobleClickBoton);
    userButton.setPressMs(3000);              // 3 segundos para considerar pulsacion larga
    userButton.attachLongPressStart(presionLargaBoton);

    // Inicializa el UART del GPS a 9600 baudios.
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    // Inicializa bus SPI y asigna pines del modulo LoRa
    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, NSS_LORA);
    LoRa.setPins(NSS_LORA, RST_LORA, DIO0_LORA);

    // Frecuencia LoRa: 433.775 MHz
    // Si la radio no inicia, el programa se queda detenido para evitar operar sin comunicacion
    if (!LoRa.begin(433775000)) {
        while (1);
    }

    // Parametros LoRa orientados a mayor alcance y robustez
    LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
    LoRa.setSpreadingFactor(12);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();

    // Una vez inicializado el hardware, se pasa a leer GPS
    estado_actual = LEER_GPS;
}

// ==========================================
// NUCLEO DE PROCESAMIENTO (LOOP)
// ==========================================

/*
  loop()
  Ejecuta continuamente la maquina de estados del tracker:
  1. Atiende boton
  2. Actualiza energia cada 2 segundos
  3. Apaga pantalla tras 20 segundos sin interaccion
  4. Lee GPS, evalua evento, arma trama APRS, transmite y espera el siguiente ciclo
*/
void loop() {
    // Requerido por OneButton para detectar clicks, doble clicks y pulsaciones largas
    userButton.tick();

    // Rutina periodica no bloqueante: muestrea energia y refresca OLED cada 2000 ms
    if (millis() - tiempo_ultima_lectura_pmu >= 2000) {
        actualizarEnergia();
        tiempo_ultima_lectura_pmu = millis();
    }

    // Temporizador de ahorro de pantalla: apaga OLED despues de 20 s sin interaccion
    if (!modo_ahorro_pantalla && !bandera_sos && (millis() - tiempo_pantalla_encendida >= 20000)) {
        modo_ahorro_pantalla = true;
        renderizarPantallaPrincipal();
    }

    // Maquina de estados principal.
    switch (estado_actual) {
        case INIT:
            // Estado de seguridad; normalmente setup() ya deja el sistema en LEER_GPS
            estado_actual = LEER_GPS;
            break;

        case LEER_GPS: {
            uint32_t inicio_lectura = millis();

            // Intenta recibir una posicion GPS valida hasta que haya fix, timeout o evento SOS
            while (millis() - inicio_lectura < timeout_gps_ms) {
                // Se mantiene el boton activo incluso durante la espera GPS.
                userButton.tick();

                // Refresco interno de energia durante esta espera para no congelar bateria/OLED
                if (millis() - tiempo_ultima_lectura_pmu >= 2000) {
                    actualizarEnergia();
                    tiempo_ultima_lectura_pmu = millis();
                }

                // Lee todos los caracteres disponibles del GPS y los entrega al parser TinyGPS++
                while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());

                // Sale si ya hay ubicacion nueva y valida, o si el usuario activo SOS
                if (gps.location.isUpdated() && gps.location.isValid()) break;
                if (bandera_sos) break;
            }

            estado_actual = EVALUAR_EVENTO;
            break;
        }

        case EVALUAR_EVENTO:
            // Prioridad de eventos: SOS > bateria baja > rutina
            if (bandera_sos) tipo_mensaje_actual = "SOS";
            else if (bandera_bateria_baja) tipo_mensaje_actual = "LOW_BATT";
            else tipo_mensaje_actual = "RUTINA";

            estado_actual = EMPAQUETAR;
            break;

        case EMPAQUETAR: {
            // Selecciona el simbolo APRS de acuerdo con el perfil activo
            char simbolo_mapa = (perfil_actual == MODO_CAMINANDO) ? SYMBOL_WALKING : SYMBOL_BOAT;

            // Cabecera APRS/AX.25 para transmitir por LoRa
            // Formato base: ORIGEN>DESTINO,RUTA,qAR:=payload
            String trama_aprs_final = CALLSIGN + ">" + TOCALL + ",WIDE1-1,qAR:=";

            if (gps.location.isValid()) {
                // Agrega posicion, simbolo, altitud y velocidad si el GPS tiene fix valido
                trama_aprs_final += conversionLatitudAPRS(gps.location.lat());
                trama_aprs_final += SYMBOL_TABLE;
                trama_aprs_final += conversionLongitudAPRS(gps.location.lng());
                trama_aprs_final += simbolo_mapa;
                trama_aprs_final += " /A=" + paddingCeros(gps.altitude.feet(), 6);
                trama_aprs_final += " Vel:" + String((int)gps.speed.kmph()) + "km/h ";
            } else {
                // Posicion de respaldo cuando no hay fix GPS valido
                trama_aprs_final += "0000.00N/00000.00W" + String(simbolo_mapa) + " ";
            }

            // Marca la trama si corresponde a emergencia o bateria baja
            if (tipo_mensaje_actual == "SOS") trama_aprs_final += "[EMERGENCY-SOS] ";
            else if (tipo_mensaje_actual == "LOW_BATT") trama_aprs_final += "[LOW BATT] ";

            // Agrega comentario final de identificacion del grupo
            trama_aprs_final += APRS_COMMENT;

            // Transmision LoRa
            // Los tres primeros bytes funcionan como encabezado simple de identificacion del paquete
            LoRa.beginPacket();
            LoRa.write('<');
            LoRa.write(0xFF);
            LoRa.write(0x01);
            LoRa.print(trama_aprs_final);
            LoRa.endPacket();

            // Despues de transmitir una alerta SOS, se limpia la bandera para no repetirla indefinidamente
            if (bandera_sos) bandera_sos = false;

            tiempo_ultima_tx = millis();

            // Si la bateria esta bien, queda en modo recepcion, si esta baja, duerme la radio para ahorrar energia
            if (!bandera_bateria_baja) LoRa.receive();
            else LoRa.sleep();

            estado_actual = DORMIR;
            break;
        }

        case DORMIR: {
            // Intervalo entre transmisiones:
            // - Caminando: 5 minutos
            // - Bote: 2 minutos
            uint32_t intervalo = (perfil_actual == MODO_CAMINANDO) ? 300000 : 120000;

            // Mientras espera el siguiente envio, intenta recibir paquetes LoRa si no hay bateria baja
            if (!bandera_bateria_baja) {
                int packetSize = LoRa.parsePacket();

                // Se esperan al menos los 3 bytes de encabezado mas contenido
                if (packetSize > 3) {
                    int b1 = LoRa.read();
                    int b2 = LoRa.read();
                    int b3 = LoRa.read();

                    // Valida encabezado simple: '<', 0xFF, 0x01
                    if (b1 == '<' && b2 == 0xFF && b3 == 0x01) {
                        String incoming = "";

                        // Lee el resto del paquete recibido.
                        while (LoRa.available()) incoming += (char)LoRa.read();

                        // Extrae el callsign tomando el texto antes de '>'
                        int index = incoming.indexOf('>');
                        if (index > 0) {
                            ultima_rx_callsign = incoming.substring(0, index);

                            // Actualiza pantalla con el ultimo emisor recibido.
                            if (!modo_ahorro_pantalla) renderizarPantallaPrincipal();
                        }
                    }
                }
            }

            // Cuando se cumple el intervalo del perfil, vuelve a leer GPS para preparar nueva transmision
            if (millis() - tiempo_ultima_tx >= intervalo) {
                estado_actual = LEER_GPS;
            }
            break;
        }
    }
}
