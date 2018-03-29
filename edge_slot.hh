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
#include <vector>
#include "types.hh"
#include <atomic>
#include "mt_queue.hh"
#include "compiler.hh"
#include <tuple>
#include <memory>
#include <thread>
#include "spinrwlock.hh"
#include "mt_semaphore.hh"


namespace bsc {


template <typename...TParams>
class TEdge;

template <typename...TParams>
class TSlot;

template <typename...TParams>
using TConnectCallee = void (*)(const TSlot<TParams...>*, void*, TParams...);


class IMessage {
public:
    virtual ~IMessage() noexcept = default;
    virtual void Consume() = 0;
};


using TMessagePtr = std::unique_ptr<IMessage>;


class TMailbox {
public:
    void enqueue(TMessagePtr msg) {
        Queue.enqueue(std::move(msg));
        if (Sem.Get() <= 0)
            Sem.Post();
    }

    TMessagePtr dequeue() {
        TMessagePtr result;
        for (;;) {
            if (Queue.dequeue(&result))
                return result;
            Sem.Wait();
        }
    }
protected:
    MPSC_TailSwap<TMessagePtr> Queue;
    TSemaphore Sem;
};


class TObjectAnchor;


class TEdgeSlotThread {
public:
    TEdgeSlotThread()
        : Mailbox(std::make_shared<TMailbox>())
    {
        Thread = std::thread(MessageLoop, this);
    }

    TEdgeSlotThread(TEdgeSlotThread&&) = default;
    TEdgeSlotThread& operator=(TEdgeSlotThread&&) = default;

    template <class Fn, class...Params>
    explicit TEdgeSlotThread(Fn&& fn, Params&&...params)
        : Mailbox(std::make_shared<TMailbox>())
    {
        Thread = std::thread(
            ThreadFuncWrapper<Fn, Params...>,
            this,
            std::ref(fn),
            std::ref(params)...);
    }

    void GrabObject(TObjectAnchor* anchor) const;

    std::shared_ptr<TMailbox> GetMailbox() const noexcept {
        return Mailbox;
    }

    static thread_local std::shared_ptr<TMailbox> LocalMailbox;

    void join() {
        return Thread.join();
    }

    void detach() {
        Thread.detach();
    }

    bool joinable() const {
        return Thread.joinable();
    }

    std::thread::id get_id() const noexcept {
        return Thread.get_id();
    }

    class EQuitLoop {};

    class TQuitMessage: public IMessage {
    public:
        virtual void Consume() override {
            throw EQuitLoop();
        }
    };

    void PostQuitMessage() const noexcept {
        TMessagePtr msg(new TQuitMessage);
        Mailbox->enqueue(std::move(msg));
    }

    static void MessageLoop(TEdgeSlotThread* self) noexcept {
        LocalMailbox = self->Mailbox;
        for (;;) {
            auto msg = TEdgeSlotThread::LocalMailbox->dequeue();
            try {
                msg->Consume();
            } catch (EQuitLoop&) {
                return;
            } catch (...) {
            }
        }
    };

protected:
    std::shared_ptr<TMailbox> Mailbox;
    std::thread Thread;

    template <class Fn, class...Params>
    static void
    ThreadFuncWrapper(TEdgeSlotThread* self, Fn&& fn, Params&&...params) {
        LocalMailbox = self->Mailbox;
        fn(std::forward<Params>(params)...);
    };
};


class TObjectMonitor {
public:
    TObjectMonitor(void* object) noexcept
        : RefCounter(1)
        , Object(object)
        , Mailbox(TEdgeSlotThread::LocalMailbox)
    {}

    TObjectMonitor(const TObjectMonitor&) = delete;
    TObjectMonitor(TObjectMonitor&&) = delete;
    void operator=(const TObjectMonitor&) = delete;
    void operator=(TObjectMonitor&&) = delete;

    void AddReference() noexcept {
        RefCounter.fetch_add(2, std::memory_order_seq_cst);
    }

