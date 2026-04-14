# Taller Integrador: Grupo 3

**Proyecto: Sistema de Monitoreo LoRa para Áreas de Conservación y Parques Nacionales**

**La problemática**

En áreas de conservación, ya sea con topografía montañosa y de alta densidad forestal (como los senderos al rededor del cráter del Volcán Poás) o en extensos humedales navegables (como la reserva de Caño Negro), la cobertura celular tradicional es nula o altamente intermitente. Esta desconexión representa un riesgo significativo para la seguridad de los guardaparques durante sus patrullajes, dificulta la coordinación de guías que lideran grupos de estudiantes o turistas, y limita el monitoreo en tiempo real de embarcaciones enfocadas en el control ambiental y la pesca. 

**La solución propuesta (El tracker)**

Desarrollar un nodo rastreador LoRa APRS portátil, de bajo consumo y alta autonomía. Este dispositivo actuará como una baliza versátil que puede ser llevada en una mochila por un guía terrestre o instalada temporalmente en un bote. Al transmitir la posición mediante radiofrecuencia a un iGate central (ubicado en la casetas o muelles principales), la administración del parque obtiene telemetría en tiempo real sin depender de redes comerciales de telecomunicaciones. 

**Máquina de estados**

A continuación se muestra el diagrama de máquina de estados propuesto para la implementación y desarrollo de este proyecto. En el diagrama se detallan los eventos y lo esperado de cada estado. 

```mermaid
stateDiagram-v2
    direction TB
    [*] --> INIT
    
    INIT --> GESTION_ENERGIA : Setup OK (Hardware, LoRa, GPS)
    
    GESTION_ENERGIA --> LEER_GPS : Actualiza banderas
    note right of GESTION_ENERGIA
        Lee PMU (AXP192/2101)
        Buzzer intermitente si <15%
        y sin Power Bank
    end note
    
    LEER_GPS --> EVALUAR_EVENTO : Fix GPS o Timeout
    
    EVALUAR_EVENTO --> EMPAQUETAR : Prioridad definida
    note right of EVALUAR_EVENTO
        Prioridad de Eventos:
        1. SOS (Pulsación > 6s)
        2. Peligro (Doble toque)
        3. Batería Baja (<15%)
        4. Rastreo de Rutina
    end note
    
    EMPAQUETAR --> TRANSMITIR : Trama APRS construida
    note right of EMPAQUETAR
        Asigna símbolo de mapa
        y comentarios: [PELIGRO],
        [EMERGENCY - SOS] o [LOW BATT]
    end note
    
    TRANSMITIR --> DORMIR : TX LoRa Finalizada
    
    DORMIR --> GESTION_ENERGIA : Fin Temporizador (Ej. 3-5 min)
    DORMIR --> EVALUAR_EVENTO : ¡Interrupción por Botón!
    note left of DORMIR
        Bajo consumo.
        Despierta instantáneamente
        ante una emergencia.
    end note
``` 

**Diagrama de Firmware por implementar**

Este diagrama de bloques muestra la forma en la que se relacionan los componentes físicos del Hardware con los módulos de código. A continuación se detalla la propuesta del diagrama. Los bloques del sistema se dividen en dos capas: 
*Capa del Hardware:*
- ESP32 (Microcontrolador Core):Este es el cerebro del LilyGO- T-Beam
- Módulo LoRa (SPI): Chip para transmisión de radiofrecuencia.
- Módulo GPS (UART): Receptor de posicionamiento satelital.
- PMU AXP192/2101 (I2C): Chip de gestión de energía (batería 18650).
- Interfaz de Usuario (GPIO): Botones y el buzzer.

*Capa de Firmware*
- Core / Main Loop: El orquestador principal, es decir la máquina de estados.
- Gestor APRS / LoRa: Se encarga de codificar el texto al estándar APRS y manejar las librerías del radio.
- Gestor GPS: Extrae latitud, longitud y hora usando librerías como TinyGPS++.
- Gestor de Energía: Se comunicará por I2C con el chip AXP para leer el porcentaje de batería.
- Gestor de Interfaz: Manejará la librería ```OneButton``` para los toques del usuario y la lógica no bloqueante (temporizador) del buzzer.

```mermaid
  flowchart TD
    %% Definición de Estilos
    classDef hardware fill:#e1f5fe,stroke:#01579b,stroke-width:2px,color:#000
    classDef software fill:#f3e5f5,stroke:#4a148c,stroke-width:2px,color:#000
    classDef baseCode fill:#e8f5e9,stroke:#1b5e20,stroke-width:2px,color:#000

    %% Capa de Hardware
    subgraph Hardware [Capa Física - LilyGO T-Beam]
        MCU[ESP32 Core]:::hardware
        GPS[Módulo GPS Neo-6M/8M]:::hardware
        LORA[Radio LoRa SX127x/SX126x]:::hardware
        AXP[PMU AXP192/2101 - Batería]:::hardware
        IO[Botón Inteligente & Buzzer]:::hardware
        
        GPS -- UART --> MCU
        LORA -- SPI --- MCU
        AXP -- I2C --- MCU
        IO -- GPIO / Interrupciones --> MCU
    end

    %% Capa de Firmware
    subgraph Firmware [Arquitectura de Software]
        MAIN[Orquestador Principal \n Máquina de Estados]:::software
        
        %% Módulos base
        subgraph Base [Módulos Base R. Guzmán]
            GPS_PARSER[Gestor NMEA / TinyGPS++]:::baseCode
            LORA_APRS[Gestor LoRa y \n Empaquetado APRS]:::baseCode
        end
        
        %% Módulos propios
        subgraph Propios [Módulos Propios - Grupo 3]
            POWER_MGR[Gestor de Energía \n Monitoreo de Batería]:::software
            UI_MGR[Gestor de Interfaz \n OneButton y Temporizadores]:::software
        end
        
        %% Conexiones de Software
        GPS_PARSER -->|Coordenadas| MAIN
        POWER_MGR -->|Nivel %| MAIN
        UI_MGR -->|Eventos: SOS/Peligro| MAIN
        
        MAIN -->|Trama y Payload| LORA_APRS
    end

    %% Relación Hardware - Software
    MCU -. Ejecuta .-> MAIN
    LORA_APRS -. Controla .-> LORA
    GPS_PARSER -. Lee .-> GPS
    POWER_MGR -. Consulta .-> AXP
    UI_MGR -. Controla/Lee .-> IO
```
