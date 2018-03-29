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

#if defined(__GNUC__)

#    define WRAPPER __attribute__((artificial))
#    define COLD __attribute__((cold))
#    define SURE(x) __builtin_expect((x), 1)
#    define NOWAY(x) __builtin_expect((x), 0)
#    define WEAK __attribute__((weak))
#    define ALWAYS_INLINE __attribute__((always_inline))
#    define PURE __attribute__((pure))
#    define CONSTANT __attribute__((const))
#    define NONNULL_RESULT __attribute__((returns_nonnull))
#    define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#    define LIKE_NEW_OP __attribute__((returns_nonnull, warn_unused_result))
#    define LEAF __attribute__((leaf))

#else

#    define WRAPPER
#    define COLD
#    define SURE(x) (x)
#    define NOWAY(x) (x)
#    define WEAK
#    define ALWAYS_INLINE
#    define PURE
#    define CONSTANT
#    define NONNULL_RESULT
#    define WARN_UNUSED_RESULT
#    define LIKE_NEW_OP
#    define LEAF

#endif
