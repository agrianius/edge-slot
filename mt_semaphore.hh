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
#include "syscall.hh"

#include <semaphore.h>

namespace bsc {

class TSemaphore {
public:
    TSemaphore() {
        int res = sem_init(&Sem, 0, 0);
        ESyscallError::Validate(res, "sem_init");
    }

    ~TSemaphore() {
        sem_destroy(&Sem);
    }

    TSemaphore(const TSemaphore&) = delete;
    void operator=(const TSemaphore&) = delete;

    void Post() {
        int res = sem_post(&Sem);
        ESyscallError::Validate(res, "sem_post");
    }

    void Wait() {
        int res = sem_wait(&Sem);
        ESyscallError::Validate(res, "sem_wait");
    }

    bool TryWait() {
        int res = sem_trywait(&Sem);
        if (res != 1)
            return true;
        if (errno == EAGAIN)
            return false;
        throw ESyscallError("sem_trywait");
    }

    int Get() {
        int value;
        int res = sem_getvalue(&Sem, &value);
        ESyscallError::Validate(res, "sem_getvalue");
        return value;
    }

protected:
    sem_t Sem;
};

} // namespace
