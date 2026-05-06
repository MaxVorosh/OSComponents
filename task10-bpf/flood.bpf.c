// #include <linux/bpf.h>
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "GPL";

#define ETH_P_IP 0x0800

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
        bpf_printk("Aborted no eth byte size");
        return XDP_ABORTED;
    }

    __u16 h_proto = eth->h_proto;
    if (h_proto != bpf_htons(ETH_P_IP)) {
        bpf_printk("Pass not ip");
        return XDP_PASS;
    }

    struct iphdr *iph = data + sizeof(*eth);
    if ((void *)(iph + 1) > data_end) {
        bpf_printk("Aborted not ip byte size");
        return XDP_ABORTED;
    }
    __u32 ip_len = iph->ihl * 4;
    if (ip_len < sizeof(*iph)) {
        bpf_printk("Aborted incorrect ip length");
        return XDP_ABORTED;
    }
    if ((void *)(iph) + ip_len > data_end) {
        bpf_printk("Aborted not ip length size");
        return XDP_ABORTED;
    }

    if (iph->protocol != IPPROTO_TCP) {
        bpf_printk("Pass not tcp, got %d", iph->protocol);
        return XDP_PASS;
    }

    struct tcphdr *tcph = data + sizeof(*eth) + ip_len;
    if ((void *)(tcph + 1) > data_end) {
        bpf_printk("Aborted not tcp byte size");
        return XDP_ABORTED;
    }

    __u32 to_addr = iph->daddr;
    
    if (!tcph->syn && !tcph->ack) {
        bpf_printk("Pass not syn, nor ack");
		return XDP_PASS;
	}

    int param_index = 0;
    __u32* max_packets_value = bpf_map_lookup_elem(&max_packets, &param_index);
    if (!max_packets_value) {
        bpf_printk("Cant get flood threshold from map -- pass");
        return XDP_PASS;
    }

    __u32* addr_in_map = bpf_map_lookup_elem(&packet_stats, &to_addr);
    if (addr_in_map) {
        if (tcph->ack) {
            if (*addr_in_map > 0) {
                *addr_in_map -= 1;
            }
            bpf_printk("Pass ack packet from addr %pI4", to_addr);
            return XDP_PASS;
        }
        *addr_in_map += 1;
        if (*addr_in_map > *max_packets_value) {
            bpf_printk("Drop syn packet from addr %pI4", to_addr);
            return XDP_DROP;
        }
        bpf_printk("Pass syn packet from addr %pI4", to_addr);
        return XDP_PASS;
    }
    else if (*max_packets_value > 0) {
        if (tcph->ack) {
            bpf_printk("Pass ack from unknown addr %pI4", to_addr);
            return XDP_PASS;
        }
        int new_value = 1;
        bpf_map_update_elem(&packet_stats, &to_addr, &new_value, BPF_ANY);
        bpf_printk("Pass new addr in map: %pI4", to_addr);
        return XDP_PASS;
    }
    bpf_printk("Drop request for new addr, since threshold is zero");
    return XDP_DROP;
}
