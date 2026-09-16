#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "defs.h"
#include "symtab.h"

#define ERROR_PROGRAM_SIZE "Program consumes more than 2^20 words"
#define ERROR_LABEL_DEFINED "Label %s already defined"
#define ERROR_OPCODE_UNKNOWN "Unknown Opcode %s"
#define ERROR_OPERAND_FORMAT "Opcode does not match the given operands"
#define ERROR_CONSTANT_ZERO "Constant must be greater than zero"
#define ERROR_CONSTANT_INVALID "Constant %d will not fit into 20 bits"
#define ERROR_OFFSET_INVALID "Offset %d will not fit into 16 bits"
#define ERROR_MULTIPLE_EXPORT "Symbol %s exported more than once"
#define ERROR_MULTIPLE_IMPORT "Symbol %s imported more than once"
#define ERROR_LABEL_REFERENCE_NOT_FOUND "Label %s is referenced but not defined or imported"
#define ERROR_SYMBOL_IMPORT_EXPORT "Symbol %s is both imported and exported"
#define ERROR_SYMBOL_IMPORT_DEFINED "Symbol %s is both imported and defined"
#define ERROR_SYMBOL_IMPORT_NO_REFERENCE "Symbol %s is imported but not referenced"
#define ERROR_SYMBOL_EXPORT_NO_DEFINITION "Symbol %s is exported but not defined"
#define ERROR_SYMBOL_IMPORT_SIZE "Symbol %s is imported and longer than 16 characters"
#define ERROR_SYMBOL_EXPORT_SIZE "Symbol %s is exported and longer than 16 characters"
#define ERROR_LABEL_SIZE16 "Reference to label %s at address %d won't fit in 16 bits"
#define ERROR_LABEL_SIZE20 "Reference to label %s at address %d won't fit in 20 bits"
FILE *outf = NULL;

struct SymbolTable *s;
unsigned int counter = 0;
int passcounter = 1;
int errorcount = 0;
typedef struct symbolinfo {
    unsigned int refaddress[1024];
    unsigned int address;
    unsigned int reference;
    unsigned int imported;
    unsigned int exported;
    unsigned int defined;
    unsigned int format2;
    unsigned int format5;
    unsigned int format8;
    unsigned int referencecount;
} si;

typedef struct validopcodes {
    char *opcode;
    unsigned int format;
    unsigned int value;
} vo;

void initAssemble() {
    s = symtabCreate(100);
    counter = 0;
    passcounter = 1;
}

