#pragma once

#include <stdexec/execution.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <thread>
#include <iostream>

struct operation_base {
    OVERLAPPED overlapped;
    void (*complete)(operation_base*, DWORD, DWORD) noexcept;
};

class iocp_context {
public:
    iocp_context() : port_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0)) {
        if (!port_) {
            throw std::system_error(GetLastError(), std::system_category(), "CreateIoCompletionPort");
        }
    }

    ~iocp_context() {
        if (port_) {
            CloseHandle(port_);
        }
    }

    iocp_context(const iocp_context&) = delete;
    iocp_context& operator=(const iocp_context&) = delete;

    void associate(HANDLE file) {
        if (!CreateIoCompletionPort(file, port_, 0, 0))
            throw std::system_error(GetLastError(), std::system_category(), "associate");
    }

    void run() {
        while (true) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* ov = nullptr;
            BOOL ok = GetQueuedCompletionStatus(port_, &bytes, &key, &ov, INFINITE);

            if (ov == nullptr) {
                if (!ok || key == stop_key_) return;
                continue;
            }

            DWORD err = ok ? ERROR_SUCCESS : GetLastError();
            auto* base = CONTAINING_RECORD(ov, operation_base, overlapped);
            base->complete(base, bytes, err);
        }
    }

    void request_stop() {
        PostQueuedCompletionStatus(port_, 0, stop_key_, nullptr);
    }

private:
    static constexpr ULONG_PTR stop_key_ = ~ULONG_PTR{0};
    HANDLE port_;
};

template<class Receiver>
struct read_op : operation_base {
    using operation_state_concept = stdexec::operation_state_t;

    Receiver rcvr;
    HANDLE file;
    void* buffer;
    DWORD length;
    std::uint64_t offset;

    read_op(Receiver r, HANDLE f, void* buf, DWORD len, std::uint64_t off) : operation_base{{}, &read_op::on_complete}, rcvr(std::move(r)), file(f), buffer(buf), length(len), offset(off) {}

    void start() noexcept {
        overlapped = OVERLAPPED{};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFull);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

        BOOL ok = ReadFile(file, buffer, length, nullptr, &overlapped);
        if (!ok) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING)
                on_complete(this, 0, err);
        }
    }

    static void on_complete(operation_base* base, DWORD bytes, DWORD error) noexcept {
        auto& self = *static_cast<read_op*>(base);
        if (error == ERROR_SUCCESS) {
            std::cout << "read_op::on_complete: error{ERROR_SUCCESS} bytes{" << bytes << "} " << std::endl;
            stdexec::set_value(std::move(self.rcvr), static_cast<std::size_t>(bytes));
        } else if (error == ERROR_HANDLE_EOF) {
            std::cout << "read_op::on_complete: error{ERROR_HANDLE_EOF}" << std::endl;
            stdexec::set_value(std::move(self.rcvr), std::size_t{0});
        } else {
            std::cout << "read_op::on_complete: error{" << error << "}" << std::endl;
            stdexec::set_error(std::move(self.rcvr), std::error_code(static_cast<int>(error), std::system_category()));
        }
    }
};

struct read_sender {
    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t(std::size_t), stdexec::set_error_t(std::error_code)>;

    HANDLE file;
    void* buffer;
    DWORD length;
    std::uint64_t offset;

    template<class Receiver>
    read_op<Receiver> connect(Receiver rcvr) {
        return read_op<Receiver>{std::move(rcvr), file, buffer, length, offset};
    }
};

stdexec::sender auto async_read_file(HANDLE file, std::span<std::byte> buf, std::uint64_t offset) {
    return read_sender{file, buf.data(), static_cast<DWORD>(buf.size()), offset};
}

int main() {
    iocp_context ctx;
    std::thread io{[&]{ctx.run();}};
    HANDLE file;
    file = CreateFileW(L"data.bin", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        std::cout << "Cannot create writer" << std::endl;
        return 1;
    }

    // TCHAR path[513];
    // LPWSTR file_part;

    // int path_len = GetFullPathNameW(L"data.bin", 513, path, &file_part);

    // printf("Path: ");
    // printf("%ls\n", path);

    const char* str_to_write = "Hello IOCP!";

    WriteFile(file, str_to_write, strlen(str_to_write), nullptr, nullptr);

    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;

    file = CreateFileW(L"data.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        std::cout << "Cannot create reader: error " << GetLastError() << std::endl;
        return 1;
    }

    ctx.associate(file);

    std::byte buf[4096];
    auto [n] = stdexec::sync_wait(async_read_file(file, buf, 0)).value();
    std::printf("read %zu bytes: %s\n", n, buf);

    CloseHandle(file);
    ctx.request_stop();
    io.join();
}