# Архитектура и реализация Lock-Free MPMC кольцевого буфера с Zero-Copy

## 1. Контекст и цели

Текущая реализация передачи записей опирается на двойную буферизацию в `RecordWriter` и `RecordReader` (поля `m_bufferA`/`m_bufferB`, фоновые `workerLoop`'ы). Это даёт два лишних `memcpy` на каждый batch и накладные расходы на синхронизацию. Цель — заменить эту схему на **Reserve/Commit + Zero-Copy** доступ к памяти `RecBuffer` без потери:

- MPMC-семантики (несколько писателей, несколько читателей, у каждого свой курсор);
- режима `blockOnFull=true` (writer спит, пока самый медленный reader не освободит место);
- безопасности на уровне типов и владения памятью.

## 2. Фундаментальное решение: Эпохи (Epochs)

В MPMC простые флаги состояний (FREE/RESERVED/COMMITTED) подвержены гонкам ABA и writer-over-RESERVED. Решение — упаковать **logical_idx (эпоху) в каждый атомарный флаг** вместе со статусом.

64-битный атомарный флаг на каждый чанк:
- младшие 8 бит — `ChunkState`;
- старшие 56 бит — `logical_idx` (эпоха, маскируется при упаковке).

```cpp
namespace cyc {

template <typename T>
struct Span {
    T* data_ptr;
    size_t length;
    T* data() const { return data_ptr; }
    size_t size() const { return length; }
    bool empty() const { return length == 0; }
    T* begin() const { return data_ptr; }
    T* end() const { return data_ptr + length; }
};

enum ChunkState : uint8_t { FREE = 0, RESERVED = 1, COMMITTED = 2, SKIPPED = 3 };

constexpr uint64_t EPOCH_MASK = 0x00FFFFFFFFFFFFFFULL;

inline uint64_t PackState(uint64_t logical_idx, ChunkState state) {
    return ((logical_idx & EPOCH_MASK) << 8) | static_cast<uint8_t>(state);
}
inline ChunkState GetState(uint64_t packed) { return static_cast<ChunkState>(packed & 0xFF); }
inline uint64_t   GetLogical(uint64_t packed) { return packed >> 8; }

inline bool EpochMatches(uint64_t flag_logical, uint64_t scan_logical) {
    return flag_logical == (scan_logical & EPOCH_MASK);
}

} // namespace cyc
```

Замечание: при ~10⁷ записей/с эпоха 2⁵⁶ исчерпается за ~228 лет. `EpochMatches` сравнивает обе стороны через маску, корректно работает даже при теоретическом wraparound.

## 3. cyc::CircularBuffer — STL-подобный контейнер

CircularBuffer **остаётся полноценным контейнером общего назначения**, пригодным к использованию вне DynamicChunkBuffer. Минимальные изменения по сравнению с существующим `CycLib/Core/CircularBuffer.h`:

**Удаляется:**
- `mutable std::shared_mutex m_mtx`;
- все `std::shared_lock`/`std::unique_lock` внутри методов.

После этого класс становится **не-потокобезопасным** по STL-конвенции (как `std::vector`/`std::deque`). Пользователю, которому нужна потокобезопасность, надлежит обернуть внешним мьютексом самостоятельно. DynamicChunkBuffer обеспечивает свою потокобезопасность не через mutex внутри контейнера, а через эпохи и атомарный `m_alloc_pos`.

**Добавляется (единственное новшество):**
- `T& operator()(size_t index)` / `const T& operator()(size_t index) const` — modulo-доступ.

**Сохраняется без изменений:**
- Конструкторы (default, with capacity, copy, move, with allocator), деструктор, allocator-семантика, `allocator_traits`.
- Поля `m_capacity`, `m_head`, `m_size` (атомарные), `m_allocator`, `m_array`. Layout не меняется, allocator-механизм не трогается.
- Аксессоры: `capacity()`, `size()`, `empty()`, `full()`, `max_size()`, `get_allocator()`, `front()`, `back()`, `at()`, `operator[]`.
- Modifiers: `clear()`, `reserve(new_capacity)`, `emplace_back()`, `push_back()`, `pop_front()`, `pop_back()`, `swap()`.
- Bulk-операции: `push_many`, `peek_many`, `peek_many_at`, `pop_many` (требуют copyable `T`; для некопируемых типов вроде `std::atomic<T>` они недоступны через стандартный SFINAE/static_assert, как и в STL).
- Iterator API целиком: `iterator_impl<>`, `begin/end/cbegin/cend`, reverse iterators, контракт `random_access_iterator`.
- `data()`/`data() const`, `toStdVector()` (для copyable T), `get_ptr_unsafe`, `get_head_index_unsafe`.

```cpp
template <typename T, typename Allocator = std::allocator<T>>
class CircularBuffer {
public:
    // Все using'и сохраняются как есть

    // Конструкторы, деструктор, copy/move/swap — как есть, только без захватов m_mtx
    explicit CircularBuffer(size_type capacity = 1,
                            allocator_type const& alloc = allocator_type());
    CircularBuffer(self_type const& other);
    CircularBuffer(self_type&& other) noexcept;
    ~CircularBuffer();
    CircularBuffer& operator=(self_type other);
    void swap(self_type& other) noexcept;

    // НОВОЕ: modulo-доступ
    reference       operator()(size_type index)       { return m_array[index % capacity()]; }
    const_reference operator()(size_type index) const { return m_array[index % capacity()]; }

    // STL-подобный API сохраняется полностью (без lock'ов)
    [[nodiscard]] bool      empty()    const;
    [[nodiscard]] bool      full()     const;
    [[nodiscard]] size_type capacity() const;
    [[nodiscard]] size_type size()     const;
    [[nodiscard]] size_type max_size() const;
    [[nodiscard]] allocator_type get_allocator() const;
    reference       front();        const_reference front() const;
    reference       back();         const_reference back() const;
    reference       operator[](size_type n);
    const_reference operator[](size_type n) const;
    reference       at(size_type n);
    const_reference at(size_type n) const;
    pointer         data() noexcept;
    const_pointer   data() const noexcept;

    void clear();
    void reserve(size_type new_capacity);
    template <typename... Args> void emplace_back(Args&&... args);
    void push_back(value_type const& item);
    void push_back(value_type&& item);
    void pop_front();
    void pop_back();

    void push_many(const T* source, size_type count);
    void peek_many(T* dest, size_type count) const;
    void peek_many_at(size_type index, T* dest, size_type count) const;
    void pop_many(size_type count);

    template<typename ElemType> class iterator_impl { /* как было */ };
    using iterator = iterator_impl<value_type>;
    using const_iterator = iterator_impl<const value_type>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    iterator begin();   iterator end();
    const_iterator begin() const;   const_iterator end() const;
    const_iterator cbegin() const;  const_iterator cend() const;

    [[nodiscard]] std::vector<T> toStdVector() const;
    const_pointer get_ptr_unsafe(size_type index) const;
    size_type     get_head_index_unsafe() const;

private:
    alignas(64) std::atomic<size_type> m_capacity;
    alignas(64) std::atomic<size_type> m_head;
    alignas(64) std::atomic<size_type> m_size;
    allocator_type m_allocator;
    pointer        m_array;
    // mutable std::shared_mutex m_mtx;  // УДАЛЕНО

    // private helpers сохраняются: assign_into_unsafe, clear_unsafe,
    // pop_many_unsafe, copy_chunk_logic, at_unchecked, begin_unsafe, end_unsafe
};

template <typename T, typename A>
void swap(CircularBuffer<T, A>& lhs, CircularBuffer<T, A>& rhs) noexcept;
```

## 4. DynamicChunkBuffer — теневой буфер и MPMC-логика

Использует две инстанции `cyc::CircularBuffer`: одна для сырых байт данных, вторая — теневой массив атомарных флагов с упакованной эпохой. Никаких `std::unique_ptr`, никаких raw-массивов в этом классе — вся память живёт внутри CircularBuffer.

```cpp
class DynamicChunkBuffer {
private:
    size_t m_capacity_chunks;
    size_t m_chunk_size;

    alignas(64) std::atomic<uint64_t> m_alloc_pos{0};

    cyc::CircularBuffer<uint8_t>                m_data;     // capacity*chunk_size байт
    cyc::CircularBuffer<std::atomic<uint64_t>>  m_flags;    // capacity флагов

public:
    DynamicChunkBuffer(size_t capacity, size_t chunk_size)
        : m_capacity_chunks(capacity)
        , m_chunk_size(chunk_size)
        , m_data(capacity * chunk_size)
        , m_flags(capacity)
    {
        // ИНВАРИАНТ: атомики в C++17 после default-construct в indeterminate
        // состоянии. Этот цикл — первый legal-доступ; все наружные операции
        // (reserveChunks/consumeContiguous) обязаны happen-after него.
        for (size_t i = 0; i < capacity; ++i) {
            m_flags(i).store(cyc::PackState(0, cyc::FREE), std::memory_order_relaxed);
        }
    }

    DynamicChunkBuffer(const DynamicChunkBuffer&) = delete;
    DynamicChunkBuffer& operator=(const DynamicChunkBuffer&) = delete;

    uint64_t getAllocPos() const { return m_alloc_pos.load(std::memory_order_acquire); }
    size_t   capacity()    const { return m_capacity_chunks; }
    size_t   chunkSize()   const { return m_chunk_size; }

    std::optional<std::pair<cyc::Span<uint8_t>, uint64_t>>
    reserveChunks(size_t max_count, const std::function<uint64_t()>& get_min_read);

    void executeCommit(uint64_t logical_idx, size_t reserved_chunks, size_t actual_chunks);
    void executeRollback(uint64_t logical_idx, size_t reserved_chunks);

    struct ConsumeResult {
        cyc::Span<const uint8_t> span;
        size_t skipped_before;
        bool   lag_detected;
    };
    ConsumeResult consumeContiguous(uint64_t reader_cursor, size_t max_count) const;
};
```

## 5. Резервирование (CAS-цикл с partial grant)

```cpp
std::optional<std::pair<cyc::Span<uint8_t>, uint64_t>>
DynamicChunkBuffer::reserveChunks(size_t max_count,
                                  const std::function<uint64_t()>& get_min_read)
{
    uint64_t current_alloc = m_alloc_pos.load(std::memory_order_acquire);
    while (true) {
        uint64_t min_read = get_min_read();
        if (min_read != UINT64_MAX && min_read > current_alloc) min_read = current_alloc;

        uint64_t used_space = (min_read == UINT64_MAX) ? 0 : (current_alloc - min_read);
        if (used_space >= m_capacity_chunks) return std::nullopt;  // drop-on-full

        size_t free_space = m_capacity_chunks - static_cast<size_t>(used_space);

        size_t physical_start      = current_alloc % m_capacity_chunks;
        size_t contiguous_capacity = m_capacity_chunks - physical_start;

        size_t allowed_count = std::min({max_count, contiguous_capacity, free_space});
        if (allowed_count == 0) return std::nullopt;

        uint64_t next_alloc = current_alloc + allowed_count;

        if (m_alloc_pos.compare_exchange_weak(current_alloc, next_alloc,
                                              std::memory_order_release,
                                              std::memory_order_relaxed))
        {
            for (size_t i = 0; i < allowed_count; ++i) {
                m_flags(current_alloc + i).store(
                    cyc::PackState(current_alloc + i, cyc::RESERVED),
                    std::memory_order_release);
            }
            uint8_t* ptr = m_data.data() + (physical_start * m_chunk_size);
            return std::make_pair(cyc::Span<uint8_t>{ptr, allowed_count * m_chunk_size},
                                  current_alloc);
        }
        // CAS не удался: current_alloc обновился — спин с новым значением.
    }
}
```

Решение по overwrite: режим `blockOnFull=false` сводится к **drop-on-full** (возврат `nullopt`). Overwrite в zero-copy MPMC убран как семантически tear-prone — writer не может перетереть память, на которую reader держит указатель, без seqlock'а на каждом field-access. Сценарии "real-time UI snapshot, drop on lag" поддерживаются через passive reader (см. §9).

## 6. Commit / Rollback

COMMITTED устанавливаются в **обратном порядке** (с конца к началу). Reader, увидев первый по логическому индексу COMMITTED, через acquire-цепочку гарантированно видит весь batch consistent — это устраняет out-of-order видимость частей одного batch'а.

```cpp
void DynamicChunkBuffer::executeCommit(uint64_t logical_idx,
                                       size_t reserved_chunks,
                                       size_t actual_chunks) {
    // SKIPPED-хвост сначала
    for (size_t i = actual_chunks; i < reserved_chunks; ++i) {
        m_flags(logical_idx + i).store(
            cyc::PackState(logical_idx + i, cyc::SKIPPED),
            std::memory_order_release);
    }
    // COMMITTED от конца к началу: reader, увидев первый COMMITTED, через
    // acquire-load получает happens-before на все последующие чанки batch'а.
    for (size_t i = actual_chunks; i-- > 0; ) {
        m_flags(logical_idx + i).store(
            cyc::PackState(logical_idx + i, cyc::COMMITTED),
            std::memory_order_release);
    }
}

void DynamicChunkBuffer::executeRollback(uint64_t logical_idx, size_t reserved_chunks) {
    for (size_t i = 0; i < reserved_chunks; ++i) {
        m_flags(logical_idx + i).store(
            cyc::PackState(logical_idx + i, cyc::SKIPPED),
            std::memory_order_release);
    }
}
```

## 7. WriteHandle (RAII)

```cpp
class WriteHandle {
private:
    RecordWriter* m_writer;
    cyc::Span<uint8_t> m_span;
    uint64_t m_logical_idx;
    bool m_active;
public:
    WriteHandle(RecordWriter* w, cyc::Span<uint8_t> s, uint64_t idx)
        : m_writer(w), m_span(s), m_logical_idx(idx), m_active(s.size() > 0) {}

    WriteHandle(const WriteHandle&) = delete;
    WriteHandle& operator=(const WriteHandle&) = delete;

    WriteHandle(WriteHandle&& o) noexcept
        : m_writer(o.m_writer), m_span(o.m_span)
        , m_logical_idx(o.m_logical_idx), m_active(o.m_active) { o.m_active = false; }

    ~WriteHandle() { if (m_active) rollback(); }

    uint8_t* data() { return m_span.data(); }
    size_t   capacity() const { return m_span.size(); }
    bool     isValid() const { return m_active; }

    void commit(size_t actual_bytes) {
        if (!m_active) return;
        m_writer->executeCommit(m_logical_idx, m_span.size(), actual_bytes);
        m_active = false;
    }
    void rollback() {
        if (!m_active) return;
        m_writer->executeRollback(m_logical_idx, m_span.size());
        m_active = false;
    }
};
```

## 8. Чтение с обработкой SKIPPED и lag

Lag-check выполняется внутри цикла после каждого epoch-mismatch break (защищает от race-window "writer прыгнул на круг между pre-loop check и началом сканирования").

```cpp
DynamicChunkBuffer::ConsumeResult
DynamicChunkBuffer::consumeContiguous(uint64_t reader_cursor, size_t max_count) const {
    size_t cap = m_capacity_chunks;
    uint64_t scan_logical = reader_cursor;
    size_t skipped = 0, contiguous = 0;

    auto check_lag = [&](uint64_t scan) {
        uint64_t alloc = m_alloc_pos.load(std::memory_order_acquire);
        return alloc >= scan && (alloc - scan) > cap;
    };

    if (check_lag(scan_logical)) return {{nullptr, 0}, 0, true};

    size_t phys_start = scan_logical % cap;

    while (contiguous < max_count) {
        uint64_t alloc_now = m_alloc_pos.load(std::memory_order_acquire);
        if (scan_logical >= alloc_now) break;

        size_t phys_idx = scan_logical % cap;
        if (contiguous > 0 && phys_idx == 0) break;  // wrap

        uint64_t packed = m_flags(scan_logical).load(std::memory_order_acquire);
        cyc::ChunkState state = cyc::GetState(packed);
        uint64_t flag_logical = cyc::GetLogical(packed);

        if (!cyc::EpochMatches(flag_logical, scan_logical)) {
            if (check_lag(scan_logical)) return {{nullptr, 0}, 0, true};
            break;
        }

        if (state == cyc::RESERVED || state == cyc::FREE) break;

        if (state == cyc::SKIPPED) {
            if (contiguous > 0) break;
            ++scan_logical;
            ++skipped;
            phys_start = scan_logical % cap;
            continue;
        }

        if (state == cyc::COMMITTED) {
            ++contiguous;
            ++scan_logical;
        }
    }

    const uint8_t* ptr = contiguous > 0
        ? (m_data.data() + phys_start * m_chunk_size)
        : nullptr;
    return {{ptr, contiguous * m_chunk_size}, skipped, false};
}
```

## 9. RecBuffer — слой интеграции

Восстанавливает связь с существующим протоколом клиентов и реализует lock-free min_read_cursor через фиксированный массив + wait/notify через стандартные `std::mutex + std::condition_variable` (используется только при block-сценарии, не на горячем пути).

```cpp
class RecBuffer {
public:
    static constexpr size_t MAX_READERS = 32;

private:
    RecRule m_rule;
    DynamicChunkBuffer m_impl;

    std::array<std::atomic<uint64_t>, MAX_READERS> m_reader_cursors;

    std::mutex              m_wait_mtx;
    std::condition_variable m_cv_readers;
    std::condition_variable m_cv_writers;
    std::atomic<uint32_t>   m_waiting_readers{0};
    std::atomic<uint32_t>   m_waiting_writers{0};

public:
    RecBuffer(const RecRule& rule, size_t capacity)
        : m_rule(rule), m_impl(capacity, rule.getRecSize())
    {
        for (auto& c : m_reader_cursors) c.store(UINT64_MAX, std::memory_order_relaxed);
    }

    int registerReader(uint64_t initial_cursor) {
        for (size_t i = 0; i < MAX_READERS; ++i) {
            uint64_t expected = UINT64_MAX;
            if (m_reader_cursors[i].compare_exchange_strong(
                    expected, initial_cursor,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                return static_cast<int>(i);
        }
        return -1;  // overflow → passive reader (без back-pressure на writer'а)
    }
    void unregisterReader(int slot) {
        if (slot < 0) return;
        m_reader_cursors[slot].store(UINT64_MAX, std::memory_order_release);
        notifyWritersFast();
    }
    void updateReaderCursor(int slot, uint64_t cursor) {
        if (slot < 0) return;
        m_reader_cursors[slot].store(cursor, std::memory_order_release);
    }

    uint64_t calculateMinReadCursor() const {
        uint64_t min_c = UINT64_MAX;
        for (const auto& a : m_reader_cursors) {
            uint64_t c = a.load(std::memory_order_acquire);
            if (c < min_c) min_c = c;
        }
        return min_c;
    }

    auto reserveRecords(size_t max_count) {
        return m_impl.reserveChunks(max_count,
                                    [this]{ return calculateMinReadCursor(); });
    }
    void commitRecords(uint64_t logical_idx, size_t reserved, size_t actual) {
        m_impl.executeCommit(logical_idx, reserved, actual);
        notifyReadersFast();
    }
    void rollbackRecords(uint64_t logical_idx, size_t reserved) {
        m_impl.executeRollback(logical_idx, reserved);
        notifyReadersFast();
    }
    auto consumeRecords(uint64_t cursor, size_t max_count) const {
        return m_impl.consumeContiguous(cursor, max_count);
    }

    // Wait/notify — НЕ lock-free, активируется только при block-сценарии
    void notifyReadersFast() {
        if (m_waiting_readers.load(std::memory_order_acquire) > 0) {
            std::lock_guard<std::mutex> lk(m_wait_mtx);
            m_cv_readers.notify_all();
        }
    }
    void notifyWritersFast() {
        if (m_waiting_writers.load(std::memory_order_acquire) > 0) {
            std::lock_guard<std::mutex> lk(m_wait_mtx);
            m_cv_writers.notify_all();
        }
    }
    void waitForData(const std::function<bool()>& stop) {
        std::unique_lock<std::mutex> lk(m_wait_mtx);
        m_waiting_readers.fetch_add(1, std::memory_order_release);
        m_cv_readers.wait(lk, [&]{ return stop(); });
        m_waiting_readers.fetch_sub(1, std::memory_order_release);
        // Условие "данные есть" проверяется caller'ом через повторный consumeRecords.
    }
    void waitForSpace(const std::function<bool()>& stop) {
        std::unique_lock<std::mutex> lk(m_wait_mtx);
        m_waiting_writers.fetch_add(1, std::memory_order_release);
        m_cv_writers.wait(lk, [&]{ return stop(); });
        m_waiting_writers.fetch_sub(1, std::memory_order_release);
    }

    uint64_t getAllocPos() const { return m_impl.getAllocPos(); }
    size_t   capacity()    const { return m_impl.capacity(); }
    size_t   getRecSize()  const { return m_impl.chunkSize(); }
    const RecRule& getRule() const { return m_rule; }
};
```

## 10. RecordWriter

```cpp
WriteHandle RecordWriter::nextBatch(size_t maxRecords, bool wait = true) {
    while (true) {
        auto result_opt = m_target->reserveRecords(maxRecords);
        if (result_opt && !result_opt->first.empty()) {
            return WriteHandle(this, result_opt->first, result_opt->second);
        }

        if (!wait || !m_blockOnFull || !m_running.load()) {
            return WriteHandle(this, {nullptr, 0}, 0);
        }

        m_target->waitForSpace([this]{ return !m_running.load(); });
    }
}

void RecordWriter::executeCommit(uint64_t logical_idx, size_t reserved_bytes, size_t actual_bytes) {
    m_target->commitRecords(logical_idx,
                            reserved_bytes / m_recSize,
                            actual_bytes / m_recSize);
}
void RecordWriter::executeRollback(uint64_t logical_idx, size_t reserved_bytes) {
    m_target->rollbackRecords(logical_idx, reserved_bytes / m_recSize);
}
```

## 11. RecordReader

```cpp
class ReadHandle {
    RecordReader* m_reader;
    cyc::Span<const uint8_t> m_span;
    size_t m_skipped;
    bool   m_active;
public:
    ReadHandle(RecordReader* r, cyc::Span<const uint8_t> s, size_t skipped)
        : m_reader(r), m_span(s), m_skipped(skipped)
        , m_active(s.size() > 0 || skipped > 0) {}

    ReadHandle(const ReadHandle&) = delete;
    ReadHandle& operator=(const ReadHandle&) = delete;
    ReadHandle(ReadHandle&& o) noexcept
        : m_reader(o.m_reader), m_span(o.m_span)
        , m_skipped(o.m_skipped), m_active(o.m_active) { o.m_active = false; }

    ~ReadHandle() {
        if (m_active && m_reader) {
            size_t total = m_skipped + (m_span.size() / m_reader->getRecSize());
            if (total > 0) m_reader->releaseBatch(total);
        }
    }

    const uint8_t* data() const { return m_span.data(); }
    size_t         size() const { return m_span.size(); }
    bool           isValid() const { return !m_span.empty(); }
};

ReadHandle RecordReader::nextBatch(size_t maxRecords, bool wait = true) {
    int recovery_attempts = 0;
    while (m_running.load(std::memory_order_acquire)) {
        auto res = m_target->consumeRecords(
            m_readerCursor.load(std::memory_order_acquire), maxRecords);

        if (res.lag_detected) {
            uint64_t alloc = m_target->getAllocPos();
            size_t   cap   = m_target->capacity();
            uint64_t new_cursor = alloc > cap ? alloc - cap : 0;
            m_readerCursor.store(new_cursor, std::memory_order_release);
            m_target->updateReaderCursor(m_slot, new_cursor);

            if (++recovery_attempts > 3) {
                std::this_thread::yield();
                recovery_attempts = 0;
            }
            continue;
        }
        recovery_attempts = 0;

        if (res.skipped_before > 0) {
            uint64_t new_cursor = m_readerCursor.fetch_add(
                res.skipped_before, std::memory_order_release) + res.skipped_before;
            m_target->updateReaderCursor(m_slot, new_cursor);
            if (res.span.empty()) continue;
        }
        if (!res.span.empty()) {
            return ReadHandle(this, res.span, 0);
        }

        if (!wait) return ReadHandle(this, {nullptr, 0}, 0);
        m_target->waitForData([this]{ return !m_running.load(); });
    }
    return ReadHandle(this, {nullptr, 0}, 0);
}

void RecordReader::releaseBatch(size_t count) {
    uint64_t new_cursor = m_readerCursor.fetch_add(count, std::memory_order_release) + count;
    m_target->updateReaderCursor(m_slot, new_cursor);
    m_target->notifyWritersFast();
}
```

Конструктор RecordReader делает `m_slot = m_target->registerReader(initial_cursor)`. Если `m_slot == -1`, reader работает как passive observer без back-pressure (эквивалент `getCursor() == UINT64_MAX` в старой архитектуре).

## 12. Семантический контракт

**Гарантии:**
- **Lock-free hot path**: `reserveChunks`, `executeCommit`, `executeRollback`, `consumeContiguous`, `releaseBatch`, `updateReaderCursor`, `calculateMinReadCursor` — без mutex'ов (только atomic CAS/load/store).
- **MPMC-корректность**: эпохи устраняют writer-over-RESERVED race, ABA, ложные коммиты чужих данных.
- **Out-of-order commit**: writer'ы независимы; reader не получит COMMITTED-чанк, перед которым есть RESERVED-чанк.
- **Atomic batch visibility (внутри одного writer'а)**: благодаря reverse-order установке COMMITTED, увидев `[idx+0]=COMMITTED`, reader через acquire-цепочку видит весь batch consistent.
- **Drop-on-full** при `blockOnFull=false`: writer возвращает невалидный handle, не перезаписывает активные данные.
- **Block-on-full** при `blockOnFull=true`: writer спит на `m_cv_writers`, разбуждается из `releaseBatch`/`unregisterReader`.
- **MAX_READERS=32** активных reader'ов с back-pressure; 33-й и далее → `m_slot=-1`, passive (best-effort consume, не блокирует writer'а).

**Не гарантируется:**
- **Overwrite-on-full в zero-copy** — убран как семантически tear-prone. Сценарии "real-time UI snapshot, drop on lag" реализуются как passive reader.
- **Span переживает nextBatch()** — ReadHandle/WriteHandle действительны только до их destruct'а или до выхода из enclosing scope (семантика идентична `std::string_view`). Если caller хочет данные надолго — `memcpy` наружу.
- **Семантическая атомарность поперёк batch'ей** — план не делает разделений одной логической записи между разными writer'ами.

**Поток wait/notify:**
- `waitForData`/`waitForSpace` спят на `condition_variable` (требуют mutex). Это единственный lock-путь в архитектуре; включается только когда буфер пуст для reader'а или полон для writer'а.
- На горячем пути нет mutex'ов: commit делает быстрый load `m_waiting_readers > 0` и берёт mutex только если есть кого будить.

## 13. Этапы внедрения

| Этап | Содержание | Риск | Тестирование |
|---|---|---|---|
| **0** | Починить Pimpl-конфликт в `CycLib/Core/DynamicChunkBuffer.h` vs `.cpp` (привести к единому стилю — Pimpl или header-only). | низкий | сборка |
| **1** | Облегчить `cyc::CircularBuffer`: убрать `m_mtx` и lock-захваты, добавить `operator()(idx)`. STL-подобный API сохраняется полностью. | средний | существующие тесты |
| **2** | Реализовать новый `DynamicChunkBuffer` с эпохами (reserve/commit/rollback/consume API), параллельно с существующим (новые методы — суффикс `*V2`). | высокий | unit + property-based |
| **3** | Реализовать `RecBuffer::reserveRecords/commitRecords/rollbackRecords/consumeRecords` + lock-free `m_reader_cursors` + wait/notify. Сохранить старые `push/readFromGlobal` для обратной совместимости. | средний | unit + integration |
| **4** | Перевести `RecordWriter` на `WriteHandle`/RAII. Удалить `m_bufferA/B/workerLoop`. | средний | бенчмарк + интеграция |
| **5** | Перевести `RecordReader` на `ReadHandle`. Удалить `m_bufferA/B/workerLoop`. | средний | бенчмарк + интеграция |
| **6** | Мигрировать call-sites (TcpDataReceiver, CbfReader и пр.) на новый API. Удалить старый `push/readFromGlobal`. | низкий | интеграция |
| **7** | Property-based stress test: N writers × M readers × random reserve/commit/rollback/lag → инвариант: "ни один зафиксированный record не теряется и не дублируется без явного skip". | критически высокий | rapidcheck/CTL |

Этапы 0-1 могут быть смержены немедленно — они независимы и улучшают код без новой семантики. Этапы 2-7 — за одной feature-веткой с финальным merge только после прохождения этапа 7.

## 14. Открытые вопросы

1. **MAX_READERS=32**: достаточно ли? Если нет — drop, dynamic grow (не lock-free), или иерархия (32×32=1024)?
2. **Capacity = power-of-2**: предложить ли как требование? Тогда `% cap` → `& (cap-1)`, заметный выигрыш на каждом modulo-доступе.
3. **Reverse-commit и зависимости между записями разных writer'ов**: если запись N+1 от writer B логически зависит от записи N от writer A, порядок реальной публикации может отличаться от логического. Семантическая проблема выше уровня буфера, но стоит задокументировать на уровне call-sites.
4. **Долгоживущие ReadHandle**: возможна ли защита через "epoch pinning" — reader атомарно фиксирует свой текущий epoch, writer-CAS проверяет нет ли pinned epoch'и в окне overwrite. Это вернуло бы overwrite-режим. Не критично для v10, кандидат на v11.

## 15. Что меняется относительно существующего кода

**Удаляется:**
- `m_bufferA`, `m_bufferB`, `workerLoop`, `m_mtx`, `m_cv`, `m_cv_done`, `swapBuffers`, `flush` в `CycLib/RecordWriter.h`.
- `m_bufferA`, `m_bufferB`, `workerLoop`, `m_cv_user`, `m_cv_worker`, `swapBuffers` в `CycLib/RecordReader.h`.
- `mutable std::shared_mutex m_mtx` и lock-захваты в `CycLib/Core/CircularBuffer.h`.
- `m_dataRwMtx` в `CycLib/Core/RecBuffer.h` (заменяется атомарной координацией через эпохи).
- Ветка `else` "блокирующий push" в `RecordWriter::workerLoop` (`CycLib/RecordWriter.cpp:217-249`) — не нужна при reserve/commit.

**Добавляется:**
- `cyc::Span<T>`, `cyc::ChunkState`, `cyc::PackState/GetState/GetLogical/EpochMatches`, `cyc::EPOCH_MASK`.
- `WriteHandle`, `ReadHandle`.
- `cyc::CircularBuffer::operator()(size_t)`.
- `DynamicChunkBuffer::reserveChunks/executeCommit/executeRollback/consumeContiguous/getAllocPos/chunkSize`.
- `RecBuffer::reserveRecords/commitRecords/rollbackRecords/consumeRecords/registerReader/unregisterReader/updateReaderCursor/calculateMinReadCursor/notifyReadersFast/notifyWritersFast/getAllocPos`.
- `RecordWriter::nextBatch (новая сигнатура)/executeCommit/executeRollback`.
- `RecordReader::nextBatch (новая сигнатура)/releaseBatch`.

**Сохраняется без изменений:**
- `RecRule`, `Record`, `IRecBufferClient`, `PReg`, `PAttr` и весь slot-based registry.
- STL-подобный API `cyc::CircularBuffer` (всё кроме mutex'а).
- Существующие call-sites продолжают работать через legacy-API `RecBuffer::push/readFromGlobal` до этапа 6.
