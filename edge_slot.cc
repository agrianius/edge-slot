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

namespace bsc {

thread_local std::shared_ptr<TMailbox>
TEdgeSlotThread::LocalMailbox = std::make_shared<TMailbox>();

thread_local std::vector<TEdgeSlotTimer*> TEdgeSlotThread::ActiveTimers;


void TActivateTimerSignal::Consume() {
    if (!ObjectLink->IsAlive())
        return;
    Timer->Activate(std::move(ObjectLink));
}


void TDeactivateTimerSignal::Consume() {
    if (!ObjectLink->IsAlive())
        return;
    Timer->Deactivate(std::move(ObjectLink));
}


void TEdgeSlotThread::RegisterTimer(TEdgeSlotTimer* timer) {
    UnregisterTimer(timer);
    for (auto i = ActiveTimers.begin(); i != ActiveTimers.end(); ++i) {
        if (timer->GetNextHitTime() >= (*i)->GetNextHitTime())
            continue;
        ActiveTimers.insert(i, timer);
        return;
    }
    ActiveTimers.push_back(timer);
}


void TEdgeSlotThread::UnregisterTimer(TEdgeSlotTimer* timer) {
    for (auto i = ActiveTimers.begin(); i != ActiveTimers.end(); ++i) {
        if (timer != *i)
            continue;
        ActiveTimers.erase(i);
        break;
    }
}


void TEdgeSlotThread::ThreadMessageLoop(TEdgeSlotThread* self) noexcept {
    LocalMailbox = self->Mailbox;
    MessageLoop();
}


} // namespace bsc
