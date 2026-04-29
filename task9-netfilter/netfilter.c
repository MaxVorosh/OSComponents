#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/moduleparam.h>

MODULE_DESCRIPTION("TCP netfilter");
MODULE_AUTHOR("MaxVorosh");
MODULE_LICENSE("GPL");

static int filter_port = 0;
module_param(filter_port, int, 0644);
MODULE_PARM_DESC(filter_port, "port to filter");

static unsigned int my_nf_hookfn(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
      if (!skb) {
		return NF_ACCEPT;
	  }

	  struct iphdr *ip_header = ip_hdr(skb);
	  if (!ip_header || ip_header->protocol != IPPROTO_TCP) {
		return NF_ACCEPT;
	  }

	  struct  tcphdr *tcp_header = tcp_hdr(skb);
	  if (!tcp_header) {
		return NF_ACCEPT;
	  }

	  int from_addr = ip_header->saddr;
	  int to_addr = ip_header->daddr;
	  int from_port = ntohs(tcp_header->source);
	  int to_port = ntohs(tcp_header->dest);
	  if (tcp_header->syn && !tcp_header->ack) {
		pr_info("TCP connection initiated from %pI4:%u to %pI4:%u\n", &from_addr, from_port, &to_addr, to_port);
	  }
	  if (to_port == filter_port) {
		pr_info("BLOCKED TCP connection from %pI4:%u to %pI4:%u\n", &from_addr, from_port, &to_addr, to_port);
		return NF_DROP;
	  }
      return NF_ACCEPT;
}

static struct nf_hook_ops my_nfho = {
      .hook        = my_nf_hookfn,
      .hooknum     = NF_INET_LOCAL_OUT,
      .pf          = PF_INET,
      .priority    = NF_IP_PRI_FIRST
};

static int __init my_hook_init(void)
{
	return nf_register_net_hook(&init_net, &my_nfho);
}

static void __exit my_hook_exit(void)
{
	nf_unregister_net_hook(&init_net, &my_nfho);
}

module_init(my_hook_init);
module_exit(my_hook_exit);