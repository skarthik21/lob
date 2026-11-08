#include <gtest/gtest.h>
#include "Spsc.h"
#include <memory>
#include "Message.h"
#include <vector>


struct NoDefaultConstr {
    NoDefaultConstr() = delete;
};

struct NonMovable {
    NonMovable() = default;
    NonMovable(NonMovable&&) = delete;
    NonMovable& operator=(NonMovable&&) = delete;
};

template<typename T, std::size_t size>
concept isValidSpsc = requires { typename Spsc<T, size>;};

// no DefaultConstr
static_assert(!isValidSpsc<NoDefaultConstr, 4>);
// non movable
static_assert(!isValidSpsc<NonMovable, 4>);
static_assert(!isValidSpsc<int&, 8>);
static_assert(isValidSpsc<std::unique_ptr<int>, 8>);


// size param test
static_assert(!isValidSpsc<int, 3>);
static_assert(!isValidSpsc<int, 0>);
static_assert(isValidSpsc<int, 2>);
static_assert(isValidSpsc<int, 256>);

Message createMsg(int msgId) {
    return Message(msgId, 1, Tick::ALPHABET, 1, 20, 1000, -1, MessageType::ADD_ORDER, Side::BUY);
}

void PrintTo(const QueueStatus& queueStatus, std::ostream* os) {
    switch (queueStatus)
    {
    case QueueStatus::EMPTY:
        *os << "QueueStatus EMPTY";
        break;
    case QueueStatus::FULL:
        *os << "QueueStatus FULL";
        break;
    case QueueStatus::POPPED:
        *os << "QueueStatus POPPED";
        break;
    case QueueStatus::PUSHED:
        *os << "QueueStatus PUSHED";
        break;
    }
}

template<std::size_t size> 
void popQueueCheck(Spsc<Message, size>& spsc, std::size_t currSize, std::vector<Message> messages) {
    Message poppedMsg;
    for (auto msg : messages) {
        currSize--;
        ASSERT_EQ(spsc.tryPop(poppedMsg), QueueStatus::POPPED);
        ASSERT_EQ(poppedMsg, msg);
        ASSERT_EQ(spsc.getQueueSize(), currSize);
    }
}

template<std::size_t size>
std::vector<Message> pushQueueCheck(Spsc<Message, size>& spsc, std::size_t currSize, std::size_t msgId) {
    std::vector<Message> ans;
    for (auto i = currSize + 1; i<=size;i++) {
        Message msg = createMsg(msgId);
        EXPECT_EQ(spsc.push(msg), QueueStatus::PUSHED);
        EXPECT_EQ(spsc.getQueueSize(), i);
        ans.push_back(msg);
        msgId++;
    }
    return ans;
}

template<std::size_t size>
std::vector<Message> rvalPushQueueCheck(Spsc<Message, size>& spsc, std::size_t currSize, std::size_t msgId) {
    std::vector<Message> ans;
    for (auto i = currSize + 1; i<=size;i++) {
        Message msg = createMsg(msgId);
        EXPECT_EQ(spsc.push(std::move(createMsg(msgId))), QueueStatus::PUSHED);
        EXPECT_EQ(spsc.getQueueSize(), i);
        ans.push_back(msg);
        msgId++;
    }
    return ans;
}





// how do we test the concpets defined for the templates
TEST(SpscTest, CompileTimeChecksPasses) { SUCCEED(); }

TEST(SpscTest, InitQueueShouldBeEmpty) {
    Spsc<Message, 32> spsc = Spsc<Message, 32>("test queue");
    EXPECT_EQ(spsc.getQueueSize(), 0);
}

TEST(SpscTest, PushOnFullQueueShouldFail) {
    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");

    std::vector<Message> messages = pushQueueCheck(spsc, 0, 1);

    // push when queue is filled should fail with QueueStatus::FULL
    Message m4 = createMsg(5);
    EXPECT_EQ(spsc.push(m4), QueueStatus::FULL);
    EXPECT_EQ(spsc.getQueueSize(), 4);

    // Pop Queue to check contents
    popQueueCheck(spsc, 4, messages);

}


