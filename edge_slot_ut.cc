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

#include "edge_slot.hh"
#include <thread>


#include <CppUTest/MemoryLeakDetectorNewMacros.h>
#include <CppUTest/TestHarness.h>


using bsc::Connect;
using bsc::GetCallee;
using bsc::IMessage;
using bsc::MPSC_TailSwap;
using bsc::TEdge;
using bsc::TEdgeSlotObject;
using bsc::TEdgeSlotThread;
using bsc::TEdgeSlotTimer;
using bsc::TMailbox;
using bsc::TMessagePtr;
using bsc::TObjectMessage;
using bsc::TSignal;


class TTestSlot: public TEdgeSlotObject {
public:
    void test_slot_func(int a, int b) {
        Counter += a + b;
    }

    DEFINE_SLOT(TTestSlot, test_slot_func, Slot);

    ui32 Counter = 0;
};


class TCheckMailboxTestSlot: public TEdgeSlotObject {
public:
    void test_slot_func(int a, int b) {
        CHECK(GetAnchor().GetLink()->SameMailbox());

        Counter += a + b;
    }

    DEFINE_SLOT(TCheckMailboxTestSlot, test_slot_func, Slot);

    ui32 Counter = 0;
};


class TTestEdge: public TEdgeSlotObject {
public:
    TEdge<int, int> Edge = TEdge<int, int>(this);
};

class TTriggerPostQuitMessage: public TEdgeSlotObject {
public:
	void timeout() {
		TEdgeSlotThread::PostSelfQuitMessage();
	}

	DEFINE_SLOT(TTriggerPostQuitMessage, timeout, Slot);
};


class TCallbackSlot: public TEdgeSlotObject {
public:
    void test_slot_func(int a, int b) {
        Counter += a + b;
        if (Callback != nullptr)
            Callback();
    }

    DEFINE_SLOT(TCallbackSlot, test_slot_func, Slot);

    ui32 Counter = 0;
    std::function<void()> Callback = nullptr;
};



TEST_GROUP(EDGE_SLOT) {
    void setup() {
        TEdgeSlotThread::LocalMailbox = std::make_shared<TMailbox>();
    }

    void teardown() {
        TEdgeSlotThread::LocalMailbox.reset();
        TEdgeSlotThread::CleanupTimers();
    }
};


TEST(EDGE_SLOT, ConnectAndEmit) {
    TTestEdge sig;
    TTestSlot slt;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

        Connect(&sig1, &sig1.Edge, &slt, &slt.Slot);
        sig1.Edge.emit(1, 2);
        sig2.Edge.emit(1, 2);
        CHECK(slt.Counter == 3);

        Connect(&sig2, &sig2.Edge, &slt, &slt.Slot);
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

        Connect(&sig, &sig.Edge, &slt1, &slt1.Slot);
        sig.Edge.emit(1, 2);
        CHECK(slt1.Counter == 3);
        CHECK(slt2.Counter == 0);

        Connect(&sig, &sig.Edge, &slt2, &slt2.Slot);
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

    Connect(&sig1, &sig1.Edge, &slt, &slt.Slot);
    CHECK(slt.Counter == 0);
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    Connect(&sig2, &sig2.Edge, &sig1, &sig1.Edge);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}


TEST(EDGE_SLOT, EdgeDisconnectSlot) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    slt.Slot.disconnect(sig.GetAnchor().GetLink(), &sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, MultipleConnect) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);
}

TEST(EDGE_SLOT, EdgeDisconnectSlotOnce) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    slt.Slot.disconnect(sig.GetAnchor().GetLink(), &sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);

    slt.Slot.disconnect(sig.GetAnchor().GetLink(), &sig.Edge);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 9);
}

TEST(EDGE_SLOT, EdgeDisconnectSlotMultiple) {
    TTestSlot slt;
    TTestEdge sig;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &proxy, &proxy.Edge);
    Connect(&proxy, &proxy.Edge, &slt1, &slt1.Slot);
    Connect(&proxy, &proxy.Edge, &slt2, &slt2.Slot);

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

    Connect(&sig, &sig.Edge, &proxy, &proxy.Edge);
    Connect(&proxy, &proxy.Edge, &slt1, &slt1.Slot);
    Connect(&proxy, &proxy.Edge, &slt2, &slt2.Slot);

    CHECK(slt1.Counter == 0);
    CHECK(slt2.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 3);
    CHECK(slt2.Counter == 3);

    proxy.Edge.disconnect_all_slots();
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 3);
    CHECK(slt2.Counter == 3);

    Connect(&proxy, &proxy.Edge, &slt1, &slt1.Slot);
    sig.Edge.emit(1, 2);
    CHECK(slt1.Counter == 6);
    CHECK(slt2.Counter == 3);
}


