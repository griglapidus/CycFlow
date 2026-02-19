// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef IRECBUFFERCLIENT_H
#define IRECBUFFERCLIENT_H

#include <cstdint>

namespace cyc {

class IRecBufferClient {
public:
    virtual ~IRecBufferClient() = default;

    /**
     * @brief Вызывается RecBuffer'ом, когда в него записали новые данные.
     * Внимание: вызывается из потока Writer'а!
     */
    virtual void notifyDataAvailable() = 0;

    /**
     * @brief Возвращает позицию курсора чтения клиента.
     * Используется буфером для расчета безопасной зоны записи.
     * Если клиент пассивный (как график) и не должен блокировать запись,
     * он должен возвращать UINT64_MAX.
     */
    virtual uint64_t getCursor() const = 0;
};

}

#endif // IRECBUFFERCLIENT_H
