# tDASH-360

**tDASH** is an open-source **ns-3 framework for simulating tiled DASH 360° / VR video streaming over WiFi 6 (802.11ax)** with multiple users. It provides a tiled DASH server and client, a field-of-view (FoV) aware adaptive bitrate (ABR) engine and a multi-user example scenario. You can use it to study the scalability, Quality of Experience (QoE) and energy consumption of WiFi-based multi-user VR streaming systems.

The framework accompanies the paper *"Scalability in WiFi-based Multi-User VR Streaming Systems"* (iMETA 2026).

<p align="center">
  <img src="images/architecture.png" width="700" alt="tDASH-360 architecture">
</p>

---

## Features

- **Tiled 360° DASH streaming** in ns-3. The video is split into a grid of tiles (6×4 = 24 tiles in the example), and each tile is available in several quality levels.
- A **modular client** with these components:
  - network manager and network tracker (throughput estimation)
  - media buffer and media decoder (playout and rebuffering)
  - FoV tracker (head-movement traces and FoV prediction)
  - tile requester
  - ABR engine
- **FoV-aware tile selection.** Every tile gets an FoV score based on where it lies relative to the user's viewport, and a minimum number of tiles (`MinTiles`) is always requested.
- **Multi-user 802.11ax scenario**:
  - users sit in a cinema-style seating layout
  - DL/UL OFDMA through the round-robin multi-user scheduler
  - Minstrel-HT rate adaptation
  - log-distance path loss with shadowing and Nakagami fading
- A **WiFi radio energy model** for the AP and every station.
- **Detailed logging**:
  - per-tile QoE logs
  - per-user PHY metrics (RSSI, SNR, MCS, RU type and data rate)
  - energy traces
  - association events

---

## Requirements

