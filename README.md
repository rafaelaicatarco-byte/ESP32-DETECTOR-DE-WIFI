# WiFi Scanner SACOMPU para ESP32

Este proyecto es un escáner de redes Wi‑Fi desarrollado para una placa ESP32 con pantalla TFT LCDWiki de 2.8 pulgadas y control táctil por GPIO. El sketch se integra en un solo archivo `esp32.ino` y ofrece una interfaz gráfica con varias pantallas para escaneo, detalle de red, laboratorio RSSI y gestión de registros en SD.

El sistema está pensado para mostrar redes detectadas, su intensidad de señal (RSSI), canal, dirección MAC, tipo de seguridad, estado oculto y estadísticas relevantes. También permite guardar un historial de redes en un archivo CSV en la tarjeta SD.

## Características principales

- Escaneo de redes Wi‑Fi en modo estación ESP32.
- Ordenamiento de redes por intensidad de señal (RSSI).
- Visualización de redes en pantalla con scroll vertical.
- Pantalla de detalle con información de una red seleccionada.
- Pantalla de laboratorio RSSI con gráfico de historial.
- Gestión de registros en tarjeta SD.
- Calibración manual del panel táctil mediante cruces en pantalla.
- Interfaz gráfica completa para navegación entre pantallas.

## Hardware requerido

- Placa ESP32 DevKit compatible con ESP32.
- Pantalla TFT LCDWiki 2.8" con controlador ILI9341.
- Módulo táctil resistivo o equivalente mediante pines GPIO bit-bang.
- Tarjeta SD opcional para almacenamiento log.
- Cableado SPI y GPIO según el esquema del sketch.

## Pinout usado en el código

El archivo define estos pines físicos:

```cpp
#define TFT_CS    15
#define TFT_DC     2
#define TFT_SCK   14
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_BL    21
#define TOUCH_CS   33
#define TOUCH_CLK  25
#define TOUCH_DIN  32
#define TOUCH_DOUT 39
#define TOUCH_IRQ  36
#define SD_CS      5
#define SD_SCK    18
#define SD_MISO   19
#define SD_MOSI   23
```

Gracias a este esquema, el ESP32 comunica:

- la pantalla TFT por SPI;
- el módulo táctil por GPIO bit-bang;
- y la tarjeta SD por SPI.

## Librerías necesarias

El sketch usa librerías de Arduino/ESP32:

- `SPI.h`
- `Adafruit_GFX.h`
- `Adafruit_ILI9341.h`
- `WiFi.h`
- `SD.h`

Además, se requieren las librerías de Adafruit para la pantalla ILI9341 y el framebuffer gráfico:

- `Adafruit_GFX`
- `Adafruit_ILI9341`

En el entorno Arduino IDE, instala el soporte para placas ESP32 y las librerías antes mencionadas.

## Estructura de pantallas

El proyecto organiza la interfaz con constantes de pantalla:

```cpp
#define SCR_MAIN  0
#define SCR_SCAN  1
#define SCR_DET   2
#define SCR_LAB   3
#define SCR_LOG   4
```

### Pantalla principal

Muestra tres botones:

- `SCAN`
- `LAB`
- `LOG`

La pantalla principal es la navegación inicial del sistema.

### Pantalla de escaneo

Muestra las redes Wi‑Fi detectadas, ordenadas por señal, con:

- SSID (nombre de la red),
- BSSID (MAC),
- canal,
- seguridad,
- RSSI y su calidad,
- barra de señal,
- y indicadores de scroll.

### Pantalla de detalle

Cuando el usuario toca una red en la lista de escaneo, se abre la pantalla de detalle donde se muestran:

- SSID,
- BSSID,
- canal,
- tipo de seguridad,
- RSSI actual,
- rango mínimo y máximo,
- promedio,
- número de escaneos.

### Pantalla de laboratorio RSSI

La pantalla de laboratorio activa una adquisición periódica de la mejor señal Wi‑Fi encontrada en un intervalo de 3 segundos. Se guardan los valores más altos y se dibuja un gráfico de historial de intensidad RSSI.

### Pantalla de log

La pantalla de log visualiza el estado de la tarjeta SD y ofrece la opción de guardar datos CSV. Si la SD está disponible, se abre un archivo:

```text
wifi_log_<session>.csv
```

La línea de encabezado es:

```text
time,ssid,bssid,rssi,channel,security,scans
```

## Base de datos lógica y estructuras

La estructura principal del proyecto es:

```cpp
struct WiFiNet {
    String ssid;
    String bssid;
    int16_t rssi;
    int16_t rssiMin;
    int16_t rssiMax;
    float rssiSum;
    uint32_t scans;
    int32_t channel;
    wifi_auth_mode_t auth;
    bool hidden;
};
```

Representa una red Wi‑Fi con todos los campos que requieren la interfaz gráfica y el registro de datos.

