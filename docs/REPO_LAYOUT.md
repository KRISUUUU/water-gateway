# Repository Layout

## Top Level

- `main/` - application entrypoint
- `components/` - modular firmware components
- `docs/` - architecture, operations, testing, and design documentation
- `tests/` - host-side tests and fixtures
- `web/` - static files for embedded web UI

## Components

Each component should:
- own one focused responsibility
- expose a clear public API
- keep implementation details private
- avoid hidden coupling

## Docs

Documentation is expected to evolve with implementation and remain aligned with the code.
