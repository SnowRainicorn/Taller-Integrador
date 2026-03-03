### 1. Sistema de comunicaciones LoRa/APRS

**APRS (Automatic Packet Reporting System)**

**¿Qué es?**:  
Es un protocolo de comunicaciones digitales por radio en tiempo real, diseñado originariamente para la transmisión de información táctica y telemetría.

**¿Para Qué sirve? (Aplicaciones)**:  
Se utiliza para el rastreo de posiciones GPS (como en nuestro caso el Tracker), reportes meteorológicos locales, envío de mensajes de texto cortos y coordinación en operaciones de búsqueda y rescate.

**¿Qué protocolos de comunicación utiliza?**:  
Usualmente utiliza el protocolo AX.25 en la capa de enlace de datos sobre radiofrecuencia. El protocolo AX.25 es casi exclusivamente utilizado en VHF y UHF.

**¿En cuales bandas de frecuencia opera?**:  
En sistemas tipicos como de radioaficionados opera en la banda de 2 metros en VHF (144.390 MHz en nuestra Región 2). Al integrarlo con LoRa para este proyecto, podría utilizar las bandas ISM (Industriales, Científicas y Médicas).

**Componentes clave de una red APRS**
Dentro de la arquitectura de red, este es el rol de cada elemento:

* **Nodo**: Es el dispositivo final (como el Tracker que se programará) que genera y transmite los datos.
* **Digipeater**: Repetidor digital que recibe un paquete de datos y lo retransmite para aumentar el alcance de la señal de radio.
* **iGate**: Puerta de enlace que recibe los paquetes de radio y los inyecta a la red global de internet (APRS-IS).
* **Servidor**: Sistema central que recibe, filtra y distribuye los datos provenientes de los iGates.
* **Cliente final**: Plataformas de visualización donde el usuario puede interactuar con los datos o verlos en un mapa.

---

### 2. Tecnología LoRa

**¿Qué es?**:
Significa _long range_ y es una tecnología de modulación de radiofrecuencia de espectro ensanchado (CSS - *Chirp Spread Spectrum*) diseñada para lograr enlaces de largo alcance con un consumo energético mínimo.

**Frecuencias que utiliza**:  
En la Región 2 de la UIT (América), opera en la banda ISM de 902 a 928 MHz (con frecuencia central en 915 MHz).

**Aplicaciones**:  
Algunas aplicaciones que se pueden mencionar son: internet de las Cosas (IoT), telemetría remota, monitoreo ambiental, agricultura de precisión y redes para ciudades inteligentes.

**Disponibilidad de módulos basados en ESP32 con chip LoRA**:  
En el mercado existe una alta disponibilidad y son económicos. Placas de desarrollo como las TTGO T-Beam o Heltec LoRa32 integran en una sola placa el microcontrolador ESP32, el transceptor LoRa (como el SX1276) y módulos GPS, lo cual agiliza la construcción de los nodos.

**Capa física y enlace (Modelo OSI)**
Según la investigación inicial realizada, los parámetros que deben configurarse en el firmware del Traker son:

* **Spreading Factor (SF)**:  
Factor de ensanchamiento. Determina la duración del símbolo. Hay que tener en cuenta que un SF mayor otorga más alcance y resistencia al ruido, pero reduce significativamente la velocidad de transmisión de datos y aumenta el consumo de batería.

* **Bandwidth (BW)**:  
Ancho de banda. Es el rango de frecuencias que ocupa la señal modulada (alrededor de 125 kHz o 250 kHz).

* **Coding Rate (CR)**:  
Tasa de codificación. Es la proporción de bits redundantes añadidos a la carga útil para la corrección de errores (*Forward Error Correction*). Valores típicos son 4/5.

* **Sync Word**:  
Palabra de sincronización. Un valor hexadecimal que permite al receptor diferenciar entre distintas redes LoRa, filtrando mensajes que no pertenecen a su sistema.

* **ADR (Adaptive Data Rate)**:  
Tasa de datos adaptativa. Un mecanismo inteligente mediante el cual la red optimiza dinámicamente el SF y la potencia de transmisión del nodo según la calidad del canal.

---

### 3. Legislación Costarricense

Luego de revisar el Cuadro Nacional de Atribución de Frecuencias del PNAF bajo el Decreto N° 44010-MICITT y el Alcance N° 99 a la gaceta N° 95 del 30 de mayo 2023, esta es la normativa que aplicaría:

**Bandas de Frecuencias en CR**:  
El sistema LoRa operará en el segmento de uso libre tipificado para aplicaciones ISM. En Costa Rica, este rango es de **902 MHz a 928 MHz**.

**Permisos requeridos para operar**:  
Al utilizar bandas de uso libre para este tipo de aplicaciones digitales, **no se requiere solicitar una concesión de frecuencias**. Sin embargo, es un requisito legal que los módulos transmisores pasen por un proceso de homologación para garantizar que no causen interferencias perjudiciales.

**Trámites y Mapa de tiempo**:  
El trámite a realizar es la **Homologación de Equipos Terminales ante la SUTEL**. Se debe presentar un formulario de solicitud junto con las especificaciones técnicas del fabricante (y certificaciones internacionales como FCC o CE). Una vez que el expediente ingresa completo, la SUTEL tiene un plazo estimado de **30 días naturales** para emitir la resolución de homologación.

**Clases de Emisión**:  
La modulación CSS de LoRa se enmarca en emisiones de datos con modulación digital. Según los designadores de la UIT, estos sistemas suelen clasificarse bajo tipos de emisión como **F1D** o **G1D** (transmisión de datos mediante modulación de frecuencia o fase).

**PIRE permitida**:  
Para la banda de 902 a 928 MHz, la Potencia Isótropa Radiada Equivalente (PIRE) máxima permitida en Costa Rica es de **30 dBm (1 Watt)**. Es fundamental que las librerías del firmware no configuren el chip de radio para exceder este límite legal.
