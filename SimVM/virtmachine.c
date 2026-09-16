#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simVM.h"



struct pTableEntry {
    int page;
    int present; //Is it in pmemory?
    int dirty; //has this been dirty?
    unsigned int time; //Last accessed time.
};

struct tlbentry {
    int virtualpage;
    int physicalpage;
    int dirty;
    int present;
    unsigned int time;
};

struct frame {
    unsigned int vpage; //Which virtual page is stored here
};
struct virtm {
    unsigned int sizeVM;   // size of the virtual memory in pages
    unsigned int sizePM;   // size of the physical memory in pages
    unsigned int pageSize; // size of a page in words
    unsigned int sizeTLB;  // number of translation lookaside buffer entries
    char pageReplAlg;      // page replacement alg.: 0 is Round Robin, 1 is LRU
    char tlbReplAlg;        // TLB replacement alg.: 0 is Round Robin, 1 is LRU
    
    unsigned int *virtmem;
    unsigned int *pmem;
    
    struct pTableEntry *ptable;
    struct tlbentry *tlb;
    struct frame *ftable;
    
    unsigned int phits;
    unsigned int pfaults;
    unsigned int dwrites;
    unsigned int timestamp;
    unsigned int tlbhits;
    unsigned int tlbmisses;
};

int smallestTimestamp(struct virtm *vm);
void pagefault(struct virtm *vm, unsigned int virtpage);
static int tlbfinder(struct virtm *, unsigned int burner);
static int findtlbvictim(struct virtm *);

//Finds an item within the TLB
static int tlbfinder(struct virtm *vm, unsigned int virtpage) {
    for (int i  = 0; i < vm -> sizeTLB; i++) {
        if (vm -> tlb[i].present && vm -> tlb[i].virtualpage == virtpage) {
            return i;
        }
    }
    return -1;
}

//Common TLB victim finder logic among functions.
static int findtlbvictim(struct virtm *vm) {
    unsigned int min_time = ~0u; 
    //Just a cleaner way of writing the highest unsigned int.
    int vict = 0; 
    for (int i = 0; i < vm -> sizeTLB; i++) {
        unsigned int tlbtime = vm -> tlb[i].time;
        if (tlbtime < min_time || (tlbtime == min_time && i < vict)) {
            min_time = tlbtime;
            vict = i;
        }
    }
    //Make sure the dirty bit is correct.
    if (vm -> tlb[vict].present) {
        int oldvirtpage = vm -> tlb[vict].virtualpage;
        if (vm -> tlb[vict].dirty) {
            vm -> ptable[oldvirtpage].dirty = 1;
        }
    }
    return vict;
}

