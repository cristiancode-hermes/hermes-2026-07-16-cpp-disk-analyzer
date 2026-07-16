# Architecture — Disk Analyzer

```mermaid
flowchart LR
    A[CLI args<br/>CLI11] --> B[main.cpp]
    B --> C[DiskAnalyzer]
    C --> D[/proc/diskstats]
    C --> E[/sys/block]
    C --> F[JSON output<br/>nlohmann_json]
    B --> G[Table output<br/>std::println]
    B --> H[Monitor mode<br/>std::thread]
```

## Flujo de datos

1. **Entrada**: CLI11 parsea argumentos (`--json`, `--monitor`, `--filter`, etc.)
2. **Lectura**: `DiskAnalyzer::read_stats()` abre y parsea `/proc/diskstats`
3. **Procesamiento**: Los datos se convierten a `DiskStats` structs con campos tipados
4. **Monitor**: En modo `--monitor`, se toman snapshots periódicos y se calculan deltas de ancho de banda
5. **Salida**: Los datos se muestran en tabla formateada o JSON estructurado

## Estructura de clases

```
DiskAnalyzer
├── read_stats()       → Result<vector<DiskStats>>
├── get_device_info()  → Result<DeviceInfo>
├── snapshot()         → Result<vector<DiskStats>>
├── calculate_bandwidth() → vector<BandwidthDelta>
└── list_devices()     → vector<string>

DiskStats
├── device, reads/writes, sectors, time, IOPS
├── to_json() → json
└── read_bytes() / write_bytes()

BandwidthDelta
├── device, read_kbps, write_kbps, read_iops, write_iops

DeviceInfo
├── device, model, size_sectors, rotational
```

## Decisiones técnicas

- **RAII**: `DiskAnalyzer` usa PIMPL con `unique_ptr<Impl>` para ocultar la implementación
- **Error handling**: `std::expected<T, Error>` en lugar de excepciones o códigos de retorno
- **FetchContent**: Dependencias obtenidas en tiempo de build — no requiere gestor de paquetes
- **C++23**: `std::println`, `std::format`, `std::expected`, `std::ranges`
- **Separación library/executable**: El código de negocio está en una library estática; `main.cpp` solo contiene CLI y presentación. Los tests linkean contra la library.
