#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char ** argv)
{
	FILE *fp;
	char *filename = NULL;
	int i, opt, size = 10, bytes = 0;
	uint8_t *byte, *file_arr, mask = 0, bits = 0, copy = 0;
	while((opt = getopt(argc, argv, "-b:B:")) != -1) {
		switch(opt) {
		case 1:
			filename = optarg;
			break;
		case 'b':
			bits = strtol(optarg, NULL, 0);
			break;
		case 'B':
			bytes = strtol(optarg, NULL, 0);
			break;
		default:
			printf("Opt: %c not supported!\n", opt);
		}
	}

	if (!filename) {
		fprintf(stderr, "%s: fatal -- you must supply a filename\n",
								argv[0]);
		return 1;
	}
	fprintf(stderr, "Read file: %s, shift %d bits, and %d bytes\n",
						filename, bits, bytes);
	fp = fopen(filename, "r+");
	if (!fp) {
		perror(NULL);
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fprintf(stderr, "Size: %d\n", size);

	rewind(fp);
	file_arr = malloc(size);
	if (!file_arr) {
		fprintf(stderr, "%s: fatal -- Couldn't allocate memory\n",
								argv[0]);
		return 0;

	}

	fread(file_arr, 1, size, fp);
	fclose(fp);
	
	if (bits) {
		mask = 0;
		for (i = 0; i < bits; i++) {
			mask >>= 1;
			mask |= 1 << 7;
		}
		fprintf(stderr, "Mask: %02x\n", mask);
		byte = file_arr;
		copy = (byte[0] & mask) >> bits;
		for (i = 0; i < size; i++) {
			byte[i] <<= bits;
			if (i + 1 < size)
				byte[i] |= (byte[i+1] & mask) >> bits;
		}
		byte[--i] |= copy;
	}
	
	// Print from MARK -> END
	byte = file_arr + bytes;
	fwrite(byte, 1, size - bytes, stdout);
	// Print from START -> MARK
	fwrite(file_arr, 1, bytes, stdout);
	
	// Set MARK at 0x3200, and print until MARK,
	// PAD with 0x00
	//copy = 0x00;
	//for(i = 0; i < 0x3200 - bytes; i++)
	//	fwrite(&copy, 1, 1, stdout);
	//fwrite(file_arr, 1, bytes, stdout);

	free(file_arr);
	return 0;
}