TEST(SpscTest, RvalPushOnFullQueueShouldFail) {
    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");

    //push to fill the queue ( use both variants of push)
    // rvalue
    std::vector<Message> messages = rvalPushQueueCheck(spsc, 0, 1);

    // push when queue is filled should fail with QueueStatus::FULL
    EXPECT_EQ(spsc.push(createMsg(5)), QueueStatus::FULL);
    EXPECT_EQ(spsc.getQueueSize(), 4);


    popQueueCheck(spsc, 4, messages);
}

TEST(SpscTest,  PopOnAEmptyQueueShouldFail) {
    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");

    //pop an empty queue
    Message m;
    EXPECT_EQ(spsc.tryPop(m), QueueStatus::EMPTY);
}

// wrap tests
TEST(SpscTest, PushAndPopWrapShouldPass) {
    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");

    std::vector<Message> messages = pushQueueCheck(spsc, 0, 1);


    popQueueCheck(spsc, 4, {messages[0], messages[1]});

    std::vector<Message> messagesNew = pushQueueCheck(spsc, 2, 5);

    popQueueCheck(spsc, 4, {messages[2], messages[3], messagesNew[0], messagesNew[1]});

}


TEST(SpscTest, RvalPushAndPopWrapShouldPass) {
    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");



    std::vector<Message> messages = rvalPushQueueCheck(spsc, 0, 1);

    popQueueCheck(spsc, 4, {messages[0], messages[1]});

    std::vector<Message> messagesNew = rvalPushQueueCheck(spsc, 2, 5);

    popQueueCheck(spsc, 4, {messages[2], messages[3], messagesNew[0], messagesNew[1]});

}

class PushedSpscTest : public ::testing::Test {
protected:
    PushedSpscTest(){};

    void SetUp() override {
        // push to queue
        ASSERT_EQ(spsc.push(Message(1, 1, Tick::ALPHABET, 1, 20, 1000, -1, MessageType::ADD_ORDER, Side::BUY)), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 1);
        ASSERT_EQ(spsc.push(m1), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 2);
    }


    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");
    Message m = Message(1, 1, Tick::ALPHABET, 1, 20, 1000, -1, MessageType::ADD_ORDER, Side::BUY);
    Message m1 = Message(2, 1, Tick::ALPHABET, 1, 20, 1000, -1, MessageType::ADD_ORDER, Side::BUY);

};

TEST_F(PushedSpscTest, PopOnAQueueShouldReturnPushedMessage) {

    popQueueCheck(spsc, 2, {m, m1});

}

/*
if we keep the batch methods as template methods then - we are losing the edge while testing which indicates problem with the design
 - the reason for going with templated size is for we get to know the size at compile time and we can use fixed size structures but if we
 are sending a fixed size vector from consumer or if the consumer already doing a reserve (compile time allocation) then it will same 
 performance wise - but in this case we are not getting any benifit with doing things at compile time and we are losing the flexibility in testing
*/

struct BatchTestParam {
    int batchSize;
    std::size_t queueSize;
    QueueStatus resStatus;
    std::vector<Message> res;
};

class BatchPopSpscParamTest : public ::testing::TestWithParam<BatchTestParam> {
protected:
    BatchPopSpscParamTest(){};

    void SetUp() override {
        // push to queue
        ASSERT_EQ(spsc.push(createMsg(1)), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 1);
        ASSERT_EQ(spsc.push(m1), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 2);
    }


    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");
    Message m = createMsg(1);
    Message m1 = createMsg(2);

};

