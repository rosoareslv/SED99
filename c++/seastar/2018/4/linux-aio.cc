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
 * Copyright (C) 2017 ScyllaDB
 */

#include "linux-aio.hh"
#include <unistd.h>
#include <sys/syscall.h>
#include <atomic>
#include <algorithm>

namespace seastar {

namespace internal {


struct linux_aio_ring {
    uint32_t id;
    uint32_t nr;
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    uint32_t magic;
    uint32_t compat_features;
    uint32_t incompat_features;
    uint32_t header_length;
};

static linux_aio_ring* to_ring(::aio_context_t io_context) {
    return reinterpret_cast<linux_aio_ring*>(uintptr_t(io_context));
}

static bool usable(const linux_aio_ring* ring) {
    return ring->magic == 0xa10a10a1 && ring->incompat_features == 0;
}

int io_setup(int nr_events, ::aio_context_t* io_context) {
    return ::syscall(SYS_io_setup, nr_events, io_context);
}

int io_destroy(::aio_context_t io_context) {
   return ::syscall(SYS_io_destroy, io_context);
}

int io_submit(::aio_context_t io_context, long nr, ::iocb** iocbs) {
    return ::syscall(SYS_io_submit, io_context, nr, iocbs);
}

int io_cancel(::aio_context_t io_context, ::iocb* iocb, ::io_event* result) {
    return ::syscall(SYS_io_cancel, io_context, iocb, result);
}

int io_getevents(::aio_context_t io_context, long min_nr, long nr, ::io_event* events, const ::timespec* timeout) {
    auto ring = to_ring(io_context);
    if (usable(ring)) {
        // Try to complete in userspace, if enough available events,
        // or if the timeout is zero

        // We're the only writer to ->head, so we can load with memory_order_relaxed (assuming
        // only a single thread calls io_getevents()).
        auto head = ring->head.load(std::memory_order_relaxed);
        // The kernel will write to the ring from an interrupt and then release with a write
        // to ring->tail, so we must memory_order_acquire here.
        auto tail = ring->tail.load(std::memory_order_acquire); // kernel writes from interrupts
        auto available = tail - head;
        if (tail < head) {
            available += ring->nr;
        }
        if (available >= uint32_t(min_nr)
                || (timeout && timeout->tv_sec == 0 && timeout->tv_nsec == 0)) {
            if (!available) {
                return 0;
            }
            auto ring_events = reinterpret_cast<const ::io_event*>(uintptr_t(io_context) + ring->header_length);
            auto now = std::min<uint32_t>(nr, available);
            auto start = ring_events + head;
            auto end = start + now;
            if (head + now > ring->nr) {
                end -= ring->nr;
            }
            if (end > start) {
                std::copy(start, end, events);
            } else {
                auto p = std::copy(start, ring_events + ring->nr, events);
                std::copy(ring_events, end, p);
            }
            head += now;
            if (head >= ring->nr) {
                head -= ring->nr;
            }
            // The kernel will read ring->head and update its view of how many entries
            // in the ring are available, so memory_order_release to make sure any ring
            // accesses are completed before the update to ring->head is visible.
            ring->head.store(head, std::memory_order_release);
            return now;
        }
    }
    return ::syscall(SYS_io_getevents, io_context, min_nr, nr, events, timeout);
}

}

}
