# evm-db

Redis-backed EVM state engine with Ethereum-compatible JSON-RPC interface, written in C.

Stores accounts, balances, nonces, storage and contract code in Redis, and exposes an RPC server compatible with standard Ethereum tooling (MetaMask, ethers.js, cast, etc.).

## Features

- **JSON-RPC 2.0** server on HTTP (thread-per-connection, CORS-ready)
- **Redis state backend** — accounts, storage, code, block metadata
- **EIP-1559** transaction support (Legacy + Type 2)
- **256-bit balance arithmetic** (no external bignum dependency)
- **EIP-2028** intrinsic gas calculation
- Configurable chain ID, gas limit, base fee

## Project Structure

```
├── config/evmdb.toml        # Configuration
├── include/evmdb/           # Public headers
├── src/
│   ├── main.c               # Entry point
│   ├── block/tx.c           # RLP decode, tx parsing
│   ├── core/                # Config, hex, logging
│   ├── evm/                 # Executor stub, gas calculation
│   ├── net/tcp.c            # TCP socket tuning
│   ├── rpc/                 # HTTP server, JSON parser, method handlers
│   └── state/               # Redis state, balance math
├── tests/                   # Unit tests
└── scripts/                 # Build & run helpers
```

## Requirements

- C11 compiler (GCC / Clang)
- CMake ≥ 3.20
- Redis server
- **hiredis** (auto-fetched if not found)
- **evmc** v12.0.0 (auto-fetched)
- **secp256k1** v0.5.1 (auto-fetched)

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Debug build with AddressSanitizer / UBSan:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Or use the helper script:

```bash
./scripts/run.sh    # starts Redis if needed, builds, runs
```

## Run

```bash
./build/evmdb                        # default config
./build/evmdb config/evmdb.toml      # custom config
```

The server listens on `0.0.0.0:8545` by default.

## Configuration

Edit `config/evmdb.toml`:

| Key | Default | Description |
|-----|---------|-------------|
| `redis_host` | `127.0.0.1` | Redis host |
| `redis_port` | `6379` | Redis port |
| `redis_password` | — | Redis auth password |
| `redis_db` | `0` | Redis database index |
| `rpc_host` | `0.0.0.0` | RPC bind address |
| `rpc_port` | `8545` | RPC listen port |
| `chain_id` | `100100` | EVM chain ID |
| `log_level` | `2` | 0=error 1=warn 2=info 3=debug |

## Supported RPC Methods

**Implemented:**
`eth_chainId` · `eth_blockNumber` · `eth_gasPrice` · `eth_getBalance` · `eth_getTransactionCount` · `eth_getCode` · `eth_sendRawTransaction` · `eth_getBlockByNumber` · `eth_getBlockByHash` · `eth_feeHistory` · `net_version` · `web3_clientVersion`

**Stubbed (return defaults):**
`eth_call` · `eth_estimateGas` · `eth_getStorageAt` · `eth_getTransactionByHash` · `eth_getTransactionReceipt` · `eth_accounts` · `eth_syncing` · `eth_mining` · `eth_hashrate` · `eth_protocolVersion` · `eth_getBlockTransactionCountByNumber`

## Tests

Requires Redis running on `localhost:6379` (tests use DB 15).

```bash
./scripts/test.sh
# or
cd build && ctest --output-on-failure
```

## Quick Test

```bash
# chain id
curl -s localhost:8545 -X POST \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"eth_chainId","id":1}'

# balance
curl -s localhost:8545 -X POST \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"eth_getBalance","params":["0x0000000000000000000000000000000000000001","latest"],"id":1}'
```

## License

This project is licensed under the GNU Affero General Public License v3.0 or later (`AGPL-3.0-or-later`).

You may use, study, copy, and modify this software, but if you distribute a modified version or make it available over a network, you must also make the corresponding source code available under the same license.
