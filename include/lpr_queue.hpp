#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>

class PRQ {
 private:
  static constexpr uint64_t R = 1ULL << 10;
  static constexpr uint64_t STARVING_THRESHOLD = R;

  struct alignas(64) Cell {
    std::atomic<uint64_t> safe_and_epoch;
    std::atomic<uint64_t> value;
  };

  static constexpr uint64_t SAFE_BIT = 1ULL << 63;
  static constexpr uint64_t EPOCH_MASK = ~SAFE_BIT;
  static bool inline decode_cell_safe(uint64_t safe_and_epoch) {
    return safe_and_epoch & SAFE_BIT;
  }
  static uint64_t inline decode_cell_epoch(uint64_t safe_and_epoch) {
    return safe_and_epoch & EPOCH_MASK;
  }
  static uint64_t inline encode_cell_safe_epoch(bool safe, uint64_t epoch) {
    return (static_cast<uint64_t>(safe) << 63) | (epoch & EPOCH_MASK);
  }

  alignas(64) std::atomic<uint64_t> _head;
  alignas(64) std::atomic<uint64_t> _tail;
  Cell _a[R];

  static constexpr uint64_t CLOSED_BIT = 1ULL << 63;
  static constexpr uint64_t T_MASK = ~CLOSED_BIT;
  static bool inline decode_tail_closed(uint64_t tail) {
    return tail & CLOSED_BIT;
  }
  static uint64_t inline decode_tail_t(uint64_t tail) { return tail & T_MASK; }
  static uint64_t inline encode_tail_closed_t(bool closed, uint64_t t) {
    return (static_cast<uint64_t>(closed) << 63) | (t & T_MASK);
  }

  static constexpr uint64_t IS_TOKEN_BIT = 1ULL;
  static inline uint64_t thread_token() {
    thread_local char token;
    return reinterpret_cast<uint64_t>(&token) | IS_TOKEN_BIT;
  }
  static inline bool is_token(uint64_t value) { return value & IS_TOKEN_BIT; }
  static constexpr uint64_t NULL_VALUE = 0;

 public:
  enum class EnqueueResult { OK, CLOSED };
  static constexpr uint64_t EMPTY = 0;

  alignas(64) std::atomic<PRQ*> next;

  PRQ() : _head(R), _tail(R), next(nullptr) {
    for (uint64_t i = 0; i < R; i++) {
      _a[i].safe_and_epoch = encode_cell_safe_epoch(true, 0);
      _a[i].value = NULL_VALUE;
    }
  }

  EnqueueResult enqueue(uint64_t item) {
    uint64_t _token = thread_token();
    uint64_t iteration = 0;

    while (true) {
      uint64_t t_currthread = _token;
      uint64_t tmp = _tail.fetch_add(1);
      bool closed = decode_tail_closed(tmp);
      uint64_t t = decode_tail_t(tmp);

      if (closed) return EnqueueResult::CLOSED;

      uint64_t cycle = t / R;
      uint64_t i = t % R;

      uint64_t safe_and_epoch = _a[i].safe_and_epoch;
      bool safe = decode_cell_safe(safe_and_epoch);
      uint64_t epoch = decode_cell_epoch(safe_and_epoch);
      uint64_t value = _a[i].value;

      if ((value == NULL_VALUE || is_token(value)) && epoch < cycle &&
          (safe || _head <= t)) {
        if (!_a[i].value.compare_exchange_strong(value, t_currthread)) {
          goto checkOverflow;
        }

        if (!_a[i].safe_and_epoch.compare_exchange_strong(
                safe_and_epoch, encode_cell_safe_epoch(true, cycle))) {
          _a[i].value.compare_exchange_strong(t_currthread, NULL_VALUE);
          goto checkOverflow;
        }

        if (_a[i].value.compare_exchange_strong(t_currthread, item)) {
          return EnqueueResult::OK;
        }
      }

    checkOverflow:
      if (t - _head >= R || iteration++ > STARVING_THRESHOLD) {
        _tail.fetch_or(CLOSED_BIT);
        return EnqueueResult::CLOSED;
      }
    }
  }

  uint64_t dequeue() {
    while (true) {
      uint64_t h = _head.fetch_add(1);

      uint64_t cycle = h / R;
      uint64_t i = h % R;

      while (true) {
        uint64_t safe_and_epoch = _a[i].safe_and_epoch;
        bool safe = decode_cell_safe(safe_and_epoch);
        uint64_t epoch = decode_cell_epoch(safe_and_epoch);
        uint64_t value = _a[i].value;

        if (safe_and_epoch != _a[i].safe_and_epoch) continue;

        if (epoch == cycle && !(value == NULL_VALUE || is_token(value))) {
          _a[i].value = NULL_VALUE;
          return value;
        }

        if (epoch <= cycle && (value == NULL_VALUE || is_token(value))) {
          if (is_token(value) &&
              !_a[i].value.compare_exchange_strong(value, NULL_VALUE)) {
            continue;
          }

          if (_a[i].safe_and_epoch.compare_exchange_strong(
                  safe_and_epoch, encode_cell_safe_epoch(safe, cycle))) {
            break;
          }
        } else if (epoch < cycle && !(value == NULL_VALUE || is_token(value))) {
          if (_a[i].safe_and_epoch.compare_exchange_strong(
                  safe_and_epoch, encode_cell_safe_epoch(false, epoch))) {
            break;
          }
          continue;
        } else {
          break;
        }
      }

      if (decode_tail_t(_tail) <= h + 1) return EMPTY;
    }
  }
};

template <typename T>
class LPRQueue {
 private:
  alignas(64) std::atomic<PRQ*> _head;
  alignas(64) std::atomic<PRQ*> _tail;

 public:
  LPRQueue() { _tail = _head = new PRQ(); }
  LPRQueue(const LPRQueue&) = delete;
  LPRQueue& operator=(const LPRQueue&) = delete;
  ~LPRQueue() {
    PRQ* curr = _head;
    while (curr != nullptr) {
      PRQ* next = curr->next;
      delete curr;
      curr = next;
    }
  }

  void enqueue(T* item) {
    uint64_t x = reinterpret_cast<uint64_t>(item);
    PRQ *prq, *newprq;

    while (true) {
      prq = _tail;

      if (prq->next != nullptr) {
        _tail.compare_exchange_strong(prq, prq->next);
        continue;
      }
      if (prq->enqueue(x) != PRQ::EnqueueResult::CLOSED) return;

      newprq = new PRQ();
      newprq->enqueue(x);

      PRQ* expected = nullptr;
      if (prq->next.compare_exchange_strong(expected, newprq)) {
        _tail.compare_exchange_strong(prq, newprq);
        return;
      }
      delete newprq;
    }
  }

  bool dequeue(T*& item) {
    PRQ* prq;
    uint64_t v;

    while (true) {
      prq = _head;
      v = prq->dequeue();

      if (v != PRQ::EMPTY) {
        item = reinterpret_cast<T*>(v);
        return true;
      }

      PRQ* next = prq->next;
      if (next == nullptr) return false;

      v = prq->dequeue();
      if (v != PRQ::EMPTY) {
        item = reinterpret_cast<T*>(v);
        return true;
      }

      _head.compare_exchange_strong(prq, next);
    }
  }
};