TEST_P(BatchPopSpscParamTest, BatchPopOnAQueue) {
    BatchTestParam batchParam =  GetParam();
    std::vector<Message> poppedRes(batchParam.batchSize, createMsg(-1));
    ASSERT_EQ(spsc.tryPopBatch(poppedRes), batchParam.resStatus);
    ASSERT_EQ(poppedRes.size(), batchParam.batchSize);
    ASSERT_EQ(spsc.getQueueSize(), batchParam.queueSize);
    for (int i = 0;i<batchParam.batchSize;i++) {
        ASSERT_EQ(poppedRes[i], batchParam.res[i]);
    }
};

INSTANTIATE_TEST_SUITE_P(PopBatchSizeTests, BatchPopSpscParamTest, ::testing::Values(
    BatchTestParam(3, 2, QueueStatus::EMPTY, {createMsg(-1), createMsg(-1), createMsg(-1)}),
    BatchTestParam(1, 1, QueueStatus::POPPED, {createMsg(1)}),
    BatchTestParam(2, 0, QueueStatus::POPPED, {createMsg(1), createMsg(2)})
));


// Push Batch - lvalue ref

class BatchPushSpscParamTest : public ::testing::TestWithParam<BatchTestParam> {
protected:
    BatchPushSpscParamTest(){};

    void SetUp() override {
        // push to queue
        ASSERT_EQ(spsc.push(createMsg(1)), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 1);
        ASSERT_EQ(spsc.push(m1), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 2);
    }


    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");
    Message m = createMsg(1);
    Message m1 = createMsg(2);

};


TEST_P(BatchPushSpscParamTest, BatchPushOnAQueue) {
    BatchTestParam batchParam =  GetParam();
    ASSERT_EQ(spsc.pushBatch(batchParam.res), batchParam.resStatus);
    ASSERT_EQ(spsc.getQueueSize(), batchParam.queueSize);

    std::vector<Message> msgs{m, m1};
    msgs.insert(msgs.end(), batchParam.res.begin(), batchParam.res.end());

    std::vector<Message> poppedRes;
    poppedRes.resize(batchParam.queueSize);
    ASSERT_EQ(spsc.tryPopBatch(poppedRes), QueueStatus::POPPED);
    ASSERT_EQ(poppedRes.size(), batchParam.queueSize);

    for (std::size_t i = 0;i<batchParam.queueSize; i++) {
         ASSERT_EQ(poppedRes[i], msgs[i]);
    }
};

INSTANTIATE_TEST_SUITE_P(PushBatchSizeTests, BatchPushSpscParamTest, ::testing::Values(
    BatchTestParam(8, 2, QueueStatus::FULL, {createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1)}),
    BatchTestParam(3, 2, QueueStatus::FULL, {createMsg(3), createMsg(4), createMsg(5)}),
    BatchTestParam(1, 3, QueueStatus::PUSHED, {createMsg(3)}),
    BatchTestParam(2, 4, QueueStatus::PUSHED, {createMsg(3), createMsg(4)})
));


// Push Batch - rvalue ref


class BatchRvalPushSpscParamTest : public ::testing::TestWithParam<BatchTestParam> {
protected:
    BatchRvalPushSpscParamTest(){};

    void SetUp() override {
        // push to queue
        ASSERT_EQ(spsc.push(createMsg(1)), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 1);
        ASSERT_EQ(spsc.push(m1), QueueStatus::PUSHED);
        ASSERT_EQ(spsc.getQueueSize(), 2);
    }


    Spsc<Message, 4> spsc = Spsc<Message, 4>("test queue");
    Message m = createMsg(1);
    Message m1 = createMsg(2);

};

TEST_P(BatchRvalPushSpscParamTest, BatchRvalPushOnAQueue) {
    BatchTestParam batchParam =  GetParam();
    std::vector<Message> resCpy = batchParam.res;
    ASSERT_EQ(spsc.pushBatch(std::move(batchParam.res)), batchParam.resStatus);
    ASSERT_EQ(spsc.getQueueSize(), batchParam.queueSize);

    std::vector<Message> msgs{m, m1};
    msgs.insert(msgs.end(), resCpy.begin(), resCpy.end());

    std::vector<Message> poppedRes;
    poppedRes.resize(batchParam.queueSize);
    ASSERT_EQ(spsc.tryPopBatch(poppedRes), QueueStatus::POPPED);
    ASSERT_EQ(poppedRes.size(), batchParam.queueSize);

    for (std::size_t i = 0;i<batchParam.queueSize; i++) {
         ASSERT_EQ(poppedRes[i], msgs[i]);
    }
};

