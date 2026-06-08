#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <LoRa.h>
#include <OneButton.h>
#define XPOWERS_CHIP_AXP192
#include <XPowersLib.h>

// ==========================================
// CONFIGURACIÓN ESTÁTICA Y LEGAL DE LA RED
// ==========================================
const String CALLSIGN = "TI0TEC3-7";
const String APRS_COMMENT = "Grupo 3: G. Mejía, E. Valle.";

// ==========================================
// ASIGNACIÓN DE PINES HARDWARE (LILYGO T-BEAM)
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
// MÁQUINA DE ESTADOS 
// ==========================================

enum EstadosTracker { INIT, GESTION_ENERGIA, LEER_GPS, EVALUAR_EVENTO, EMPAQUETAR, TRANSMITIR, DORMIR };
EstadosTracker estado_actual = INIT;

enum PerfilUso { MODO_CAMINANDO, MODO_BOTE };
PerfilUso perfil_actual = MODO_CAMINANDO;

const char SYMBOL_WALKING = '['; 
const char SYMBOL_BOAT    = 'Y'; 
const char SYMBOL_TABLE   = '/';

bool bandera_sos = false;
bool bandera_bateria_baja = false;
bool modo_ahorro_pantalla = true; 

uint32_t tiempo_ultima_tx = 0;
uint32_t tiempo_pantalla_encendida = 0;
uint32_t timeout_gps_ms = 120000; 
String tipo_mensaje_actual = "RUTINA";
String trama_aprs_final = "";
String ultima_hora_rx = "--:--:--";

int porcentaje_bateria = 100;

const unsigned char icon_batt_low[] PROGMEM = {
  0xff, 0xff, 0xff, 0x80, 0x00, 0x01, 0xbf, 0xff, 0xfd, 0xa0, 0x00, 0x05,
  0xa0, 0x00, 0x05, 0xa0, 0x00, 0x05, 0xa0, 0x00, 0x05, 0xa3, 0xff, 0xc5,
  0xa3, 0xff, 0xc5, 0xa3, 0xff, 0xc5, 0xa0, 0x00, 0x05, 0xa0, 0x00, 0x05,
  0xa0, 0x00, 0x05, 0xbf, 0xff, 0xfd, 0x80, 0x00, 0x01, 0xff, 0xff, 0xff
};

// ==========================================
// FUNCIONES AUXILIARES DE CONVERSIÓN APRS
// ==========================================
String paddingCeros(int numero, int ancho) {
    String res = String(numero);
    while (res.length() < ancho) {
        res = "0" + res;
    }
    return res;
}

String conversionLatitudAPRS(double lat) {
    char direccion = (lat >= 0) ? 'N' : 'S';
    lat = fabs(lat);
    int grados = (int)lat;
    double minutos = (lat - grados) * 60.0;
    int minutos_enteros = (int)minutos;
    int centesimas = (int)((minutos - minutos_enteros) * 100.0);
    
    return paddingCeros(grados, 2) + paddingCeros(minutos_enteros, 2) + "." + paddingCeros(centesimas, 2) + direccion;
}

String conversionLongitudAPRS(double lon) {
    char direccion = (lon >= 0) ? 'E' : 'W';
    lon = fabs(lon);
    int grados = (int)lon;
    double minutos = (lon - grados) * 60.0;
    int minutos_enteros = (int)minutos;
    int centesimas = (int)((minutos - minutos_enteros) * 100.0);
    
    return paddingCeros(grados, 3) + paddingCeros(minutos_enteros, 2) + "." + paddingCeros(centesimas, 2) + direccion;
}

// ==========================================
// CONTROL DE LA INTERFAZ DE USUARIO (OLED)
// ==========================================
void renderizarAlertaBateria() {
    display.clearDisplay();
    display.drawBitmap(52, 6, icon_batt_low, 24, 16, WHITE);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(18, 32);
    display.print("ALERTA: BATT < 15%");
    display.setCursor(6, 48);
    display.print("CONECTE CARGADOR USB");
    display.display();
}

void renderizarPantallaPrincipal() {
    if (modo_ahorro_pantalla) {
        display.clearDisplay();
        display.display();
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    
    display.setCursor(0, 0);
    display.print(CALLSIGN);
    display.setCursor(85, 0);
    display.print(String(porcentaje_bateria) + "%");

    display.setCursor(0, 11);
    display.print("Modo: ");
    if (perfil_actual == MODO_CAMINANDO) display.print("Caminando [ ]");
    else display.print("Bote en Rio [Y]");

    display.setCursor(0, 22);
    if (gps.time.isValid()) {
        display.print("Hora: " + paddingCeros(gps.time.hour(), 2) + ":" + paddingCeros(gps.time.minute(), 2) + ":" + paddingCeros(gps.time.second(), 2));
    } else {
        display.print("Hora: Buscando...");
    }

    display.setCursor(0, 33);
    if (gps.location.isValid()) {
        display.print("Lat: " + String(gps.location.lat(), 4));
        display.setCursor(68, 33);
        display.print("Lon: " + String(gps.location.lng(), 4));
    } else {
        display.print("Coordenadas: No Fix");
    }

    display.setCursor(0, 46);
    display.print("Last Tx Estado: " + tipo_mensaje_actual);
    
    display.setCursor(0, 56);
    display.print("Reloj Sinc: " + ultima_hora_rx);
    
    display.display();
}

// ==========================================
// CALLBACKS DE INTERRUPCIÓN DEL BOTÓN (GPIO 38)
// ==========================================

void clickSimpleBoton() {
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    renderizarPantallaPrincipal();
}

void dobleClickBoton() {
    if (perfil_actual == MODO_CAMINANDO) {
        perfil_actual = MODO_BOTE;
    } else {
        perfil_actual = MODO_CAMINANDO;
    }
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    renderizarPantallaPrincipal();
}

void presionLargaBoton() {

    bandera_sos = true;
    modo_ahorro_pantalla = false;
    tiempo_pantalla_encendida = millis();
    estado_actual = EVALUAR_EVENTO; 

// ==========================================
// CONFIGURACIÓN DE HARDWARE (SETUP)
// ==========================================
void setup() {
    Serial.begin(115200);
    
    Wire.begin(OLED_SDA, OLED_SCL);
    

    if (PMU.begin(Wire, AXP192_SLAVE_ADDRESS, OLED_SDA, OLED_SCL)) {
        
        PMU.setPowerChannelVoltage(XPOWERS_LDO2, 3300);
        PMU.enablePowerChannel(XPOWERS_LDO2);
        PMU.setPowerChannelVoltage(XPOWERS_LDO3, 3300); 
        PMU.enablePowerChannel(XPOWERS_LDO3);
    }

    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay();
        display.display();
    }


    userButton.attachClick(clickSimpleBoton);
    userButton.attachDoubleClick(dobleClickBoton);
    userButton.setPressTicks(6000); 
    userButton.attachLongPressStart(presionLargaBoton);

    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, NSS_LORA);
    LoRa.setPins(NSS_LORA, RST_LORA, DIO0_LORA);
    
    if (!LoRa.begin(433775000)) { 
        Serial.println("Falla crítica en inicialización LoRa");
        while (1);
    }
    
    LoRa.setSpreadingFactor(12);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();
    
    estado_actual = GESTION_ENERGIA;
}