    void RemoveReference() noexcept {
        if (RefCounter.fetch_sub(2, std::memory_order_seq_cst) == 2)
            delete this;
    }

    void ObjectIsDead() noexcept {
        // RefCounter -= 1  and AddReference() in one operation
        RefCounter.fetch_add(1, std::memory_order_seq_cst);

        // Drop mailbox to avoid cyclic referencing
        SetMailbox(std::shared_ptr<TMailbox>());
        RemoveReference();
    }

    bool IsAlive() const noexcept {
        return 1 & RefCounter.load(std::memory_order_acquire);
    }

    void* GetObject() const noexcept {
        return Object.load();
    }

    std::shared_ptr<TMailbox> GetMailbox() const noexcept {
        TReadGuard guard(&MailboxLock);
        return Mailbox;
    }

    void SetMailbox(std::shared_ptr<TMailbox> mailbox) noexcept {
        TWriteGuard guard(&MailboxLock);
        Mailbox = std::move(mailbox);
    }

    bool SameMailbox() const noexcept {
        TReadGuard guard(&MailboxLock); // UB if no read guard
        return TEdgeSlotThread::LocalMailbox.get() == Mailbox.get();
    }

protected:
    ~TObjectMonitor() noexcept = default;

    friend class TObjectAnchor;

    void ResetObject(void* NewObject) {
        Object.store(NewObject);
    }

    std::atomic<uintptr_t> RefCounter;
    std::atomic<void*> Object;
    std::shared_ptr<TMailbox> Mailbox;
    mutable TSpinRWLock MailboxLock;
};


class TMonitorPtr {
public:
    TMonitorPtr() noexcept = default;

    explicit TMonitorPtr(TObjectMonitor* monitor) noexcept
        : Monitor(monitor)
    {
        if (Monitor != nullptr)
            Monitor->AddReference();
    }

    TMonitorPtr(const TMonitorPtr& copy) noexcept
        : TMonitorPtr(copy.Monitor)
    {}

    TMonitorPtr(TMonitorPtr&& move) noexcept
        : Monitor(move.Monitor)
    {
        move.Monitor = nullptr;
    }

    TMonitorPtr& operator=(const TMonitorPtr& copy) noexcept {
        if (Monitor != nullptr)
            Monitor->RemoveReference();
        if ((Monitor = copy.Monitor) != nullptr)
            Monitor->AddReference();
        return *this;
    }

    TMonitorPtr& operator=(TMonitorPtr&& move) noexcept {
        if (Monitor != nullptr)
            Monitor->RemoveReference();
        Monitor = move.Monitor;
        move.Monitor = nullptr;
        return *this;
    }

    ~TMonitorPtr() noexcept {
        if (Monitor != nullptr)
            Monitor->RemoveReference();
    }

    void reset(TObjectMonitor* newptr = nullptr) {
        if (Monitor != nullptr)
            Monitor->RemoveReference();
        if ((Monitor = newptr) != nullptr)
            Monitor->AddReference();
    }

    TObjectMonitor* operator->() const {
        return Monitor;
    }

    TObjectMonitor* get() const {
        return Monitor;
    }

    bool empty() const {
        return Monitor == nullptr;
    }

protected:
    TObjectMonitor* Monitor = nullptr;
};


class TObjectAnchor {
public:
    TObjectAnchor(void* object)
        : Monitor(new TObjectMonitor(object))
    {}

    ~TObjectAnchor() {
        Unlink();
    }

    TObjectAnchor(const TObjectAnchor& copy) {
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        Monitor = new TObjectMonitor(AdjustObjectPtr(copy));
    }

    TObjectAnchor(TObjectAnchor&& copy) noexcept {
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        void* object = AdjustObjectPtr(copy);
        Monitor = copy.Monitor;
        copy.Monitor = nullptr;
        Monitor->ResetObject(object);
    }

    void operator=(const TObjectAnchor& copy) {
        Unlink();
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        Monitor = new TObjectMonitor(AdjustObjectPtr(copy));
    }