void *createVM(unsigned int sizeVM, unsigned int sizePM, unsigned int pageSize, unsigned int sizetlb, char pagheReplAlg, char tlbreplalg) {
    if (sizeVM <= sizePM) { return NULL; }
    if (sizePM == 0) { return NULL; }
    if (pageSize == 0 || (pageSize & (pageSize - 1)) != 0) { return NULL; }
    if (pagheReplAlg != VM_LRU_REPLACEMENT) { return NULL; }
    
    if (sizetlb > sizePM) return NULL;
    if (sizeVM > (1u << 30) / pageSize) return NULL; //Does it exceed 2^32?

    struct virtm *vm = malloc(sizeof(struct virtm));
    if (vm == NULL) {
        fprintf(stderr, "Malloc failed");
        exit(1);
    }

    vm -> sizeVM = sizeVM;
    vm -> sizePM = sizePM;
    vm -> pageSize = pageSize;
    vm -> pageReplAlg = pagheReplAlg;
    vm -> tlbReplAlg = tlbreplalg;

    vm -> virtmem = malloc(sizeVM * pageSize * sizeof(int));
    vm -> pmem = malloc(sizePM * pageSize * sizeof(int));
    vm -> ptable = malloc(sizeVM * sizeof(struct pTableEntry));
    vm -> ftable = malloc(sizePM * sizeof(struct frame));
    vm -> tlb = malloc(sizetlb * sizeof(struct tlbentry));

    vm -> tlbhits = 0;
    vm  -> tlbmisses = 0;
    vm -> phits = 0;
    vm -> pfaults = 0;
    vm -> dwrites = 0;
    vm -> timestamp = 0;

    for (int i = 0; i < vm -> sizeVM; i++) {
        if (i < sizePM) {
            vm -> ptable[i].page = i;
            vm -> ptable[i].present = 1;
            vm -> ptable[i].dirty = 0;
            vm -> ptable[i].time = 0;
        }
        else {
            vm -> ptable[i].page = -1;
            vm -> ptable[i].present = 0;
            vm -> ptable[i].dirty = 0;
            vm -> ptable[i].time = 0;
        }
    }
    if (sizetlb > 0) {
        for (int i = 0; i < sizetlb; i++) {
            vm -> tlb[i].virtualpage = i;
            vm -> tlb[i].physicalpage = i;
            vm -> tlb[i].present = 1;
            vm -> tlb[i].dirty = 0;
            vm -> tlb[i].time = 0;
        }
    }

    vm->sizeTLB = sizetlb;
    
    for (int i = 0; i < vm -> sizePM; i++) {
        vm -> ftable[i].vpage = i;
    }
    for (int i = 0; i < vm -> sizePM; i++) {
        for (int j = 0; j < vm -> pageSize; j++) {
            int startindex = i * vm -> pageSize + j;
            int endindex = i * pageSize + j;
            vm -> pmem[endindex] = vm -> virtmem[startindex];
        }
    }

    return (void*)vm;
}

int readInt(void *handle, unsigned int address) {
    struct virtm *vm = (struct virtm *) handle;
    if (address >= (vm -> sizeVM * vm -> pageSize)) {
        fprintf(stderr, "Address out of range!");
        exit(1);
    }

    vm -> timestamp++;

    int virtpage = address / vm -> pageSize;
    int offset = address % vm -> pageSize;

    int tlbindex = tlbfinder(vm, virtpage);

    if (tlbindex != -1) {
        //It was found: TLB hit case success!
        vm -> tlbhits++;
        vm -> tlb[tlbindex].time = vm -> timestamp;
        vm -> ptable[virtpage].time = vm -> timestamp;

        unsigned int ppage = vm -> tlb[tlbindex].physicalpage;
        return vm -> pmem[ppage * vm -> pageSize + offset];
    }
    //If this is reached, tlb miss case has been reached.
    vm -> tlbmisses++;

    if (!vm -> ptable[virtpage].present) {
        vm -> pfaults++;
        pagefault(vm, virtpage);
    } else {
        vm -> phits++;
    }

    int tlbindex2 = findtlbvictim(vm);
    //Update PMEM
    vm -> ptable[virtpage].time = vm -> timestamp;

    //Update the TLB, evicting the proper victim item.
    vm -> tlb[tlbindex2].virtualpage = virtpage;
    vm -> tlb[tlbindex2].physicalpage = vm -> ptable[virtpage].page;
    vm -> tlb[tlbindex2].time = vm -> timestamp;
    vm -> tlb[tlbindex2].present = 1;
    vm -> tlb[tlbindex2].dirty = vm -> ptable[virtpage].dirty;

    unsigned int ppage = vm -> ptable[virtpage].page;

    //index = physicalpage# * pagesize + whatever offset 
    return vm -> pmem[ppage * vm -> pageSize + offset];
}

