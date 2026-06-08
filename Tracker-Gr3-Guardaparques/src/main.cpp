#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <LoRa.h>
#include <OneButton.h>

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

// ==========================================
// CONFIGURACIÓN ESTÁTICA Y LEGAL DE LA RED
// ==========================================
const String CALLSIGN = "TI0TEC3-7";
const String APRS_COMMENT = "Grupo 3: G. Mejia, E. Valle.";
const String TOCALL = "APRS"; // Identificador 

// ==========================================
// ASIGNACIÓN DE PINES HARDWARE (T-BEAM V1.2)
// ==========================================

#define SCK_LORA     5
#define MISO_LORA    19
#define MOSI_LORA    27
#define NSS_LORA     18
#define RST_LORA     23
#define DIO0_LORA    26

#define GPS_RX_PIN   34
#define GPS_TX_PIN   12
#define BUTTON_PIN   38  

#define OLED_SDA     21
#define OLED_SCL     22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ==========================================
// INSTANCIACIÓN DE OBJETOS
// ==========================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
OneButton userButton(BUTTON_PIN, true, true);
XPowersPMU PMU;

// ==========================================
// MÁQUINA DE ESTADOS Y BANDERAS FINITAS
// ==========================================
enum EstadosTracker { INIT, LEER_GPS, EVALUAR_EVENTO, EMPAQUETAR, TRANSMITIR, DORMIR };
EstadosTracker estado_actual = INIT;

enum PerfilUso { MODO_CAMINANDO, MODO_BOTE };
PerfilUso perfil_actual = MODO_CAMINANDO;

const char SYMBOL_WALKING = '['; 
const char SYMBOL_BOAT    = 'Y'; 
const char SYMBOL_TABLE   = '/';

bool bandera_sos = false;
bool bandera_bateria_baja = false;
bool modo_ahorro_pantalla = false; 

uint32_t tiempo_ultima_tx = 0;
uint32_t tiempo_pantalla_encendida = 0;
uint32_t tiempo_ultima_lectura_pmu = 0;
uint32_t timeout_gps_ms = 120000; 

String tipo_mensaje_actual = "RUTINA";
String ultima_rx_callsign = "Ninguno";
int porcentaje_bateria = 0;

// ==========================================
// VECTORES GRÁFICOS (16x16 px)
// ==========================================