    void operator=(TObjectAnchor&& copy) {
        Unlink();
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        void* object = AdjustObjectPtr(copy);
        Monitor = copy.Monitor;
        copy.Monitor = nullptr;
        Monitor->ResetObject(object);
    }

    TMonitorPtr GetLink() const noexcept {
        return TMonitorPtr(Monitor);
    }

    void MoveToMailbox(std::shared_ptr<TMailbox> mailbox) {
        Monitor->SetMailbox(std::move(mailbox));
    }

    void MoveToLocalThread() {
        MoveToMailbox(TEdgeSlotThread::LocalMailbox);
    }

    void MoveToThread(const TEdgeSlotThread* thread) {
        MoveToMailbox(thread->GetMailbox());
    }

private:
    void* AdjustObjectPtr(const TObjectAnchor& copy) const noexcept {
        ptrdiff_t distance =
            reinterpret_cast<const char*>(this) -
            reinterpret_cast<const char*>(&copy);
        char* object = reinterpret_cast<char*>(copy.Monitor->GetObject());
        object += distance;
        return object;
    }

    void Unlink() noexcept {
        if (Monitor != nullptr)
            Monitor->ObjectIsDead();
    }

    TObjectMonitor* Monitor;
};


WEAK void TEdgeSlotThread::GrabObject(TObjectAnchor* anchor) const {
    anchor->MoveToMailbox(Mailbox);
}


class TObjectMessage: public IMessage {
public:
    TObjectMessage(TMonitorPtr link)
        : ObjectLink(std::move(link))
    {}

protected:
    TMonitorPtr ObjectLink;

    void JustSend() {
        auto mbox = ObjectLink->GetMailbox();
        if (mbox.get() != nullptr)
            mbox->enqueue(TMessagePtr(this));
        else
            delete this;
    }
};


class TQuitMessage {
public:
};


template <typename...TParams>
class TSignal: public TObjectMessage {
public:
    TSignal(TMonitorPtr link, TSlot<TParams...>* slot, TParams...params)
        : TObjectMessage(std::move(link))
        , ParamsTuple(slot,
                ObjectLink->GetObject(),
                std::forward<TParams>(params)...)
    {}

    virtual void Consume() override {
        if (ObjectLink->IsAlive())
            ApplyFunction(ConsumeImpl, ParamsTuple);
    }

    static void ConsumeImpl(
            TSlot<TParams...>* slot,
            void* object,
            TParams... params) noexcept
    {
        slot->receive(object, std::forward<TParams>(params)...);
    }

protected:
    std::tuple<TSlot<TParams...>*, void*, TParams...> ParamsTuple;

    template <class F, class Tuple, std::size_t... I>
    static void ApplyImpl(F&& f, Tuple&& t, std::index_sequence<I...>) {
        return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
    }

    template <class F, class Tuple>
    static void ApplyFunction(F&& f, Tuple&& t) {
        return ApplyImpl(
            std::forward<F>(f), std::forward<Tuple>(t),
            std::make_index_sequence<
                std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
    }
};


template <typename TDest, typename TApart>
class THalfDisconnectMsg: public TObjectMessage {
public:
    THalfDisconnectMsg(TMonitorPtr dest_link,
                           TDest* dest,
                           TApart* apart)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
        , Apart(apart)
    {}

    virtual void Consume() override {
        if (!ObjectLink->IsAlive())
            return;
        Dest->half_disconnect(std::move(ObjectLink), Apart);
    }

    template <typename...TParams>
    static void Send(TParams&&...params) {
        auto msg = new THalfDisconnectMsg(std::forward<TParams>(params)...);
        msg->JustSend();
    }
protected:
    TDest* Dest;
    TApart* Apart;
};


template <typename TDest, typename TApart>
class THalfConnectMsg: public TObjectMessage {
public:
    THalfConnectMsg(TMonitorPtr dest_link,
                    TDest* dest,
                    TMonitorPtr apart_link,
                    TApart* apart)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
        , ApartLink(std::move(apart_link))
        , Apart(apart)
    {}

