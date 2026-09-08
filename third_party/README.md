# Vendored dependencies

Single-header/amalgamated libraries checked directly into this repo, so the
build has no package-manager dependency.

| File | Library | License | Notes |
|---|---|---|---|
| `json.hpp` | [nlohmann/json](https://github.com/nlohmann/json) | MIT | Vendored in Phase 9 for JSONL logging and the API payload. |

LZ4 decompression (Phase 6) links against the system/vendored `liblz4`
instead of a vendored header — see the top-level README once Phase 6 lands.
