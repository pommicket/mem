#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

static void read_mem(int fd, unsigned char *bitset, unsigned long bitset_off, unsigned long bytes, unsigned char addrval) {
#define BLOCKSZ 4096
	unsigned long bytes_left = bytes;
	unsigned char buf[BLOCKSZ];
	while (bytes_left > 0) {
		unsigned long bytes_to_read = bytes_left > BLOCKSZ ? BLOCKSZ : bytes_left;
		ssize_t bytes_read = read(fd, buf, bytes_to_read), b;
		if (bytes_read < 0) {
			// this part of block is unreadable; maybe the heap shrunk or something
			bytes_read = (ssize_t)bytes_to_read;
			memset(buf, !addrval, bytes_to_read); // make sure this gets eliminated
		}
		for (b = 0; b < bytes_read; ++b) {
			if (buf[b] != addrval) {
				unsigned long byte_idx = bitset_off + (bytes - bytes_left) + (unsigned)b;
				bitset[byte_idx >> 3] &= ~(1<<(byte_idx & 7));
			}
		}
		assert((unsigned long)bytes_read <= bytes_left);
		bytes_left -= (unsigned long)bytes_read;
	}
}

typedef struct {
	unsigned long lo, size;
} AddrRange;


int main(int argc, char **argv) {
	static AddrRange ranges[10000];
	int pid;
	unsigned short nranges = 0;
	char procmaps_name[32], procmem_name[32];
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
			unsigned long lo = 0, hi = 0;
			char perm[8];
			if (sscanf(line, "%lx-%lx %s", &lo, &hi, perm) == 3) {
				if (strncmp(perm, "rw-", 3) == 0) { // only look at read,write,no-execute memory
					AddrRange *range = &ranges[nranges++];
					range->lo = lo;
					range->size = hi - lo;
				}
			}
		}
		fclose(maps);
	}
	
	
	{
		int poke_only_mode = 0;
		unsigned long total_size = 0;
		unsigned long bitset_bytes;
		unsigned char *bitset;
		int max_mem_MB = getenv("MEM_LIMIT") ? atoi(getenv("MEM_LIMIT")) : 1024;
		{
			int r;
			for (r = 0; r < nranges; ++r) {
				total_size += ranges[r].size;
			}
		}
		
		bitset_bytes = (total_size + 7) / 8;
		
		printf("Memory size: %luMB\n", total_size>>20);
		
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
			unsigned long poke_addr = 0;
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
					unsigned long r, b, idx = 0;
					printf("They are:\n");
					for (r = 0; r < nranges; ++r) {
						for (b = 0; b < ranges[r].size; ++b, ++idx) {
							if (bitset[idx>>3] & (1<<(idx&7))) {
								printf(">> %lx\n", ranges[r].lo + b);
							#if 0
								{
								unsigned char B;
								int memfd = open(procmem_name, O_RDONLY);
								lseek(memfd, (off_t)(ranges[r].lo + b), SEEK_SET);
								read(memfd, &B, 1);
								printf(" - %u\n",B);
								close(memfd);
								}
							#endif
							}
						}
					}
				}
			}
			
			do {
				char line[8];
				if (!poke_only_mode) {
					printf("Enter a number from 0-255 to narrow down search,\n");
					printf("Or x to stop, and poke memory: ");
					fgets(line, sizeof line, stdin);
				}
				if (poke_only_mode || line[0] == 'x') {
					poke = 1;
					poke_only_mode = 1;
					printf("Poke address (default is all candidates): ");
					fgets(line, sizeof line, stdin);
					sscanf(line, "%lx", &poke_addr);
					printf("Set to (0-255): ");
					do {
						fgets(line, sizeof line, stdin);
						sscanf(line, "%u", &addrval);
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
					if (poke_addr) {
						lseek(memfd, (off_t)poke_addr, SEEK_SET);
						write(memfd, &byte, 1);
						printf("Writing %u to %lx.\n", byte, poke_addr);
					} else {
						int lines_printed = 0;
						unsigned long r, b, idx = 0;
						for (r = 0; r < nranges; ++r) {
							for (b = 0; b < ranges[r].size; ++b, ++idx) {
								if (bitset[idx>>3] & (1<<(idx&7))) {
									unsigned long addr = ranges[r].lo + b;
									lseek(memfd, (off_t)addr, SEEK_SET);
									write(memfd, &byte, 1);
									if (lines_printed++ < 10) {
										if (lines_printed == 10) {
											printf("...\n");
										} else {
											printf("Writing %u to %lx.\n", byte, addr);
										}
									}
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
					int r;
					unsigned long off = 0;
					for (r = 0; r < nranges; ++r) {
						lseek(memfd, (off_t)ranges[r].lo, SEEK_SET);
						read_mem(memfd, bitset, off, ranges[r].size, byte);
						off += ranges[r].size;
					}
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
		}
						
		free(bitset);
	}
	
	return 0;
}