    virtual ~THalfConnectMsg() noexcept {
        if (!Delivered) {
            THalfDisconnectMsg<TApart, TDest>::Send(
                    std::move(ApartLink), Apart, Dest);
        }
    }

    virtual void Consume() override {
        Delivered = true;

        if (ObjectLink->IsAlive()) {
            Dest->half_connect(
                std::move(ObjectLink), std::move(ApartLink), Apart);
            return;
        }

        if (!ApartLink->IsAlive())
            return;

        THalfDisconnectMsg<TApart, TDest>::Send(
                std::move(ApartLink), Apart, Dest);
    }

    template <typename...TParams>
    static void Send(TParams&&...params) {
        auto msg = new THalfConnectMsg(std::forward<TParams>(params)...);
        msg->JustSend();
    }

protected:
    TDest* Dest;
    TMonitorPtr ApartLink;
    TApart* Apart;
    bool Delivered = false;
};


template <typename...TParams>
class TFullConnectMsg: public TObjectMessage {
public:
    TFullConnectMsg(TMonitorPtr dest_link,
                    TSlot<TParams...>* dest,
                    TMonitorPtr apart_link,
                    TEdge<TParams...>* apart)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
        , ApartLink(std::move(apart_link))
        , Apart(apart)
    {}

    virtual void Consume() override {
        if (!ObjectLink->IsAlive() || !ApartLink->IsAlive())
            return;
        Dest->connect(std::move(ObjectLink), std::move(ApartLink), Apart);
    }

    template <typename...Types>
    static void Send(Types&&...params) {
        auto msg = new TFullConnectMsg(std::forward<Types>(params)...);
        msg->JustSend();
    }
protected:
    TSlot<TParams...>* Dest;
    TMonitorPtr ApartLink;
    TEdge<TParams...>* Apart;
};


template <typename...TParams>
class TSlot {
public:
    TSlot(TConnectCallee<TParams...> slot)
        : Slot(slot)
    {}

    ~TSlot() {
        for (auto& i: SlotConnections)
            i.Edge->half_disconnect(i.ObjectLink, this);
    }

    void disconnect(TEdge<TParams...>* edge) {
        for (auto i = SlotConnections.begin(); i != SlotConnections.end(); ++i)
            if (i->Edge == edge) {
                edge->half_disconnect(std::move(i->ObjectLink), this);
                SlotConnections.erase(i);
                break;
            }
    }

    void disconnect_all(TEdge<TParams...>* edge) {
        for (ui32 i = 0; i < SlotConnections.size();) {
            if (SlotConnections[i].Edge != edge) {
                ++i;
                continue;
            }
            edge->half_disconnect(
                std::move(SlotConnections[i].ObjectLink), this);
            SlotConnections.erase(SlotConnections.begin() + i);
        }
    }

    void disconnect_all() {
        for (auto i = SlotConnections.begin(); i != SlotConnections.end(); ++i)
        {
            i->Edge->half_disconnect(std::move(i->ObjectLink), this);
        }
        SlotConnections.clear();
    }

    void connect(TMonitorPtr slot_link,
                 TMonitorPtr edge_link,
                 TEdge<TParams...>* edge)
    {
        if (slot_link->SameMailbox()) {
            half_connect(slot_link, edge_link, edge);
            edge->half_connect(edge_link, slot_link, this);
        } else {
            TFullConnectMsg<TParams...>::
                Send(slot_link, this, edge_link, edge);
        }
    }

protected:
    template <typename...>
    friend class TEdge;

    template <typename...Types>
    friend void Connect(
            const TObjectAnchor* edge_object,
            TEdge<Types...>* edge,
            const TObjectAnchor* slot_object,
            TSlot<Types...>* slot);

    template <typename TDest, typename TApart>
    friend class THalfConnectMsg;

    template <typename TDest, typename TApart>
    friend class THalfDisconnectMsg;

