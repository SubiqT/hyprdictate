#include "inject.hpp"

#include <cerrno>
#include <cstring>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace hyprdictate {

    WtypeInjector::WtypeInjector() = default;

    bool WtypeInjector::inject(const std::string&                  text,
                               const std::optional<WindowContext>& /*window*/) {
        if (text.empty()) {
            spdlog::debug("wtype: empty transcript, skipping");
            return true;
        }

        int pipefd[2] = {-1, -1};
        if (pipe(pipefd) < 0) {
            spdlog::error("wtype: pipe() failed: {}", std::strerror(errno));
            return false;
        }

        const pid_t pid = fork();
        if (pid < 0) {
            spdlog::error("wtype: fork() failed: {}", std::strerror(errno));
            close(pipefd[0]);
            close(pipefd[1]);
            return false;
        }

        if (pid == 0) {
            // Child: rewire stdin from the pipe's read end and exec
            // wtype. Anything after execlp is unreachable on success;
            // _exit rather than exit skips libc atexit hooks which
            // aren't safe in a post-fork environment.
            if (dup2(pipefd[0], STDIN_FILENO) < 0)
                _exit(127);
            close(pipefd[0]);
            close(pipefd[1]);

            // "-" tells wtype to read text from stdin. Every byte we
            // pipe is typed as characters into the Wayland compositor
            // via wtype's virtual-keyboard client.
            execlp("wtype", "wtype", "-", static_cast<char*>(nullptr));
            _exit(127);  // execlp only returns on failure.
        }

        // Parent: close the pipe read end (child owns it now), write
        // the transcript, close write end to signal EOF.
        close(pipefd[0]);

        std::size_t written = 0;
        while (written < text.size()) {
            const ssize_t n = write(pipefd[1],
                                    text.data() + written,
                                    text.size() - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                spdlog::error("wtype: write to child failed: {}",
                              std::strerror(errno));
                close(pipefd[1]);
                // Reap the child; discard the exit status.
                int status = 0;
                waitpid(pid, &status, 0);
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        close(pipefd[1]);

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            spdlog::error("wtype: waitpid failed: {}", std::strerror(errno));
            return false;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            spdlog::info("wtype: injected {} chars", text.size());
            return true;
        }

        if (WIFEXITED(status)) {
            spdlog::warn("wtype: exit status {}", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            spdlog::warn("wtype: killed by signal {}", WTERMSIG(status));
        }
        return false;
    }

}
