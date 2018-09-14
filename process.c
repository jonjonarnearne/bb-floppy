#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>

static void find_sync(uint16_t *data, int offset, long size)
{
	uint32_t dw = 0;
	int i = offset-1;
	int end = i+24;
	if (i < 0 || (i + 16) >= size) {
		fprintf(stderr, "Can't print dw, outiler error!\n");
		return;
	}

	printf("[%4d %4d %4d %4d %4d %4d %4d %4d]\n",
		       data[i], data[i+1], data[i+2], data[i+3],
		       data[i+4], data[i+5], data[i+6], data[i+7]); 

	while(i < size) {
		if (data[i] > 720) {
			dw <<= 1;
			if (dw == 0x89448944 || dw == 0x44894489) {
				break;
			}
		}
		if (data[i] > 540) {
			dw <<= 1;
			if (dw == 0x89448944 || dw == 0x44894489) {
				break;
			}
		}
		dw <<= 1;
		if (dw == 0x89448944 || dw == 0x44894489) {
			break;
		}
		dw <<= 1;
		dw |= 1;
		if (dw == 0x89448944 || dw == 0x44894489) {
			break;
		}
		i++;
	}

	if (dw == 0x44894489)
		printf("Got dw: %08x after %d samples\n", dw, (i - offset));
	else
		printf("Now sync found!\n");

}

static void guess_sync(uint16_t *data, int offset, long size)
{
	uint32_t dw = 0;
	int i = offset;
	int end = i+10;
	if (i < 0 || end >= size) {
		fprintf(stderr, "Can't print dw, outiler error!\n");
		return;
	}

	printf("[%4d %4d %4d %4d %4d %4d %4d %4d]\n",
		       data[i], data[i+1], data[i+2], data[i+3],
		       data[i+4], data[i+5], data[i+6], data[i+7]); 

	while(i < end) {
		if (data[i] > 720) {
			dw <<= 1;
		}
		if (data[i] > 540) {
			dw <<= 1;
		}
		dw <<= 1;
		dw <<= 1;
		dw |= 1;
		i++;
	}

	if (dw == 0x44894489)
		printf("Got dw: %08x after %d samples\n", dw, (i - offset));
	else
		printf("found: %08x\n", dw);

}

int main(int argc, char ** argv)
{
	FILE *in;
	int i;
	long size;
	uint16_t *data;

	if (argc != 2) {
		fprintf(stderr, "You must specify exactly one filename\n");
		return -1;
	}

	in = fopen(argv[1], "rb");
	fseek(in, 0L, SEEK_END);
	size = ftell(in);
	data = mmap(0, size, PROT_READ, MAP_PRIVATE, fileno(in), 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "Couldn't open file!\n");
		return -1;
	}

	for (i = 1; i < size/2; i++) {
		if (data[i] < 420 && data[i] > 380) continue;
		printf("Found mark @ sample %d\n", i);
		guess_sync(data, i, size/2);
		break;
	}

	munmap(data, size);
	fclose(in);
	
	return 0;
}
