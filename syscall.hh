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

#include <exception>
#include <errno.h>
#include <string.h>


namespace bsc {

constexpr int SYSCALL_NAME_MAX_LEN = 30;
constexpr int SYSCALL_ERROR_MESSAGE_BUFFER_SIZE = 120;


class ESyscallError: public std::exception {
public:
    ESyscallError(): Errno_(errno) {}
    ESyscallError(int myerrno): Errno_(myerrno) {}

    ESyscallError(const char* name)
        : Errno_(errno)
        , Name_(name)
    {}

    ESyscallError(const char* name, int myerrno)
        : Errno_(myerrno)
        , Name_(name)
    {}


    virtual const char* what() const noexcept override {
        const char* descr = ::strerror_r(Errno_, Buf_, sizeof(Buf_));
        if (Name_ == nullptr)
            return descr;

        ::strncpy(Buf_, Name_, SYSCALL_NAME_MAX_LEN);
        Buf_[SYSCALL_NAME_MAX_LEN] = 0;
        char* tail = &Buf_[0] + strlen(Buf_);

        *(tail++) = ':';
        *(tail++) = ' ';

        strncpy(tail, descr,
                SYSCALL_ERROR_MESSAGE_BUFFER_SIZE - (tail - &Buf_[0]) - 1);
        Buf_[SYSCALL_ERROR_MESSAGE_BUFFER_SIZE - 1] = 0;

        return &Buf_[0];
    }


    int get_errno() const noexcept {
    	return Errno_;
    }

    const char* get_name() const noexcept {
        return Name_;
    }

    static void Validate(int res, const char* name = nullptr) {
        if (res == -1)
            throw ESyscallError(name);
    }

protected:
    const int Errno_;
    const char* Name_ = nullptr;
    mutable char Buf_[SYSCALL_ERROR_MESSAGE_BUFFER_SIZE];
};

} // namespace bsc
