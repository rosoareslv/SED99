/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#include "thread.hh"
#include "posix.hh"
#include <ucontext.h>
#include <algorithm>

/// \cond internal

namespace seastar {

thread_local jmp_buf_link g_unthreaded_context;
thread_local jmp_buf_link* g_current_context;

thread_context::thread_context(std::function<void ()> func)
        : _func(std::move(func)) {
    setup();
}

std::unique_ptr<char[]>
thread_context::make_stack() {
    auto stack = std::make_unique<char[]>(_stack_size);
#ifdef DEBUG
    // Avoid ASAN false positive due to garbage on stack
    std::fill_n(stack.get(), _stack_size, 0);
#endif
    return stack;
}

void
thread_context::setup() {
    // use setcontext() for the initial jump, as it allows us
    // to set up a stack, but continue with longjmp() as it's
    // much faster.
    ucontext_t initial_context;
    auto q = uint64_t(reinterpret_cast<uintptr_t>(this));
    auto main = reinterpret_cast<void (*)()>(&thread_context::s_main);
    auto r = getcontext(&initial_context);
    throw_system_error_on(r == -1);
    initial_context.uc_stack.ss_sp = _stack.get();
    initial_context.uc_stack.ss_size = _stack_size;
    initial_context.uc_link = nullptr;
    makecontext(&initial_context, main, 2, int(q), int(q >> 32));
    auto prev = g_current_context;
    _context.link = prev;
    _context.thread = this;
    g_current_context = &_context;
    if (setjmp(prev->jmpbuf) == 0) {
        setcontext(&initial_context);
    }
}

void
thread_context::switch_in() {
    auto prev = g_current_context;
    g_current_context = &_context;
    _context.link = prev;
    if (setjmp(prev->jmpbuf) == 0) {
        longjmp(_context.jmpbuf, 1);
    }
}

void
thread_context::switch_out() {
    g_current_context = _context.link;
    if (setjmp(_context.jmpbuf) == 0) {
        longjmp(g_current_context->jmpbuf, 1);
    }
}

void
thread_context::s_main(unsigned int lo, unsigned int hi) {
    uintptr_t q = lo | (uint64_t(hi) << 32);
    reinterpret_cast<thread_context*>(q)->main();
}

void
thread_context::main() {
    try {
        _func();
        _done.set_value();
    } catch (...) {
        _done.set_exception(std::current_exception());
    }
    g_current_context = _context.link;
    longjmp(g_current_context->jmpbuf, 1);
}

namespace thread_impl {

thread_context* get() {
    return g_current_context->thread;
}

void switch_in(thread_context* to) {
    to->switch_in();
}

void switch_out(thread_context* from) {
    from->switch_out();
}

void init() {
    g_unthreaded_context.link = nullptr;
    g_unthreaded_context.thread = nullptr;
    g_current_context = &g_unthreaded_context;
}

}

void thread::yield() {
    later().get();
}

}

/// \endcond
