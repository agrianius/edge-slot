/*
Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2018 Vitaliy Manushkin.

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <CppUTest/MemoryLeakDetectorNewMacros.h>
#include <CppUTest/TestHarness.h>


#include "edge_slot.hh"
#include <thread>


using bsc::TEdge;
using bsc::GetCallee;
using bsc::Connect;
using bsc::IMessage;
using bsc::TObjectMessage;
using bsc::TMessagePtr;
using bsc::TSignal;
using bsc::TMailbox;
using bsc::MPSC_TailSwap;
using bsc::TMessagePtr;
using bsc::TObjectAnchor;
using bsc::TEdgeSlotThread;


class TTestSlot {
public:
    void test_slot_func(int a, int b) {
        Counter += a + b;
    }

    decltype(GetCallee(&TTestSlot::test_slot_func))::TSlotType Slot =
        decltype(GetCallee(&TTestSlot::test_slot_func))::
        GetSlot<&TTestSlot::test_slot_func>();

    TObjectAnchor Anchor = TObjectAnchor(this);
    ui32 Counter = 0;
};


class TTestEdge {
public:
    TObjectAnchor Anchor = TObjectAnchor(this);
    TEdge<int, int> Edge;
};


class TCallbackSlot {
public:
    void test_slot_func(int a, int b) {
        Counter += a + b;
        if (Callback != nullptr)
            Callback();
    }

    decltype(GetCallee(&TCallbackSlot::test_slot_func))::TSlotType Slot =
        decltype(GetCallee(&TCallbackSlot::test_slot_func))::
        GetSlot<&TCallbackSlot::test_slot_func>();

    TObjectAnchor Anchor = TObjectAnchor(this);
    ui32 Counter = 0;
    std::function<void()> Callback = nullptr;
};



TEST_GROUP(EDGE_SLOT) {
    void teardown() {
        TEdgeSlotThread::LocalMailbox.reset();
    }
};

TEST(EDGE_SLOT, ConnectAndEmit) {
    TTestEdge sig;
    TTestSlot slt;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, TwoEdges) {
    TTestSlot slt;
    TTestEdge sig1;

    {
        TTestEdge sig2;

        CHECK(slt.Counter == 0);
        sig1.Edge.emit(1, 2);
        sig2.Edge.emit(1, 2);
        CHECK(slt.Counter == 0);

        Connect(&sig1.Anchor, &sig1.Edge, &slt.Anchor, &slt.Slot);
        sig1.Edge.emit(1, 2);
        sig2.Edge.emit(1, 2);
        CHECK(slt.Counter == 3);

        Connect(&sig2.Anchor, &sig2.Edge, &slt.Anchor, &slt.Slot);
        sig1.Edge.emit(1, 2);
        sig2.Edge.emit(1, 2);
        CHECK(slt.Counter == 9);
    }

    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 12);
}

TEST(EDGE_SLOT, TwoSlots) {
    TTestSlot slt1;
    TTestEdge sig;

    {
        TTestSlot slt2;

        CHECK(slt1.Counter == 0);
        CHECK(slt2.Counter == 0);

        Connect(&sig.Anchor, &sig.Edge, &slt1.Anchor, &slt1.Slot);
        sig.Edge.emit(1, 2);
        CHECK(slt1.Counter == 3);
        CHECK(slt2.Counter == 0);

        Connect(&sig.Anchor, &sig.Edge, &slt2.Anchor, &slt2.Slot);
        sig.Edge.emit(1, 2);
        CHECK(slt1.Counter == 6);
        CHECK(slt2.Counter == 3);
    }

    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 9);
}

TEST(EDGE_SLOT, EdgeThroughEdge) {
    TTestSlot slt;
    TTestEdge sig1;
    TTestEdge sig2;

    Connect(&sig1.Anchor, &sig1.Edge, &slt.Anchor, &slt.Slot);
    CHECK(slt.Counter == 0);
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    Connect(&sig2.Anchor, &sig2.Edge, &sig1.Anchor, &sig1.Edge);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}


TEST(EDGE_SLOT, EdgeDisconnectSlot) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.disconnect(&slt.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, SlotDisconnectEdge) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    slt.Slot.disconnect(&sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, MultipleConnect) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}

TEST(EDGE_SLOT, EdgeDisconnectSlotOnce) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    sig.Edge.disconnect(&slt.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);

    sig.Edge.disconnect(&slt.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);
}

TEST(EDGE_SLOT, SlotDisconnectEdgeOnce) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    slt.Slot.disconnect(&sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);

    slt.Slot.disconnect(&sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);
}

TEST(EDGE_SLOT, EdgeDisconnectSlotMultiple) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    sig.Edge.disconnect_all(&slt.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}

TEST(EDGE_SLOT, SlotDisconnectEdgeMultiple) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    slt.Slot.disconnect_all(&sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}

TEST(EDGE_SLOT, EdgeDisconnectAllSlots) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    sig.Edge.disconnect_all();
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}

TEST(EDGE_SLOT, SlotDisconnectAllEdges) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    slt.Slot.disconnect_all();
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}

TEST(EDGE_SLOT, ProxyDisconnectSlot) {
    TTestSlot slt1;
    TTestSlot slt2;
    TTestEdge sig;
    TTestEdge proxy;

    Connect(&sig.Anchor, &sig.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&proxy.Anchor, &proxy.Edge, &slt1.Anchor, &slt1.Slot);
    Connect(&proxy.Anchor, &proxy.Edge, &slt2.Anchor, &slt2.Slot);

    CHECK(slt1.Counter == 0);
    CHECK(slt2.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 3);
    CHECK(slt2.Counter == 3);

    proxy.Edge.disconnect(&slt1.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 3);
    CHECK(slt2.Counter == 6);
}

TEST(EDGE_SLOT, ProxyDisconnectAllSlots) {
    TTestSlot slt1;
    TTestSlot slt2;
    TTestEdge sig;
    TTestEdge proxy;

    Connect(&sig.Anchor, &sig.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&proxy.Anchor, &proxy.Edge, &slt1.Anchor, &slt1.Slot);
    Connect(&proxy.Anchor, &proxy.Edge, &slt2.Anchor, &slt2.Slot);

    CHECK(slt1.Counter == 0);
    CHECK(slt2.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 3);
    CHECK(slt2.Counter == 3);

    proxy.Edge.disconnect_all_slots();
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 3);
    CHECK(slt2.Counter == 3);

    Connect(&proxy.Anchor, &proxy.Edge, &slt1.Anchor, &slt1.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 6);
    CHECK(slt2.Counter == 3);
}


TEST(EDGE_SLOT, ProxyDisconnectEdge) {
    TTestSlot slt;
    TTestEdge proxy;
    TTestEdge sig1;
    TTestEdge sig2;

    Connect(&sig1.Anchor, &sig1.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&sig2.Anchor, &sig2.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&proxy.Anchor, &proxy.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    proxy.Edge.disconnect(&sig1.Edge);
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);
}

TEST(EDGE_SLOT, ProxyDisconnectAllEdges) {
    TTestSlot slt;
    TTestEdge proxy;
    TTestEdge sig1;
    TTestEdge sig2;

    Connect(&sig1.Anchor, &sig1.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&sig2.Anchor, &sig2.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&proxy.Anchor, &proxy.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    proxy.Edge.disconnect_all_edges();
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
    proxy.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);
}

TEST(EDGE_SLOT, ProxyDisconnectAllEdgesAndSlots) {
    TTestSlot slt1;
    TTestSlot slt2;
    TTestEdge proxy;
    TTestEdge sig1;
    TTestEdge sig2;

    Connect(&sig1.Anchor, &sig1.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&sig2.Anchor, &sig2.Edge, &proxy.Anchor, &proxy.Edge);
    Connect(&proxy.Anchor, &proxy.Edge, &slt1.Anchor, &slt1.Slot);
    Connect(&proxy.Anchor, &proxy.Edge, &slt2.Anchor, &slt2.Slot);

    proxy.Edge.disconnect_all();
    CHECK(slt1.Counter == 0);
    CHECK(slt2.Counter == 0);

    sig1.Edge.emit(1, 2);
    sig2.Edge.emit(1, 2);

    CHECK(slt1.Counter == 0);
    CHECK(slt2.Counter == 0);
}

TEST(EDGE_SLOT, SlotDisconnectEdgeWhileEmitting) {
    TCallbackSlot slt;
    TTestEdge sig;

    auto cb = [&](){
        slt.Slot.disconnect(&sig.Edge);
    };
    slt.Callback = cb;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

}

TEST(EDGE_SLOT, EdgeDisconnectSlotWhileEmitting) {
    TCallbackSlot slt;
    TTestEdge sig;

    auto cb = [&](){
        sig.Edge.disconnect(&slt.Slot);
    };
    slt.Callback = cb;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

}

TEST(EDGE_SLOT, SlotDisconnectEdgeOnceWhileEmitting) {
    TCallbackSlot slt;
    TTestEdge sig;

    auto cb = [&](){
        slt.Slot.disconnect(&sig.Edge);
        slt.Callback = nullptr;
    };
    slt.Callback = cb;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);

}

TEST(EDGE_SLOT, EdgeDisconnectSlotOnceWhileEmitting) {
    TCallbackSlot slt;
    TTestEdge sig;

    auto cb = [&](){
        sig.Edge.disconnect(&slt.Slot);
        slt.Callback = nullptr;
    };
    slt.Callback = cb;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);

}

TEST(EDGE_SLOT, SlotDisconnectMultipleEdgesWhileEmitting) {
    TCallbackSlot slt;
    TTestEdge sig;

    auto cb = [&](){
        slt.Slot.disconnect_all(&sig.Edge);
        slt.Callback = nullptr;
    };
    slt.Callback = cb;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

}

TEST(EDGE_SLOT, EdgeDisconnectMultipleSlotsWhileEmitting) {
    TCallbackSlot slt;
    TTestEdge sig;

    auto cb = [&](){
        sig.Edge.disconnect_all(&slt.Slot);
        slt.Callback = nullptr;
    };
    slt.Callback = cb;

    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

}

TEST(EDGE_SLOT, Message) {
    TTestSlot slt;
    TSignal<int, int> msg(slt.Anchor.GetLink(), &slt.Slot, 1, 2);
    CHECK(slt.Counter == 0);
    msg.Consume();
    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, MailboxSingleThread) {
    auto proc = []() {
        TTestSlot slt;

        TMessagePtr msg(
            new TSignal<int, int>(slt.Anchor.GetLink(), &slt.Slot, 1, 2));
        TEdgeSlotThread::LocalMailbox->enqueue(std::move(msg));
        CHECK(msg.get() == nullptr);

        auto res = TEdgeSlotThread::LocalMailbox->dequeue();
        CHECK(slt.Counter == 0);
        CHECK(res.get() != nullptr);
        res->Consume();
        CHECK(slt.Counter == 3);
    };

    std::thread run(proc);
    run.join();
}

TEST(EDGE_SLOT, MailboxTwoThreads) {
    MPSC_TailSwap<TMessagePtr> boxlink;
    TTestSlot slt;

    auto proc_dequeue = [&]() {
        TMessagePtr msg;

        while (!boxlink.dequeue(&msg));

        CHECK(slt.Counter == 0);
        CHECK(msg.get() != nullptr);
        msg->Consume();
        CHECK(slt.Counter == 3);
    };

    auto proc_enqueue = [&]() {
        TMessagePtr msg(
            new TSignal<int, int>(slt.Anchor.GetLink(), &slt.Slot, 1, 2));
        boxlink.enqueue(std::move(msg));
        CHECK(msg.get() == nullptr);
    };


    CHECK(slt.Counter == 0);
    std::thread run_dequeue(proc_dequeue);
    std::thread run_enqueue(proc_enqueue);
    run_dequeue.join();
    run_enqueue.join();
    CHECK(slt.Counter == 3);
}


TEST_GROUP(EDGE_SLOT_THREAD) {
    void teardown() {
        TEdgeSlotThread::LocalMailbox.reset();
    }
};

TEST(EDGE_SLOT_THREAD, Create) {
    bool flag = false; // should not be atomic because of thread.join

    auto thread_func = [](bool& flag) {
        flag = true;
    };

    TEdgeSlotThread thr(thread_func, flag);
    thr.join();
    CHECK(flag);
}

TEST(EDGE_SLOT_THREAD, CreateSimple) {
    TEdgeSlotThread thr;
    thr.PostQuitMessage();
    thr.join();
}

TEST(EDGE_SLOT_THREAD, MoveObjectToThread) {
    TEdgeSlotThread thr;

    TTestSlot slt;
    TTestEdge sig;
    Connect(&sig.Anchor, &sig.Edge, &slt.Anchor, &slt.Slot);

    slt.Anchor.MoveToThread(&thr);

    sig.Edge.emit(1, 2);
    thr.PostQuitMessage();
    thr.join();

    CHECK(slt.Counter == 3);
}
