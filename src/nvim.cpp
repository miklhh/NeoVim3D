#include "nvim.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

Nvim::~Nvim() { stop(); }

void Nvim::stop() {
    if (stopping_.exchange(true)) return;
    if (to_nvim_ >= 0) {
        std::lock_guard lk(write_mu_);
        close(to_nvim_);  // nvim exits when its stdin closes
        to_nvim_ = -1;
    }
    if (pid_ > 0) {
        // Give it a moment, then make sure it is gone.
        for (int i = 0; i < 50 && waitpid(pid_, nullptr, WNOHANG) == 0; ++i) usleep(10000);
        if (waitpid(pid_, nullptr, WNOHANG) == 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
        }
    }
    if (reader_.joinable()) {
        // A grandchild may still hold nvim's stdout open; never block on it.
        for (int i = 0; i < 20 && !exited_; ++i) usleep(10000);
        if (exited_) {
            reader_.join();
            if (from_nvim_ >= 0) close(from_nvim_);
        } else {
            reader_.detach();
        }
    }
}

bool Nvim::start(const std::string& nvim_path, const std::vector<std::string>& args,
                 std::function<void()> wake) {
    wake_ = std::move(wake);
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) != 0 || pipe2(out, O_CLOEXEC) != 0) {
        std::perror("pipe");
        return false;
    }
    // Errors from exec are reported through this pipe.
    int err[2];
    if (pipe2(err, O_CLOEXEC) != 0) return false;

    std::vector<std::string> argv_s{nvim_path, "--embed"};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : argv_s) argv.push_back(s.data());
    argv.push_back(nullptr);

    pid_ = fork();
    if (pid_ < 0) {
        std::perror("fork");
        return false;
    }
    if (pid_ == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        execvp(argv[0], argv.data());
        int e = errno;
        (void)!::write(err[1], &e, sizeof e);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    close(err[1]);
    int child_errno = 0;
    if (read(err[0], &child_errno, sizeof child_errno) == sizeof child_errno) {
        std::fprintf(stderr, "neovim3d: failed to run '%s': %s\n", nvim_path.c_str(),
                     std::strerror(child_errno));
        close(err[0]);
        close(in[1]);
        close(out[0]);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
        return false;
    }
    close(err[0]);
    to_nvim_ = in[1];
    from_nvim_ = out[0];
    signal(SIGPIPE, SIG_IGN);
    reader_ = std::thread([this] { reader_loop(); });
    return true;
}

void Nvim::reader_loop() {
    mp::Decoder dec([this](char* buf, size_t n) -> size_t {
        for (;;) {
            ssize_t r = read(from_nvim_, buf, n);
            if (r < 0 && errno == EINTR) continue;
            return r > 0 ? size_t(r) : 0;
        }
    });
    try {
        for (;;) {
            mp::Value msg = dec.next();
            int type = int(msg[0].as_int(-1));
            Incoming in;
            in.type = type;
            if (type == 0) {
                in.msgid = msg[1].as_int();
                in.method = msg[2].as_str();
                in.a = msg[3];
            } else if (type == 1) {
                in.msgid = msg[1].as_int();
                in.a = msg[2];
                in.b = msg[3];
            } else if (type == 2) {
                in.type = 2;
                in.method = msg[1].as_str();
                in.a = msg[2];
            } else {
                continue;
            }
            {
                std::lock_guard lk(queue_mu_);
                queue_.push_back(std::move(in));
            }
            if (wake_ && !stopping_) wake_();
        }
    } catch (const std::exception&) {
    }
    exited_ = true;
    if (wake_ && !stopping_) wake_();
}

void Nvim::write(const std::string& data) {
    std::lock_guard lk(write_mu_);
    if (to_nvim_ < 0) return;
    size_t off = 0;
    while (off < data.size()) {
        ssize_t w = ::write(to_nvim_, data.data() + off, data.size() - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        off += size_t(w);
    }
}

void Nvim::request(const std::string& method, mp::Array params, ResponseFn cb) {
    if (to_nvim_ < 0) return;
    uint64_t id = next_msgid_++;
    if (cb) pending_[id] = std::move(cb);
    write(mp::encode(mp::Array{0, id, method, std::move(params)}));
}

void Nvim::notify(const std::string& method, mp::Array params) {
    if (to_nvim_ < 0) return;
    write(mp::encode(mp::Array{2, method, std::move(params)}));
}

void Nvim::dispatch(const NotificationFn& on_notify, const RequestFn& on_request) {
    std::deque<Incoming> batch;
    {
        std::lock_guard lk(queue_mu_);
        batch.swap(queue_);
    }
    for (auto& in : batch) {
        if (in.type == 2) {
            on_notify(in.method, in.a);
        } else if (in.type == 1) {
            auto it = pending_.find(in.msgid);
            if (it != pending_.end()) {
                auto cb = std::move(it->second);
                pending_.erase(it);
                cb(in.a, in.b);
            }
        } else if (in.type == 0) {
            mp::Value error;
            mp::Value result = on_request(in.method, in.a, error);
            write(mp::encode(mp::Array{1, in.msgid, error, result}));
        }
    }
}