// ==========================================
// CICLO DE EJECUCIÓN CONTINUO (LOOP)
// ==========================================
void loop() {

    userButton.tick();

    if (!modo_ahorro_pantalla && !bandera_bateria_baja) {
        if (millis() - tiempo_pantalla_encendida >= 15000) {
            modo_ahorro_pantalla = true;
            renderizarPantallaPrincipal();
        }
    }

    switch (estado_actual) {
        case INIT:
            estado_actual = GESTION_ENERGIA;
            break;

        case GESTION_ENERGIA: {
            if (PMU.isBatteryConnect()) {
                porcentaje_bateria = PMU.getBatteryPercent();
            } else {
                porcentaje_bateria = 100; /
            }

            if (porcentaje_bateria < 15 && !PMU.isVbusIn()) {
                bandera_bateria_baja = true;
                modo_ahorro_pantalla = false;
                renderizarAlertaBateria();
            } else {
                bandera_bateria_baja = false;
                renderizarPantallaPrincipal();
            }
            estado_actual = LEER_GPS;
            break;
        }

        case LEER_GPS: {
            uint32_t inicio_lectura_gps = millis();
            bool tiene_fix_gps = false;

            while (millis() - inicio_lectura_gps < timeout_gps_ms) {
                userButton.tick();
                while (gpsSerial.available() > 0) {
                    gps.encode(gpsSerial.read());
                }
                
                if (gps.location.isUpdated() && gps.location.isValid()) {
                    tiene_fix_gps = true;
                    if (gps.time.isValid()) {
                        ultima_hora_rx = paddingCeros(gps.time.hour(), 2) + ":" + paddingCeros(gps.time.minute(), 2) + ":" + paddingCeros(gps.time.second(), 2);
                    }
                    break;
                }

                if (bandera_sos && gps.location.isValid()) {
                    break;
                }
            }
            estado_actual = EVALUAR_EVENTO;
            break;
        }

        case EVALUAR_EVENTO:
            if (bandera_sos) {
                tipo_mensaje_actual = "SOS";
            } else if (bandera_bateria_baja) {
                tipo_mensaje_actual = "LOW_BATT";
            } else {
                tipo_mensaje_actual = "RUTINA";
            }
            estado_actual = EMPAQUETAR;
            break;

        case EMPAQUETAR: {
            char tabla_simbolos = SYMBOL_TABLE;
            char simbolo_mapa = (perfil_actual == MODO_CAMINANDO) ? SYMBOL_WALKING : SYMBOL_BOAT;
            
            trama_aprs_final = CALLSIGN + ">APRS,TCPIP*,qAC:=";
            
            if (gps.location.isValid()) {
                trama_aprs_final += conversionLatitudAPRS(gps.location.lat());
                trama_aprs_final += tabla_simbolos;
                trama_aprs_final += conversionLongitudAPRS(gps.location.lng());
                trama_aprs_final += simbolo_mapa;
            } else {
                trama_aprs_final += "0000.00N/00000.00W" + String(simbolo_mapa);
            }

            if (tipo_mensaje_actual == "SOS") {
                trama_aprs_final += " [EMERGENCY - SOS] ";
            } else if (tipo_mensaje_actual == "LOW_BATT") {
                trama_aprs_final += " [LOW BATT] ";
            }

            trama_aprs_final += APRS_COMMENT;
            estado_actual = TRANSMITIR;
            break;
        }

        case TRANSMITIR:
            LoRa.beginPacket();
            LoRa.print(trama_aprs_final);
            LoRa.endPacket();
            
            if (bandera_sos) {
                bandera_sos = false;
            }

            tiempo_ultima_tx = millis();
            renderizarPantallaPrincipal();
            
            estado_actual = DORMIR;
            break;

        case DORMIR: {

            uint32_t intervalo_reposo = (perfil_actual == MODO_CAMINANDO) ? 300000 : 600000; 
            
            if (millis() - tiempo_ultima_tx >= intervalo_reposo) {
                estado_actual = GESTION_ENERGIA;
            }
            break;
        }
    }
}
