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


#pragma once
#include <atomic>
#include <utility>
#include "mt_semaphore.hh"


namespace bsc {


template <typename Payload>
class MPSC_TailSwap {
public:
    MPSC_TailSwap() {
        auto new_elem = new Elem;
        head = new_elem;
        tail.store(new_elem, std::memory_order_relaxed);
    }

    ~MPSC_TailSwap() {
        for (auto i = head; i != nullptr;) {
            auto next = i->next.load(std::memory_order_relaxed);
            delete i;
            i = next;
        }
    }

    void enqueue(Payload payload) {
        auto new_elem = new Elem(std::move(payload));
        auto prev = tail.exchange(new_elem, std::memory_order_seq_cst);
        prev->next.store(new_elem, std::memory_order_release);
    }

    bool dequeue(Payload* store) {
        Elem* next = head->next.load(std::memory_order_acquire);
        if (next == nullptr)
            return false;
        *store = std::move(next->payload);
        delete head;
        head = next;
        return true;
    }

protected:
    struct Elem {
        Elem(): next(nullptr) {}
        Elem(Payload pl): next(nullptr), payload(std::move(pl)) {}

        std::atomic<Elem*> next;
        Payload payload;
    };

    Elem* head;
    std::atomic<Elem*> tail;
};


template <typename TPayload>
class MPSC_TailSwap_Wait {
public:
    void enqueue(TPayload msg) {
        Queue.enqueue(std::move(msg));
        if (Sem.Get() <= 0)
            Sem.Post();
    }

    TPayload dequeue() {
        TPayload result;
        for (;;) {
            if (Queue.dequeue(&result))
                return result;
            Sem.Wait();
        }
    }

    bool dequeue(TPayload* result, ui64 wait_time) {
        for (;;) {
            if (Queue.dequeue(result))
                return true;
            if (Sem.Wait(wait_time))
                return false;
        }
    }

protected:
    MPSC_TailSwap<TPayload> Queue;
    TSemaphore Sem;
};



}