const unsigned char icon_walk[] PROGMEM = {
  0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x03, 0xc0, 0x07, 0x00, 0x0e, 0x00,
  0x18, 0x00, 0x0f, 0xc0, 0x01, 0x80, 0x03, 0x00, 0x06, 0x00, 0x0c, 0x00,
  0x18, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_boat[] PROGMEM = {
  0x00, 0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x80, 0x0f, 0x80, 0x01, 0x80,
  0x01, 0x80, 0x01, 0x80, 0xff, 0xff, 0x7f, 0xfe, 0x3f, 0xfc, 0x1f, 0xf8,
  0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ==========================================
// RUTINAS DE CONVERSIÓN TOPOGRÁFICA APRS
// ==========================================

String paddingCeros(int numero, int ancho) {
    String res = String(numero);
    while (res.length() < ancho) res = "0" + res;
    return res;
}

String conversionLatitudAPRS(double lat) {
    char direccion = (lat >= 0) ? 'N' : 'S';
    lat = fabs(lat);
    int grados = (int)lat;
    double minutos = (lat - grados) * 60.0;
    return paddingCeros(grados, 2) + paddingCeros((int)minutos, 2) + "." + paddingCeros((int)((minutos - (int)minutos) * 100.0), 2) + direccion;
}

String conversionLongitudAPRS(double lon) {
    char direccion = (lon >= 0) ? 'E' : 'W';
    lon = fabs(lon);
    int grados = (int)lon;
    double minutos = (lon - grados) * 60.0;
    return paddingCeros(grados, 3) + paddingCeros((int)minutos, 2) + "." + paddingCeros((int)((minutos - (int)minutos) * 100.0), 2) + direccion;
}

// ==========================================
// CONTROL DE UI Y EVENTOS DE INTERRUPCIÓN
// ==========================================

void renderizarPantallaPrincipal() {
    if (modo_ahorro_pantalla) {
        display.clearDisplay();
        display.display();
        return;
    }

    display.clearDisplay();
    display.setTextColor(WHITE);
    
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(CALLSIGN);
    
    if (perfil_actual == MODO_CAMINANDO) display.drawBitmap(112, 0, icon_walk, 16, 16, WHITE);
    else display.drawBitmap(112, 0, icon_boat, 16, 16, WHITE);

    display.setTextSize(1);
    
    display.setCursor(0, 18);
    if (gps.time.isValid() && gps.date.isValid()) {
        int hora_local = gps.time.hour() - 6;
        if (hora_local < 0) hora_local += 24;
        display.print(paddingCeros(gps.date.year(), 4) + "-" + paddingCeros(gps.date.month(), 2) + "-" + paddingCeros(gps.date.day(), 2));
        display.print("  " + paddingCeros(hora_local, 2) + ":" + paddingCeros(gps.time.minute(), 2) + ":" + paddingCeros(gps.time.second(), 2));
    } else {
        display.print("Esperando Satelites...");
    }

    display.setCursor(0, 30);
    if (gps.location.isValid()) display.print(String(gps.location.lat(), 4) + "  " + String(gps.location.lng(), 4));
    else display.print("No Fix");

    display.setCursor(0, 42);
    display.print("A=" + paddingCeros((int)gps.altitude.meters(), 4) + "m    " + String((int)gps.speed.kmph()) + "km/h");

    display.setCursor(0, 54);
    display.print("Rx:" + ultima_rx_callsign);
    
    display.setCursor(68, 54);
    if (PMU.isVbusIn()) display.print("Cargando...");
    else display.print("B: " + String(porcentaje_bateria) + "%");
    
    display.display();
}

void actualizarEnergia() {
    // Muestreo asíncrono para garantizar actualización en tiempo real
    if (PMU.isBatteryConnect()) {
        uint16_t vol_mv = PMU.getBattVoltage();
        if (vol_mv > 0) {
            float voltaje = vol_mv / 1000.0;
            porcentaje_bateria = (int)(((voltaje - 3.2) / (4.2 - 3.2)) * 100.0);
            if (porcentaje_bateria > 100) porcentaje_bateria = 100;
            if (porcentaje_bateria < 0) porcentaje_bateria = 0;
        } else {
            porcentaje_bateria = 0;
        }
    } else {
        porcentaje_bateria = 0;
    }
    
    if (porcentaje_bateria < 15 && !PMU.isVbusIn() && PMU.isBatteryConnect()) {
        bandera_bateria_baja = true;
    } else {
        bandera_bateria_baja = false;
    }

    if (!modo_ahorro_pantalla && !bandera_sos) {
        renderizarPantallaPrincipal();
    }
}

void clickSimpleBoton() {
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    actualizarEnergia();
}

void dobleClickBoton() {
    perfil_actual = (perfil_actual == MODO_CAMINANDO) ? MODO_BOTE : MODO_CAMINANDO;
    clickSimpleBoton();
}

void presionLargaBoton() {
    bandera_sos = true;
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    estado_actual = EVALUAR_EVENTO; 
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(15, 25);
    display.print("SOS !!!");
    display.display();
}

// ==========================================
// INICIALIZACIÓN DE HARDWARE
// ==========================================
void setup() {
    Serial.begin(115200);
    Wire.begin(OLED_SDA, OLED_SCL);
    
    if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, OLED_SDA, OLED_SCL)) {
        PMU.setALDO2Voltage(3300);
        PMU.enableALDO2();
        PMU.setALDO3Voltage(3300);
        PMU.enableALDO3();

        PMU.enableBattDetection();
        PMU.enableVbusVoltageMeasure();
        PMU.enableBattVoltageMeasure();
        
        PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
        PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA); 
    }

    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay();
        display.display();
    }

    userButton.attachClick(clickSimpleBoton);
    userButton.attachDoubleClick(dobleClickBoton);
    userButton.setPressMs(3000); 
    userButton.attachLongPressStart(presionLargaBoton);

    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, NSS_LORA);
    LoRa.setPins(NSS_LORA, RST_LORA, DIO0_LORA);
    
    if (!LoRa.begin(433775000)) { 
        while (1); 
    }
    
    LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN); 
    LoRa.setSpreadingFactor(12);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();
    
    estado_actual = LEER_GPS;
}

