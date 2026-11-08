#include "Message.h"
#include "Tick.h"
#include <unordered_map>
#include "OrderController.h"
#include "RequestDispatcher.h"
#include "OrderBook.h"
#include "MessageLogger.h"
#include "NSpsc.h"
#include <memory>
#include <csignal>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

class Main {

public:
     static int run() {

        try {
            std::signal(SIGINT, signalHandler);
            std::signal(SIGTERM, signalHandler);

            std::unique_ptr<NSpsc<Message>> requestQueue = std::make_unique<NSpsc<Message>>("request_queue", REQUEST_QUEUE_SIZE);
            std::unique_ptr<NSpsc<InstrumentationEvent>> instrumentationQueue = std::make_unique<NSpsc<InstrumentationEvent>>("order_controller_instrumentation_queue", INSTRUMENTATION_QUEUE_SIZE);


            // Book Maps for each tick
            std::unique_ptr<NSpsc<Message>> appleBookQueue = std::make_unique<NSpsc<Message>>("apple_book_queue", REQUEST_QUEUE_SIZE);
            std::unique_ptr<NSpsc<Message>> alphabetBookQueue = std::make_unique<NSpsc<Message>>("alphabet_book_queue", REQUEST_QUEUE_SIZE);
            std::unique_ptr<NSpsc<Message>> mcsftBookQueue = std::make_unique<NSpsc<Message>>("mcsft_book_queue", REQUEST_QUEUE_SIZE);

            // Log Maps for each tick
            std::unique_ptr<NSpsc<LogMessage>> appleLogMsgQueue = std::make_unique<NSpsc<LogMessage>>("apple_log_queue", LOG_MESSAGE_QUEUE_SIZE);
            std::unique_ptr<NSpsc<LogMessage>> alphabetLogMsgQueue = std::make_unique<NSpsc<LogMessage>>("alphabet_log_queue", LOG_MESSAGE_QUEUE_SIZE);
            std::unique_ptr<NSpsc<LogMessage>> mcsftLogMsgQueue = std::make_unique<NSpsc<LogMessage>>("mcsft_log_queue", LOG_MESSAGE_QUEUE_SIZE);

            std::unordered_map<Tick, NSpsc<Message>*> bookMap = {
                {Tick::APPLE, appleBookQueue.get()},
                {Tick::ALPHABET, alphabetBookQueue.get()},
                {Tick::MCSFT, mcsftBookQueue.get()}
            };

            std::unordered_map<Tick, NSpsc<LogMessage>*> bookLogMap = {
                {Tick::APPLE, appleLogMsgQueue.get()},
                {Tick::ALPHABET, alphabetLogMsgQueue.get()},
                {Tick::MCSFT, mcsftLogMsgQueue.get()}
            };

            OrderController controller = OrderController(8099, *requestQueue, *instrumentationQueue);
            RequestDispatcher dispatcher = RequestDispatcher(*requestQueue, bookMap);

            // OrderBooks for each TICK
            OrderBook appleOrderBook = OrderBook(*bookMap[Tick::APPLE], *bookLogMap[Tick::APPLE], Tick::APPLE, 100, 2, 300, 1000, 50, true);
            OrderBook alphabetOrderBook = OrderBook(*bookMap[Tick::ALPHABET], *bookLogMap[Tick::ALPHABET], Tick::ALPHABET, 200, 3, 200, 1000, 50, true);
            OrderBook mcsftOrderBook = OrderBook(*bookMap[Tick::MCSFT], *bookLogMap[Tick::MCSFT], Tick::MCSFT, 450, 2, 300, 1000, 50, true);

            // loggers for each TICK
            MessageLogger<LogMessage> appleMessageLogger = MessageLogger(*bookLogMap[Tick::APPLE], BASE_PATH, 100, Tick::APPLE);
            MessageLogger<LogMessage> alphabetMessageLogger = MessageLogger(*bookLogMap[Tick::ALPHABET], BASE_PATH, 100, Tick::ALPHABET);
            MessageLogger<LogMessage> mcsftMessageLogger = MessageLogger(*bookLogMap[Tick::MCSFT], BASE_PATH, 100, Tick::MCSFT);


            // start order loggers -> orderbook -> dispatcher -> controller
            // stop order controller->dispatcher->orderbook->loggers
            start(controller, dispatcher, {&appleOrderBook, &alphabetOrderBook, &mcsftOrderBook}, {&appleMessageLogger, &alphabetMessageLogger, &mcsftMessageLogger});
            while (!signalPressed.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stop(controller, dispatcher, {&appleOrderBook, &alphabetOrderBook, &mcsftOrderBook}, {&appleMessageLogger, &alphabetMessageLogger, &mcsftMessageLogger});
            return 0;

        } catch (const std::exception& e) {
            std::cerr << "startup failed: " << e.what() << '\n';
            return 1;
        }
    }

private:
    static inline std::atomic_bool signalPressed = false;
    static constexpr std::size_t REQUEST_QUEUE_SIZE = std::size_t{1}<<16;
    static constexpr std::size_t INSTRUMENTATION_QUEUE_SIZE = std::size_t{1}<<12;    
    static constexpr std::size_t LOG_MESSAGE_QUEUE_SIZE = std::size_t{1}<<12;
    static constexpr std::string BASE_PATH = "/tmp/lob/book";


    static void start(OrderController& controller, RequestDispatcher& dispatcher, const std::vector<OrderBook*>& books, const std::vector<MessageLogger<LogMessage>*>& loggers) {

        for (auto logger: loggers) {
            logger->start();
        }

        for (auto book : books) {
            book->start();
        }

        dispatcher.start();
        controller.start();

    }

    static void stop(OrderController& controller, RequestDispatcher& dispatcher, const std::vector<OrderBook*>& books, const std::vector<MessageLogger<LogMessage>*>& loggers) {

        controller.stop();
        dispatcher.stop();

        for (auto book : books) {
            book->stop();
        }

        for (auto logger: loggers) {
            logger->stop();
        }

    }

    static void signalHandler(int signal) {
        if ((signal == SIGINT) || (signal == SIGTERM)) signalPressed.store(true);
    }

};

int main() {
    return Main::run();
}