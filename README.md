# Disk Analyzer — Monitor de disco en C++23

Herramienta CLI de análisis de disco en **C++23** que lee estadísticas de E/S de disco directamente desde `/proc/diskstats`. Compatible con salida en tabla o JSON, monitorización en tiempo real de ancho de banda y filtrado por dispositivo.

## Features

- ✅ **Estadísticas de disco**: lecturas/escrituras completadas, sectores, tiempos de E/S desde `/proc/diskstats`
- ✅ **Monitorización en tiempo real**: modo `--monitor` con refresco periódico y cálculo de ancho de banda
- ✅ **Información de dispositivo**: modelo, tamaño, tipo (HDD/SSD) desde `/sys/block`
- ✅ **Salida estructurada JSON**: integrable con scripts y herramientas externas
- ✅ **Filtrado por dispositivo**: limita la salida a un dispositivo específico con `--filter`
- ✅ **Arquitectura RAII**: `DiskAnalyzer` con manejo de errores mediante `std::expected`
- ✅ **C++23 moderno**: concepts, ranges, string_view, std::println, std::format
- ✅ **Logging**: spdlog estructurado
- ✅ **Tests**: Catch2 v3 con unitarios, integración y benchmarks

## Stack tecnológico

| Componente   | Tecnología                        |
|--------------|------------------------------------|
| Lenguaje     | C++23 (GCC 14)                    |
| Build        | CMake 3.28+ / Ninja               |
| Argumentos   | CLI11 (FetchContent)              |
| JSON         | nlohmann-json (FetchContent)      |
| Logging      | spdlog (FetchContent)             |
| Tests        | Catch2 v3 (FetchContent)          |
| APIs Linux   | /proc/diskstats, /sys/block       |
| Contenedor   | Docker multi-stage (gcc:14)       |

## Requisitos

- **Compiler**: GCC 14+ (C++23 completo: std::expected, std::println)
- **CMake**: ≥ 3.28
- **Sistema**: Linux (lee /proc/diskstats y /sys/block)

## Compilación

```bash
# Usando CMake presets (recomendado)
cmake --preset debug
cmake --build --preset debug

# O manualmente
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Ejecutar tests
ctest --preset debug
```

## Uso

```bash
# Mostrar estadísticas de todos los discos
./build/debug/disk-analyzer

# Mostrar información de dispositivos
./build/debug/disk-analyzer --info

# Modo monitor en tiempo real
./build/debug/disk-analyzer --monitor

# Salida JSON
./build/debug/disk-analyzer --json

# Filtrar por dispositivo
./build/debug/disk-analyzer --filter sda

# Monitor cada 3 segundos
./build/debug/disk-analyzer --monitor --interval 3

# Monitor en JSON con filtro
./build/debug/disk-analyzer --monitor --json --filter nvme0n1
```

## Docker

```bash
docker build -t disk-analyzer .
docker run --rm --privileged disk-analyzer
```

## API /proc

| Archivo                | Descripción                          |
|------------------------|--------------------------------------|
| `/proc/diskstats`      | Estadísticas de E/S de disco         |
| `/sys/block/*/size`    | Tamaño del dispositivo en sectores   |
| `/sys/block/*/queue/rotational` | Tipo: 0=SSD, 1=HDD       |
| `/sys/block/*/device/model`     | Modelo del dispositivo     |
