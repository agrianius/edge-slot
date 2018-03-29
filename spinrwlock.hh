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
#include "types.hh"
#include <atomic>

namespace bsc {

class TSpinRWLock {
public:
    TSpinRWLock(): Lock(0) {}

    void ReadLock() {
        while (Lock.fetch_add(2) & 1) {
            if (!(Lock.fetch_sub(2) & 1))
                continue;
            while (Lock.load() & 1); // empty loop
        }
    }

    void ReadUnlock() {
        Lock.fetch_sub(2);
    }

    void WriteLock() {
        for (;;) {
            auto prev = Lock.fetch_or(1);
            if (prev == 0)
                return;
            if ((prev & 1) == 0)
                break;
            while (Lock.load() & 1); // empty loop
        }
        while (Lock.load() != 1); // empty loop
    }

    void WriteUnlock() {
        Lock.fetch_sub(1);
    }

protected:
    std::atomic<ui32> Lock;
};


class TReadGuard {
public:
    TReadGuard(TSpinRWLock* rwlock)
        : LockObject(rwlock)
    {
        LockObject->ReadLock();
    }

    TReadGuard(TReadGuard&& move)
        : LockObject(move.LockObject)
    {
        move.LockObject = nullptr;
    }

    TReadGuard& operator=(TReadGuard&& move) {
        if (LockObject != nullptr)
            LockObject->ReadUnlock();
        LockObject = move.LockObject;
        move.LockObject = nullptr;
        return *this;
    }

    ~TReadGuard() {
        if (LockObject != nullptr)
            LockObject->ReadUnlock();
    }

protected:
    TSpinRWLock* LockObject;
};


class TWriteGuard {
public:
    TWriteGuard(TSpinRWLock* rwlock)
        : LockObject(rwlock)
    {
        LockObject->WriteLock();
    }

    TWriteGuard(TWriteGuard&& move)
        : LockObject(move.LockObject)
    {
        move.LockObject = nullptr;
    }

    TWriteGuard& operator=(TWriteGuard&& move) {
        if (LockObject != nullptr)
            LockObject->ReadUnlock();
        LockObject = move.LockObject;
        move.LockObject = nullptr;
        return *this;
    }

    ~TWriteGuard() {
        if (LockObject != nullptr)
            LockObject->WriteUnlock();
    }

protected:
    TSpinRWLock* LockObject;
};

} // namespace bsc
