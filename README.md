# CycFlow

A C++ framework for collecting, processing, and transmitting streaming data. The project consists of a core library (`CycLib`), a reusable Qt6 chart widget (`ChartWidget`), and two Qt6 GUI applications for data visualization and network reception.

## 📦 Components

* **CycLib (Core):** A thread-safe C++17 library providing dynamic record schemas (`RecRule`), a circular ring buffer (`RecBuffer`), asynchronous and zero-copy reader/writer pipelines, file serialization (CBF/CSV), and TCP streaming built on `asio`.
* **ChartWidget:** A standalone Qt6 charting component (model/view/delegate, themed header, panning, zoom, cursor) used by both GUI applications.
* **CbfView:** A Qt6 GUI application for offline viewing of `.cbf` (Cyc Binary Format) files.
* **CycBufReceiver:** A Qt6 MDI tool that receives `CycFlow` buffers over TCP and visualizes them in real time.
* **CycTestServer:** A small console application that generates synthetic sensor data and serves it through the singleton `TcpServerManager` — handy for testing `CycBufReceiver`.

---

## 🧩 Core Architecture & Modules

The **CycLib** core library is built around thread safety, zero-copy data paths, and a runtime-defined record layout.

* **Dynamic Schema & Fast Field Access:** Instead of hardcoding C++ structs, [RecRule](CycLib/Core/RecRule.h) defines record layouts at runtime from a list of [PAttr](CycLib/Core/PAttr.h) attributes. Fields are sorted and aligned automatically. A Parameter Registry ([PReg](CycLib/Core/PReg.h)) caches attribute offsets, giving O(1) lookups; the `Record` accessors inline through `RecRule::getOffsetById` so typed gets/sets collapse to a couple of loads + a store.
* **Bit Fields:** Integer attributes can declare named bits via the `PAttr(name, type, bitDefs)` constructor. Numeric strings in `bitDefs` skip a number of bits, non-numeric strings register a named bit through PReg. Access is via `Record::setBit(id, val)` / `Record::getBit(id)`.
* **Ring Storage:** [RecBuffer](CycLib/Core/RecBuffer.h) wraps a `DynamicChunkBuffer`, exposes both copy and zero-copy reads, and uses the minimum reader cursor across registered clients as the writer backpressure point.
* **Writer / Reader Strategies:** A common base interface ([RecordWriterBase](CycLib/RecordWriterBase.h), [RecordReaderBase](CycLib/RecordReaderBase.h)) is implemented by two strategies:

  | Class | Strategy | Concurrency | Use when… |
  |-------|----------|-------------|-----------|
  | [RecordWriter](CycLib/RecordWriter.h)   | Double-buffered, async worker thread | **Multi-producer safe** — several writers may target the same `RecBuffer` | Producer must never stall — decouple from buffer backpressure; or several producers share one buffer |
  | [RecordWriterZC](CycLib/RecordWriterZC.h) | Single-buffered, synchronous, one copy total | **Single-producer only** — must be the sole writer for its `RecBuffer` | Lower latency / simpler flow; caller may block on full buffer |
  | [RecordReader](CycLib/RecordReader.h)   | Double-buffered, async worker thread | Multiple readers per buffer | Slow consumer (disk I/O, network) — isolate processing time |
  | [RecordReaderZC](CycLib/RecordReaderZC.h) | Zero-copy, synchronous, direct pointers into the ring | Multiple readers per buffer | Fast consumer (UI, in-process pipeline) |

  Consumers (`CsvWriter`, `CbfWriter`, …) and producers (`TcpDataReceiver`, …) pick the strategy with a `UseReader<…>{}` / `UseWriter<…>{}` tag in their constructor; the default is the buffered variant.

  > ⚠️ **Writer concurrency:** if more than one producer needs to write into the same `RecBuffer`, use `RecordWriter` for **every** producer. `RecordWriterZC` bypasses the synchronisation that makes multi-writer use safe and must be the **only** writer attached to its buffer.

