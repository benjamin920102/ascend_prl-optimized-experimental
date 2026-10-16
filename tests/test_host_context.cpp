/* Exercise the production TLS cache without an NPU or CANN installation. */
#include <cassert>
#include <cstdio>
#include <thread>
#include "../kernel/host_context.h"

static thread_local int calls = 0;
static thread_local aclError next_error = ACL_SUCCESS;
static thread_local aclrtContext current = nullptr;

aclError aclrtSetCurrentContext(aclrtContext context)
{
    ++calls;
    if (next_error != ACL_SUCCESS) return next_error;
    current = context;
    return ACL_SUCCESS;
}

int main()
{
    int first, second;
    aclrtContext context = &first;
    // Init has already made the explicit context current via CreateContext.
    current = context;
    pearl_thread_context() = context;
    for (int i = 0; i < 10000; ++i)
        assert(pearl_bind_context(context) == ACL_SUCCESS);
    assert(calls == 0);

    auto worker = [context]() {
        assert(pearl_thread_context() == nullptr);
        for (int i = 0; i < 10000; ++i)
            assert(pearl_bind_context(context) == ACL_SUCCESS);
        assert(calls == 1 && current == context);
    };
    std::thread a(worker), b(worker);
    a.join(); b.join();
    // A fresh prep thread must bind even when an OS thread ID is reused.
    std::thread c(worker);
    c.join();
    assert(calls == 0 && current == context);

    // A failed bind is not cached; the next attempt must reach the runtime.
    next_error = 42;
    assert(pearl_bind_context(&second) == 42);
    assert(calls == 1 && current == context);
    next_error = ACL_SUCCESS;
    assert(pearl_bind_context(&second) == ACL_SUCCESS);
    assert(calls == 2 && current == &second);
    assert(pearl_bind_context(&second) == ACL_SUCCESS);
    assert(calls == 2);
    assert(pearl_bind_context(nullptr) == ACL_ERROR_INVALID_PARAM);
    assert(calls == 2);

    // Explicit invalidation (e.g. finalization) forces a subsequent rebind.
    pearl_thread_context() = nullptr;
    assert(pearl_bind_context(&second) == ACL_SUCCESS);
    assert(calls == 3);
    std::puts("PASS: per-thread binding, hot-path API count, failed bind retry, invalidation");
}
