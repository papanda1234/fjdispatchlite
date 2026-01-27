#include "fjdispatchlite.h"
#include "fjsharedmem.h"
#include "fjtimerlite.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>
#include <unistd.h>

namespace {
constexpr int kBoardSize = 50;
constexpr int kMinInner = 1;
constexpr int kMaxInner = 48;
constexpr uint32_t kMagic = 0x534E414B; // "SNAK"

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

struct Position {
    int x;
    int y;

    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
};

struct PositionHash {
    std::size_t operator()(const Position& pos) const {
        return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 1);
    }
};

struct Snake {
    pid_t pid;
    int x;
    int y;
    Direction dir;
};

std::atomic<bool> g_running(true);

void on_signal(int) {
    g_running.store(false);
}
} // namespace

class AutoSnakeServer : public FJSharedMem, public FJUnitFrames {
public:
    AutoSnakeServer(const std::string& name, const char* srcfunc)
        : FJSharedMem(name, sizeof(GameShared), srcfunc,
                      {MID_JOIN_REQ, MID_STATE_REQ, MID_VECTOR_UPDATE}),
          shared_(static_cast<GameShared*>(_get())),
          rng_(std::random_device{}()) {
        initBoard();
    }

    int onTick(fjt_handle_t /*handle*/, fjt_time_t /*now*/) {
        updateSnakes();
        return 0;
    }

    void updateWithData(FJSharedMem* /*obj*/, fjt_msg_t msg, const void* buf, size_t size) override {
        if (!buf) {
            return;
        }
        switch (msg) {
            case MID_JOIN_REQ:
                if (size >= sizeof(JoinRequest)) {
                    JoinRequest req;
                    std::memcpy(&req, buf, sizeof(req));
                    handleJoin(req);
                }
                break;
            case MID_STATE_REQ:
                if (size >= sizeof(StateRequest)) {
                    StateRequest req;
                    std::memcpy(&req, buf, sizeof(req));
                    handleState(req);
                }
                break;
            case MID_VECTOR_UPDATE:
                if (size >= sizeof(VectorUpdate)) {
                    VectorUpdate update;
                    std::memcpy(&update, buf, sizeof(update));
                    handleVector(update);
                }
                break;
            default:
                break;
        }
    }

private:
    void initBoard() {
        tick_ = 0;
        for (int y = 0; y < kBoardSize; ++y) {
            for (int x = 0; x < kBoardSize; ++x) {
                if (x == 0 || y == 0 || x == kBoardSize - 1 || y == kBoardSize - 1) {
                    board_[y][x] = '#';
                } else {
                    board_[y][x] = ' ';
                }
            }
        }
        writeSharedBoard();
    }

    void writeSharedBoard() {
        if (!shared_) {
            return;
        }
        FJSyncGuard guard(this);
        shared_->magic = kMagic;
        shared_->tick = tick_;
        for (int y = 0; y < kBoardSize; ++y) {
            for (int x = 0; x < kBoardSize; ++x) {
                shared_->board[y][x] = board_[y][x];
            }
        }
        for (const auto& entry : snakes_) {
            const Snake& snake = entry.second;
            if (snake.x < 0 || snake.x >= kBoardSize || snake.y < 0 || snake.y >= kBoardSize) {
                continue;
            }
            shared_->board[snake.y][snake.x] = headChar(snake.dir);
        }
    }

    bool isOpenCell(int x, int y) const {
        if (x < 0 || y < 0 || x >= kBoardSize || y >= kBoardSize) {
            return false;
        }
        if (board_[y][x] != ' ') {
            return false;
        }
        for (const auto& entry : snakes_) {
            if (entry.second.x == x && entry.second.y == y) {
                return false;
            }
        }
        return true;
    }

    void handleJoin(const JoinRequest& req) {
        auto it = snakes_.find(req.pid);
        if (it != snakes_.end()) {
            sendJoinResponse(it->second);
            return;
        }
        std::uniform_int_distribution<int> dist(kMinInner, kMaxInner);
        Position pos{dist(rng_), dist(rng_)};
        int attempts = 0;
        while (!isOpenCell(pos.x, pos.y) && attempts < 2000) {
            pos = {dist(rng_), dist(rng_)};
            ++attempts;
        }
        if (!isOpenCell(pos.x, pos.y)) {
            std::cerr << "[server] unable to place new snake.\n";
            return;
        }
        std::uniform_int_distribution<int> dir_dist(0, 3);
        Snake snake{req.pid, pos.x, pos.y, static_cast<Direction>(dir_dist(rng_))};
        snakes_.emplace(req.pid, snake);
        std::cerr << "[server] joined pid=" << req.pid << " at (" << pos.x << "," << pos.y
                  << ") dir=" << snake.dir << "\n";
        sendJoinResponse(snake);
        writeSharedBoard();
    }

