#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
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
    printf("Usage: ./flood_bpf_loader <flood_threshold> <interface=lo>");
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) {
        help();
        return 1;
    }

    int max_packets = atoi(argv[1]);

    struct bpf_object *obj;
    struct bpf_program *prog;

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

    char* ifname = argc == 2 ? "lo" : argv[2];
    int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "failed to get interface %s\n", ifname);
        return 1;
    }

    prog = bpf_object__find_program_by_name(obj, "bpf_flood");
    int err = bpf_xdp_attach(ifindex, bpf_program__fd(prog), 0, NULL);
    if (err)
    {
        fprintf(stderr, "attach failed\n");
        return 1;
    }

   int  max_packets_fd = bpf_object__find_map_fd_by_name(obj, "max_packets");
    int index = 0;
    bpf_map_update_elem(max_packets_fd, &index, &max_packets, BPF_ANY);

    printf("Running...\n");

    while (!exiting) {
        usleep(100);
    }

    bpf_xdp_detach(ifindex, 0, NULL);
    bpf_object__close(obj);

    return 0;
}
