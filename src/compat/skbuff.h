//
// skbuff.h — Freestanding sk_buff socket buffer emulation
// For rtw89 driver on macOS / freestanding C++17 environments
//
#pragma once

#include "linux_shim.h"

struct skb_data_block;

struct sk_buff {
    unsigned char *data;
    unsigned char *head;
    unsigned char *tail;
    unsigned char *end;
    unsigned int len;
    unsigned int data_len;
    unsigned int truesize;
    alignas(8) char cb[48];
    __be16 protocol;
    u32 priority;

    // Internal management
    atomic_t users;
    atomic_t *dataref;
    struct skb_data_block *data_block;
    bool cloned;
};

#ifdef __cplusplus
extern "C" {
#endif

// Allocation and freeing
struct sk_buff *alloc_skb(unsigned int size, gfp_t gfp);
struct sk_buff *dev_alloc_skb(unsigned int size);
void kfree_skb(struct sk_buff *skb);
void dev_kfree_skb_any(struct sk_buff *skb);

// Manipulation functions
unsigned char *skb_put(struct sk_buff *skb, unsigned int len);
void *skb_put_data(struct sk_buff *skb, const void *data, unsigned int len);
unsigned char *skb_push(struct sk_buff *skb, unsigned int len);
unsigned char *skb_pull(struct sk_buff *skb, unsigned int len);
void skb_reserve(struct sk_buff *skb, unsigned int len);
unsigned int skb_headroom(const struct sk_buff *skb);
unsigned int skb_tailroom(const struct sk_buff *skb);
struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t gfp);
struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp);

#ifdef __cplusplus
}
#endif
