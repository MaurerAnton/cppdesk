# AGENTS.md — cppdesk agent instructions

## Build parallelism — RAM-based `-j` rule (mandatory)

Low-RAM environment. NEVER use plain `-j$(nproc)` or unbounded `--parallel`.
Always gate parallelism on **available** RAM (`MemAvailable`).

| Available RAM | Max jobs |
|---------------|----------|
| < 1.5 GB      | WAIT — do not compile, sleep and re-check in a loop |
| >= 1.5 GB and < 3 GB | `-j1` |
| >= 3 GB and < 4.5 GB | `-j2` |
| >= 4.5 GB     | `-j3` (never higher, even if more cores exist) |

### How to check (Linux)

```bash
# GB with two decimals, from MemAvailable in /proc/meminfo
awk '/MemAvailable/ {printf "%.2f\n", $2/1024/1024}' /proc/meminfo
# or: free -m | awk '/Mem:/ {print $7}'
```

### Required pattern before any `cmake --build` / `make` / `ninja`

```bash
# Wait until at least 1.5 GB is available, then pick -j1/-j2/-j3.
while true; do
  AVAIL_MB=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)
  echo "Available RAM: ${AVAIL_MB} MB"
  if [ "$AVAIL_MB" -ge 4608 ]; then JOBS=3; break;
  elif [ "$AVAIL_MB" -ge 3072 ]; then JOBS=2; break;
  elif [ "$AVAIL_MB" -ge 1536 ]; then JOBS=1; break;
  else echo "Less than 1.5GB available, sleeping 60s..."; sleep 60;
  fi
done
echo "Building with -j${JOBS}"
cmake --build build --parallel "$JOBS"
# plain `make`: `make -j"$JOBS"`; plain `ninja`: `ninja -j"$JOBS"`
```

Rules:
- Re-check available RAM right before each build invocation (it changes over time).
- If available RAM drops below 1.5 GB mid-work, stop starting new compiles; wait (`sleep 60` loop) until it recovers.
- Never exceed `-j3`.