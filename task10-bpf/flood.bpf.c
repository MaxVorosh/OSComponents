// #include <linux/bpf.h>
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "GPL";

struct {
        __uint(type, BPF_MAP_TYPE_HASH);
        __uint(max_entries, 128);
        __type(key, __u32);
        __type(value, __u32);
} packet_stats SEC(".maps");

struct {
        __uint(type, BPF_MAP_TYPE_ARRAY);
        __uint(max_entries, 1);
        __type(key, __u32);
        __type(value, __u32);
} max_packets SEC(".maps");

SEC("xdp")
int bpf_flood(struct xdp_md *ctx)
{
    bpf_printk("Got request");
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end) {
        bpf_printk("No eth byte size");
        return XDP_ABORTED;
    }

    __u16 h_proto = eth->h_proto;
    if (h_proto != bpf_htons(0x0800)) {
        bpf_printk("Not ip");
        return XDP_PASS;
    }

    struct iphdr *iph = data + sizeof(*eth);
    if ((void *)(iph + 1) > data_end) {
        bpf_printk("Not ip byte size");
        return XDP_ABORTED;
    }

    if (iph->protocol != IPPROTO_TCP) {
        bpf_printk("Not tcp");
        return XDP_PASS;
    }

    struct tcphdr *tcph = data + sizeof(*eth) + sizeof(*iph);
    if ((void *)(tcph + 1) > data_end) {
        bpf_printk("Not tcp byte size");
        return XDP_ABORTED;
    }

    __u32 to_addr = iph->daddr;
    
    if (!tcph->syn || tcph->ack) {
        bpf_printk("Not syn");
		return XDP_PASS;
	}

    int param_index = 0;
    __u32* max_packets_value = bpf_map_lookup_elem(&max_packets, &param_index);
    if (!max_packets_value) {
        bpf_printk("Cant get flood threshold from map");
        return XDP_PASS;
    }

    __u32* addr_in_map = bpf_map_lookup_elem(&packet_stats, &to_addr);
    if (addr_in_map) {
        *addr_in_map += 1;
        if (*addr_in_map > *max_packets_value) {
            *addr_in_map -= 1;
            bpf_printk("Drop packet from addr %pI4\n", to_addr);
            return XDP_DROP;
        }
        bpf_printk("Pass packet from addr %pI4\n", to_addr);
        return XDP_PASS;
    }
    else if (*max_packets_value > 0) {
        int new_value = 1;
        bpf_map_update_elem(&packet_stats, &to_addr, &new_value, BPF_ANY);
        bpf_printk("New addr in map");
        return XDP_PASS;
    }
    bpf_printk("Drop request for new addr, since threshold is zero");
    return XDP_DROP;
}