    friend class TSignal<TParams...>;

    void half_connect(TMonitorPtr edge_link, TEdge<TParams...>* edge) {
        SlotConnections.emplace_back(
            TSlotConnection{std::move(edge_link), edge});
    }

    void half_connect(TMonitorPtr slot_link,
                      TMonitorPtr edge_link,
                      TEdge<TParams...>* edge)
    {
        if (slot_link->SameMailbox()) {
            half_connect(std::move(edge_link), edge);
        } else {
            THalfConnectMsg<TSlot<TParams...>, TEdge<TParams...>>::
                Send(std::move(slot_link), this, std::move(edge_link), edge);
        }
    }

    void half_disconnect(TEdge<TParams...>* edge) {
        for (auto i = SlotConnections.begin(); i != SlotConnections.end(); ++i)
            if (i->Edge == edge) {
                SlotConnections.erase(i);
                break;
            }
    }

    void half_disconnect(TMonitorPtr slot_link, TEdge<TParams...>* edge) {
        if (!slot_link->SameMailbox())
            THalfDisconnectMsg<TSlot<TParams...>, TEdge<TParams...>>::
                Send(std::move(slot_link), this, edge);
        else
            half_disconnect(edge);
    }

    TConnectCallee<TParams...> get_callee() const noexcept {
        return Slot;
    }

    struct TSlotConnection {
        TMonitorPtr ObjectLink;
        TEdge<TParams...>* Edge;
    };

    const TConnectCallee<TParams...> Slot;
    std::vector<TSlotConnection> SlotConnections;

    void receive(void* object, TParams...params) {
        Slot(this, object, std::move(params)...);
    }
};


template <typename...TParams>
class TEdge: public TSlot<TParams...> {
public:
    TEdge(): TSlot<TParams...>(&forward_callee) {}

    ~TEdge() {
        for (const auto& i: EdgeConnections)
            i.Slot->half_disconnect(i.ObjectLink, this);
    }

    void emit(TParams...params) const {
        DontErase = true;
        // do not emit signal to connections appeared while emitting
        auto size = EdgeConnections.size();

        for (size_t i = 0; i < size; ++i) {
            const auto& elem = EdgeConnections[i];
            if (elem.Slot == nullptr)
                continue;
            if (!elem.ObjectLink->IsAlive())
                continue;
            elem.Slot->receive(elem.ObjectLink->GetObject(), params...);
        }

        if (NeedCleanup) {
            for (size_t i = 0; i < EdgeConnections.size();)
                if (EdgeConnections[i].ObjectLink.empty())
                    EdgeConnections.erase(EdgeConnections.begin() + i);
                else
                    ++i;
            NeedCleanup = false;
        }

        DontErase = false;
    }

    void disconnect(TSlot<TParams...>* slot) {
        for (auto i = EdgeConnections.begin(); i != EdgeConnections.end(); ++i)
            if (i->Slot == slot && !i->ObjectLink.empty()) {
                slot->half_disconnect(std::move(i->ObjectLink), this);
                if (DontErase) {
                    i->Slot = nullptr;
                    i->ObjectLink.reset();
                    NeedCleanup = true;
                } else {
                    EdgeConnections.erase(i);
                }
                break;
            }
    }

    using TSlot<TParams...>::disconnect;

    void disconnect_all(TSlot<TParams...>* slot) {
        for (ui32 i = 0; i < EdgeConnections.size();) {
            auto& elem = EdgeConnections[i];
            if (elem.Slot != slot) {
                ++i;
                continue;
            }
            if (elem.ObjectLink.empty()) {
                ++i;
                continue;
            }
            slot->half_disconnect(std::move(elem.ObjectLink), this);
            if (DontErase) {
                elem.Slot = nullptr;
                elem.ObjectLink.reset();
                NeedCleanup = true;
                ++i;
            } else {
                EdgeConnections.erase(EdgeConnections.begin() + i);
            }
        }
    }

