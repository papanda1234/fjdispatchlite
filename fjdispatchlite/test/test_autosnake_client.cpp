#include "fjdispatchlite.h"
#include "fjsharedmem.h"
#include "fjtimerlite.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <vector>
#include <unistd.h>

namespace {
constexpr int kBoardSize = 50;
constexpr uint32_t kMagic = 0x534E414B;

constexpr fjt_msg_t MID_JOIN_REQ = 61001;
constexpr fjt_msg_t MID_JOIN_RESP = 61002;
constexpr fjt_msg_t MID_STATE_REQ = 61003;
constexpr fjt_msg_t MID_STATE_RESP = 61004;
constexpr fjt_msg_t MID_VECTOR_UPDATE = 61005;

struct GameShared {
    uint32_t magic;
    uint32_t tick;
    char board[kBoardSize][kBoardSize];
};

struct JoinRequest {
    pid_t pid;
};

struct JoinResponse {
    pid_t pid;
    int x;
    int y;
    int dir;
};

struct StateRequest {
    pid_t pid;
};

struct StateResponse {
    pid_t pid;
    int x;
    int y;
    int dir;
    uint32_t tick;
};

struct VectorUpdate {
    pid_t pid;
    int dir;
};

enum Direction {
    kUp = 0,
    kDown = 1,
    kLeft = 2,
    kRight = 3,
    kDead = 4,
};

char headChar(Direction dir) {
    switch (dir) {
        case kUp:
            return 'A';
        case kDown:
            return 'V';
        case kLeft:
            return '<';
        case kRight:
            return '>';
        case kDead:
        default:
            return 'X';
    }
}

std::atomic<bool> g_running(true);

void on_signal(int) {
    g_running.store(false);
}
} // namespace

class AutoSnakeClient : public FJSharedMem, public FJUnitFrames {
public:
    AutoSnakeClient(const std::string& name, const char* srcfunc)
        : FJSharedMem(name, sizeof(GameShared), srcfunc, {MID_JOIN_RESP, MID_STATE_RESP}),
          shared_(static_cast<GameShared*>(_get())) {}

    int onTick(fjt_handle_t /*handle*/, fjt_time_t /*now*/) {
        if (!joined_.load()) {
            return 0;
        }
        if (!alive_.load()) {
            return -1;
        }
        StateRequest req{pid_};
        notify(this, MID_STATE_REQ, &req, sizeof(req));
        return 0;
    }

    void updateWithData(FJSharedMem* /*obj*/, fjt_msg_t msg, const void* buf, size_t size) override {
        if (!buf) {
            return;
        }
        switch (msg) {
            case MID_JOIN_RESP:
                if (size >= sizeof(JoinResponse)) {
                    JoinResponse resp;
                    std::memcpy(&resp, buf, sizeof(resp));
                    if (resp.pid != pid_) {
                        break;
                    }
                    x_ = resp.x;
                    y_ = resp.y;
                    dir_ = static_cast<Direction>(resp.dir);
                    joined_.store(true);
                    alive_.store(true);
                    std::cerr << "[client] joined at (" << x_ << "," << y_ << ") dir=" << dir_ << "\n";
                }
                break;
            case MID_STATE_RESP:
                if (size >= sizeof(StateResponse)) {
                    StateResponse resp;
                    std::memcpy(&resp, buf, sizeof(resp));
                    if (resp.pid != pid_) {
                        break;
                    }
                    x_ = resp.x;
                    y_ = resp.y;
                    dir_ = static_cast<Direction>(resp.dir);
                    tick_ = resp.tick;
                    renderBoard();
                    Direction next = decideNextMove();
                    VectorUpdate update{pid_, static_cast<int>(next)};
                    notify(this, MID_VECTOR_UPDATE, &update, sizeof(update));
                    if (next == kDead) {
                        alive_.store(false);
                    }
                }
                break;
            default:
                break;
        }
    }

    void sendJoin() {
        JoinRequest req{pid_};
        notify(this, MID_JOIN_REQ, &req, sizeof(req));
    }

private:
    void renderBoard() {
        char local[kBoardSize][kBoardSize];
        if (!copyBoard(local)) {
            return;
        }
        if (x_ >= 0 && x_ < kBoardSize && y_ >= 0 && y_ < kBoardSize) {
            local[y_][x_] = headChar(dir_);
        }
        std::cout << "\033[H\033[2J";
        std::cout << "AutoSnake tick=" << tick_ << " pid=" << pid_ << "\n";
        for (int y = 0; y < kBoardSize; ++y) {
            for (int x = 0; x < kBoardSize; ++x) {
                std::cout << local[y][x];
            }
            std::cout << "\n";
        }
        std::cout.flush();
    }

    bool copyBoard(char out[kBoardSize][kBoardSize]) {
        if (!shared_) {
            return false;
        }
        FJSyncGuard guard(this);
        if (shared_->magic != kMagic) {
            return false;
        }
        for (int y = 0; y < kBoardSize; ++y) {
            for (int x = 0; x < kBoardSize; ++x) {
                out[y][x] = shared_->board[y][x];
            }
        }
        return true;
    }

    bool isOpenCell(int x, int y, const char board[kBoardSize][kBoardSize]) const {
        if (x < 0 || y < 0 || x >= kBoardSize || y >= kBoardSize) {
            return false;
        }
        return board[y][x] == ' ';
    }

    Direction decideNextMove() {
        char local[kBoardSize][kBoardSize];
        if (!copyBoard(local)) {
            return dir_;
        }
        std::vector<Direction> candidates;
        candidates.push_back(dir_);
        candidates.push_back(kUp);
        candidates.push_back(kRight);
        candidates.push_back(kDown);
        candidates.push_back(kLeft);
        for (Direction candidate : candidates) {
            int nx = x_;
            int ny = y_;
            switch (candidate) {
                case kUp:
                    ny -= 1;
                    break;
                case kDown:
                    ny += 1;
                    break;
                case kLeft:
                    nx -= 1;
                    break;
                case kRight:
                    nx += 1;
                    break;
                case kDead:
                default:
                    break;
            }
            if (isOpenCell(nx, ny, local)) {
                return candidate;
            }
        }
        return kDead;
    }

    GameShared* shared_;
    std::atomic<bool> joined_{false};
    std::atomic<bool> alive_{false};
    int x_ = -1;
    int y_ = -1;
    Direction dir_ = kRight;
    uint32_t tick_ = 0;
};

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    AutoSnakeClient client("/fj_autosnake", __PRETTY_FUNCTION__);
    client.sendJoin();

    FJTimerLite* timer = FJTimerLite::GetInstance();
    fjt_handle_t tick_timer = timer->createTimer(&client, &AutoSnakeClient::onTick, 1000, __FUNCTION__, __LINE__);

    std::cerr << "[client] AutoSnake client started.\n";

    while (g_running.load()) {
        sleep(1);
    }

    timer->removeTimer(tick_timer);
    std::cerr << "[client] stopping.\n";
    return 0;
}