* **Backpressure & overrun behavior (`blockOnFull`):** Both writer classes accept a `blockOnFull` flag (default `true`).

  | `blockOnFull` | Writer behavior when buffer is full | Reader behavior when it falls behind |
  |---|---|---|
  | `true` | Writer stalls until readers free space — no data loss | Readers always see every record; they directly pace the writer |
  | `false` | Writer overwrites oldest data immediately — never stalls | Readers that lag by more than the buffer capacity are automatically skipped forward to the oldest available record |

  When a reader is skipped forward a warning is emitted via `LOG_WARN` and the count of dropped records is logged. Within a recovered batch all records are always internally consistent (no torn records).

  > ⚠️ **ZC reader + `blockOnFull=false` — torn reads:** `RecordReaderZC` returns a **direct pointer into the ring buffer**. With `blockOnFull=false` the writer ignores backpressure, so it can overwrite the memory the caller is currently reading. This means **data integrity within a batch is not guaranteed** when a ZC reader operates against a non-blocking writer under heavy concurrent load.
  >
  > `RecordReader` is immune to this because it **copies** each batch into a private buffer under a shared lock before returning it. Choose the right combination for your use case:
  >
  > | | `blockOnFull=true` | `blockOnFull=false` |
  > |---|---|---|
  > | **RecordReader** | Safe — writer stalls | Safe — reader skips, copies protect data |
  > | **RecordReaderZC** | Safe — writer stalls, memory pinned | ⚠️ Torn reads possible under high write load |

* **File I/O & Serialization:** [CbfWriter](CycLib/Cbf/CbfWriter.h) / [CbfReader](CycLib/Cbf/CbfReader.h) for the Cyc Binary Format and [CsvWriter](CycLib/Csv/CsvWriter.h) for CSV. Both operate via background batching.
* **Networking:** Built on `asio`. [TcpServer](CycLib/Tcp/TcpServer.h) registers buffers under a name (with configurable batch size) and spawns [TcpDataSender](CycLib/Tcp/TcpDataSender.h) sessions; [TcpDataReceiver](CycLib/Tcp/TcpDataReceiver.h) connects, negotiates the `RecRule` schema, and streams records into a local buffer. [TcpServerManager](CycLib/Tcp/TcpServerManager.h) is a singleton that owns one `io_context` + `TcpServer` for the whole process. `unregisterBuffer()` closes the matching sessions when a buffer goes away.
* **Default constructor + `init()`:** `RecBuffer`, `RecordWriter[ZC]`, `RecordReader[ZC]`, `RecordConsumer`, and `RecordProducer` all support default construction followed by `init()`, so they can be embedded as members and initialised later.

---

## 🖥️ GUI Applications

### CycBufReceiver (Network Streaming Viewer)
A Multi-Document Interface viewer that connects to one or more `CycFlow` TCP servers.

* **Auto-Discovery:** Queries the server for the list of buffers and their `RecRule` schemas.
* **Dynamic UI:** Builds plots and data tracks from the received schema — arrays, floats, integers, bit fields, and timestamps are rendered with appropriate value formatters.
* **Live Mode:** The cursor follows the newest sample as data arrives; toggle off to inspect history.
* **Chart Controls:** Toolbar buttons for X/Y zoom, right-mouse panning, header context menu, cursor timestamp readout. Updates are throttled by period rather than record count to keep the UI smooth at high data rates. Unified dark / light theming.
* **Bit-Field Visualization:** Named bits packed into integer parameters are plotted as individual digital tracks.

<img width="1920" height="1032" alt="CycBuffReceiver" src="https://github.com/user-attachments/assets/c17a52f1-ec15-42c2-84f5-9555967f5009" />

*Real-time visualization of the data generated by `CycTestServer`, plotting Counter, Voltage, Current, ADC, Pressure, and bit-field channels.*

### CbfView (Binary File Viewer)
An offline viewer for `.cbf` files.

* Supports OS-level file associations.
* Overlay of multiple graphs and batch data updates for fast loading of long sessions.
* Shares the same `ChartWidget` rendering and navigation as `CycBufReceiver`.

### CycTestServer
A console data generator that publishes a multi-channel sensor schema (counter array, named bit register, float / int / double sensor values) on TCP port 5000 via the singleton `TcpServerManager`. Use it together with `CycBufReceiver` to exercise the full pipeline.

---

## 🛠️ Build Instructions

### Prerequisites
* **C++17** compatible compiler.
* **CMake** 3.14 or higher.
* **asio** — fetched automatically via CMake `FetchContent` (no system install required).
* **Qt 6.0+** — only required for the GUI applications. The core library and tests build without Qt; if Qt is not detected, the Qt projects are skipped with a warning.

### Building the Project
```bash
git clone https://github.com/griglapidus/CycFlow.git
cd CycFlow

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build --config Release
```

Build artifacts are placed under `build/<Compiler>_<Arch>/{bin,lib,include}/<Config>` so multiple toolchains can coexist in the same source tree.