    void disconnect_all_slots() {
        for (auto i = EdgeConnections.begin();
                i != EdgeConnections.end();
                ++i)
        {
            if (i->Slot == nullptr)
                continue;
            i->Slot->half_disconnect(i->ObjectLink, this);
            if (!DontErase)
                continue;
            i->Slot = nullptr;
            i->ObjectLink.reset();
        }

        if (!DontErase)
            EdgeConnections.clear();
        else
            NeedCleanup = true;
    }

    void disconnect_all_edges() {
        TSlot<TParams...>::disconnect_all();
    }

    void disconnect_all() {
        disconnect_all_edges();
        disconnect_all_slots();
    }

protected:
    template <typename...>
    friend class TSlot;

    template <typename...Types>
    friend void Connect(
            const TObjectAnchor* edge_object,
            TEdge<Types...>* edge,
            const TObjectAnchor* slot_object,
            TSlot<Types...>* slot);

    template <typename TDest, typename TApart>
    friend class THalfConnectMsg;

    template <typename TDest, typename TApart>
    friend class THalfDisconnectMsg;


    void half_connect(TMonitorPtr slot_link, TSlot<TParams...>* slot) {
        EdgeConnections.emplace_back(
                TEdgeConnection{std::move(slot_link), slot});
    }

    void half_connect(TMonitorPtr edge_link,
                      TMonitorPtr slot_link,
                      TSlot<TParams...>* slot)
    {
        if (edge_link->SameMailbox())
            half_connect(std::move(slot_link), slot);
        else
            THalfConnectMsg<TEdge<TParams...>, TSlot<TParams...>>::
                Send(std::move(edge_link), this, std::move(slot_link), slot);
    }

    void half_disconnect(TSlot<TParams...>* slot) {
        for (auto i = EdgeConnections.begin();
                i != EdgeConnections.end();
                ++i)
        {
            if (i->Slot != slot)
                continue;
            if (i->ObjectLink.empty())
                continue;
            if (DontErase) {
                i->Slot = nullptr;
                i->ObjectLink.reset();
                NeedCleanup = true;
            } else {
                EdgeConnections.erase(i);
            }
            break;
        }
    }

    void half_disconnect(TMonitorPtr edge_link, TSlot<TParams...>* slot) {
        if (edge_link->SameMailbox())
            half_disconnect(slot);
        else
            THalfDisconnectMsg<TEdge<TParams...>, TSlot<TParams...>>::
                Send(std::move(edge_link), this, slot);
    }

    static void
    forward_callee(const TSlot<TParams...>* slot,
            void*, TParams...params) noexcept
    {
        auto self = static_cast<const TEdge<TParams...>*>(slot);
        self->emit(params...);
    }

    struct TEdgeConnection {
        TMonitorPtr ObjectLink;
        TSlot<TParams...>* Slot;
    };

    mutable bool DontErase = false;
    mutable bool NeedCleanup = false;
    mutable std::vector<TEdgeConnection> EdgeConnections;
};


template <typename TObject, typename...TParams>
class TCallee {
public:
    template <void (TObject::* Member)(TParams...)>
    static void Callee(const TSlot<TParams...>*, void* object, TParams...params) {
        (reinterpret_cast<TObject*>(object)->*Member)(params...);
    }

    using TSlotType = TSlot<TParams...>;

    template <void (TObject::* Member)(TParams...)>
    static TSlotType GetSlot() {
        return TSlot<TParams...>(&Callee<Member>);
    }
};


template <typename TObject, typename...TParams>
TCallee<TObject, TParams...> GetCallee(void (TObject::*)(TParams...)) {
    return TCallee<TObject, TParams...>{};
}


template <typename...TParams>
void Connect(const TObjectAnchor* edge_object,
             TEdge<TParams...>* edge,
             const TObjectAnchor* slot_object,
             TSlot<TParams...>* slot)
{
    slot->connect(slot_object->GetLink(), edge_object->GetLink(), edge);
}


} // namespace bsc
