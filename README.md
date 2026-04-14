# Taller Integrador: Grupo 3

**Proyecto: Sistema de Monitoreo LoRa para Áreas de Conservación y Parques Nacionales**

**La problemática**

En áreas de conservación, ya sea con topografía montañosa y de alta densidad forestal (como los senderos al rededor del cráter del Volcán Poás) o en extensos humedales navegables (como la reserva de Caño Negro), la cobertura celular tradicional es nula o altamente intermitente. Esta desconexión representa un riesgo significativo para la seguridad de los guardaparques durante sus patrullajes, dificulta la coordinación de guías que lideran grupos de estudiantes o turistas, y limita el monitoreo en tiempo real de embarcaciones enfocadas en el control ambiental y la pesca. 

**La solución propuesta (El tracker)**

Desarrollar un nodo rastreador LoRa APRS portátil, de bajo consumo y alta autonomía. Este dispositivo actuará como una baliza versátil que puede ser llevada en una mochila por un guía terrestre o instalada temporalmente en un bote. Al transmitir la posición mediante radiofrecuencia a un iGate central (ubicado en la casetas o muelles principales), la administración del parque obtiene telemetría en tiempo real sin depender de redes comerciales de telecomunicaciones. 

**Máquina de estados**

A continuación se muestra el diagrama de máquina de estados propuesto para la implementación y desarrollo de este proyecto. 

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
``` mermaid
