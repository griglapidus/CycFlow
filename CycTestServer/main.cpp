#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <vector>

#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "Core/PAttr.h"
#include "RecordWriter.h"
#include "Tcp/TcpServer.h"
#include <asio.hpp>
#include "Core/PReg.h"

using namespace cyc;

int main() {
    try {
        asio::io_context io_context;

        // 1. Prepare Schema with explicit IDs
        std::vector<PAttr> attrs;
        attrs.push_back(PAttr("Counter",  DataType::dtInt8,   2));
        attrs.push_back(PAttr("BitReg",  DataType::dtUInt8,
                              std::vector<std::string>{"f0","f1","f2","f3","f4","f5","f6","f7"}));
        attrs.push_back(PAttr("Voltage",  DataType::dtFloat,  1));
        attrs.push_back(PAttr("Current",  DataType::dtFloat,  1));
        attrs.push_back(PAttr("ADC ch0",  DataType::dtInt16,  1));
        attrs.push_back(PAttr("Pressure", DataType::dtDouble, 1));

        RecRule rule;
        rule.init(attrs);

        int idCounter = PReg::getID("Counter");
        int idVoltage = PReg::getID("Voltage");
        int idCurrent = PReg::getID("Current");
        int idAdc     = PReg::getID("ADC ch0");
        int idPres    = PReg::getID("Pressure");
        int idI0 = PReg::getID("f0");
        int idI1 = PReg::getID("f1");
        int idI2 = PReg::getID("f2");
        int idI3 = PReg::getID("f3");
        int idI4 = PReg::getID("f4");
        int idI5 = PReg::getID("f5");
        int idI6 = PReg::getID("f6");
        int idI7 = PReg::getID("f7");

        // 2. Create Ring Buffer and Writer
        auto buffer = std::make_shared<RecBuffer>(rule, 10000);
        RecordWriter writer(buffer, 2000);

        // 3. Start TCP Server
        uint16_t port = 5000;
        TcpServer server(io_context, port);
        server.registerBuffer("Buffer_1", buffer, 500);
        server.start();

        // Start ASIO event loop in a background thread with a work guard
        std::thread asioThread([&io_context]() {
            auto workGuard = asio::make_work_guard(io_context);
            io_context.run();
        });

        std::cout << "Data Generator Server running on port " << port << "...\n";
        std::cout << "Buffer registered as 'Buffer_1'\n";

        // 4. Data Generation Loop
        const int batchSize = 200;
        const int intervalMs = 50;
        uint64_t tick = 0;

        while (true) {
            auto start_time = std::chrono::steady_clock::now();

            for (int i = 0; i < batchSize; ++i) {
                Record rec = writer.nextRecord();
                int8_t counterVal = static_cast<int8_t>((tick + i) % 256);
                float  vVal       = 12.0f + 2.5f * std::sin((tick + i) * 0.05f);
                float  cVal       = 3.2f + 0.8f * std::sin((tick + i) * 0.08f);
                int16_t adcVal    = static_cast<int16_t>(std::sin((tick + i) * 0.02f) * 5000.0f + 500.0f);
                double pVal       = 101.3 + 1.2 * std::sin((tick + i) * 0.03);

                bool b0 = ((tick + i) & (1 << 0)) != 0;
                bool b1 = ((tick + i) & (1 << 1)) != 0;
                bool b2 = ((tick + i) & (1 << 2)) != 0;
                bool b3 = ((tick + i) & (1 << 3)) != 0;
                bool b4 = ((tick + i) & (1 << 4)) != 0;
                bool b5 = ((tick + i) & (1 << 5)) != 0;
                bool b6 = ((tick + i) & (1 << 6)) != 0;
                bool b7 = ((tick + i) & (1 << 7)) != 0;

                // Use specific setters for performance and type safety
                rec.setInt8(idCounter, counterVal);
                rec.setInt8(idCounter, counterVal + 20, 1);
                rec.setFloat(idVoltage, vVal);
                rec.setFloat(idCurrent, cVal);
                rec.setInt16(idAdc, adcVal);
                rec.setDouble(idPres, pVal);
                rec.setBit(idI0, b0);
                rec.setBit(idI1, b1);
                rec.setBit(idI2, b2);
                rec.setBit(idI3, b3);
                rec.setBit(idI4, b4);
                rec.setBit(idI5, b5);
                rec.setBit(idI6, b6);
                rec.setBit(idI7, b7);

                writer.commitRecord();
            }

            // Flush the batch so consumers can read it immediately
            writer.flush();
            tick += batchSize;

            // Sleep for the remainder of the interval to maintain stable FPS
            auto end_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            if (elapsed.count() < intervalMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs) - elapsed);
            }
        }

        // Unreachable in this infinite loop, but good practice for clean exit
        io_context.stop();
        if (asioThread.joinable()) {
            asioThread.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }

    return 0;
}
