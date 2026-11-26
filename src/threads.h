#pragma once

#include <functional>
#include <memory>

namespace endgame {

// A joinable thread with a large stack. The search recurses up to MAX_PLY
// plies with move lists on the stack, which overflows the 512 KB default
// that macOS gives secondary threads (seen as SIGBUS in a mate-in-N
// position where iterative deepening reached depth 228).
class NativeThread {
public:
    static constexpr std::size_t STACK_SIZE = 16 * 1024 * 1024;

    NativeThread() = default;
    explicit NativeThread(std::function<void()> fn);
    ~NativeThread();
    NativeThread(NativeThread&& other) noexcept;
    NativeThread& operator=(NativeThread&& other) noexcept;
    NativeThread(const NativeThread&) = delete;
    NativeThread& operator=(const NativeThread&) = delete;

    bool joinable() const { return impl_ != nullptr; }
    void join();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace endgame
