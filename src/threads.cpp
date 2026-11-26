#include "threads.h"

#include <cstdlib>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace endgame {

struct NativeThread::Impl {
    std::function<void()> fn;
#if defined(_WIN32)
    HANDLE handle = nullptr;
#else
    pthread_t handle{};
#endif
};

namespace {
#if defined(_WIN32)
DWORD WINAPI trampoline(LPVOID arg) {
    auto* impl = static_cast<std::function<void()>*>(arg);
    (*impl)();
    return 0;
}
#else
void* trampoline(void* arg) {
    auto* fn = static_cast<std::function<void()>*>(arg);
    (*fn)();
    return nullptr;
}
#endif
}  // namespace

NativeThread::NativeThread(std::function<void()> fn) : impl_(new Impl{std::move(fn)}) {
#if defined(_WIN32)
    impl_->handle = CreateThread(nullptr, STACK_SIZE, trampoline, &impl_->fn,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!impl_->handle) throw std::runtime_error("CreateThread failed");
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    int rc = pthread_create(&impl_->handle, &attr, trampoline, &impl_->fn);
    pthread_attr_destroy(&attr);
    if (rc != 0) throw std::runtime_error("pthread_create failed");
#endif
}

void NativeThread::join() {
    if (!impl_) return;
#if defined(_WIN32)
    WaitForSingleObject(impl_->handle, INFINITE);
    CloseHandle(impl_->handle);
#else
    pthread_join(impl_->handle, nullptr);
#endif
    delete impl_;
    impl_ = nullptr;
}

NativeThread::~NativeThread() {
    if (impl_) std::abort();  // like std::thread: must be joined first
}

NativeThread::NativeThread(NativeThread&& o) noexcept : impl_(std::exchange(o.impl_, nullptr)) {}

NativeThread& NativeThread::operator=(NativeThread&& o) noexcept {
    if (impl_) std::abort();
    impl_ = std::exchange(o.impl_, nullptr);
    return *this;
}

}  // namespace endgame
