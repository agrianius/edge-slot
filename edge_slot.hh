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
#include <time.h>


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


using TMessagePtr = std::shared_ptr<IMessage>;


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

    TMessagePtr dequeue(ui64 wait_time) {
        TMessagePtr result;
        for (;;) {
            if (Queue.dequeue(&result))
                return result;
            if (Sem.Wait(wait_time))
                return TMessagePtr();
        }
    }

protected:
    MPSC_TailSwap<TMessagePtr> Queue;
    TSemaphore Sem;
};


class TObjectAnchor;
class TEdgeSlotObject;
class TEdgeSlotTimer;


class TEdgeSlotThread {
public:
    TEdgeSlotThread()
        : Mailbox(std::make_shared<TMailbox>())
    {
        Thread = std::thread(ThreadMessageLoop, this);
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

    void GrabObject(TEdgeSlotObject* obj) const;

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

    static void PostSelfQuitMessage() noexcept {
    	TMessagePtr msg(new TQuitMessage);
    	LocalMailbox->enqueue(std::move(msg));
    }

    template <typename Fn>
    static void MessageLoop(Fn&& condition) noexcept;

    static void MessageLoop() noexcept {
        auto always_true = []() -> bool { return true; };
        MessageLoop(always_true);
    }

    static void RegisterTimer(TEdgeSlotTimer* timer);
    static void UnregisterTimer(TEdgeSlotTimer* timer);

    static void CleanupTimers() noexcept {
    	ActiveTimers.clear();
    	ActiveTimers.shrink_to_fit();
    }


    template <typename Fn, typename...TParams>
    static bool WaitForSignal(
	    TEdgeSlotObject* object, TEdge<TParams...>* edge, Fn&& start) noexcept;

    template <typename...TParams>
    static void WaitForDisconnected(TSlot<TParams...>* slot) noexcept;

protected:
    std::shared_ptr<TMailbox> Mailbox;
    std::thread Thread;
    static thread_local std::vector<TEdgeSlotTimer*> ActiveTimers;

    static void ThreadMessageLoop(TEdgeSlotThread* self) noexcept;

    template <class Fn, class...Params>
    static void
    ThreadFuncWrapper(TEdgeSlotThread* self, Fn&& fn, Params&&...params) {
        LocalMailbox = self->Mailbox;
        fn(std::forward<Params>(params)...);
    };
};


class TObjectMonitor {
public:
    TObjectMonitor()
        : RefCounter(1)
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
        return TEdgeSlotThread::LocalMailbox == Mailbox;
    }

protected:
    ~TObjectMonitor() noexcept = default;

    friend class TObjectAnchor;

    std::atomic<uintptr_t> RefCounter;
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

    bool operator==(const TMonitorPtr& right_op) const noexcept {
    	return Monitor == right_op.Monitor;
    }

