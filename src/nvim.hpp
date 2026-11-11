// Spawns `nvim --embed` and speaks msgpack-RPC with it.
#pragma once

#include <sys/types.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "msgpack.hpp"

class Nvim {
public:
    using ResponseFn = std::function<void(const mp::Value& error, const mp::Value& result)>;
    using NotificationFn = std::function<void(const std::string& method, const mp::Value& params)>;
    // Returns the result of an RPC request coming from Nvim; set `error` to fail it.
    using RequestFn =
        std::function<mp::Value(const std::string& method, const mp::Value& params, mp::Value& error)>;

    Nvim() = default;
    ~Nvim();
    Nvim(const Nvim&) = delete;
    Nvim& operator=(const Nvim&) = delete;

    // Start `nvim_path --embed args...`. `wake` is invoked from the reader
    // thread whenever new messages are queued (used to wake the event loop).
    bool start(const std::string& nvim_path, const std::vector<std::string>& args,
               std::function<void()> wake);

    void request(const std::string& method, mp::Array params, ResponseFn cb = {});
    void notify(const std::string& method, mp::Array params);

    // Dispatch all queued incoming messages on the calling thread.
    void dispatch(const NotificationFn& on_notify, const RequestFn& on_request);

    bool exited() const { return exited_; }

    // Close the connection and reap the process. Called by the destructor.
    void stop();

private:
    struct Incoming {
        int type;  // 0 request, 1 response, 2 notification
        uint64_t msgid;
        std::string method;
        mp::Value a, b;  // request/notification: params; response: error, result
    };

    void reader_loop();
    void write(const std::string& data);

    pid_t pid_ = -1;
    int to_nvim_ = -1, from_nvim_ = -1;
    std::thread reader_;
    std::function<void()> wake_;
    std::atomic<bool> exited_{false};
    std::atomic<bool> stopping_{false};

    std::mutex write_mu_;
    std::mutex queue_mu_;
    std::deque<Incoming> queue_;

    uint64_t next_msgid_ = 1;
    std::unordered_map<uint64_t, ResponseFn> pending_;
};
