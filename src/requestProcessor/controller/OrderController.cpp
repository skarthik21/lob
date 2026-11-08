#include "OrderController.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Utils.h"
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>




OrderController::OrderController(int port, NSpsc<Message>& requestQueue, NSpsc<InstrumentationEvent>& instrQueue): requestQueue(requestQueue), instrQueue(instrQueue) {
    try {
        listenFd = ::socket(AF_INET, SOCK_DGRAM, 0);
        handleSysCallError(listenFd, "listen-socket", "unable to create listen socket");

        // non-blocking: recvfrom returns EAGAIN instead of waiting, so run() keeps checking stopRequested
        int flags = ::fcntl(listenFd, F_GETFL, 0);
        handleSysCallError(flags, "listen-socket-fcntl", "unable to read socket flags");
        handleSysCallError(::fcntl(listenFd, F_SETFL, flags | O_NONBLOCK), "listen-socket-fcntl", "unable to make socket non-blocking");

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        handleSysCallError(::bind(listenFd, reinterpret_cast<sockaddr*> (&addr), sizeof(addr)), "listen-socket-bind", "unable to bind socket");
    } catch (const std::runtime_error&) {
        if (listenFd >= 0) ::close(listenFd);
        throw;
    }
}

// it can only be started once
void OrderController::start() {
    if (t.joinable()) return;
    t = std::thread(&OrderController::run, this);
}


void OrderController::run() {
    while (!(stopRequested.load(std::memory_order_relaxed))) {
            char buf[sizeof(Message) + 1];
            try {
                ssize_t rcvSize = ::recvfrom(listenFd, buf, sizeof(buf), 0, nullptr, nullptr);

                if (rcvSize < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
                handleSysCallError(rcvSize, "recv-error", "exception while recv msg");

                reqTotalCount++;
                if (reqTotalCount == constants::THROUGHPUT_COUNT_LIMIT) {
                    pushInstru(constants::OVERALL_THROUGHPUT_ORDERID, reqTotalCount);
                    reqTotalCount = 0;
                }

                if (rcvSize != sizeof(Message)) {
                    errCountRcvLessFailure++;
                    if (errCountRcvLessFailure == constants::THROUGHPUT_COUNT_LIMIT) {
                        pushInstru(constants::RCV_THROUGHPUT_LESS_FAILURE_ORDERID, errCountRcvLessFailure);
                        errCountRcvLessFailure = 0;
                    }
                    continue;
                }
                Message m;
                std::memcpy(&m, buf, sizeof(m));

                if (!m.validateMsg()) {
                    errCountValidationFailure++;
                    if (errCountValidationFailure == constants::THROUGHPUT_COUNT_LIMIT) {
                        pushInstru(constants::RCV_THROUGHPUT_VALIDATION_FAILURE_ORDERID, errCountValidationFailure);
                        errCountValidationFailure = 0;
                    }
                    continue;
                }

                if (requestQueue.push(m) == QueueStatus::FULL) {
                    errCountQueueFull++;
                    if (errCountQueueFull == constants::THROUGHPUT_COUNT_LIMIT) {
                        pushInstru(constants::RCV_THROUGHPUT_QUEUE_FULL_FAILURE_ORDERID, errCountQueueFull);
                        errCountQueueFull = 0;
                    }
                    continue;       // dropped: no latency sample for an order that never reached the book
                }

                // latency monitoring
                //pushInstru(constants::OVERALL_LATENCY_ORDERID, m.orderId);
            } catch (const std::runtime_error&) {
                errCountRcvFailure++;
                if (errCountRcvFailure == constants::THROUGHPUT_COUNT_LIMIT) {
                    pushInstru(constants::RCV_THROUGHPUT_FAILURE_ORDERID, errCountRcvFailure);
                    errCountRcvFailure = 0;
                }
            }
        }
}

void OrderController::pushInstru(long orderId, long val) {
    if (instrQueue.push(InstrumentationEvent(orderId, Utils::getTS(), val)) == QueueStatus::FULL) errCountInstrQueueFull++;
}

void OrderController::stop() {
    stopRequested.store(true, std::memory_order_relaxed);
    if (t.joinable()) t.join();
}

OrderController::~OrderController() {
    stop();
    if (listenFd >= 0) ::close(listenFd);
}

void OrderController::handleSysCallError(ssize_t res, const char* name, const char* exception) {
        if (res >= 0) return;
        perror(name);
        throw std::runtime_error(exception);
}