void assemble(char *lookup, INSTR i) {
    unsigned int format = i.format;
    unsigned int encoded = 0;
    vo validopcodes[] = {{"halt", 1, 0x00}, {"ret", 1, 0x10 }, 
            {"jmp", 2, 0x14}, {"call",2,0x0F}, 
            {"push",3, 0x18}, {"pop", 3, 0x19}, {"getpid", 3, 0x16},{"getpn",3,0x17},
            {"ldimm",4,0x03},
            {"load",5, 0x01},{"store",5, 0x02},{"ldaddr",5, 0x04},
            {"addf", 6, 0x07},{"subf",6, 0x08},{"divf",6, 0x09},{"mulf",6, 0x0A},
            {"addi",6,0x0B},{"subi",6, 0x0C},{"divi",6, 0x0D},{"muli",6, 0x0E},
            {"ldind",7,0x05},{"stind",7,0x06},
            {"blt",8, 0x11},{"bgt",8, 0x12},{"beq",8, 0x13},{"cmpxchg",8, 0x15}, {"ldquad",8,0x1A}};
    //Created a list of valid opcodes: Starting with the english name, the format it falls under, then hex value for object code assembling.
    if (passcounter == 1) {
        if (lookup != NULL) {
            si *lookup1 = symtabLookup(s, lookup); //Get the value if it exists already.
            if (lookup1 != NULL && lookup1->defined ) { 
                error(ERROR_LABEL_DEFINED, lookup); //An already defined label  has been found.
            } else {
                if (lookup1 == NULL) {
                    si *sii = calloc(1,sizeof(si));
                    if (sii == NULL) { fatal("calloc returned NULL");}
                    sii->defined = 1;
                    sii -> address = counter;
                    symtabInstall(s, lookup, sii); //Defines the value, sets the address to the correct location, then updates the symtable.
                } else {
                    lookup1 -> address = counter;
                    lookup1 -> defined = 1; //In this case it is in the symtable but isnt defined, so you set defined to one and update.
                    symtabInstall(s, lookup, lookup1);
                }
            }
        }
        if (i.opcode != NULL) { //There is an opcode
            if (strcmp(i.opcode, "export") == 0) { //Export and import need to be handled differently. 
                if (i.format != 2 || i.u.format2.addr == NULL) { return; }
                si *lookupret =(si *)symtabLookup(s, i.u.format2.addr);
                if (lookupret == NULL) {
                    si *addexport_import = calloc(1, sizeof(si));
                    if (addexport_import == NULL) { fatal("calloc returned NULL");}
                    addexport_import -> defined = 0;
                    addexport_import -> exported = 1;
                    symtabInstall(s, i.u.format2.addr, addexport_import);
                    return;
                } //If lookupret is null the symbol hasnt been encountered yet. Mark is as exported but undefined, 
                //then add it to the symtable to be updated later once its found.
                else { //Otherwise it has indeed been found, so just set exported one and update the table.
                    lookupret ->exported = 1;
                    symtabInstall(s, i.u.format2.addr, lookupret);
                }
                return;
            }
            if (strcmp(i.opcode, "import") == 0) {
                if (i.format != 2 || i.u.format2.addr == NULL) { return; }
                si *res = symtabLookup(s, i.u.format2.addr);
                if (res == NULL) {
                    si *addexport_import = calloc(1, sizeof(si));
                    if (addexport_import == NULL) { fatal("calloc returned NULL");}
                    addexport_import ->imported = 1;
                    symtabInstall(s, i.u.format2.addr, addexport_import);
                } //Same story as above: This symbol hasnt been found yet in the asm file, so add it to the symtable and set imported to 1. 
                else { //Otherwise it has been found, so just set imported to one and install it.
                    res -> imported = 1;
                    symtabInstall(s, i.u.format2.addr, res);
                }
                return;
            }
        }
        int found = 0;

        if (format >= 1 && format <= 9) {
            
            if (i.opcode != NULL) {
                if (strcmp(i.opcode, "alloc") == 0) {
                    if (i.u.format9.constant <= 0) {
                        found = 1;
                        error(ERROR_CONSTANT_ZERO); //Constant is less than zero. it was found, though, so set found to 1 and report the error.
                    } else {
                        counter += i.u.format9.constant;
                        if (counter >= pow(2,20)) {
                            error(ERROR_PROGRAM_SIZE); //Program size exceeds the allowed parameters, so report the error.
                        }
                        found = 1; //Otherwise, just set found to one.
                    }
                }
                //now repeat it for all of the other special cases.
                if (strcmp(i.opcode, "word") == 0) { 
                    counter += 1;
                    if (counter >= pow(2,20)) {
                        error(ERROR_PROGRAM_SIZE);
                    }
                    found = 1;
                }
                if (strcmp(i.opcode, "byte") == 0) {
                    int constant = i.u.format9.constant;
                    if (constant < -128 || constant > 127) {
                        error("Value will not fit into 8 bits.");
                    } 
                    counter++;
                    found = 1;
                }
                if (strcmp(i.opcode, "short") == 0) {
                    int constant = i.u.format9.constant;
                    if (constant < -32768 || constant > 32767) {
                        error("Value will not fit into 16 bits.");
                    }
                    counter++;
                    found = 1;
                }
            }

            if (i.opcode != NULL) {
                for (int j = 0; j < (sizeof(validopcodes)/sizeof(validopcodes[0])); j++) {
                    vo curropcode = validopcodes[j]; //Get the appropriate member of the list order.
                    if (strcmp(curropcode.opcode, i.opcode) == 0) {
                        if (format != curropcode.format) {
                            error(ERROR_OPERAND_FORMAT); //Opcode doesn't match the given operand format.
                        } 
                        if (strcmp(curropcode.opcode, "ldimm") == 0) {
                            if (i.u.format4.constant < -524288 || i.u.format4.constant > 524287) {
                                error(ERROR_CONSTANT_INVALID, i.u.format4.constant);
                            }
                        }//Constant doesnt fit into 20 bits.
                        if  (curropcode.format == 7) {
                            if (i.u.format7.offset < -32768 || i.u.format7.offset > 32767) {
                                error(ERROR_OFFSET_INVALID, i.u.format7.offset);
                            }
                        } //offset wont fit into 16 bits.
                        
                        if (format == curropcode.format) {
                            counter++;
                            if (counter >= pow(2,20)) {
                                error(ERROR_PROGRAM_SIZE); //program is too big.
                            }
                        }
                        found = 1;
                        break;
                    }
                }
            }
            
            if (found == 0 && i.opcode != NULL) {
                error(ERROR_OPCODE_UNKNOWN, i.opcode);
            }
        }

        if (found == 1 && (format == 2 || format == 5 || format == 8)) { 
            char *address = NULL;
            unsigned int tempformat2 = 0;
            unsigned int tempformat5 = 0;
            unsigned int tempformat8 = 0;
            //Values to save if its formats 2 5 or 8.
            if (format == 2) {address = i.u.format2.addr; tempformat2 = (format == 2);}
            if (format == 5) {address = i.u.format5.addr; tempformat5 = (format == 5);}
            if (format == 8) {address = i.u.format8.addr; tempformat8 = (format == 8);}
            
            if (address != NULL) {
                si *res = symtabLookup(s, address);
                if (res == NULL) {
                    si *install = calloc(1, sizeof(si));
                    if (install == NULL) {fatal("calloc failed");}
                    install->defined  = 0;
                    install -> reference = 1;
                    install->refaddress[0] = counter - 1;
                    install->referencecount = 1;
                    install -> format2 = tempformat2;
                    install -> format5 = tempformat5;
                    install -> format8 = tempformat8; //Set whichever format it is to 1, if any.
                    symtabInstall(s, address, install);
                } else {
                    res -> refaddress[res -> referencecount] = counter - 1;
                    res -> referencecount++;

                    res -> reference = 1;
                    if (tempformat2) {
                        res -> format2 = tempformat2;
                    }
                    if (tempformat5) {
                        res -> format5 = tempformat5;
                    }
                    if (tempformat8) {
                        res -> format8 = tempformat8;
                    } //Set the appropriate format to 1.
                    symtabInstall(s, address, res);
                }
            }
        }
    }

    else { //Second pass
        if (i.opcode != NULL) {
            if (strcmp(i.opcode, "alloc") == 0) {
                unsigned int zero = 0;
                for (int k = 0; k < i.u.format9.constant; k++) {
                    fwrite(&zero, sizeof(unsigned int), 1, outf);
                }
                counter += i.u.format9.constant;
                return;
            }
            

            if (strcmp(i.opcode, "export") == 0) { return; }
            if (strcmp(i.opcode, "import") == 0) { return; }
            //Everything above this point increments counter differently or not at all, everything below increments it by 1.
            counter++;

            //Printing the object code to the file:

            if (strcmp(i.opcode, "word") == 0) {
                unsigned int wordval = (unsigned int)i.u.format9.constant;
                fwrite(&wordval, sizeof(unsigned int),1,outf);
                return;
            }
            if (strcmp(i.opcode, "byte") == 0) {
                unsigned int byte = (unsigned int)(i.u.format9.constant & 0xFF);
                fwrite(&byte, sizeof(unsigned int), 1, outf);
                return;
            }
            if (strcmp(i.opcode, "short") == 0) {
                unsigned int shortv = (unsigned int)(i.u.format9.constant & 0xFFFF);
                fwrite(&shortv, sizeof(unsigned int), 1, outf);
                return;
            }
            //All of these values have custom requirements for printing.
            for (int j = 0; j < (sizeof(validopcodes)/sizeof(validopcodes[0])); j++) {
                vo curropcode = validopcodes[j];
                if (strcmp(curropcode.opcode, i.opcode) == 0) {
                    encoded = curropcode.value;
                    break;
                }
            }
            if (format == 1) {
                //All set
            }
            if (format == 2) { //Get the value from the symbol table, if its imported, print 0, otherwise print the address - counter.
                si *lookupn =(si *)symtabLookup(s, i.u.format2.addr);
                unsigned int address = (lookupn->imported == 1) ? 0 : (lookupn->address - counter);
                encoded |= ((address) << 12);
            }
            if (format == 3) {
                encoded |= (i.u.format3.reg << 8);
            }
            if (format == 4) {
                encoded |= (i.u.format4.reg << 8);
                encoded |= (i.u.format4.constant << 12);
            }
            if (format == 5) {
                si *lookupn = (si*) symtabLookup(s, i.u.format5.addr);
                encoded |= (i.u.format5.reg << 8);
                unsigned int address = (lookupn->imported == 1) ? 0 : (lookupn->address - counter);
                encoded |= ((address) << 12);
            }
            if (format == 6) {
                encoded |= (i.u.format6.reg1 << 8);
                encoded |= (i.u.format6.reg2 << 12);
            }
            if (format == 7) {
                encoded |= (i.u.format7.reg1 << 8);
                encoded |= (i.u.format7.reg2 << 12);
                encoded |= (i.u.format7.offset << 16);
            }
            if (format == 8) {
                si *lookupn = (si*) symtabLookup(s, i.u.format8.addr);
                encoded |= (i.u.format8.reg1 << 8);
                encoded |= (i.u.format8.reg2 << 12);
                unsigned int address = (lookupn->imported == 1) ? 0 : (lookupn->address - counter);

                encoded |= ((address) << 16);
            }
            fwrite(&encoded, sizeof(unsigned int),1,outf); //This line will never be reached in pass 1, and file is opened and not closed in main (between passes),
            //so no opening of the file is necessary here or anywhere else in this file.
        }
    }
}