void writeInt(void *handle, unsigned int address, int value) {
    //Similar to the readInt function, however in this case dirty bit must be checked on the TLB and ptable.
    struct virtm *vm = (struct virtm *) handle;
    if (address >= (vm -> sizeVM * vm -> pageSize)) {
        fprintf(stderr, "Address out of range!");
        exit(1);
    }

    vm -> timestamp++;

    int virtpage = address / vm -> pageSize;
    int offset = address % vm -> pageSize;
    //int found = 0;
    int tlbindex = tlbfinder(vm, virtpage);

    if (tlbindex != -1) {
        vm -> tlbhits++;
        vm -> tlb[tlbindex].time = vm -> timestamp;
        vm -> ptable[virtpage].time = vm -> timestamp;
        
        vm -> tlb[tlbindex].dirty = 1;
        vm -> ptable[virtpage].dirty = 1;

        unsigned int ppage = vm -> tlb[tlbindex].physicalpage;
        vm -> pmem[ppage * vm -> pageSize + offset] = value;
        return;
    }
    //If this is reached, tlb miss case has been reached.
    vm -> tlbmisses++;

    if (!vm -> ptable[virtpage].present) {
        vm -> pfaults++;
        pagefault(vm, virtpage);
    } else {
        vm -> phits++;
    }

    int tlbindex2 = findtlbvictim(vm);
    vm -> ptable[virtpage].time = vm -> timestamp;

    vm -> tlb[tlbindex2].virtualpage = virtpage;
    vm -> tlb[tlbindex2].physicalpage = vm -> ptable[virtpage].page;
    vm -> tlb[tlbindex2].time = vm -> timestamp;
    vm -> tlb[tlbindex2].present = 1;
    vm -> tlb[tlbindex2].dirty = 1;

    unsigned int ppage = vm -> ptable[virtpage].page;
    vm -> pmem[ppage * vm -> pageSize + offset] = value;
}

void writeFloat(void *handle, unsigned int address, float valuefloat) {
    struct virtm *vm = (struct virtm *) handle;
    if (address >= (vm -> sizeVM * vm -> pageSize)) {
        fprintf(stderr, "Address out of range!");
        exit(1);
    }
    int value = *((int*)&valuefloat);

    vm -> timestamp++;

    int virtpage = address / vm -> pageSize;
    int offset = address % vm -> pageSize;
    int tlbindex = tlbfinder(vm, virtpage);

    if (tlbindex != -1) {
        vm -> tlbhits++;
        vm -> tlb[tlbindex].time = vm -> timestamp;
        vm -> ptable[virtpage].time = vm -> timestamp;
        
        vm -> ptable[virtpage].dirty = 1;
        vm -> tlb[tlbindex].dirty = 1;

        unsigned int ppage = vm -> tlb[tlbindex].physicalpage;
        vm -> pmem[ppage * vm -> pageSize + offset] = value;
        return;
    }
    //If this is reached, tlb miss case has been reached.
    vm -> tlbmisses++;

    if (!vm -> ptable[virtpage].present) {
        vm -> pfaults++;
        pagefault(vm, virtpage);
    } else {
        vm -> phits++;
    }

    int tlbindex2 = findtlbvictim(vm);
    
    vm -> ptable[virtpage].time = vm -> timestamp;
    vm -> ptable[virtpage].dirty = 1;

    vm -> tlb[tlbindex2].virtualpage = virtpage;
    vm -> tlb[tlbindex2].physicalpage = vm -> ptable[virtpage].page;
    vm -> tlb[tlbindex2].time = vm -> timestamp;
    vm -> tlb[tlbindex2].present = 1;
    vm -> tlb[tlbindex2].dirty = 1;

    unsigned int ppage = vm -> ptable[virtpage].page;
    vm -> pmem[ppage * vm -> pageSize + offset] = value;
    return;
    
}

