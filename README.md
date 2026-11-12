# drum5

Bateria de 16 pads con retrigger al estilo del Note Repeat de la MPC2000XL. Plugin VST3 para Windows y Linux.
Hecho con [JUCE 9](https://juce.com), con la misma linea visual que sampler5.

## Que hace (v0.1, etapa 1)

- 16 pads, cada uno con su sample. Se carga con **CARGAR** (en el pad elegido), con clic derecho sobre el pad,
  o arrastrando archivos encima: varios archivos llenan pads seguidos desde donde se sueltan.
- Formatos: WAV, AIFF, FLAC, MP3 y OGG (hasta 30 segundos por sample).
- Los pads se tocan por MIDI desde C5 de FL Studio (nota 60) en adelante, o con el mouse
  (mas arriba en el pad suena mas fuerte).
- Disposicion de MPC: el pad 1 abajo a la izquierda, el 16 arriba a la derecha.
- **VACIAR** saca el sample del pad elegido. **MASTER** es el volumen general.
- El proyecto del DAW guarda la ruta de cada sample. Si un archivo se movio, el pad dice **NO ENCONTRADO**
  y la ruta se conserva.

## Plan

1. Base: 16 pads que suenan, compilacion automatica (esta version).
2. Motor de retrigger: roll a tempo con divisiones y swing de la MPC, LATCH, PRESION, Gate, Accent, Rate Ramp,
   Pitch Var, Humanize y modo LIBRE.
3. Sampler completo por pad, choke, MIDI Learn, rango y canal MIDI, multi-out.
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
