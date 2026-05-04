#pragma once

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>

class CRQ {
 private:
  static constexpr uint64_t R = 1ULL << 10;
  static constexpr uint64_t STARVING_THRESHOLD = R;

  struct alignas(64) Node {
    uint64_t safe_and_idx;
    uint64_t val;
  };

  static constexpr uint64_t SAFE_BIT = 1ULL << 63;
  static constexpr uint64_t IDX_MASK = ~SAFE_BIT;
  static bool inline decode_node_safe(uint64_t safe_and_idx) {
    return safe_and_idx & SAFE_BIT;
  }
  static uint64_t inline decode_node_idx(uint64_t safe_and_idx) {
    return safe_and_idx & IDX_MASK;
  }
  static uint64_t inline encode_node_safe_idx(bool safe, uint64_t idx) {
    return (static_cast<uint64_t>(safe) << 63) | (idx & IDX_MASK);
  }

  alignas(64) std::atomic<uint64_t> _head;
  alignas(64) std::atomic<uint64_t> _tail;
  Node _array[R];

  static constexpr uint64_t CLOSED_BIT = 1ULL << 63;
  static constexpr uint64_t T_MASK = ~CLOSED_BIT;
  static bool inline decode_tail_closed(uint64_t tail) {
    return tail & CLOSED_BIT;
  }
  static uint64_t inline decode_tail_t(uint64_t tail) { return tail & T_MASK; }
  static uint64_t inline encode_tail_closed_t(bool closed, uint64_t t) {
    return (static_cast<uint64_t>(closed) << 63) | (t & T_MASK);
  }

  void inline fixState() {
    while (true) {
      uint64_t h = _head.fetch_add(0);
      uint64_t t = _tail.fetch_add(0);

      if (_tail != t) continue;
      if (h <= t) return;

      if (_tail.compare_exchange_strong(t, h)) return;
    }
  }

  static inline bool CAS2(Node* n, const Node& expected_node,
                          const Node& new_node) {
    unsigned __int128 expected = ((unsigned __int128)expected_node.val << 64) |
                                 expected_node.safe_and_idx;
    unsigned __int128 desired =
        ((unsigned __int128)new_node.val << 64) | new_node.safe_and_idx;

    return __atomic_compare_exchange_n(
        reinterpret_cast<unsigned __int128*>(&n->safe_and_idx), &expected,
        desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  }

  static constexpr uint64_t UNDEFINED_NODE_VAL = 0;

 public:
  enum class EnqueueResult { OK, CLOSED };
  static constexpr uint64_t EMPTY = 0;

  alignas(64) std::atomic<CRQ*> next;

  CRQ() : _head(0), _tail(0), next(nullptr) {
    for (uint64_t i = 0; i < R; i++) {
      _array[i].safe_and_idx = encode_node_safe_idx(true, i);
      _array[i].val = UNDEFINED_NODE_VAL;
    }
  }

  EnqueueResult enqueue(uint64_t arg) {
    uint64_t val, idx;
    uint64_t h, t;
    Node* node;
    bool closed;
    bool safe;

    uint64_t iteration = 0;
    while (true) {
      uint64_t tmp = _tail.fetch_add(1);
      closed = decode_tail_closed(tmp);
      t = decode_tail_t(tmp);

      if (closed) return EnqueueResult::CLOSED;

      node = &_array[t % R];
      val = __atomic_load_n(&node->val, __ATOMIC_ACQUIRE);
      uint64_t safe_and_idx =
          __atomic_load_n(&node->safe_and_idx, __ATOMIC_ACQUIRE);
      safe = decode_node_safe(safe_and_idx);
      idx = decode_node_idx(safe_and_idx);

      if (val == EMPTY) {
        if ((idx <= t) && (safe == 1 || _head <= t) &&
            CAS2(node,
                 Node{encode_node_safe_idx(safe, idx), UNDEFINED_NODE_VAL},
                 Node{encode_node_safe_idx(true, t), arg})) {
          return EnqueueResult::OK;
        }
      }

      h = _head;
      if (t - h >= R || iteration++ > STARVING_THRESHOLD) {
        _tail.fetch_or(CLOSED_BIT);
        return EnqueueResult::CLOSED;
      }
    }
  }

  uint64_t dequeue() {
    uint64_t val, idx;
    uint64_t h, t;
    Node* node;
    bool safe;

    while (true) {
      h = _head.fetch_add(1);
      node = &_array[h % R];
      while (true) {
        val = __atomic_load_n(&node->val, __ATOMIC_ACQUIRE);
        uint64_t enc = __atomic_load_n(&node->safe_and_idx, __ATOMIC_ACQUIRE);
        safe = decode_node_safe(enc);
        idx = decode_node_idx(enc);

        if (idx > h) break;
        if (val != UNDEFINED_NODE_VAL) {
          if (idx == h) {
            if (CAS2(node, Node{encode_node_safe_idx(safe, h), val},
                     Node{encode_node_safe_idx(safe, h + R),
                          UNDEFINED_NODE_VAL})) {
              return val;
            }
          } else {
            if (CAS2(node, Node{encode_node_safe_idx(safe, idx), val},
                     Node{encode_node_safe_idx(false, idx), val})) {
              break;
            }
          }
        } else {
          if (CAS2(node,
                   Node{encode_node_safe_idx(safe, idx), UNDEFINED_NODE_VAL},
                   Node{encode_node_safe_idx(safe, h + R),
                        UNDEFINED_NODE_VAL})) {
            break;
          }
        }
      }

      t = decode_tail_t(_tail);
      if (t <= h + 1) {
        fixState();
        return EMPTY;
      }
    }
  }
};

template <typename T>
class LCRQueue {
 private:
  alignas(64) std::atomic<CRQ*> _head;
  alignas(64) std::atomic<CRQ*> _tail;

 public:
  LCRQueue() { _tail = _head = new CRQ(); }
  LCRQueue(const LCRQueue&) = delete;
  LCRQueue& operator=(const LCRQueue&) = delete;
  ~LCRQueue() {
    CRQ* curr = _head;
    while (curr != nullptr) {
      CRQ* next = curr->next;
      delete curr;
      curr = next;
    }
  }

  void enqueue(T* item) {
    uint64_t x = reinterpret_cast<uint64_t>(item);
    CRQ *crq, *newcrq;

    while (true) {
      crq = _tail;

      if (crq->next != nullptr) {
        _tail.compare_exchange_strong(crq, crq->next);
        continue;
      }
      if (crq->enqueue(x) != CRQ::EnqueueResult::CLOSED) return;

      newcrq = new CRQ();
      newcrq->enqueue(x);

      CRQ* expected = nullptr;
      if (crq->next.compare_exchange_strong(expected, newcrq)) {
        _tail.compare_exchange_strong(crq, newcrq);
        return;
      }
      delete newcrq;
    }
  }

  bool dequeue(T*& item) {
    CRQ* crq;
    uint64_t v;

    while (true) {
      crq = _head;
      v = crq->dequeue();

      if (v != CRQ::EMPTY) {
        item = reinterpret_cast<T*>(v);
        return true;
      }

      CRQ* next = crq->next;
      if (next == nullptr) return false;

      _head.compare_exchange_strong(crq, next);
    }
  }
};