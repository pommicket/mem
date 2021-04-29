#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

static void read_mem(int fd, unsigned char *bitset, unsigned long bitset_off, unsigned long bytes, unsigned char addrval) {
#define BLOCKSZ 4096
	unsigned long bytes_left = bytes;
	unsigned char buf[BLOCKSZ];
	while (bytes_left > 0) {
		unsigned long bytes_to_read = bytes_left > BLOCKSZ ? BLOCKSZ : bytes_left;
		ssize_t bytes_read = read(fd, buf, bytes_to_read), b;
		if (bytes_read < 0) {
			// this part of block is unreadable; maybe the heap shrunk or something
			bytes_left -= bytes_to_read;
			bytes_read = (ssize_t)bytes_to_read;
			memset(buf, !addrval, bytes_to_read); // make sure this gets eliminated
		}
		for (b = 0; b < bytes_read; ++b) {
			if (buf[b] != addrval) {
				unsigned long byte_idx = bitset_off + (bytes - bytes_left) + (unsigned)b;
				bitset[byte_idx >> 3] &= ~(1<<(byte_idx & 7));
			}
		}
		bytes_left -= (unsigned long)bytes_read;
	}
}

int main(int argc, char **argv) {
	int pid;
	char procmaps_name[32], procmem_name[32];
	unsigned long stack_lo = 0, stack_hi = 0, heap_lo = 0, heap_hi = 0;
	if (argc < 2 || atoi(argv[1]) == 0) {
		fprintf(stderr, "Usage: %s <pid>\n", argv[0] ? argv[0] : "mem");
		return EXIT_FAILURE;
	}
	setbuf(stdout, NULL);
	
	pid = atoi(argv[1]);
	
	sprintf(procmaps_name, "/proc/%d/maps", pid);
	sprintf(procmem_name, "/proc/%d/mem", pid);
	{
		char line[256];
		FILE *maps = fopen(procmaps_name, "rb");
		if (!maps) {
			perror(procmaps_name);
			return EXIT_FAILURE;
		}
		while (fgets(line, sizeof line, maps)) {
			size_t len = strlen(line);
			if (len < 8) continue;
			if (strcmp(&line[len-8], "[stack]\n") == 0) {
				sscanf(line, "%lx-%lx", &stack_lo, &stack_hi);
			}
			if (strcmp(&line[len-7], "[heap]\n") == 0) {
				sscanf(line, "%lx-%lx", &heap_lo, &heap_hi);
			}
		}
		fclose(maps);
	}
	
	{
		unsigned long stack_size = stack_hi - stack_lo, heap_size = heap_hi - heap_lo;
		unsigned long total_size = stack_size + heap_size;
		unsigned long bitset_bytes = total_size / 8;
		unsigned char *bitset;
		int max_mem_MB = getenv("MEM_LIMIT") ? atoi(getenv("MEM_LIMIT")) : 1024;
		printf("Stack size: %luMB\n", stack_size>>20);
		printf("Heap size:  %luMB\n", heap_size>>20);
		
		if ((bitset_bytes >> 20) > (unsigned long)max_mem_MB) {
			fprintf(stderr, "Need %luMB, but refusing to use more than the limit of %d.\n",
				bitset_bytes >> 20, max_mem_MB);
			fprintf(stderr, "You can set the memory limit in MB by setting the environment variable MEM_LIMIT.\n");
			return EXIT_FAILURE;	
		}
		
		bitset = malloc(bitset_bytes);
		memset(bitset, 0xff, bitset_bytes);

		while (1) {
			int poke = 0;
			unsigned addrval = 256;
			unsigned char byte;
			int err;
			{
				unsigned long i;
				unsigned long pop = 0;
				for (i = 0; i < bitset_bytes; ++i) {
					pop += (unsigned)__builtin_popcount(bitset[i]);
				}
				printf("%lu candidates\n", pop);
				if (pop == 0) return 0;
				if (pop < 10) {
					printf("They are:\n");
					for (i = 0; i < bitset_bytes; ++i) {
						if (bitset[i]) {
							unsigned bit;
							for (bit = 0; bit < 8; ++bit) {
								if (bitset[i] & (1<<bit)) {
									unsigned long idx = 8 * i + bit;
									unsigned long addr = 0;
									if (idx >= stack_size)
										addr = idx - stack_size + heap_lo;
									else
										addr = idx + stack_lo;
									printf("%lx %lx %lx\n",heap_lo,stack_lo,addr);
								}
							}
						}
					}
				}
			}
			
			do {
				char line[8];
				printf("Enter a number from 0-255 to narrow down search,\n");
				printf("Or x to stop, and poke memory: ");
				fgets(line, sizeof line, stdin);
				if (line[0] == 'x') {
					poke = 1;
					printf("Set all matches to (0-255): ");
					do {
						scanf("%u", &addrval);
					} while (addrval > 255);
				} else {
					sscanf(line, "%u", &addrval);
				}
			} while (addrval > 255);
			
			byte = (unsigned char)addrval;
			
			err = kill(pid, SIGSTOP);
			if (err == -1) {
				switch (errno) {
				case EPERM:
					fprintf(stderr, "Couldn't send stop signal to process (permission denied). Try running as root?\n");
					break;
				case ESRCH:
					fprintf(stderr, "Process %d no longer exists.\n", pid);
					break;
				default:
					perror("Stop process");
					break;
				}
				return EXIT_FAILURE;
			}
			if (poke) {
				int memfd = open(procmem_name, O_WRONLY);
				if (memfd != -1) {
					unsigned long i;
					for (i = 0; i < bitset_bytes; ++i) {
						if (bitset[i]) {
							unsigned bit;
							for (bit = 0; bit < 8; ++bit) {
								if (bitset[i] & (1<<bit)) {
									unsigned long idx = 8 * i + bit;
									unsigned long addr = 0;
									if (idx >= stack_size)
										addr = idx - stack_size + heap_lo;
									else
										addr = idx + stack_lo;
									lseek(memfd, (off_t)addr, SEEK_SET);
									write(memfd, &byte, 1);
								}
							}
						}
					}
				} else {
					perror(procmem_name);
				}
			} else {
				int memfd = open(procmem_name, O_RDONLY);
				if (memfd != -1) {
					lseek(memfd, (off_t)stack_lo, SEEK_SET);
					read_mem(memfd, bitset, 0, stack_size, byte);
					lseek(memfd, (off_t)heap_lo, SEEK_SET);
					read_mem(memfd, bitset, stack_size, heap_size, byte);
					
					close(memfd);
				} else {
					perror(procmem_name);
				}
			}
			
			err = kill(pid, SIGCONT);
			if (err == -1) {
				if (errno == ESRCH) {
					fprintf(stderr, "Process %d no longer exists.\n", pid);
				} else {
					perror("Continue process");
				}
				return EXIT_FAILURE;
			}
			if (poke)
				return 0;
		}
						
		free(bitset);
		
	}
	
	return 0;
}
