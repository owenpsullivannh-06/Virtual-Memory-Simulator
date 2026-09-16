#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

typedef struct validopcodes {
    char *opcode;
    unsigned int format;
    unsigned int value;
} vo;

int main(int argc, char *argv[]) {
    //OPCODE ARRAY
    vo validopcodes[] = {{"halt", 1, 0x00}, {"ret", 1, 0x10 }, 
            {"jmp", 2, 0x14}, {"call",2,0x0F}, 
            {"push",3, 0x18}, {"pop", 3, 0x19}, {"getpid", 3, 0x16},{"getpn",3,0x17},
            {"ldimm",4,0x03},
            {"load",5, 0x01},{"store",5, 0x02},{"ldaddr",5, 0x04},
            {"addf", 6, 0x07},{"subf",6, 0x08},{"divf",6, 0x09},{"mulf",6, 0x0A},
            {"addi",6,0x0B},{"subi",6, 0x0C},{"divi",6, 0x0D},{"muli",6, 0x0E},
            {"ldind",7,0x05},{"stind",7,0x06},
            {"blt",8, 0x11},{"bgt",8, 0x12},{"beq",8, 0x13},{"cmpxchg",8, 0x15}, {"ldquad",8,0x1A}};
    if (argc != 2) {
        return 1;
    }
    FILE *readfrom = fopen(argv[1], "rb");
    if (readfrom == NULL) {
        printf("Opening file failed.\n");
        return 1;
    }
    //BEGIN READ HEADER
    unsigned int insymsize;
    unsigned int outsymsize;
    unsigned int objsize;
    
    fread(&insymsize, sizeof(unsigned int),1,readfrom);
    fread(&outsymsize, sizeof(unsigned int),1,readfrom);
    fread(&objsize, sizeof(unsigned int),1,readfrom);

    unsigned int insymcnt = insymsize/5;
    unsigned int outsymcnt = outsymsize/5;


    printf("Insymbol section size: %d words (%08x in hex)\n", insymsize, insymsize);
    printf("Outsymbol section size: %d words (%08x in hex)\n", outsymsize, outsymsize);
    printf("Object code section size: %d words (%08x in hex)\n", objsize, objsize);
    //END READ HEADER

    //BEGIN READ INSYMBOL
    printf("Insymbol Section (%d Entries)\n",insymcnt);
    for (int i = 0; i < insymcnt; i++) {
        char name[17];
        memset(name, 0, 17);
        fread(name, sizeof(char), 16, readfrom);
        unsigned int address;
        fread(&address, sizeof(unsigned int),1,readfrom);
        printf("%s %d\n", name, address);
    }
    //END READ INSYMBOL
    
    //BEGIN READ OUTSYMBOL
    printf("Outsymbol Section (%d Entries)\n", outsymcnt);
    for (int i = 0; i < outsymcnt; i++) {
        char name[17];
        memset(name, 0, 17);
        fread(name, sizeof(char), 16, readfrom);
        unsigned int address;
        fread(&address, sizeof(unsigned int),1,readfrom);
        printf("%s %d\n", name, address);
    }
    //END READ OUTSYMBOL

    //BEGIN READ OBJ CODE
    printf("Object Code (%d words)\n",objsize);
    for (int i = 0; i < objsize; i++) {
        unsigned int obc;
        fread(&obc, sizeof(unsigned int), 1, readfrom);
        unsigned int opval = obc & 0xFF; //Opcode value
        unsigned int reg1 = (obc >> 8) & 0xF; //register 1
        unsigned int reg2 = (obc >> 12)  & 0xF; //register 2
        int addr16 = (short)(obc >> 16); // 16 bit sign
        int addr20 = ((int)(obc & 0xFFFFF000)) >> 12; // 20 bit sign
        for (int j = 0; j < (sizeof(validopcodes)/sizeof(validopcodes[0])); j++) { 
            vo curropcode = validopcodes[j];
            if (opval == curropcode.value) { //Finding the correct value:
                if (curropcode.format == 1) {printf("%s\n",curropcode.opcode);}
                if (curropcode.format == 2) {
                    //Add one to these because VM520 is relative to the next instruction.
                    int resolvedaddr = i + addr20 + 1;
                    printf("%s %d\n",curropcode.opcode, resolvedaddr);}
                if (curropcode.format == 3) {printf("%s r%d\n",curropcode.opcode, reg1);}
                if (curropcode.format == 4) {printf("%s r%d, %d\n",curropcode.opcode, reg1, addr20);}
                if (curropcode.format == 5) {
                    int resolvedaddr = i + addr20 + 1;
                    printf("%s r%d, %d\n",curropcode.opcode, reg1, resolvedaddr);}
                if (curropcode.format == 6) {printf("%s r%d, r%d\n",curropcode.opcode, reg1, reg2);}
                if (curropcode.format == 7) {printf("%s r%d, %d(r%d)\n",curropcode.opcode, reg1, addr16, reg2);}
                if (curropcode.format == 8) {
                    int resolvedaddr = i + addr16 + 1;
                    printf("%s r%d, r%d, %d\n",curropcode.opcode, reg1,reg2, resolvedaddr);}
                if (curropcode.format == 9) {printf("%d\n",obc);}
            }
        }

    }
    //END READ OBJ CODE
    fclose(readfrom);
}