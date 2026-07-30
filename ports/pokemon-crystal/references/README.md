# Local references

`vendor/` contains ignored, pinned checkouts used to understand Pokémon
Crystal and Game Boy Color behavior. `generated/` contains ignored prepared
symbol files and other reproducible reference artifacts.

Use:

```bash
python3 ../scripts/references.py fetch
python3 ../scripts/references.py verify
```

Standalone generation only needs one pinned symbol file. The generation scope
downloads the commit-addressed raw file and validates its checked SHA-256
without creating a Git object database:

```bash
python3 ../scripts/references.py fetch --scope generation
python3 ../scripts/references.py verify --scope generation
```

The default `all` scope remains the developer/oracle setup and fetches every
entry in `sources.lock.json`.

The port consumes the pinned raw RGBDS symbol file directly.
`prepare_symbols.py` is retained only to reproduce the historical
address-only compatibility projection.

The source lock is safe to track. The fetched repositories and generated
symbol files are not part of the port and must not be copied into a public
release without a separate license review.