### CMake Options
* `-DBUILD_SHARED_LIBS=ON` — build the core as a shared library.
* `-DBUILD_TESTING=ON` — build the test suite (GoogleTest unit tests for Core / CBF / CSV / TCP / zero-copy reader+writer) and `CycTestServer`.
* `-DCYCLIB_COPY_ASIO_HEADERS=ON` *(default)* — copy fetched asio headers to the install/header destination so downstream consumers can `#include <asio.hpp>` without re-fetching.

### Using CycLib in another CMake project
`CycLib` can be embedded via `add_subdirectory` or consumed from an install tree via `find_package(CycStruct)` — it exports the `CycStruct::CycLib` target.

---

## 🚀 Usage Examples

The following examples show how to set up a data generator, serve it over TCP, receive it, and save it to disk.

### 1. Data Source & TCP Server

```cpp
#include <asio.hpp>
#include <thread>
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/PReg.h"
#include "RecordWriterZC.h"
#include "Tcp/TcpServerManager.h"

using namespace cyc;

// 1. Define the data schema (plain fields + a bit-field register)
std::vector<PAttr> attrs = {
    PAttr("Counter",  DataType::dtInt8,   2),                                  // array of 2
    PAttr("BitReg",   DataType::dtUInt8,
          std::vector<std::string>{"txReady", "rxReady", "4", "errFlag"}),     // named bits
    PAttr("Voltage",  DataType::dtFloat,  1),
    PAttr("Pressure", DataType::dtDouble, 1),
};
RecRule rule;
rule.init(attrs);

// Cache attribute IDs for O(1) access
int idCounter  = PReg::getID("Counter");
int idVoltage  = PReg::getID("Voltage");
int idPressure = PReg::getID("Pressure");
int idErr      = PReg::getID("errFlag");

// 2. Create a circular buffer and a zero-copy writer
auto buffer = std::make_shared<RecBuffer>();
buffer->init(rule, 10000);

RecordWriterZC writer;
writer.init(buffer, /*batchCapacity*/ 2000);

// 3. Start the singleton TCP Server and register the buffer
auto& mgr = TcpServerManager::instance();
mgr.start(/*port*/ 5000);
mgr.server()->registerBuffer("SensorStream", buffer, /*batchSize*/ 500);

// 4. Generate and push data
while (true) {
    Record rec = writer.nextRecord();

    rec.setInt8(idCounter, 1);          // index 0
    rec.setInt8(idCounter, 2, 1);       // index 1
    rec.setFloat(idVoltage, 12.5f);
    rec.setDouble(idPressure, 101.3);
    rec.setBit(idErr, false);

    writer.commitRecord();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
```

### 2. Receiving Network Streams (Client)
The schema (`RecRule`) is negotiated automatically and reconstructed on the client.

```cpp
#include "Tcp/TcpDataReceiver.h"
#include "RecordReaderZC.h"

// Receiver with a local 10 000-record buffer
cyc::TcpDataReceiver receiver(/*bufferCapacity*/ 10000);

if (receiver.connect("127.0.0.1", 5000, "SensorStream")) {
    auto clientBuffer = receiver.getBuffer();

    // Zero-copy reader — direct pointers into the ring buffer
    cyc::RecordReaderZC reader;
    reader.init(clientBuffer, /*batchCapacity*/ 100);

    while (auto batch = reader.nextBatch(100); batch.isValid()) {
        // Process records in batch.data[0 .. batch.count * batch.recordSize) ...
        reader.release();   // unpin so the writer can reuse the region
    }
}
```

### 3. Saving Data to Disk (CBF & CSV)
Disk writers run in background threads to avoid blocking the producer.

```cpp
#include "Cbf/CbfWriter.h"
#include "Csv/CsvWriter.h"
#include "RecordReaderZC.h"

// Default — buffered RecordReader (good for slow disks)
cyc::CbfWriter cbfWriter("session_data.cbf", buffer, /*autoStart*/ true);
cbfWriter.setAlias("TestRun");

// Pick the zero-copy reader explicitly when the consumer can keep up
cyc::CsvWriter csvWriter(cyc::UseReader<cyc::RecordReaderZC>{},
                         "session_data.csv", buffer);

// Wait for the background threads to flush and close the files
cbfWriter.finish();
csvWriter.finish();
```

## License
MIT License — see [LICENSE.txt](LICENSE.txt).
