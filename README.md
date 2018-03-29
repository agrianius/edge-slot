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

Signal delivery is lock-free except the case of moving objects between threads. While moving an object to another thread there is a short time lock that prevents signal delivery to the object.

Sending of a signal is wait-free. But receiving of a signal is not wait-free becase one sleeping sending thread hinder delivery of all subsequent signals.