INSTANTIATE_TEST_SUITE_P(RvalPushBatchSizeTests, BatchRvalPushSpscParamTest, ::testing::Values(
    BatchTestParam(8, 2, QueueStatus::FULL, {createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1), createMsg(-1)}),
    BatchTestParam(3, 2, QueueStatus::FULL, {createMsg(3), createMsg(4), createMsg(5)}),
    BatchTestParam(1, 3, QueueStatus::PUSHED, {createMsg(3)}),
    BatchTestParam(2, 4, QueueStatus::PUSHED, {createMsg(3), createMsg(4)})
));

// wrap tests

TEST_F(PushedSpscTest, BatchPushAndPopWithWrapShouldPass) {

    Message m2 = createMsg(3);
    Message m3 = createMsg(4);


    std::vector<Message> res{m2, m3};
    ASSERT_EQ(spsc.pushBatch(res), QueueStatus::PUSHED);
    ASSERT_EQ(spsc.getQueueSize(), 4);

    std::vector<Message> poppedRes(4);
    ASSERT_EQ(spsc.tryPopBatch(poppedRes), QueueStatus::POPPED);
    EXPECT_EQ(poppedRes[0], m);
    EXPECT_EQ(poppedRes[1], m1);
    EXPECT_EQ(poppedRes[2], m2);
    EXPECT_EQ(poppedRes[3], m3);
    ASSERT_EQ(spsc.getQueueSize(), 0);

    Message m4 = createMsg(5);
    Message m5 = createMsg(6);


    std::vector<Message> res1{m4, m5};
    ASSERT_EQ(spsc.pushBatch(res1), QueueStatus::PUSHED);
    ASSERT_EQ(spsc.getQueueSize(), 2);

    std::vector<Message> poppedRes1(2);
    ASSERT_EQ(spsc.tryPopBatch(poppedRes1), QueueStatus::POPPED);
    EXPECT_EQ(poppedRes1[0], m4);
    EXPECT_EQ(poppedRes1[1], m5);
    ASSERT_EQ(spsc.getQueueSize(), 0);

}

TEST_F(PushedSpscTest, BatchRvalPushAndPopWithWrapShouldPass) {

    Message m2 = createMsg(3);
    Message m3 = createMsg(4);


    ASSERT_EQ(spsc.pushBatch(std::vector<Message> {createMsg(3), createMsg(4)}), QueueStatus::PUSHED);
    ASSERT_EQ(spsc.getQueueSize(), 4);

    std::vector<Message> poppedRes(4);
    ASSERT_EQ(spsc.tryPopBatch(poppedRes), QueueStatus::POPPED);
    EXPECT_EQ(poppedRes[0], m);
    EXPECT_EQ(poppedRes[1], m1);
    EXPECT_EQ(poppedRes[2], m2);
    EXPECT_EQ(poppedRes[3], m3);
    ASSERT_EQ(spsc.getQueueSize(), 0);

    Message m4 = createMsg(5);
    Message m5 = createMsg(6);


    ASSERT_EQ(spsc.pushBatch(std::vector<Message>{createMsg(5),  createMsg(6)}), QueueStatus::PUSHED);
    ASSERT_EQ(spsc.getQueueSize(), 2);

    std::vector<Message> poppedRes1(2);
    ASSERT_EQ(spsc.tryPopBatch(poppedRes1), QueueStatus::POPPED);
    EXPECT_EQ(poppedRes1[0], m4);
    EXPECT_EQ(poppedRes1[1], m5);
    ASSERT_EQ(spsc.getQueueSize(), 0);
}