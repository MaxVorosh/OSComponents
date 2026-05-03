#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <string.h>
#include <net/if.h>
#include <ifaddrs.h>

static volatile sig_atomic_t exiting = 0;

static void handle_sigint(int sig)
{
    (void)sig;
    exiting = 1;
}

void help() {
    printf("Wrong number of arguments\n");
    printf("Usage: ./flood_bpf_loader <flood_threshold>");
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        help();
        return 1;
    }

    int max_packets = atoi(argv[1]);

    struct bpf_object *obj;
    struct bpf_program *prog;
    struct bpf_link *link;
    struct bpf_map *max_packets_obj;

    struct sigaction sa =
    {
        .sa_handler = handle_sigint
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) ||
        sigaction(SIGTERM, &sa, NULL))
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    obj = bpf_object__open_file("flood.bpf.o", NULL);
    if (!obj || bpf_object__load(obj))
    {
        fprintf(stderr, "failed to load bpf object\n");
        return 1;
    }

    struct ifaddrs* ifaddr;
    getifaddrs(&ifaddr);
    int ifindex = if_nametoindex(ifaddr->ifa_name);
    if (ifindex == 0) {
        fprintf(stderr, "failed to get interface %s\n", ifaddr->ifa_name);
        return 1;
    }

    prog = bpf_object__find_program_by_name(obj, "bpf_flood");
    link = bpf_program__attach_xdp(prog, ifindex);
    if (!link)
    {
        fprintf(stderr, "attach failed\n");
        return 1;
    }

    max_packets_obj = bpf_object__find_map_by_name(obj, "max_packets");
    int index = 0;
    bpf_map__update_elem(max_packets_obj, &index, sizeof(__u32), &max_packets, sizeof(__u32), 0);

    printf("Running...\n");

    while (!exiting) {}

    bpf_link__destroy(link);
    bpf_object__close(obj);

    return 0;
}