## Funciones principales

### Control del touch

Las funciones relacionadas con el táctil son:

- `tXfer()`
- `tAxis()`
- `tAvg()`
- `tInit()`
- `tTouched()`
- `tMap()`
- `tWait()`

Se usa un protocolo bit-bang para intercambiar datos con el controlador táctil y luego se hace el mapeo de coordenadas a la pantalla TFT.

### Calibración

La función `runCalibration()` permite tocar cuatro cruces en pantalla para aprender los rangos del receptor táctil y ajustar la escala X e Y. Esto es importante porque las pantallas táctiles resistivas requieren una lectura muy precisa para que el toque se corresponda con la pantalla.

### Interfaz gráfica

Funciones de dibujo:

- `drawBtn()`
- `drawHeader()`
- `drawRSSI()`
- `drawScrollBar()`
- `drawScrollBtns()`

La interfaz se dibuja en su totalidad con el controlador `Adafruit_ILI9341`.

### Escaneo de Wi‑Fi

La función `doScan()` ejecuta el escaneo de redes con:

```cpp
int n = WiFi.scanNetworks(false, false);
```

La lista de redes se almacena en el arreglo `nets[]` y se ordena con `sortNets()` para presentar las redes por mayor RSSI primero.

### Laboratorio RSSI

La función `wifiLabScan()` activa un examen periódico de señal y guarda los mejores valores en un buffer circular `rssiH[]`, usando un índice circular.

## Gestión de tarjeta SD

El proyecto habilita el almacenamiento en SD con la macro:

```cpp
#define ENABLE_SD 1
```

Las funciones de SD son:

```cpp
bool sdB()
void sdE()
void saveSD()
```

La lectura del puerto SPI se alterna entre la pantalla TFT y la tarjeta SD:

1. la tarjeta SD usa buses `SD_SCK`, `SD_MISO`, `SD_MOSI`, `SD_CS`;
2. la pantalla TFT usa `TFT_SCK`, `TFT_MISO`, `TFT_MOSI`, `TFT_CS`.

Esto es necesario porque la pantalla y la tarjeta SD comparten el mismo SPI físico en distintos canales.

## Flujo del programa

### En `setup()`

El arranque hace lo siguiente:

1. inicializa el puerto serial;
2. prende la retroiluminación de la pantalla;
3. inicia la comunicación SPI con la TFT;
4. inicializa la pantalla y el panel táctil;
5. muestra una pantalla splash;
6. espera durante 3 segundos para detectar si el usuario toca la pantalla y entra en modo de calibración;
7. si toca la pantalla, ejecuta `runCalibration()`;
8. activa el modo estación Wi‑Fi;
9. inicia la tarjeta SD si está habilitada;
10. llama a `drawMain()` para entrar a la interfaz principal.

### En `loop()`

El bucle principal:

1. verifica si hay toque en pantalla;
2. mapea la coordenada del toque al sistema TFT;
3. entrega la coordenada al handler `handleTouch()`;
4. si el modo laboratorio está activo y transcurrieron 3 segundos, ejecuta `wifiLabScan()`.

## Modo de uso

1. Enciende el equipo y espera el splash.
2. Si aparece una pantalla de calibración, toca las cuatro cruces en orden.
3. En la pantalla principal, elige `SCAN` para buscar redes.
4. Selecciona una red de la lista para abrir el detalle.
5. Usa `LAB` para activar un laboratorio de RSSI gráfico.
6. Usa `LOG` para guardar el escaneo actual en un archivo CSV.

## Consideraciones importantes

- La detección de redes ocultas se marca como `[Hidden]` o `Unknown` si el SSID no es visible.
- El listado se ordena por RSSI, de mayor a menor.
- El historial de laboratorio es un buffer de 120 muestras.
- La tarjeta SD debe estar formateada en formato compatible con `SD.h`.
- La calibración de la pantalla táctil es una etapa crítica; si el tacto no coincide con los botones, conviene repetir la calibración.

## Recomendaciones para compilar

1. Instala Arduino IDE o PlatformIO con soporte para ESP32.
2. Instala las librerías `Adafruit_GFX`, `Adafruit_ILI9341` y la base de ESP32.
3. Selecciona la placa `ESP32 Dev Module` (o la tarjeta que corresponda al modelo).
4. Define el puerto serial correcto para cargar el firmware.
5. Verifica que el pinout de los módulos coincida con la definición del sketch.

## Proyecto

Este código es un ejemplo completo de un escáner Wi‑Fi gráfico para ESP32 con una interfaz visual en pantalla TFT, módulo táctil, laboratorio de señal y registro en SD.

## Autor y propósito

El código está diseñado como una estación de análisis visual de redes Wi‑Fi y una pequeña aplicación de laboratorio orientada a observación de la intensidad de las señales sobre una pantalla LCD bajo una interfaz de usuario local.