    bool operator!=(const TMonitorPtr& right_op) const noexcept {
    	return Monitor != right_op.Monitor;
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
    TObjectAnchor()
        : Monitor(new TObjectMonitor)
    {}

    ~TObjectAnchor() {
        Unlink();
    }

    TObjectAnchor(const TObjectAnchor& copy) {
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        Monitor = new TObjectMonitor;
    }

    TObjectAnchor(TObjectAnchor&& copy) noexcept {
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        Monitor = copy.Monitor;
        copy.Monitor = nullptr;
    }

    void operator=(const TObjectAnchor& copy) {
        Unlink();
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        Monitor = new TObjectMonitor;
    }

    void operator=(TObjectAnchor&& copy) {
        Unlink();
        if (copy.Monitor == nullptr) {
            Monitor = nullptr;
            return;
        }
        Monitor = copy.Monitor;
        copy.Monitor = nullptr;
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
    void Unlink() noexcept {
        if (Monitor != nullptr)
            Monitor->ObjectIsDead();
    }

    TObjectMonitor* Monitor;

    template <typename...TParams> friend class TSlot;
};


class TAnchorHolder {
public:
    TObjectAnchor& GetAnchor() noexcept {
        return Anchor_;
    }

    const TObjectAnchor& GetAnchor() const noexcept {
        return Anchor_;
    }

private:
    TObjectAnchor Anchor_;
};


class TEdgeSlotObject: public virtual TAnchorHolder {
};


WEAK void TEdgeSlotThread::GrabObject(TObjectAnchor* anchor) const {
    anchor->MoveToMailbox(Mailbox);
}


WEAK void TEdgeSlotThread::GrabObject(TEdgeSlotObject* obj) const {
    GrabObject(&obj->GetAnchor());
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
        , ParamsTuple(slot, std::forward<TParams>(params)...)
    {}

    virtual void Consume() override {
        if (ObjectLink->IsAlive())
            ApplyFunction(ConsumeImpl, ParamsTuple);
    }

    static void ConsumeImpl(
            TSlot<TParams...>* slot,
            TParams... params) noexcept
    {
        slot->receive(std::forward<TParams>(params)...);
    }

protected:
    std::tuple<TSlot<TParams...>*, TParams...> ParamsTuple;

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


class TBlockSignal: public IMessage {
public:
    TBlockSignal(TMessagePtr payload)
        : Payload(std::move(payload))
    {}

    virtual void Consume() override {
        Payload->Consume();
        Event.Post();
    }

    void Wait() noexcept {
        Event.Wait();
    }

protected:
    TMessagePtr Payload;
    TSemaphore Event;
};


template <typename TDest, typename TApart>
class THalfDisconnectMsg: public TObjectMessage {
public:
    THalfDisconnectMsg(TMonitorPtr dest_link,
                       TDest* dest,
                       TMonitorPtr apart_link,
                       TApart* apart)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
        , ApartLink(std::move(apart_link))
        , Apart(apart)
    {}

    virtual void Consume() override {
        if (!ObjectLink->IsAlive())
            return;
        Dest->half_disconnect(
            std::move(ObjectLink), std::move(ApartLink), Apart);
    }

    template <typename...TParams>
    static void Send(TParams&&...params) {
        auto msg = new THalfDisconnectMsg(std::forward<TParams>(params)...);
        msg->JustSend();
    }
protected:
    TDest* Dest;
    TMonitorPtr ApartLink;
    TApart* Apart;
};


enum class DELIVERY {
    AUTO,
    DIRECT,
    QUEUE,
    BLOCK_QUEUE,
};


template <typename TDest, typename TApart>
class THalfConnectMsg: public TObjectMessage {
public:
    THalfConnectMsg(TMonitorPtr dest_link,
                    TDest* dest,
                    TMonitorPtr apart_link,
                    TApart* apart,
                    DELIVERY type = DELIVERY::AUTO)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
        , ApartLink(std::move(apart_link))
        , Apart(apart)
        , Type(type)
    {}


    virtual ~THalfConnectMsg() noexcept {
        if (Delivered)
            return;
        THalfDisconnectMsg<TApart, TDest>::Send(
            std::move(ApartLink), Apart, std::move(ObjectLink), Dest);
    }


    virtual void Consume() override {
        Delivered = true;

        if (ObjectLink->IsAlive()) {
            Dest->half_connect(
                std::move(ObjectLink), std::move(ApartLink), Apart, Type);
            return;
        }

        if (!ApartLink->IsAlive())
            return;

        THalfDisconnectMsg<TApart, TDest>::Send(
            std::move(ApartLink), Apart, std::move(ObjectLink), Dest);
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
    DELIVERY Type;
    bool Delivered = false;
};


template <typename...TParams>
class TFullConnectMsg: public TObjectMessage {
public:
    TFullConnectMsg(TMonitorPtr dest_link,
                    TSlot<TParams...>* dest,
                    TMonitorPtr apart_link,
                    TEdge<TParams...>* apart,
                    DELIVERY type = DELIVERY::AUTO)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
        , ApartLink(std::move(apart_link))
        , Apart(apart)
        , Type(type)
    {}

    virtual void Consume() override {
        if (!ObjectLink->IsAlive() || !ApartLink->IsAlive())
            return;
        Dest->connect(std::move(ObjectLink), std::move(ApartLink), Apart, Type);
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
    DELIVERY Type;
};


template <typename TDest, typename TApart>
class TFullDisconnectMsg: public TObjectMessage {
public:
    TFullDisconnectMsg(TMonitorPtr dest_link,
                       TDest* dest,
					   TMonitorPtr edge_link,
                       TApart* apart)
        : TObjectMessage(std::move(dest_link))
        , Dest(dest)
		, ApartLink(std::move(edge_link))
        , Apart(apart)
    {}

    virtual void Consume() override {
        if (!ObjectLink->IsAlive())
            return;
        Dest->disconnect(std::move(ObjectLink), std::move(ApartLink), Apart);
    }

    template <typename...Types>
    static void Send(Types&&...params) {
        auto msg = new TFullDisconnectMsg<TDest, TApart>(
                std::forward<Types>(params)...);
        msg->JustSend();
    }

protected:
    TDest* Dest;
    TMonitorPtr ApartLink;
    TApart* Apart;
};


#define DEFINE_SLOT(TObject, Method, SlotName)                                \
    typename decltype(bsc::GetCallee(&TObject::Method))::TSlotType SlotName = \
        decltype(bsc::GetCallee(&TObject::Method))::                          \
            template GetSlot<&TObject::Method>(this)


template <typename...TParams>
class TSlot {
public:
    template <typename TObject>
    TSlot(TObject* object, TConnectCallee<TParams...> slot)
        : Object(object)
        , Link(static_cast<TEdgeSlotObject*>(object)->GetAnchor().Monitor)
        , Slot(slot)
    {}

    ~TSlot() {
        for (auto& i: SlotConnections)
            i.Edge->half_disconnect(i.ObjectLink, TMonitorPtr(Link), this);
    }

    void disconnect(TMonitorPtr edge_link, TEdge<TParams...>* edge) {
        for (auto i = SlotConnections.begin(); i != SlotConnections.end(); ++i)
            if (i->Edge == edge && i->ObjectLink == edge_link) {
                edge->half_disconnect(
                        std::move(i->ObjectLink), TMonitorPtr(Link), this);
                SlotConnections.erase(i);
                break;
            }
    }

    void disconnect(TMonitorPtr slot_link,
    				TMonitorPtr edge_link,
					TEdge<TParams...>* edge)
    {
    	if (slot_link->SameMailbox())
    		disconnect(std::move(edge_link), edge);
    	else
    		TFullDisconnectMsg<TSlot<TParams...>, TEdge<TParams...>>::
				Send(std::move(slot_link), this, std::move(edge_link), edge);
    }

    void disconnect_all(TEdge<TParams...>* edge) {
        for (ui32 i = 0; i < SlotConnections.size();) {
            if (SlotConnections[i].Edge != edge) {
                ++i;
                continue;
            }
            edge->half_disconnect(
                std::move(SlotConnections[i].ObjectLink),
                TMonitorPtr(Link),
                this);
            SlotConnections.erase(SlotConnections.begin() + i);
        }
    }

    void disconnect_all() {
        for (auto i = SlotConnections.begin(); i != SlotConnections.end(); ++i)
        {
            i->Edge->half_disconnect(
                    std::move(i->ObjectLink), TMonitorPtr(Link), this);
        }
        SlotConnections.clear();
    }

    void connect(TMonitorPtr slot_link,
                 TMonitorPtr edge_link,
                 TEdge<TParams...>* edge,
                 DELIVERY type = DELIVERY::AUTO)
    {
        if (slot_link->SameMailbox()) {
            half_connect(slot_link, edge_link, edge);
            edge->half_connect(edge_link, slot_link, this, type);
        } else {
            TFullConnectMsg<TParams...>::
                Send(slot_link, this, edge_link, edge, type);
        }
    }

    bool is_connected() const noexcept {
        return SlotConnections.size() > 0;
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

    void half_connect(TMonitorPtr edge_link,
                      TEdge<TParams...>* edge,
                      DELIVERY = DELIVERY::AUTO)
    {
        SlotConnections.emplace_back(
            TSlotConnection{std::move(edge_link), edge});
    }

    void half_connect(TMonitorPtr slot_link,
                      TMonitorPtr edge_link,
                      TEdge<TParams...>* edge,
                      DELIVERY = DELIVERY::AUTO)
    {
        if (slot_link->SameMailbox()) {
            half_connect(std::move(edge_link), edge);
        } else {
            THalfConnectMsg<TSlot<TParams...>, TEdge<TParams...>>::
                Send(std::move(slot_link), this, std::move(edge_link), edge);
        }
    }

    void half_disconnect(TMonitorPtr edge_link, TEdge<TParams...>* edge) {
        for (auto i = SlotConnections.begin(); i != SlotConnections.end(); ++i)
            if (i->Edge == edge && i->ObjectLink == edge_link) {
                SlotConnections.erase(i);
                break;
            }
    }

    void half_disconnect(TMonitorPtr slot_link,
                         TMonitorPtr edge_link,
                         TEdge<TParams...>* edge)
    {
        if (!slot_link->SameMailbox())
            THalfDisconnectMsg<TSlot<TParams...>, TEdge<TParams...>>::
                Send(std::move(slot_link), this, std::move(edge_link), edge);
        else
            half_disconnect(std::move(edge_link), edge);
    }

    TConnectCallee<TParams...> get_callee() const noexcept {
        return Slot;
    }

    struct TSlotConnection {
        TMonitorPtr ObjectLink;
        TEdge<TParams...>* Edge;
    };

    void* Object;
    TObjectMonitor* Link;
    const TConnectCallee<TParams...> Slot;
    std::vector<TSlotConnection> SlotConnections;

    void receive(TParams...params) {
        Slot(this, Object, std::move(params)...);
    }
};


template <typename...TParams>
class TEdge: public TSlot<TParams...> {
public:
    template <typename TObject>
    TEdge(TObject* object)
        : TSlot<TParams...>(object, &forward_callee)
    {}

    ~TEdge() {
        for (const auto& i: EdgeConnections)
            i.Slot->half_disconnect(
                i.ObjectLink, TMonitorPtr(TSlot<TParams...>::Link), this);
    }

    void emit(TParams...params) const {
        DontErase = true;
        // do not emit signal to connections appeared while emitting
        auto size = EdgeConnections.size();

        TMessagePtr msg;

        for (size_t i = 0; i < size; ++i) {
            const auto& elem = EdgeConnections[i];
            if (elem.Slot == nullptr)
                continue;
            if (!elem.ObjectLink->IsAlive())
                continue;

            auto mbox = elem.ObjectLink->GetMailbox();

            switch (elem.Type) {
            case DELIVERY::AUTO:
                if (mbox == TEdgeSlotThread::LocalMailbox) {
                    elem.Slot->receive(params...);
                    continue;
                }
                // fall through
            case DELIVERY::QUEUE:
                if (mbox.get() == nullptr)
                    continue;

                if (msg.get() == nullptr)
                    msg = std::make_shared<TSignal<TParams...>>(
                            elem.ObjectLink, elem.Slot, params...);

                mbox->enqueue(msg);
                break;

            case DELIVERY::DIRECT:
                elem.Slot->receive(params...);
                break;

            case DELIVERY::BLOCK_QUEUE:
                {
                    if (mbox.get() == nullptr)
                        continue;

                    if (msg.get() == nullptr)
                        msg = std::make_shared<TSignal<TParams...>>(
                                elem.ObjectLink, elem.Slot, params...);

                    std::shared_ptr<TBlockSignal> block =
                            std::make_shared<TBlockSignal>(msg);
                    mbox->enqueue(block);
                    block->Wait();
                }
                break;
            }
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
                slot->half_disconnect(
                        std::move(i->ObjectLink),
                        TMonitorPtr(TSlot<TParams...>::Link),
                        this);

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


    void disconnect(TMonitorPtr slot_link, TSlot<TParams...>* slot) {
        for (auto i = EdgeConnections.begin(); i != EdgeConnections.end(); ++i)
            if (i->Slot == slot && slot_link == i->ObjectLink) {
                slot->half_disconnect(
                        std::move(i->ObjectLink),
                        TMonitorPtr(TSlot<TParams...>::Link),
                        this);

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


    void disconnect(TMonitorPtr edge_link,
                    TMonitorPtr slot_link,
                    TSlot<TParams...>* slot)
    {
        if (edge_link->SameMailbox())
            disconnect(std::move(slot_link), slot);
        else
            TFullDisconnectMsg<TEdge<TParams...>, TSlot<TParams...>>::
                Send(std::move(edge_link), this, std::move(slot_link), slot);
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

            slot->half_disconnect(
                std::move(elem.ObjectLink),
                TMonitorPtr(TSlot<TParams...>::Link),
                this);

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

            i->Slot->half_disconnect(
                i->ObjectLink,
                TMonitorPtr(TSlot<TParams...>::Link),
                this);

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


    void half_connect(TMonitorPtr slot_link,
                      TSlot<TParams...>* slot,
                      DELIVERY type = DELIVERY::AUTO)
    {
        EdgeConnections.emplace_back(
                TEdgeConnection{std::move(slot_link), slot, type});
    }

    void half_connect(TMonitorPtr edge_link,
                      TMonitorPtr slot_link,
                      TSlot<TParams...>* slot,
                      DELIVERY type = DELIVERY::AUTO)
    {
        if (edge_link->SameMailbox())
            half_connect(std::move(slot_link), slot, type);
        else
            THalfConnectMsg<TEdge<TParams...>, TSlot<TParams...>>::
                Send(std::move(edge_link), this,
                     std::move(slot_link), slot, type);
    }

    void half_disconnect(TMonitorPtr slot_link, TSlot<TParams...>* slot) {
        for (auto i = EdgeConnections.begin();
                i != EdgeConnections.end();
                ++i)
        {
            if (i->Slot != slot)
                continue;
            if (i->ObjectLink != slot_link)
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

    void half_disconnect(TMonitorPtr edge_link,
                         TMonitorPtr slot_link,
                         TSlot<TParams...>* slot)
    {
        if (edge_link->SameMailbox())
            half_disconnect(slot_link, slot);
        else
            THalfDisconnectMsg<TEdge<TParams...>, TSlot<TParams...>>::
                Send(std::move(edge_link), this, slot_link, slot);
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
        DELIVERY Type;
    };

    mutable bool DontErase = false;
    mutable bool NeedCleanup = false;
    mutable std::vector<TEdgeConnection> EdgeConnections;
};


template <typename TObject, typename...TParams>
class TCallee {
public:
    template <void (TObject::* Member)(TParams...)>
    static void
    Callee(const TSlot<TParams...>*, void* object, TParams...params) {
        (reinterpret_cast<TObject*>(object)->*Member)(params...);
    }

    using TSlotType = TSlot<TParams...>;

    template <void (TObject::* Member)(TParams...)>
    static TSlotType GetSlot(void* object) {
        return TSlot<TParams...>(object, &Callee<Member>);
    }
};


template <typename TObject, typename...TParams>
TCallee<TObject, TParams...> GetCallee(void (TObject::*)(TParams...)) {
    return TCallee<TObject, TParams...>{};
}


template <typename TEdgeContainer, typename TSlotContainer, typename...TParams>
void Connect(const TEdgeContainer* edge_object,
             TEdge<TParams...>* edge,
             const TSlotContainer* slot_object,
             TSlot<TParams...>* slot,
             DELIVERY type = DELIVERY::AUTO)
{
    slot->connect(
        slot_object->GetAnchor().GetLink(),
        edge_object->GetAnchor().GetLink(),
        edge,
        type);
}


template <typename TEdgeContainer, typename TSlotContainer, typename...TParams>
void Disconnect(const TEdgeContainer* edge_object,
				TEdge<TParams...>* edge,
				const TSlotContainer* slot_object,
				TSlot<TParams...>* slot)
{
	slot->disconnect(
			slot_object->GetAnchor().GetLink(),
			edge_object->GetAnchor().GetLink(),
			edge);
}


template <typename TEdgeContainer, typename TSlotContainer, typename...TParams>
void DisconnectFromEdge(
        const TEdgeContainer* edge_object,
        TEdge<TParams...>* edge,
        const TSlotContainer* slot_object,
        TSlot<TParams...>* slot)
{
    edge->disconnect(
            edge_object->GetAnchor().GetLink(),
            slot_object->GetAnchor().GetLink(),
            slot);
}


class TActivateTimerSignal: public TObjectMessage {
public:
    TActivateTimerSignal(TMonitorPtr link, TEdgeSlotTimer* timer)
        : TObjectMessage(std::move(link))
        , Timer(timer)
    {}

    virtual void Consume() override;

protected:
    TEdgeSlotTimer* Timer;

    friend class TEdgeSlotTimer;
};


class TDeactivateTimerSignal: public TObjectMessage {
public:
    TDeactivateTimerSignal(TMonitorPtr link, TEdgeSlotTimer* timer)
        : TObjectMessage(std::move(link))
        , Timer(timer)
    {}

    virtual void Consume() override;

protected:
    TEdgeSlotTimer* Timer;

    friend class TEdgeSlotTimer;
};


class TEdgeSlotTimer: public TEdgeSlotObject {
public:
    TEdgeSlotTimer(ui64 period = 0/* in microseconds */, bool repeat = false)
        : Period(period)
        , Repeat(repeat)
    {}

    ui64 GetNextHitTime() const noexcept {
        return NextHit;
    }

    static ui64 GetNow() {
        timespec ts;
        int res = ::clock_gettime(CLOCK_MONOTONIC, &ts);
        ESyscallError::Validate(res, "clock_gettime");
        return (ui64) ts.tv_sec * 1000000 + (ui64) ts.tv_nsec / 1000;
    }

    void Hit() {
        if (!ActiveState.load(std::memory_order_acquire))
            return;
        Timeout.emit();
    }

    TEdge<> Timeout = TEdge<>(this);

    void Reregister() {
        if (!Repeat) {
            ActiveState.store(false, std::memory_order_release);
            return;
        }
        NextHit += Period;
        TEdgeSlotThread::RegisterTimer(this);
    }

    void Activate() {
        Activate(GetAnchor().GetLink());
    }

    void Activate(ui64 period) {
        Period = period;
        Activate();
    }

    void Activate(ui64 period, bool repeat) {
        Period = period;
        Repeat = repeat;
        Activate();
    }

    void Activate(TMonitorPtr link) {
        ActiveState.store(true, std::memory_order_release);
        if (link->SameMailbox()) {
            NextHit = Period + GetNow();
            TEdgeSlotThread::RegisterTimer(this);
        } else {
            auto ts = new TActivateTimerSignal(std::move(link), this);
            ts->JustSend();
        }
    }

    void Deactivate(TMonitorPtr link) {
        ActiveState.store(false, std::memory_order_release);
        if (link->SameMailbox()) {
            TEdgeSlotThread::UnregisterTimer(this);
        } else {
            auto ts = new TDeactivateTimerSignal(std::move(link), this);
            ts->JustSend();
        }
    }

    void Deactivate() {
        Deactivate(GetAnchor().GetLink());
    }

    bool IsActive() const {
        return ActiveState.load(std::memory_order_acquire);
    }

protected:
    ui64 Period;
    ui64 NextHit;
    bool Repeat;
    std::atomic<bool> ActiveState = {false};
};


WEAK void TActivateTimerSignal::Consume() {
    if (!ObjectLink->IsAlive())
        return;
    Timer->Activate(std::move(ObjectLink));
}


WEAK void TDeactivateTimerSignal::Consume() {
    if (!ObjectLink->IsAlive())
        return;
    Timer->Deactivate(std::move(ObjectLink));
}


WEAK void TEdgeSlotThread::RegisterTimer(TEdgeSlotTimer* timer) {
    UnregisterTimer(timer);
    for (auto i = ActiveTimers.begin(); i != ActiveTimers.end(); ++i) {
        if (timer->GetNextHitTime() >= (*i)->GetNextHitTime())
            continue;
        ActiveTimers.insert(i, timer);
        return;
    }
    ActiveTimers.push_back(timer);
}


WEAK void TEdgeSlotThread::UnregisterTimer(TEdgeSlotTimer* timer) {
    for (auto i = ActiveTimers.begin(); i != ActiveTimers.end(); ++i) {
        if (timer != *i)
            continue;
        ActiveTimers.erase(i);
        break;
    }
}


WEAK void TEdgeSlotThread::ThreadMessageLoop(TEdgeSlotThread* self) noexcept {
	LocalMailbox = self->Mailbox;
	MessageLoop();
}


template <typename Fn>
void TEdgeSlotThread::MessageLoop(Fn&& condition) noexcept {
    for (;;) {
        while (ActiveTimers.size() > 0) {
            auto now = TEdgeSlotTimer::GetNow();
            auto front_hit = ActiveTimers.front()->GetNextHitTime();
            if (now < front_hit)
                break;
            auto timer = ActiveTimers.front();
            ActiveTimers.erase(ActiveTimers.begin());
            timer->Hit();
            timer->Reregister();
        }

        if (!condition())
            return;

        TMessagePtr msg;

        if (ActiveTimers.size() > 0) {
            auto front_hit = ActiveTimers.front()->GetNextHitTime();
            auto now = TEdgeSlotTimer::GetNow();
            ui64 max_wait_time = front_hit - now;
            msg = TEdgeSlotThread::LocalMailbox->dequeue(max_wait_time);
            if (msg.get() == nullptr)
                continue;
        } else {
            msg = TEdgeSlotThread::LocalMailbox->dequeue();
        }

        try {
            msg->Consume();
        } catch (EQuitLoop&) {
            return;
        } catch (...) {
        }
    }
};


template <typename Fn, typename...TParams>
bool TEdgeSlotThread::WaitForSignal(
		TEdgeSlotObject* object, TEdge<TParams...>* edge, Fn&& start) noexcept
{
    class TCatcher: public TEdgeSlotObject {
    public:
        void catch_signal(TParams...) {
            GotIt = true;
            TEdgeSlotThread::PostSelfQuitMessage();
        }

        DEFINE_SLOT(TCatcher, catch_signal, CatchSlot);

        bool GotIt = false;
    };

	TCatcher catcher;
	Connect(object, edge, &catcher, &catcher.CatchSlot);

	if (!start())
	    return false;

    auto connect_checker = [&]() -> bool {
        return catcher.CatchSlot.is_connected();
    };

	MessageLoop(connect_checker);

	return catcher.GotIt;
}


template <typename...TParams>
void TEdgeSlotThread::WaitForDisconnected(TSlot<TParams...>* slot) noexcept {
    auto connect_checker = [&]() -> bool {
        return slot->is_connected();
    };

    MessageLoop(connect_checker);
}


} // namespace bsc