- **ns-3.44** ([download](https://www.nsnam.org/releases/ns-3-44/))
- Linux (Ubuntu 22.04 / 24.04 recommended)
- g++ ≥ 10, CMake ≥ 3.13, Python 3
---

## Repository structure

The `tDASH/` folder has the **same layout as the ns-3.44 source tree**, so its contents can be copied straight into your ns-3 installation.

```
tDASH-360/
├── README.md
├── LICENSE
├── images/                         # figures used in this README
└── tDASH/
    ├── src/
    │   └── applications/
    │       ├── helper/             # TiledDashServerHelper, TiledDashClientHelper
    │       ├── model/              # tiled DASH server, client and its components, ABR engine
    │       └── CMakeLists.txt      # applications module build file (with tDASH files added)
    ├── examples/
    │   └── TestDashV16_42.cc       # multi-user 802.11ax 360° streaming scenario
    └── videoFiles3/                # video tile-size traces, head-movement (pov3.txt)
                                    # and FoV prediction (prediction2_1.txt) files
```

---

## Installation

### 1. Download ns-3.44

### 2. Copy tDASH into ns-3

```bash
git clone https://github.com/RijadF/tDASH-360.git
cp -r tDASH-360/tDASH/* .
```

> **Note** This **replaces** `src/applications/CMakeLists.txt`. If you have changed that file yourself, add the tDASH `.cc` files to `SOURCE_FILES` and the `.h` files to `HEADER_FILES` by hand instead of overwriting it.

The simplest way to run the example script is from the `scratch/` folder, where ns-3 builds it automatically:

```bash
cp examples/TestDashV16_42.cc scratch/
```

### 3. Set the file paths

The example script uses **absolute paths**. Change them to match your own ns-3 location, for example `/home/<user>/ns-allinone-3.44/ns-3.44/`:

| Where in `TestDashV16_42.cc` | Attribute / call | Points to |
|---|---|---|
| Server setup | `ServerFolder` | `.../ns-3.44/videoFiles3` |
| Client helper | `PovFile` | `.../ns-3.44/videoFiles3/pov3.txt` |
| Client configuration | `fovTracker->SetPredictionFile(...)` | `.../ns-3.44/videoFiles3/prediction2_1.txt` |

### 4. Configure and build

```bash
./ns3 configure --enable-examples
./ns3 build
```

---

## Running the simulation

From the `ns-3.44` root folder:

```bash
./ns3 run "TestDashV16_42 --numClients=12 --seed=2 --runNumber=1"
```

### Command-line arguments

| Argument | Default | Description |
|---|---|---|
| `--numClients` | `12` | Number of VR users (stations) |
| `--seed` | `2` | Base random seed. It is also used as `RunId` in the tile log file names |
| `--runNumber` | `1` | ns-3 run number. Different values give independent random realizations |
| `--numAPs` | `1` | Number of access points. Leave at `1`: the script defines one AP position and one channel |
| `--maxClientsPerRow` | `6` | Seats per row in the seating layout |
| `--maxRows` | `4` | Number of rows |

Example with 8 users and a different realization:

```bash
./ns3 run "TestDashV16_42 --numClients=8 --seed=2 --runNumber=5"
```

### Default scenario

| Parameter | Value |
|---|---|
| WiFi | 802.11ax, 5 GHz, channel 42, 80 MHz |
| MAC | DL/UL OFDMA (`RrMultiUserScheduler`), BSRP enabled |
| Rate control | `MinstrelHtWifiManager` |
| TX power / noise figure | 10 dBm / 7 dB |
| Propagation | Log-distance (exponent 3.0, reference loss 46.67 dB @ 1 m) + normal shadowing + Nakagami fading |
| Layout | Cinema seating, 0.8 m between seats, 1.2 m between rows, user height 0.91 m. AP at 2.51 m on the left side |
| Backhaul | Point-to-point link AP ↔ server, 200 Mbps |
| Transport | TCP Veno, 1460 B segments |
| Video | `360_Google_DoodlesSpotlight_Stories_Back_to_the_Moon_6x4_1000_dash`, 60 s, 1 s segments, 6×4 tiles |
| Quality levels | 5.69, 8.53, 12.80, 19.20 Mbps (`QualityRates`) |
| Viewport | 110° × 96°, equirectangular projection |
| Buffer | max 6 s; initial buffering and rebuffering 2 s |
| Throughput estimation | Exponential average, tile-based sampling |
| ABR | `AbrStrategy::Simple` |
| Client start | Staggered: client *i* starts at `2.0 + (i−1)·0.8` s |
| Simulation time | 1050 s (`simTime`) |

To change the ABR algorithm, edit this line in the script:

```cpp
abrEngine->SetAbrAlgorithm(AbrStrategy::Simple);
```

To place users in specific seats, fill in the `customClientPositions` map at the top of the script, for example `{1, ClientPosition(3, 2)}` puts client 1 in row 3, seat 2.

---

## Output files

All files are written to the folder the simulation is run from, which is normally `ns-allinone-3.44/ns-3.44/`.

| File | Content |
|---|---|
| `tile_info_log_<ClientId>_<RunId>.csv` | Per-tile download and QoE log for each client (see below) |
| `wifi6_supported_metrics.csv` | Per-user PHY metrics every 10 ms: RSSI, SNR, MCS, NSS, RU type and utilization, channel, modulation, code rate, PHY data rate, application data rate |
| `wifi6_energy_consumption.csv` | Energy trace for the AP and stations every 10 ms: total and remaining energy, power, voltage, current, WiFi PHY state |
| `final_energy_statistics.csv` | Total energy consumed by each node at the end of the simulation |
| `association_log.csv` | Station association and disassociation events (time, STA MAC, AP MAC) |
| `routing-tables.txt` | Routing tables of all nodes at t = 2 s |

### Tile log: `tile_info_log_<ClientId>_<RunId>.csv`

- `ClientId` is the seat index in the grid, `(row − 1) · maxClientsPerRow + seat`.
- `RunId` is the `--seed` value.

For example, `tile_info_log_2_2.csv` is the user in row 1, seat 2, from a run with seed 2.

Each row is one downloaded tile:

| Column | Description |
|---|---|
| `ClientId`, `RunId` | Client grid index and seed |
| `SegmentId` | Video segment number (1 s segments) |
| `TileId` | Tile index (0–23 for a 6×4 grid) |
| `Quality` | Selected quality level (0 = lowest … 3 = highest) |
| `Size` | Tile size in bytes |
| `FetchDuration` | Download time of the tile [s] |
| `RequestTime`, `ReceiveTime` | Simulation time when the tile was requested and received [s] |
| `BufferLevel` | Buffer level when the tile was received [segments] |
| `FovScore` | FoV score of the tile assigned by the ABR engine (−1 … 3) |
| `IsViewed` | 1 if the tile was in the user's viewport, otherwise 0 |
| `StallDuration` | Stall time [s] |
| `CurrentPlayoutState` | Player state (`PLAYOUT` or `REBUFFERING`) |
| `SimulationTime` | Simulation time of the log entry [s] |
| `CumulativeCompletionTime` | Cumulative download completion time [s] |
| `ActualPlayoutTime` | Played-out video time [s] |
| `BufferDurationLevel` | Buffer level [s] |
| `NetRate` | Estimated network throughput [bit/s] |

The last line of each file is a summary row: `<ClientId>,<RunId>,FINAL,SUMMARY,...,CLIENT_COMPLETED,...`. It marks that the client finished streaming. Skip it when you load the file for analysis, for example in Python:

```python
import pandas as pd
df = pd.read_csv("tile_info_log_2_2.csv")
df = df[df["SegmentId"] != "FINAL"]
```

---

## Citation

**If you use tDASH in your research cite it**:

```bibtex
@inproceedings{fetahagic2026tdash,
  author    = {Rijad Fetahagi{\'c}, Darijo Raca and Ahmed Zahran},
  title     = {Scalability in WiFi-based Multi-User VR Streaming Systems},
  booktitle = {Proceedings of iMETA 2026},
  address   = {Barcelona, Spain},
  year      = {2026}
}
```

---

## License

This project is released under the **GNU General Public License v2.0 (GPL-2.0)**, the same license as ns-3. See [LICENSE](LICENSE).

---

## Contact

**Rijad Fetahagić**
Faculty of Electrical Engineering, University of Sarajevo, Bosnia and Herzegovina

Questions and bug reports are welcome through [GitHub Issues](https://github.com/RijadF/tDASH-360/issues).