float readFloat(void *handle, unsigned int address) {
    struct virtm *vm = (struct virtm *) handle;
    if (address >= (vm -> sizeVM * vm -> pageSize)) {
        fprintf(stderr, "Address out of range!");
        exit(1);
    }

    vm -> timestamp++;

    int virtpage = address / vm -> pageSize;
    int offset = address % vm -> pageSize;

    int tlbindex = tlbfinder(vm, virtpage);

    if (tlbindex != -1) {
        vm -> tlbhits++;
        vm -> tlb[tlbindex].time = vm -> timestamp;
        vm -> ptable[virtpage].time = vm -> timestamp;

        unsigned int ppage = vm -> tlb[tlbindex].physicalpage;
        return *((float*)&vm -> pmem[ppage * vm -> pageSize + offset]);
    }
    //If this is reached, tlb miss case has been reached.
    vm -> tlbmisses++;

    if (!vm -> ptable[virtpage].present) {
        vm -> pfaults++;
        pagefault(vm, virtpage);
    } else {
        vm -> phits++;
    }

    int tlbindex2 = findtlbvictim(vm);
    vm -> ptable[virtpage].time = vm -> timestamp;

    vm -> tlb[tlbindex2].virtualpage = virtpage;
    vm -> tlb[tlbindex2].physicalpage = vm -> ptable[virtpage].page;
    vm -> tlb[tlbindex2].time = vm -> timestamp;
    vm -> tlb[tlbindex2].present = 1;
    vm -> tlb[tlbindex2].dirty = vm -> ptable[virtpage].dirty;

    unsigned int ppage = vm -> ptable[virtpage].page;
    return *((float*)&vm -> pmem[ppage * vm -> pageSize + offset]);
}


void pagefault(struct virtm *vm, unsigned int virtpage) {
    int smallest = smallestTimestamp(vm);
    unsigned int virtualpage = vm -> ftable[smallest].vpage;
    for (int k = 0; k < vm -> sizeTLB; k++) {
        if (vm -> tlb[k].virtualpage == virtualpage) {
            if (vm -> tlb[k].dirty) vm -> ptable[virtualpage].dirty = 1;
            vm -> tlb[k].present = 0;
            vm -> tlb[k].dirty = 0;
            vm -> tlb[k].virtualpage = -1;
            vm -> tlb[k].time = 0;
        }
    }
    if (vm -> ptable[virtualpage].dirty) {
        vm -> dwrites++;
        for (int i = 0; i < vm -> pageSize; i++) {
            int startindex = smallest * vm -> pageSize + i; //Get the source location
            int endindex = virtualpage * vm -> pageSize + i; //Get the destination. Current present data will be evicted.
            vm -> virtmem[endindex] = vm -> pmem[startindex]; //Write the data from the source into the destination.
        }
        vm -> ptable[virtualpage].dirty = 0;
    }

    vm -> ptable[virtualpage].present = 0;
    vm -> ptable[virtualpage].page = -1;


    for (int j = 0; j < vm -> pageSize; j++) {
        int startindex = virtpage * vm -> pageSize + j;
        int endindex = smallest * vm -> pageSize + j;
        vm -> pmem[endindex] = vm -> virtmem[startindex];        
    }
    vm -> ptable[virtpage].present = 1;
    vm -> ptable[virtpage].page = smallest;
    vm -> ptable[virtpage].dirty = 0;

    vm -> ftable[smallest].vpage = virtpage;
}

int smallestTimestamp(struct virtm *vm) {
    unsigned int smallesttime = ~0u;
    int frame = 0;

    for (int i = 0; i < vm -> sizePM; i++) {
        int virtpageindex = vm -> ftable[i].vpage;
        unsigned int accessed = vm -> ptable[virtpageindex].time;

        if (accessed < smallesttime || (accessed == smallesttime && i < frame)) {
            smallesttime = accessed;
            frame = i;
        } 

    }
    return frame;
}

void printStatistics(void *handle) {
    struct virtm *vm = (struct virtm *)handle;
    printf("Number of page hits: %d\n", vm -> phits);
    printf("Number of page faults: %d\n", vm -> pfaults);
    if (vm->sizeTLB > 0) {
        printf("Number of TLB hits: %d\n", vm -> tlbhits);
        printf("Number of TLB misses: %d\n", vm -> tlbmisses);
    }
    printf("Number of disk writes: %d\n", vm -> dwrites);
}

void cleanupVM(void *handle) {

   struct virtm *vm = (struct virtm *)handle;
   free(vm -> virtmem);
   free(vm -> pmem);
   free(vm -> ptable);
   free(vm -> ftable);
   free(vm -> tlb);
   free(vm);
}