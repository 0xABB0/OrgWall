#include <execution/execution.h>

#include <allocator/allocator.h>

#include <new>
#include <utility>

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

using Mel_Value_Sender = exec::any_sender<exec::any_receiver<stdexec::completion_signatures<stdexec::set_value_t(void*)>>>;

struct Mel_Execution_Sender
{
    Mel_Value_Sender sender;
};

extern "C" Mel_Execution_Sender* mel_execution_sender_create(const Mel_Alloc* alloc, Mel_Execution_Work work, void* ctx)
{
    Mel_Value_Sender sender = stdexec::just() | stdexec::then([work, ctx]() noexcept -> void* { return work(ctx); });
    void*            mem    = mel_aligned_alloc(alloc, sizeof(Mel_Execution_Sender), alignof(Mel_Execution_Sender));
    return new (mem) Mel_Execution_Sender{ std::move(sender) };
}

extern "C" void* mel_execution_sync_wait(Mel_Execution_Sender* sender)
{
    auto result = stdexec::sync_wait(std::move(sender->sender));
    if (!result)
        return nullptr;
    auto [value] = std::move(*result);
    return value;
}

extern "C" void mel_execution_sender_destroy(const Mel_Alloc* alloc, Mel_Execution_Sender* sender)
{
    sender->~Mel_Execution_Sender();
    mel_aligned_dealloc(alloc, sender, alignof(Mel_Execution_Sender));
}