// ==========================================
// NÚCLEO DE PROCESAMIENTO (LOOP)
// ==========================================
void loop() {
    userButton.tick();

    // Rutina asíncrona: Muestrea datos energéticos y refresca OLED cada 2000 ms
    if (millis() - tiempo_ultima_lectura_pmu >= 2000) {
        actualizarEnergia();
        tiempo_ultima_lectura_pmu = millis();
    }

    // Temporizador de ahorro de pantalla
    if (!modo_ahorro_pantalla && !bandera_sos && (millis() - tiempo_pantalla_encendida >= 20000)) { 
        modo_ahorro_pantalla = true;
        renderizarPantallaPrincipal();
    }

    switch (estado_actual) {
        case INIT:
            estado_actual = LEER_GPS;
            break;

        case LEER_GPS: {
            uint32_t inicio_lectura = millis();
            
            while (millis() - inicio_lectura < timeout_gps_ms) {
                userButton.tick(); 
                
                // Refresco interno de energía durante el bloqueo del GPS
                if (millis() - tiempo_ultima_lectura_pmu >= 2000) {
                    actualizarEnergia();
                    tiempo_ultima_lectura_pmu = millis();
                }

                while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
                
                if (gps.location.isUpdated() && gps.location.isValid()) break;
                if (bandera_sos) break;
            }
            estado_actual = EVALUAR_EVENTO;
            break;
        }

        case EVALUAR_EVENTO:
            if (bandera_sos) tipo_mensaje_actual = "SOS";
            else if (bandera_bateria_baja) tipo_mensaje_actual = "LOW_BATT";
            else tipo_mensaje_actual = "RUTINA";
            estado_actual = EMPAQUETAR;
            break;

        case EMPAQUETAR: {
            char simbolo_mapa = (perfil_actual == MODO_CAMINANDO) ? SYMBOL_WALKING : SYMBOL_BOAT;
            
            // Corrección de la cabecera AX.25: El TOCALL y el PATH deben estar separados correctamente
            String trama_aprs_final = CALLSIGN + ">" + TOCALL + ",WIDE1-1,qAR:=";
            
            if (gps.location.isValid()) {
                trama_aprs_final += conversionLatitudAPRS(gps.location.lat());
                trama_aprs_final += SYMBOL_TABLE;
                trama_aprs_final += conversionLongitudAPRS(gps.location.lng());
                trama_aprs_final += simbolo_mapa;
                trama_aprs_final += " /A=" + paddingCeros(gps.altitude.feet(), 6);
                trama_aprs_final += " Vel:" + String((int)gps.speed.kmph()) + "km/h ";
            } else {
                trama_aprs_final += "0000.00N/00000.00W" + String(simbolo_mapa) + " ";
            }

            if (tipo_mensaje_actual == "SOS") trama_aprs_final += "[EMERGENCY-SOS] ";
            else if (tipo_mensaje_actual == "LOW_BATT") trama_aprs_final += "[LOW BATT] ";

            trama_aprs_final += APRS_COMMENT;

            LoRa.beginPacket();
            LoRa.write('<');
            LoRa.write(0xFF);
            LoRa.write(0x01);
            LoRa.print(trama_aprs_final);
            LoRa.endPacket();
            
            if (bandera_sos) bandera_sos = false;
            tiempo_ultima_tx = millis();
            
            if (!bandera_bateria_baja) LoRa.receive(); 
            else LoRa.sleep();
            
            estado_actual = DORMIR;
            break;
        }

        case DORMIR: {
            uint32_t intervalo = (perfil_actual == MODO_CAMINANDO) ? 300000 : 120000; 
            
            if (!bandera_bateria_baja) {
                int packetSize = LoRa.parsePacket();
                if (packetSize > 3) {
                    int b1 = LoRa.read(); int b2 = LoRa.read(); int b3 = LoRa.read();
                    if (b1 == '<' && b2 == 0xFF && b3 == 0x01) {
                        String incoming = "";
                        while (LoRa.available()) incoming += (char)LoRa.read();
                        int index = incoming.indexOf('>');
                        if (index > 0) {
                            ultima_rx_callsign = incoming.substring(0, index);
                            if (!modo_ahorro_pantalla) renderizarPantallaPrincipal();
                        }
                    }
                }
            }
            
            if (millis() - tiempo_ultima_tx >= intervalo) {
                estado_actual = LEER_GPS;
            }
            break;
        }
    }
}
