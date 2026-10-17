# Imported FM6 upstream

This directory is populated by:

```bash
python3 tools/import_felucca.py
```

Generated/imported files are intentionally ignored by Git:

- `fm6_core.c`
- `felucca_tables.h`
- `REVISION.txt`

The importer pins Felucca revision `5738617968a22b5d4c0d3470cfcc95559052f446` so every benchmark run can reproduce the same FM6 core and fixed-point tables.