    void sendJoinResponse(const Snake& snake) {
        JoinResponse resp{snake.pid, snake.x, snake.y, static_cast<int>(snake.dir)};
        notify(this, MID_JOIN_RESP, &resp, sizeof(resp));
    }

    void handleState(const StateRequest& req) {
        auto it = snakes_.find(req.pid);
        if (it == snakes_.end()) {
            return;
        }
        const Snake& snake = it->second;
        StateResponse resp{snake.pid, snake.x, snake.y, static_cast<int>(snake.dir), tick_};
        notify(this, MID_STATE_RESP, &resp, sizeof(resp));
    }

    void handleVector(const VectorUpdate& update) {
        auto it = snakes_.find(update.pid);
        if (it == snakes_.end()) {
            return;
        }
        if (update.dir == kDead) {
            board_[it->second.y][it->second.x] = 'X';
            snakes_.erase(it);
            std::cerr << "[server] pid=" << update.pid << " died by request.\n";
            if (snakes_.empty()) {
                initBoard();
            } else {
                writeSharedBoard();
            }
            return;
        }
        it->second.dir = static_cast<Direction>(update.dir);
    }

    Position nextPosition(const Snake& snake) const {
        Position next{snake.x, snake.y};
        switch (snake.dir) {
            case kUp:
                next.y -= 1;
                break;
            case kDown:
                next.y += 1;
                break;
            case kLeft:
                next.x -= 1;
                break;
            case kRight:
                next.x += 1;
                break;
            case kDead:
            default:
                break;
        }
        return next;
    }

    void updateSnakes() {
        if (snakes_.empty()) {
            writeSharedBoard();
            return;
        }
        ++tick_;
        std::unordered_map<pid_t, Position> candidates;
        std::unordered_map<pid_t, Position> death_marks;
        std::unordered_map<Position, std::vector<pid_t>, PositionHash> occupancy;

        for (const auto& entry : snakes_) {
            const Snake& snake = entry.second;
            if (snake.dir == kDead) {
                death_marks.emplace(entry.first, Position{snake.x, snake.y});
                continue;
            }
            Position next = nextPosition(snake);
            if (next.x <= 0 || next.y <= 0 || next.x >= kBoardSize - 1 || next.y >= kBoardSize - 1) {
                death_marks.emplace(entry.first, Position{snake.x, snake.y});
                continue;
            }
            if (board_[next.y][next.x] != ' ') {
                death_marks.emplace(entry.first, Position{snake.x, snake.y});
                continue;
            }
            candidates.emplace(entry.first, next);
            occupancy[next].push_back(entry.first);
        }

        for (const auto& entry : occupancy) {
            if (entry.second.size() > 1) {
                for (pid_t pid : entry.second) {
                    death_marks[pid] = entry.first;
                }
            }
        }

        for (auto it = snakes_.begin(); it != snakes_.end();) {
            pid_t pid = it->first;
            Snake& snake = it->second;
            auto death_it = death_marks.find(pid);
            if (death_it != death_marks.end()) {
                Position mark = death_it->second;
                if (mark.x > 0 && mark.y > 0 && mark.x < kBoardSize - 1 && mark.y < kBoardSize - 1) {
                    board_[mark.y][mark.x] = 'X';
                } else {
                    board_[snake.y][snake.x] = 'X';
                }
                std::cerr << "[server] pid=" << pid << " died.\n";
                it = snakes_.erase(it);
                continue;
            }
            auto cand_it = candidates.find(pid);
            if (cand_it != candidates.end()) {
                board_[snake.y][snake.x] = '@';
                snake.x = cand_it->second.x;
                snake.y = cand_it->second.y;
            }
            ++it;
        }

        if (snakes_.empty()) {
            initBoard();
            return;
        }

        writeSharedBoard();
    }

    GameShared* shared_;
    char board_[kBoardSize][kBoardSize]{};
    uint32_t tick_ = 0;
    std::unordered_map<pid_t, Snake> snakes_;
    std::mt19937 rng_;
};

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    AutoSnakeServer server("/fj_autosnake", __PRETTY_FUNCTION__);
    FJTimerLite* timer = FJTimerLite::GetInstance();
    fjt_handle_t tick_timer = timer->createTimer(&server, &AutoSnakeServer::onTick, 1000, __FUNCTION__, __LINE__);

    std::cerr << "[server] AutoSnake server started.\n";

    while (g_running.load()) {
        sleep(1);
    }

    timer->removeTimer(tick_timer);
    std::cerr << "[server] stopping.\n";
    return 0;
}
