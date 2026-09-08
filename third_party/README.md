# Vendored dependencies

Single-header/amalgamated libraries checked directly into this repo, so the
build has no package-manager dependency.

| File | Library | License | Notes |
|---|---|---|---|
| `json.hpp` | [nlohmann/json](https://github.com/nlohmann/json) | MIT | Vendored in Phase 9 for JSONL logging and the API payload. |
| `lz4.c`, `lz4.h` | [lz4/lz4](https://github.com/lz4/lz4) (`lib/`, `dev` branch) | BSD 2-Clause | Raw-block decompression for `cars5.ccrs` tuning data, via `LZ4_decompress_safe_usingDict` (chunk-to-chunk back-references need the dictionary/history variant, not the plain one-shot API). |
