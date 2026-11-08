#pragma once

#include <string>
#include "NSpsc.h"
#include "Tick.h"
#include <thread>
#include <fstream>
#include <format>
#include "Utils.h"
#include <filesystem>
#include <iostream>
#include <vector>
#include <atomic>
#include <concepts>
#include <climits>
#include <stdexcept>


/*
- T needs to have a print method.
*/

template <typename T>
concept Printable = requires(const T& obj, std::string& out) {
    { obj.print(out) } -> std::same_as<void>;
};

template <typename T>
requires Printable<T>
class MessageLogger {
public:
    MessageLogger(NSpsc<T>& logQueue, std::string basePath, int msgLimit, Tick tick):msgLimit(msgLimit), tick(tick), basePath(basePath), logQueue(logQueue), path(getFilePath()) {
        if (msgLimit <= 0) {
            throw std::invalid_argument("message Limit should be positive");
        }
        std::filesystem::create_directories(std::format("{}/{}", basePath, toString(tick)));
        file = std::ofstream(path);
        if (!file.is_open()) {
            throw std::runtime_error("unable to open file");
        }
        msgReserve.resize(LOG_BATCH_SIZE);
        reserveMsg.reserve(10000);
    }


    ~MessageLogger() {
        stop();
        file.close();
    }

    MessageLogger(const MessageLogger& messageLogger) = delete;
    MessageLogger(MessageLogger&& messageLogger) = delete;
    MessageLogger& operator=(const MessageLogger& messageLogger) = delete;
    MessageLogger& operator=(MessageLogger&& messageLogger) = delete;

    void start() {
        if (t.joinable()) return;
        t = std::thread(&MessageLogger::run, this);
    }

    void stop() {
        stopRequested.store(true, std::memory_order_relaxed);
        if (t.joinable()) t.join();
        flush();
    }
private:
    int msgLimit;
    int msgCount = 0;
    static const int LOG_BATCH_SIZE = 50;
    static const int SPIN_CYCLES = 5;
    long long seqNumber = 1;
    std::vector<T> msgReserve;
    Tick tick;
    std::string basePath;
    std::string reserveMsg;
    NSpsc<T>& logQueue;
    std::atomic_bool stopRequested = false;
    std::thread t;
    std::string path;
    std::ofstream file;

    void run() {
        int cyclesCount = 0;
        bool fileFailed = false;
        while (!stopRequested.load(std::memory_order_relaxed)) {
            if (msgCount >= msgLimit) {
                file.close();
                if (seqNumber == LLONG_MAX) seqNumber = 0;
                seqNumber++;
                path = getFilePath();
                file = std::ofstream(path);
                msgCount = 0;
            }
            if (file.is_open()) {
                if (!popFromQueue(cyclesCount)) continue;
                for (int i = 0;i<MessageLogger::LOG_BATCH_SIZE;i++) msgReserve[i].print(reserveMsg);

                file<<reserveMsg<<std::flush;
                reserveMsg.clear();
                msgCount += MessageLogger::LOG_BATCH_SIZE;
            } else {
                if (!fileFailed) std::cerr<<"Unable to Open the file will be draining msgs\n";
                fileFailed = true;
                popFromQueue(cyclesCount);
            }
        }
    }

    void flush() {
        T poppedMsg;
        while (logQueue.tryPop(poppedMsg) != QueueStatus::EMPTY) {
            poppedMsg.print(reserveMsg);
            file<<reserveMsg<<std::flush;
            reserveMsg.clear();
            msgCount++;
        }
    }

    // file name: <Utils::getTS()>_<seqNumber>.log, where getTS() is steady_clock milliseconds since boot (not a date)
    std::string getFilePath() {
        return std::format("{}/{}/{}_{}.log", basePath, toString(tick), Utils::getTS(), seqNumber);
    }

    bool popFromQueue(int& cyclesCount) {
        if (logQueue.tryPopBatch(msgReserve) ==  QueueStatus::EMPTY) {
            if (cyclesCount == SPIN_CYCLES) {
                cyclesCount = 0;
                std::this_thread::yield();
                return false;
            }
            cyclesCount++;
            return false;
        }
        return true;
    }

};