# edge-slot
This is something similar to QT signals&amp;slots paradigm

### Some recurrent definitions
Edge - source of signals

Signal - event that is delivered from Edge to Slot

Slot - receiver of signals

(I guess these are more logically correct names)


### Basic properties

Every object with an edge or a slot belongs to a particular thread, by default it is a thread where the object was created.

The library do not take care about life time of objects. Library do not deliver signals to destroyed objects (under certain circumstances, see below).

The library is effectively thread-safe. All signals is delivered to a object in its thread.

Destroy an object in a thread that the object belongs to. This allows to avoid signal delivery to the destroyed object. Or you can do the destroying in any thread if you are sure that there is no signals on the way to the destroying object.

All signals is delivered to objects in one of two ways. If it was a thread of the object then a slot is directly called. If it was other thread then a signal is delivered via lock-free message queue to the object's thread. All threads that have objects should perform message loop. That is what TEdgeSlotThread is doing by default.

Connections and disconnections of objects from different threads is performed the same way as signal delivery.

An object can be moved to another thread. This will cause arbitrary delivery order of signals that were on the way while moving. Do not make connections to an object while moving it to avoid ABA-problems or leaks in connections and disconnections.

If memory allocator was lock free then signal delivery is lock-free except the case of moving objects between threads. While moving an object to another thread there is a short time lock that may hinder signal delivery to the object. The lock is needed for correct reference counting to mailbox.

Sending of a signal is wait-free if memory allocator was wait-free. But receiving of a signal is not wait-free because one sleeping sending thread may hinder receiving of all subsequent signals.

### Quick guide

Edge is a signal source. Example for an edge that emits two integers:

    class Foo: public bsc::TEdgeSlotObject {
    public:
        bsc::TEdge<int, int> Edge = bsc::TEdge<int, int>(this);
    };

Slot is a receiver and consumer of signals. Example for a slot that consumes two integers:

    class Bar: public TEdgeSlotObject {
    public:
        void slot_func(int a, int b) {
            /* put here your code */
        }

        DEFINE_SLOT(Bar, slot_func, SomeSlotName);
    };

DEFINE_SLOT is a macro that defined as:

    #define DEFINE_SLOT(TObject, Method, SlotName)                            \
    typename decltype(bsc::GetCallee(&TObject::Method))::TSlotType SlotName = \
        decltype(bsc::GetCallee(&TObject::Method))::                          \
            template GetSlot<&TObject::Method>(this)

actually this can be simpler in c++17 by means of auto in template parameters, but right now many delelopers do not use a c++17 compiler. This will change in the future.

To connect the edge and the slot:

    Foo edge_obj;
    Bar slot_obj;

    Connect(&edge_obj, &edge_obj.Edge, &slot_obj, &slot_obj.SomeSlotName);

To emit a signal:

    edge_obj.Edge.emit(1, 2);
    
You may use 4 different types of connections (AUTO is by default):

    enum class DELIVERY {
        AUTO,
        DIRECT,
        QUEUE,
        BLOCK_QUEUE,
    };

To define a connection type:

    Connect(&edge_obj, &edge_obj.Edge, &slot_obj, &slot_obj.SomeSlotName, bsc::DELIVERY::QUEUE);

You may connect an edge with an edge, the latter edge will forward all signals that comes from such connections.

You may create a thread and move an object to the thread for delivering all signals in the separate thread

    bsc::TEdgeSlotThread thr;

    Foo edge;
    Bar slot;

    thr.GrabObject(&slot);
    Connect(&edge_obj, &edge_obj.Edge, &slot_obj, &slot_obj.SomeSlotName);

    edge.Edge.emit(1, 2);
    thr.PostQuitMessage();
    thr.join();
    
You may grab object after creating a connection, the following is valid

    Connect(&edge_obj, &edge_obj.Edge, &slot_obj, &slot_obj.SomeSlotName);
    thr.GrabObject(&slot);

Actually each TEdgeSlotObject object belongs to some thread. AUTO, QUEUE and BLOCK_QUEUE connection types always deliver signals in a thread a slot belongs to.

WARNING: DIRECT connection always delivers signals in the current thread.

NOTICE: BLOCK_QUEUE connection makes a direct call if a slot in the same thread.

WARNING: Always emit signals from a thread an edge belongs to unless you know what you are doing.

WARNING: You may safely delete an object in a thread the object belongs to. You may safely delete object in any thread if there is no ongoing signals coming to the object.

NOTICE: After destroying a thread all objects that belong to the thread will be suspended. All signals to the objects will never be delivered (except for DIRECT connections) and will stay in the memory until all the objects will be grabbed to another thread. This is because all signals is put into a queue which will be destroyed only if no objects associated with the queue.

It's ok to use multiple inheritance with bsc::TEdgeSlotObject, actually it virually inherits a helper class:

    class TEdgeSlotObject: public virtual TAnchorHolder {
    };

There is also timers and WaitForSignal helpers, see unit tests.