TEST(EDGE_SLOT, ProxyDisconnectEdge) {
    TTestSlot slt;
    TTestEdge proxy;
    TTestEdge sig1;
    TTestEdge sig2;

    Connect(&sig1, &sig1.Edge, &proxy, &proxy.Edge);
    Connect(&sig2, &sig2.Edge, &proxy, &proxy.Edge);
    Connect(&proxy, &proxy.Edge, &slt, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig1.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);
    sig2.Edge.emit(1, 2);
    CHECK(slt.Counter == 6);

    proxy.Edge.disconnect(sig1.GetAnchor().GetLink(), &sig1.Edge);
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

    Connect(&sig1, &sig1.Edge, &proxy, &proxy.Edge);
    Connect(&sig2, &sig2.Edge, &proxy, &proxy.Edge);
    Connect(&proxy, &proxy.Edge, &slt, &slt.Slot);

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

    Connect(&sig1, &sig1.Edge, &proxy, &proxy.Edge);
    Connect(&sig2, &sig2.Edge, &proxy, &proxy.Edge);
    Connect(&proxy, &proxy.Edge, &slt1, &slt1.Slot);
    Connect(&proxy, &proxy.Edge, &slt2, &slt2.Slot);

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
        slt.Slot.disconnect(sig.GetAnchor().GetLink(), &sig.Edge);
    };
    slt.Callback = cb;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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
        slt.Slot.disconnect(sig.GetAnchor().GetLink(), &sig.Edge);
        slt.Callback = nullptr;
    };
    slt.Callback = cb;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

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

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

}

TEST(EDGE_SLOT, EdgeDisconnectMultipleSlotsWhileEmitting) {


    TCallbackSlot slt;
    TTestEdge sig;

    CHECK(slt.GetAnchor().GetLink()->GetMailbox().get() != nullptr);

    auto cb = [&](){
        sig.Edge.disconnect_all(&slt.Slot);
        slt.Callback = nullptr;
    };
    slt.Callback = cb;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

}

TEST(EDGE_SLOT, Message) {
    TTestSlot slt;
    TSignal<int, int> msg(slt.GetAnchor().GetLink(), &slt.Slot, 1, 2);
    CHECK(slt.Counter == 0);
    msg.Consume();
    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, MailboxSingleThread) {
    auto proc = []() {
        TTestSlot slt;

        TMessagePtr msg(
            new TSignal<int, int>(slt.GetAnchor().GetLink(), &slt.Slot, 1, 2));
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
            new TSignal<int, int>(slt.GetAnchor().GetLink(), &slt.Slot, 1, 2));
        boxlink.enqueue(std::move(msg));
        CHECK(msg.get() == nullptr);
    };


    CHECK(slt.Counter == 0);
    std::thread run_enqueue(proc_enqueue);
    run_enqueue.join();
    std::thread run_dequeue(proc_dequeue);
    run_dequeue.join();
    CHECK(slt.Counter == 3);
}


TEST_GROUP(EDGE_SLOT_THREAD) {
    void setup() {
        TEdgeSlotThread::LocalMailbox = std::make_shared<TMailbox>();
    }

    void teardown() {
        TEdgeSlotThread::LocalMailbox.reset();
        TEdgeSlotThread::CleanupTimers();
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

    TCheckMailboxTestSlot slt;
    TTestEdge sig;
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);

    thr.GrabObject(&slt);

    sig.Edge.emit(1, 2);
    thr.PostQuitMessage();
    thr.join();

    CHECK(slt.Counter == 3);
}


TEST(EDGE_SLOT_THREAD, ConnectToObjectInAnotherThreadAndEmit) {
    TEdgeSlotThread thr;

    TCheckMailboxTestSlot slt;
    thr.GrabObject(&slt);

    TTestEdge sig;
    Connect(&sig, &sig.Edge, &slt, &slt.Slot);
    sig.Edge.emit(1, 2);

    thr.PostQuitMessage();
    thr.join();

    CHECK(slt.Counter == 3);
}


TEST(EDGE_SLOT_THREAD, BlockingDelivery) {
    TEdgeSlotThread thr;

    TCheckMailboxTestSlot slt;
    TTestEdge sig;
    Connect(&sig, &sig.Edge, &slt, &slt.Slot, bsc::DELIVERY::BLOCK_QUEUE);

    thr.GrabObject(&slt);

    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3);

    thr.PostQuitMessage();
    thr.join();

    CHECK(slt.Counter == 3);
}

TEST(EDGE_SLOT, Timer) {
	TEdgeSlotTimer timer(100000);
	TTriggerPostQuitMessage slt;

	Connect(&timer, &timer.Timeout, &slt, &slt.Slot);
	timer.Activate();

	TEdgeSlotThread::MessageLoop();
}


TEST(EDGE_SLOT, WaitForSignal) {
    TEdgeSlotTimer timer(100000);

    auto starter = [&]() -> bool {
        timer.Activate();
        return true;
    };

    bool catched =
        TEdgeSlotThread::WaitForSignal(&timer, &timer.Timeout, starter);
    CHECK(catched);
}


TEST(EDGE_SLOT, WaifForSignalAndDeleteEdge) {
    std::unique_ptr<TTestEdge> sig(new TTestEdge);

    auto deleter = [&]() -> bool {
        sig.reset();
        return true;
    };

    bool catched =
        TEdgeSlotThread::WaitForSignal(sig.get(), &sig->Edge, deleter);

    CHECK(!catched);
}


TEST(EDGE_SLOT, NoDeadlockFor_BLOCK_QUEUE_InOneThread) {
    TTestEdge sig;
    TTestSlot slt;

    Connect(&sig, &sig.Edge, &slt, &slt.Slot, bsc::DELIVERY::BLOCK_QUEUE);

    CHECK(slt.Counter == 0);
    sig.Edge.emit(1, 2);
    CHECK(slt.Counter == 3)
}