extern int betweenPasses(FILE *file) {
    passcounter = 2;
    outf = file;
    
    struct Iterator *tabit = symtabCreateIterator(s);
    struct Node *tree = symtabCreateBST(tabit);
    struct bstiterator* bsti = symtabCreateBSTIterator(tree);
    struct bstiterator* bstiinex = symtabCreateBSTIterator(tree);
    struct Iterator *exportiterator = symtabCreateBSTIterator(tree);
    struct Iterator *importiterator = symtabCreateBSTIterator(tree); //Open all of the iterators: You cannnot reuse them.

    void *syminf;
    const char *symbol = NULL;
    const char *symbolinex = NULL;
    unsigned int exportcount = 0;
    unsigned int importcount = 0;
    
    while ((symbolinex = symtabBSTNext(bstiinex, &syminf)) != NULL) {
        si* syminfo = (si*)syminf;
        if (syminfo ->exported == 1) {
            exportcount += 1;
        } 
        else if (syminfo -> imported == 1) {
            importcount += syminfo->referencecount;//Symbols can be referenced many times, so its important to increment reference count and add it here.
        }
    }

    importcount *= 5;
    exportcount *= 5;

    fwrite(&exportcount, sizeof(unsigned int),1, file);
    fwrite(&importcount, sizeof(unsigned int),1, file);
    fwrite(&counter, sizeof(unsigned int),1, file);
    //Header^

    counter = 0;

    while (( symbol = symtabBSTNext(exportiterator, &syminf)) != NULL) {
        si* syminfo = (si *)syminf;
        if (syminfo->exported == 1) {
            char symarray[16];
            memset(symarray, 0, 16);
            strncpy(symarray, symbol, 16); //Adds the symbol (Max total of 16 bits) to the array, 
            //and writes it to the file.
            fwrite(symarray, sizeof(char), 16, file);
            fwrite(&syminfo->address, sizeof(unsigned int), 1, file);//insymbol        
        }
    }

    while ((symbol = symtabBSTNext(importiterator, &syminf)) != NULL) {
        si* syminfo = (si *)syminf;
        if (syminfo -> imported == 1) {
            for (int k = 0; k < syminfo->referencecount; k++) {
                char symarray[16];
                memset(symarray, 0,16);
                //Same as above but with outsymbols.
                strncpy(symarray, symbol, 16);
                fwrite(symarray, sizeof(char), 16, file);
                fwrite(&syminfo->refaddress[k], sizeof(unsigned int), 1, file);//outsymbol
            }
        }
    }
    
    while (( symbol = symtabBSTNext(bsti, &syminf)) != NULL) {
        si* syminfo = (si *)syminf;
        if (syminfo->reference == 1 && syminfo->defined == 0 && syminfo->imported == 0) {
            error(ERROR_LABEL_REFERENCE_NOT_FOUND, symbol); //Referenced but not imported/defined
        }
        if (syminfo->exported == 1 && syminfo->imported == 1) {
            error(ERROR_SYMBOL_IMPORT_EXPORT, symbol); //Imported and exported symbols
        }
        if (syminfo->imported == 1 && syminfo->defined == 1) {
            error (ERROR_SYMBOL_IMPORT_DEFINED,symbol); //Imported and defined symbol
        }
        if (syminfo->imported == 1 && syminfo->reference == 0) {
            error(ERROR_SYMBOL_IMPORT_NO_REFERENCE, symbol); //Imported symbol is not referenced.
        }
        if (syminfo->exported == 1 && syminfo->defined == 0) {
            error(ERROR_SYMBOL_EXPORT_NO_DEFINITION, symbol); //Exported but no definition.
        }
        if (syminfo -> imported == 1 && strlen(symbol) > 16) {
            error (ERROR_SYMBOL_IMPORT_SIZE, symbol); //Import size exceeds 16 characters.
        }
        if (syminfo -> exported == 1 && strlen(symbol) > 16) {
            error (ERROR_SYMBOL_EXPORT_SIZE, symbol); //Export size exceeds 16 characters.
        }
        if (syminfo -> defined == 1) {
            if (syminfo -> format5 == 1) {
                for (int k = 0; k < syminfo -> referencecount; k++) {
                    int offset = (int)syminfo->address - (int)syminfo->refaddress[k];
                    if (offset > 524287 || offset < -524288) {
                        error(ERROR_LABEL_SIZE20, symbol, syminfo -> address); //Label size of over 20, accounting for offet (Hence the loop).
                    }
                }
            }
            if (syminfo -> format2 == 1) {
                for (int k = 0; k < syminfo -> referencecount; k++) {
                    int offset = (int)syminfo->address - (int)syminfo->refaddress[k];
                    if (offset > 524287 || offset < -524288) {
                        error(ERROR_LABEL_SIZE20, symbol, syminfo -> address);
                    }
                }
            }
            if (syminfo -> format8 == 1) {
                for (int k = 0; k < syminfo -> referencecount; k++) {
                    int offset = (int)syminfo->address - (int)syminfo->refaddress[k];
                    if (offset > 32767 || offset < -32768) {
                        error(ERROR_LABEL_SIZE16, symbol, syminfo -> address); //Label size over 16, accounts for offset (Loop)
                    }
                }
            }
        }
    }

    symtabDeleteBSTIterator(exportiterator);
    symtabDeleteBSTIterator(importiterator);

    symtabDeleteBSTIterator(bsti);
    symtabDeleteBSTIterator(bstiinex);
    symtabDeleteIterator(tabit);
    symtabBSTDelete(tree);
    
    return errorcount; //Close everything & return errorcount.
}