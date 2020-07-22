#include <stdio.h>
#include <stdint.h>


int main(int argc, char** argv)
{
	uint64_t periphId = 0;
	uint32_t tab[1024] = {0}, i, base, cp;
	long baseL;
	uint8_t *buf = (uint8_t*)tab, *end = buf + 4096;
	
	if (argc != 2) {
		fprintf(stderr, "USAGE: %s <BASE> < romtab\n", argv[0]);
		return -2;
	}
	
	sscanf(argv[1], "%li", &baseL);
	base = baseL;
	
	for (buf = (uint8_t*)tab, end = buf + 4096; buf != end; buf++) {
		
		int c = getchar();
		
		if (c == EOF) {
			fprintf(stderr, "underrun!\n");
			return -1;
		}
		
		*buf = c;
	}
	
	if (getchar() != EOF)
	 {
		fprintf(stderr, "overrun!\n");
		return -1;
	}
	
	
	fprintf(stderr, "ROM TABLE at 0x%08X:\n", base);
	if (tab[0xff0 / 4] != 0x0d || (tab[0xff4 / 4] &~ 0xf0) != 0 || tab[0xff8 / 4] != 0x05 || tab[0xffc / 4] != 0xb1) {
		fprintf(stderr, "CID values invalid\n");
		return -1;
	}
	fprintf(stderr, " Component Type: ");
	switch ((tab[0xff4 / 4] >> 4) & 0x0f) {
		case 0:
			fprintf(stderr, "Generic Verification Component\n");
			break;
		case 1:
			fprintf(stderr, "ROM Table\n");
			break;
		case 9:
			fprintf(stderr, "Debug Component\n");
			break;
		case 11:
			fprintf(stderr, "Peripheral Test Block\n");
			break;
		case 13:
			fprintf(stderr, "DESS Component\n");
			break;
		case 14:
			fprintf(stderr, "Generic IP Component\n");
			break;
		case 15:
			fprintf(stderr, "PrimeCell Peripheral\n");
			break;
		default:
			fprintf(stderr, "Unknown type (%d)\n", (tab[0xff4 / 4] >> 4) & 0x0f);
			break;
		
	}
	
	for (i = 0; i < 8; i++) {
		if (tab[0xfd0 / 4 + i] >> 8) {
			fprintf(stderr, "PIDR regs invalid\n");
			return  -1;
		}
	}
	
	periphId += ((uint64_t)tab[0xfd0 / 4]) << 32;
	periphId += ((uint64_t)tab[0xfd4 / 4]) << 40;
	periphId += ((uint64_t)tab[0xfd8 / 4]) << 48;
	periphId += ((uint64_t)tab[0xfdc / 4]) << 56;
	periphId += ((uint64_t)tab[0xfe0 / 4]) <<  0;
	periphId += ((uint64_t)tab[0xfe4 / 4]) <<  8;
	periphId += ((uint64_t)tab[0xfe8 / 4]) << 16;
	periphId += ((uint64_t)tab[0xfec / 4]) << 24;
	
	fprintf(stderr, " PID: 0x%016llX\n", (unsigned long long)periphId);
	
	if (periphId >> 40) {
		fprintf(stderr, "PID reserved bit set\n");
		return -1;
	}
	
	fprintf(stderr, " PERIPH SIZE: 0x%08X\n", 4096 << ((periphId >> 36) & 0x0f));
	fprintf(stderr, " ID nfo:\n");
	fprintf(stderr, "   CONT: %d\n", (unsigned)((periphId >> 32) & 0x0f));
	fprintf(stderr, "   REVAND: %d\n", (unsigned)((periphId >> 28) & 0x0f));
	fprintf(stderr, "   CMOD: %d\n", (unsigned)((periphId >> 24) & 0x0f));
	fprintf(stderr, "   REVISION: %d\n", (unsigned)((periphId >> 20) & 0x0f));
	fprintf(stderr, "   JEDEC: %d\n", (unsigned)((periphId >> 19) & 0x01));
	fprintf(stderr, "   ID: 0x%02X\n", (unsigned)((periphId >> 12) & 0x7f));
	fprintf(stderr, "   PART: 0x%03X\n", (unsigned)(periphId & 0xfff));
	if (tab[0xfcc / 4] &~ 1)
		fprintf(stderr, " MEMTYPE reg is weird: 0x%08X\n", tab[0xfcc / 4]);
	fprintf(stderr, " MEMTYPE.SYSTEM = %d\n", tab[0xfcc / 4] & 1);
	
	
	for (cp = 0; cp < 0xfcc; cp+= 4)
	{
		uint32_t v = tab[cp / 4];
		
		if (!(v & 1))
			break;
		
		if (!(v & 2)) {
			fprintf(stderr, "NOT 32-bit table!\n");
			continue;
		}
		
		fprintf(stderr, " -> TAB @ 0x%08X", (uint32_t)((v & 0xfffff000) + base));
		if (v & 4) {
			fprintf(stderr, " (power domain %d)", (v >> 4) & 0x1f);
		}
		fprintf(stderr, "\n");
	}
	
	return 0;
}
