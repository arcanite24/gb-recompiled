# Crystal Recompiled evidence summaries

This directory holds durable, tracked summaries for completed backlog items.
Raw logs, captures, state dumps, generated projects, saves, and ROM-derived
artifacts remain under the ignored parent `logs/` and `output/` directories.

Create one `<item-id>.md` file when an item is ready for its completion audit:

```md
# <item-id> — <title>

Status: verified

## Inputs and identity

- ROM SHA-256:
- recompiler source identity:
- runtime identity:
- symbols and annotations:
- input and active mods:
- host, compiler, and build profile:

## Commands

List the exact reproduction commands.

## Results

Record machine-verifiable outputs and hashes.

## Gate audit

Address every clause in the backlog item's gate.

## Limits

State untested behavior, independent-oracle boundaries, manual checks, and
claims this evidence does not support.
```

Never include a ROM, save, extracted asset, generated ROM-bearing source,
private path, credential, or upstream unlicensed file in a tracked summary.
