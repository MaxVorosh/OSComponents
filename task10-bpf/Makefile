all: flood_bpf_loader test_sender

flood_bpf_loader: flood.bpf.o flood_bpf_loader.c
	gcc -O2 -Wall -Wextra flood_bpf_loader.c -o flood_bpf_loader -lbpf

flood.bpf.o: flood.bpf.c vmlinux.h
	clang -O2 -g -Wall -Wextra -target bpf -c flood.bpf.c -o flood.bpf.o

clean:
	rm *.o flood_bpf_loader vmlinux.h

vmlinux.h: /sys/kernel/btf/vmlinux
	bpftool btf dump file /sys/kernel/btf/vmlinux format c >vmlinux.h

test_sender: test_sender.c
	gcc -O2 -Wall -Wextra test_sender.c -o test_sender

run: flood_bpf_loader
	sudo ./flood_bpf_loader 10
