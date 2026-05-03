// #include <linux/bpf.h>
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "map_info.h"

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
        __type(value, __u32);
} max_packets SEC(".maps");

SEC("xdp/ingress")
int bpf_flood(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end) {
        return XDP_ABORTED;
    }

    __u16 h_proto = eth->h_proto;
    if (h_proto != bpf_htons(ETH_P_IP)) {
        return XDP_PASS;
    }

    struct iphdr *iph = data + sizeof(*eth);
    if ((void *)(iph + 1) > data_end) {
        return XDP_ABORTED;
    }

    if (iph->protocol != bpf_htons(IPPROTO_TCP)) {
        return XDP_PASS;
    }

    struct tcphdr *tcph = data + sizeof(*eth) + sizeof(*iph);
    if ((void *)(tcph + 1) > data_end) {
        return XDP_ABORTED;
    }

    __u32 to_addr = ip_header->daddr;
    
    if (!tcp_header->syn || tcp_header->ack) {
		return XDP_PASS;
	}

    int param_index = 0;
    void* max_packets_value = bpf_map_lookup_elem(&max_packets, &param_index);
    if (!max_packets_value) {
        return XDP_PASS;
    }

    __u32* addr_in_map = bpf_map_lookup_elem(&packet_stats, &to_addr);
    if (addr_in_map) {
        __u32 packets = __sync_fetch_and_add(&addr_in_map->packets, 1);
        if (packets > *max_packets_value) {
            __sync_fetch_and_sub(&addr_in_map->packets, 1);
            bpf_printk("Drop packet from addr %pI4\n", to_addr);
            return XDP_DROP;
        }
        bpf_printk("Pass packet from addr %pI4\n", to_addr);
        return XDP_PASS;
    }
    else if (*max_packets_value > 0) {
        int new_value = 1;
        bpf_map_update_elem(&packet_stats, &to_addr, &new_value, 0);
        return XDP_PASS;
    }
    return XDP_DROP;
}
