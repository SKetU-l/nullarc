//
// skbuff.cpp — Freestanding sk_buff socket buffer emulation implementation
// For rtw89 driver on macOS / freestanding C++17 environments
//
#include "skbuff.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <IOKit/IOLib.h>
#include <string.h>
static inline void *skb_mem_alloc(size_t sz) { return IOMalloc(sz); }
static inline void *skb_mem_calloc(size_t sz) { return IOMallocZero(sz); }
static inline void skb_mem_free(void *ptr, size_t sz) { if (ptr) IOFree(ptr, sz); }
#else
#include <cstdlib>
#include <cstring>
static inline void *skb_mem_alloc(size_t sz) { return std::malloc(sz); }
static inline void *skb_mem_calloc(size_t sz) { return std::calloc(1, sz); }
static inline void skb_mem_free(void *ptr, size_t) { std::free(ptr); }
#endif

#define NET_SKB_PAD 64

struct alignas(16) skb_data_block {
    atomic_t dataref;
    size_t capacity;
};

struct sk_buff *alloc_skb(unsigned int size, gfp_t gfp) {
    (void)gfp;
    size_t alloc_bytes = sizeof(struct skb_data_block) + size;
    auto *block = static_cast<struct skb_data_block *>(skb_mem_alloc(alloc_bytes));
    if (!block)
        return nullptr;

    atomic_set(&block->dataref, 1);
    block->capacity = size;

    auto *skb = static_cast<struct sk_buff *>(skb_mem_calloc(sizeof(struct sk_buff)));
    if (!skb) {
        skb_mem_free(block, alloc_bytes);
        return nullptr;
    }

    unsigned char *buf = reinterpret_cast<unsigned char *>(block + 1);
    skb->head = buf;
    skb->data = buf;
    skb->tail = buf;
    skb->end = buf + size;
    skb->len = 0;
    skb->data_len = 0;
    skb->truesize = static_cast<unsigned int>(sizeof(struct sk_buff) + size);
    atomic_set(&skb->users, 1);
    skb->dataref = &block->dataref;
    skb->data_block = block;
    skb->cloned = false;
    skb->protocol = 0;
    skb->priority = 0;

    return skb;
}

struct sk_buff *dev_alloc_skb(unsigned int size) {
    struct sk_buff *skb = alloc_skb(size + NET_SKB_PAD, GFP_ATOMIC);
    if (!skb)
        return nullptr;
    skb_reserve(skb, NET_SKB_PAD);
    return skb;
}

void kfree_skb(struct sk_buff *skb) {
    if (!skb)
        return;
    if (atomic_dec_and_test(&skb->users)) {
        if (skb->dataref && atomic_dec_and_test(skb->dataref)) {
            if (skb->data_block) {
                size_t bsz = sizeof(struct skb_data_block) + skb->data_block->capacity;
                skb_mem_free(skb->data_block, bsz);
                skb->data_block = nullptr;
            }
        }
        skb_mem_free(skb, sizeof(struct sk_buff));
    }
}

void dev_kfree_skb_any(struct sk_buff *skb) {
    kfree_skb(skb);
}

unsigned int skb_headroom(const struct sk_buff *skb) {
    if (!skb || !skb->data || !skb->head || skb->data < skb->head)
        return 0;
    return static_cast<unsigned int>(skb->data - skb->head);
}

unsigned int skb_tailroom(const struct sk_buff *skb) {
    if (!skb || !skb->end || !skb->tail || skb->tail > skb->end)
        return 0;
    return static_cast<unsigned int>(skb->end - skb->tail);
}

void skb_reserve(struct sk_buff *skb, unsigned int len) {
    if (!skb)
        return;
    if (len > skb_tailroom(skb))
        return;
    skb->data += len;
    skb->tail += len;
}

unsigned char *skb_put(struct sk_buff *skb, unsigned int len) {
    if (!skb)
        return nullptr;
    if (len > skb_tailroom(skb))
        return nullptr;
    unsigned char *prev = skb->tail;
    skb->tail += len;
    skb->len += len;
    return prev;
}

void *skb_put_data(struct sk_buff *skb, const void *data, unsigned int len) {
    if (!skb || !data)
        return nullptr;
    unsigned char *dest = skb_put(skb, len);
    if (!dest)
        return nullptr;
    memcpy(dest, data, len);
    return dest;
}

unsigned char *skb_push(struct sk_buff *skb, unsigned int len) {
    if (!skb)
        return nullptr;
    if (len > skb_headroom(skb))
        return nullptr;
    skb->data -= len;
    skb->len += len;
    return skb->data;
}

unsigned char *skb_pull(struct sk_buff *skb, unsigned int len) {
    if (!skb)
        return nullptr;
    if (len > skb->len)
        return nullptr;
    skb->len -= len;
    skb->data += len;
    return skb->data;
}

struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t gfp) {
    (void)gfp;
    if (!skb)
        return nullptr;
    auto *clone = static_cast<struct sk_buff *>(skb_mem_calloc(sizeof(struct sk_buff)));
    if (!clone)
        return nullptr;
    *clone = *skb;
    atomic_set(&clone->users, 1);
    if (clone->dataref) {
        atomic_inc(clone->dataref);
    }
    clone->cloned = true;
    skb->cloned = true;
    return clone;
}

struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp) {
    if (!skb || !skb->head || !skb->end || skb->end < skb->head)
        return nullptr;

    unsigned int total_size = static_cast<unsigned int>(skb->end - skb->head);
    struct sk_buff *copy = alloc_skb(total_size, gfp);
    if (!copy)
        return nullptr;

    unsigned int hroom = skb_headroom(skb);
    skb_reserve(copy, hroom);

    if (skb->len > 0 && skb->data) {
        if (!skb_put_data(copy, skb->data, skb->len)) {
            kfree_skb(copy);
            return nullptr;
        }
    }

    copy->protocol = skb->protocol;
    copy->priority = skb->priority;
    copy->data_len = skb->data_len;
    copy->truesize = skb->truesize;
    memcpy(copy->cb, skb->cb, sizeof(copy->cb));
    return copy;
}
