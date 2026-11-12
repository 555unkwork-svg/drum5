# drum5

Bateria de 16 pads con retrigger al estilo del Note Repeat de la MPC2000XL. Plugin VST3 para Windows y Linux.
Hecho con [JUCE 9](https://juce.com), con la misma linea visual que sampler5.

## Que hace (v0.3, etapa 3)

### Pads
- 16 pads, cada uno con su sample. Se carga con **CARGAR** (en el pad elegido), con clic derecho sobre el pad,
  o arrastrando archivos encima: varios archivos llenan pads seguidos desde donde se sueltan.
  La carga es en segundo plano (el pad dice **CARGANDO...**), asi los samples largos no traban la interfaz.
- Formatos: WAV, AIFF, FLAC, MP3 y OGG (hasta 30 segundos por sample).
- Se tocan por MIDI desde C5 de FL Studio (nota 60) en adelante, o con el mouse (mas arriba suena mas fuerte).
- Disposicion de MPC: el pad 1 abajo a la izquierda, el 16 arriba a la derecha.
- El proyecto del DAW guarda la ruta de cada sample. Si un archivo se movio, el pad dice **NO ENCONTRADO**.

### Sonido de cada pad (columna del medio)
- Forma de onda del sample (clic para escucharlo). Lo que corta el DECAY se ve apagado.
- **VOL**, **PAN**, **PITCH** (+-24 semitonos) y **DECAY** (100 % = suena entero; menos, se apaga antes con un fundido).
- **REVERSE**, **MUTE** y **MONO** (un golpe nuevo corta el anterior del mismo pad).
- **CHOKE**: grupos 1 a 8; los pads del mismo grupo se cortan entre si (hi-hat cerrado y abierto).
- **SALIDA**: Main o Salida 2 a 16. En FL se conectan al mixer desde las entradas y salidas del plugin.
  Si el DAW no activa una salida, ese pad suena por Main.
- **NOTA** y **LEARN**: tocar LEARN y despues una nota del controlador la asigna al pad.

### MIDI
- **CANAL**: todos o uno solo (1 a 16).
- **RANGO**: las notas fuera del rango se ignoran.

### Roll (como el Note Repeat de la MPC2000XL), ajustes por pad
- **RETRIGGER** (viene apagado): mantener el pad repite el golpe. El primero suena al instante; los siguientes caen en la grilla
  del proyecto (con el proyecto parado, siguen el tempo del proyecto desde el primer golpe).
  Si la grilla cae pegada al primer golpe (menos de 30 % de un paso), ese punto se saltea para no sonar doble.
- **RATE**: las divisiones de la MPC: 1/8, 1/8T, 1/16, 1/16T, 1/32 y 1/32T.
- **MODO**: SYNC (grilla) o LIBRE (golpes por segundo, de 1 a 30 Hz, sin grilla).
- **LATCH**: el roll sigue al soltar el pad; se corta tocandolo otra vez.
- **GATE**: largo de cada golpe respecto del intervalo (100 % = suena entero).
- **ACCENT**: el primer golpe de cada grupo de 2, 3 o 4 suena normal y el resto mas suave segun el %.
- **RATE RAMP**: despues de 1, 2 o 4 tiempos pasa a la division siguiente (1/16 a 1/32, 1/8T a 1/16T...).
  En 1/32 y 1/32T no hay division mas rapida. En LIBRE duplica la velocidad.
- **PITCH VAR**: cada golpe sube (o baja) de a poco hasta los semitonos elegidos, a los que llega en 1 compas.

### General
- **SWING** de 50 a 75 %, como en la MPC: solo en 1/8 y 1/16, atrasa los pasos pares.
- **HUMANIZE**: pequenas variaciones de fuerza y de tiempo (siempre un poco tarde, hasta 8 ms) en las repeticiones.
- **PRESION**: apagado, todas las repeticiones suenan con la fuerza del primer golpe. Encendido, la presion del pad
  (aftertouch de canal o polifonico) manda la fuerza de las repeticiones, como en la MPC.
- **VEL**: cuanto sigue cada golpe a la velocity (0 % = siempre a tope).
- **VOCES**: cuantos golpes suenan a la vez (1 a 64); al pasarse se corta el mas viejo.
- **MASTER**: volumen general.

## Plan

1. Base: 16 pads que suenan, compilacion automatica. (hecho)
2. Motor de retrigger. (hecho)
3. Sampler completo por pad, choke, MIDI Learn, rango y canal MIDI, multi-out. (esta version)
4. Browser de samples con deteccion de tipo por nombre y carpeta, categorias propias y exportar kit (carpeta o zip).
5. Presets y pulido visual.

## Como conseguir el plugin

Cada vez que se sube codigo, GitHub lo compila solo:

1. Pestana **Actions** del repositorio, ultimo "Compilar plugin".
2. Abajo, en **Artifacts**, bajar `Drum5-VST3-Windows`.
3. Descomprimir y copiar la carpeta `drum5.vst3` a `C:\Program Files\Common Files\VST3\`.
4. En el DAW, volver a escanear plugins.

`Drum5-Standalone-Windows` es el mismo plugin como programa suelto. Para Linux estan `Drum5-VST3-Linux`
y `Drum5-Standalone-Linux` (el VST3 va en `~/.vst3/`).
