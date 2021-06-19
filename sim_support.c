#include <stdlib.h>
#if MD5
   #include <openssl/md5.h>
#endif
#include "sim_support.h"
#include "exmemwb.h"
#include "rsp-server.h"

u64 cycleCount = 0;
u64 last_insnCount = 0;
u64 insnCount = 0;
u64 wastedCycles = 0;
u32 cyclesSinceReset = 0;
u32 cyclesSinceCP = 0;
u32 resetAfterCycles = 0;
u32 addrOfCP = 0;
u32 addrOfRestoreCP = 0;
u32 do_reset = 0;
u32 wdt_seed = 0;
u32 wdt_val = 0;
u32 md5[4] = {0,0,0,0,0};
u32 PRINT_STATE_DIFF = PRINT_STATE_DIFF_INIT;
#if MEM_COUNT_INST
  u32 store_count = 0;
  u32 load_count = 0;
  u32 cp_count = 0;
#endif
u32 ram[RAM_SIZE >> 2];
u32 flash[FLASH_SIZE >> 2];

// Performance counter start/stop PC values.
// The default value of 0 prevents the starting/stopping of traces
// without explicit --from-pc/--to-pc option setting since in Thumb
// mode only odd PC values are valid.
u32 trace_start_pc = 0;
u32 trace_stop_pc = 0;

// Event counters
u64 ram_data_reads = 0;
u64 ram_insn_reads = 0;
u64 ram_writes = 0;
u64 flash_data_reads = 0;
u64 flash_insn_reads = 0;
u64 flash_writes = 0;
u64 taken_branches = 0;
u64 nonword_branch_destinations = 0;
u64 ram_insn_prefetch_hits = 0;
u64 flash_insn_prefetch_hits = 0;
u64 nonword_taken_branches = 0;
u64 bl_insns = 0, blx_insns = 0, bx_insns = 0;
u64 last_bl_insns = 0, last_blx_insns = 0, last_bx_insns = 0;
bool branch_fetch_stall = 0;
u64 branch_fetch_stalls = 0;
bool ram_access = 0; // Boolean to mark a RAM request in the current decode cycle
bool flash_access = 0; // Boolean to mark a Flash request in the current decode cycle
u64 arbitration_conflicts = 0; // Count of potential RAM/Flash arbitration conflicts
// Last snapshot of event counters
u64 last_ram_data_reads = 0;
u64 last_ram_insn_reads = 0;
u64 last_ram_writes = 0;
u64 last_flash_data_reads = 0;
u64 last_flash_insn_reads = 0;
u64 last_flash_writes = 0;
u64 last_taken_branches = 0;
u64 last_nonword_branch_destinations = 0;
u64 last_ram_insn_prefetch_hits = 0;
u64 last_flash_insn_prefetch_hits = 0;
u64 last_nonword_taken_branches = 0;
u64 last_branch_fetch_stalls = 0;
u64 last_arbitration_conflicts = 0;
// Current/future data bus accesses
bool data_access_in_cur_cycle = 0;
bool data_access_in_next_cycle = 0;
bool data_access_in_two_cycles = 0;
bool data_access_in_three_cycles = 0;
// Current/PAST loads, stores, compares
bool load_in_cur_insn = 0;
bool load_in_prev_insn = 0;
bool store_in_cur_insn = 0;
bool store_in_prev_insn = 0;
char reg_loaded_in_cur_insn = -1;
char reg_loaded_in_prev_insn = -1;
bool cmp_in_cur_insn = 0;
// Count of back-to-banck mem operations.
u64 load_after_load = 0, last_load_after_load = 0;
u64 load_after_store = 0, last_load_after_store = 0;
u64 store_after_load = 0, last_store_after_load = 0;
u64 store_after_store = 0, last_store_after_store = 0;
u64 use_after_load_ld = 0, last_use_after_load_ld = 0;
u64 use_after_load_st = 0, last_use_after_load_st = 0;
u64 use_after_load_alu = 0, last_use_after_load_alu = 0;
u64 use_after_load_cmp = 0, last_use_after_load_cmp = 0;
u64 burst_loads = 0, last_burst_loads = 0, burst_stores = 0, last_burst_stores = 0;
u64 pop_high_regs = 0, pop_sp = 0, pop_pc = 0;
u64 last_pop_high_regs = 0, last_pop_sp = 0, last_pop_pc = 0;
u64 word_aligned_bl = 0, last_word_aligned_bl = 0;
bool use_after_load_seen = 0;
bool store_addr_reg_load_in_prev_insn = 0;
// Prefetch buffering: single word version for 32-bit memories
u32 last_fetched_address = 0xffffffff;
u32 last_fetched_word = 0xffffffff;
u32 prefetch_mode = PREFETCH_MODE_NONE;
u32 prefetch_addresses[3] = { 0xffffffff, 0xffffffff, 0xffffffff };
u32 prefetch_words[3] = { 0xffffffff, 0xffffffff, 0xffffffff };
 // LRU implementation: use tail of MRU queue (prefetch_mru_q[2]).
u32 prefetch_mru_q[3] = { 0, 1, 2};
// Use tracing mode.
bool doTrace = 0;
bool useCSVoutput = 0;
// Trace or differential counters: 1 if in progress.
bool tracingActive = 0;
// Log all events even if not tracing the execution in any way: 1 if enabled.
bool logAllEvents = 0;
bool takenBranch = 0;
ADDRESS_LIST addressReadBeforeWriteList = {0, NULL};
ADDRESS_LIST addressWriteBeforeReadList = {0, NULL};
ADDRESS_LIST addressConflictList = {0, NULL};
int addressConflicts = 0;
int addressConflictsStack = 0;
int addressWrites = 0;
int addressReads = 0;

// Reserve a space inside the simulator for variables that GDB and python can use to control the simulator
// Essentially creates a new block of addresses on the bus of the processor that only the debug read and write commands can access
//MEMMAPIO mmio = {.cycleCountLSB = &cycleCount, .cycleCountMSB = &cycleCount+4,
//                 .cyclesSince = &cyclesSinceReset, .resetAfter = &resetAfterCycles};
u32* mmio[] = {&cycleCount, ((u32*)&cycleCount)+1, &wastedCycles, ((u32*)&wastedCycles)+1,
  &cyclesSinceReset, &cyclesSinceCP, &addrOfCP, &addrOfRestoreCP, 
  &resetAfterCycles, &do_reset, &PRINT_STATE_DIFF, &wdt_seed, 
  &wdt_val, &(md5[0]), &(md5[1]), &(md5[2]), 
  &(md5[3]), &(md5[4])};

void printOpcodeCounts(u64 subcode_stats[], u32 count)
{
    u32 i;
    fprintf(stderr, ", [");
    for (i = 0; i < count - 1; i++)
        fprintf(stderr, "%ld, ", subcode_stats[i]);

    fprintf(stderr, "%ld]\n", subcode_stats[i]);
}

void printOpcodeCountDeltas(u64 subcode_stats[], u64 last_subcode_stats[], u32 count)
{
    u32 i;
    fprintf(stderr, ", [");
    for (i = 0; i < count - 1; i++)
        fprintf(stderr, "%ld, ", subcode_stats[i] - last_subcode_stats[i]);

    fprintf(stderr, "%ld]\n", subcode_stats[i], last_subcode_stats[i]);
}

void saveStats(void)
{
    u32 i;
    last_insnCount = insnCount;
    last_ram_data_reads = ram_data_reads;
    last_ram_insn_reads = ram_insn_reads;
    last_ram_writes = ram_writes;
    last_flash_data_reads = flash_data_reads;
    last_flash_insn_reads =flash_insn_reads;
    last_flash_writes = flash_writes;
    last_taken_branches = taken_branches;
    last_nonword_branch_destinations = nonword_branch_destinations;
    last_ram_insn_prefetch_hits = ram_insn_prefetch_hits;
    last_flash_insn_prefetch_hits = flash_insn_prefetch_hits;
    last_nonword_taken_branches = nonword_taken_branches;
    last_branch_fetch_stalls = branch_fetch_stalls;
    last_arbitration_conflicts = arbitration_conflicts;
    last_load_after_load = load_after_load;
    last_store_after_load = store_after_load;
    last_load_after_store = load_after_store;
    last_store_after_store = store_after_store;
    last_use_after_load_ld = use_after_load_ld;
    last_use_after_load_st = use_after_load_st;
    last_use_after_load_alu = use_after_load_alu;
    last_use_after_load_cmp = use_after_load_cmp;
    last_burst_loads = burst_loads;
    last_burst_stores = burst_stores;
    last_bl_insns = bl_insns;
    last_blx_insns = blx_insns;
    last_bx_insns = bx_insns;
    last_pop_high_regs = pop_high_regs;
    last_pop_sp = pop_sp;
    last_pop_pc = pop_pc;
    last_word_aligned_bl = word_aligned_bl;

    memcpy(last_primary_opcode_stats, primary_opcode_stats, sizeof (primary_opcode_stats));
    memcpy(last_opcode_stats, opcode_stats, sizeof(opcode_stats));
}

// Print execution statistics.
void printStats(void)
{
    u32 i;
#if MEM_COUNT_INST
    fprintf(stderr, "Loads: %u\nStores: %u\nCheckpoints: %u\n", load_count, store_count, cp_count);
#endif
    fprintf(stderr, "Executed insns:      %12ld\n", insnCount);
    fprintf(stderr, "RAM data reads:      %12ld\n", ram_data_reads);
    fprintf(stderr, "RAM insn reads:      %12ld\n", ram_insn_reads);
    fprintf(stderr, "RAM writes:          %12ld\n", ram_writes);
    fprintf(stderr, "Flash data reads:    %12ld\n", flash_data_reads);
    fprintf(stderr, "Flash insn reads:    %12ld\n", flash_insn_reads);
    fprintf(stderr, "Flash writes:        %12ld\n", flash_writes);
    fprintf(stderr, "Taken branches:      %12ld\n", taken_branches);
    fprintf(stderr, "Nonword branch dsts: %12ld\n", nonword_branch_destinations);
    fprintf(stderr, "Nonword taken branch:%12ld\n", nonword_taken_branches);
    fprintf(stderr, "RAM prefetch hits:   %12ld\n", ram_insn_prefetch_hits);
    fprintf(stderr, "Flash prefetch hits: %12ld\n", flash_insn_prefetch_hits);
    fprintf(stderr, "Branch fetch stalls: %12ld\n", branch_fetch_stalls);
    fprintf(stderr, "Arbitration clashes: %12ld\n", arbitration_conflicts);
    fprintf(stderr, "Load-after-load:     %12ld\n", load_after_load);
    fprintf(stderr, "Load-after-store:    %12ld\n", load_after_store);
    fprintf(stderr, "Store-after-load:    %12ld\n", store_after_load);
    fprintf(stderr, "Store-after-store:   %12ld\n", store_after_store);
    fprintf(stderr, "Use-after-load-LD:   %12ld\n", use_after_load_ld);
    fprintf(stderr, "Use-after-load-ST:   %12ld\n", use_after_load_st);
    fprintf(stderr, "Use-after-load-ALU:  %12ld\n", use_after_load_alu);
    fprintf(stderr, "Use-after-load-CMP:  %12ld\n", use_after_load_cmp);
    fprintf(stderr, "Burst loads:         %12ld\n", burst_loads);
    fprintf(stderr, "Burst stores:        %12ld\n", burst_stores);
    fprintf(stderr, "BL insns:            %12ld\n", bl_insns);
    fprintf(stderr, "BL word-aligned:     %12ld\n", word_aligned_bl);
    fprintf(stderr, "BLX insns:           %12ld\n", blx_insns);
    fprintf(stderr, "BX insns:            %12ld\n", bx_insns);
    fprintf(stderr, "PUSH/POP high regs:  %12ld\n", pop_high_regs);
    fprintf(stderr, "PUSH/POP SP:         %12ld\n", pop_sp);
    fprintf(stderr, "PUSH/POP PC/LR:      %12ld\n", pop_pc);

    fprintf(stderr, "Opcode statistics:\n");
    for (i = 0; i < 64; i++)
    {
        fprintf(stderr, "%2d: %9ld", i, primary_opcode_stats[i]);
        printOpcodeCounts(opcode_stats[i], 16);
    }
}

void printStatsDelta(void)
{
    u32 i;
#if MEM_COUNT_INST
    fprintf(stderr, "Loads: %u\nStores: %u\nCheckpoints: %u\n", load_count, store_count, cp_count);
#endif
    fprintf(stderr, "Executed insns:      %12ld\n", insnCount - last_insnCount);
    fprintf(stderr, "RAM data reads:      %12ld\n", ram_data_reads - last_ram_data_reads);
    fprintf(stderr, "RAM insn reads:      %12ld\n", ram_insn_reads - last_ram_insn_reads);
    fprintf(stderr, "RAM writes:          %12ld\n", ram_writes - last_ram_writes);
    fprintf(stderr, "Flash data reads:    %12ld\n", flash_data_reads - last_flash_data_reads);
    fprintf(stderr, "Flash insn reads:    %12ld\n", flash_insn_reads - last_flash_insn_reads);
    fprintf(stderr, "Flash writes:        %12ld\n", flash_writes - last_flash_writes);
    fprintf(stderr, "Taken branches:      %12ld\n", taken_branches - last_taken_branches);
    fprintf(stderr, "Nonword branch dsts: %12ld\n", nonword_branch_destinations - last_nonword_branch_destinations);
    fprintf(stderr, "Nonword taken branch:%12ld\n", nonword_taken_branches - last_nonword_taken_branches);
    fprintf(stderr, "RAM prefetch hits:   %12ld\n", ram_insn_prefetch_hits - last_ram_insn_prefetch_hits);
    fprintf(stderr, "Flash prefetch hits: %12ld\n", flash_insn_prefetch_hits - last_flash_insn_prefetch_hits);
    fprintf(stderr, "Branch fetch stalls: %12ld\n", branch_fetch_stalls - last_branch_fetch_stalls);
    fprintf(stderr, "Arbitration clashes: %12ld\n", arbitration_conflicts - last_arbitration_conflicts);
    fprintf(stderr, "Load-after-load:     %12ld\n", load_after_load - last_load_after_load);
    fprintf(stderr, "Load-after-store:    %12ld\n", load_after_store - last_load_after_store);
    fprintf(stderr, "Store-after-load:    %12ld\n", store_after_load - last_store_after_load);
    fprintf(stderr, "Store-after-store:   %12ld\n", store_after_store - last_store_after_store);
    fprintf(stderr, "Use-after-load-LD:   %12ld\n", use_after_load_ld - last_use_after_load_ld);
    fprintf(stderr, "Use-after-load-ST:   %12ld\n", use_after_load_st - last_use_after_load_st);
    fprintf(stderr, "Use-after-load-ALU:  %12ld\n", use_after_load_alu - last_use_after_load_alu);
    fprintf(stderr, "Use-after-load-CMP:  %12ld\n", use_after_load_cmp - last_use_after_load_cmp);
    fprintf(stderr, "Burst loads:         %12ld\n", burst_loads - last_burst_loads);
    fprintf(stderr, "Burst stores:        %12ld\n", burst_stores - last_burst_stores);
    fprintf(stderr, "BL insns:            %12ld\n", bl_insns - last_bl_insns);
    fprintf(stderr, "BL word-aligned:     %12ld\n", word_aligned_bl - last_word_aligned_bl);
    fprintf(stderr, "BLX insns:           %12ld\n", blx_insns - last_blx_insns);
    fprintf(stderr, "BX insns:            %12ld\n", bx_insns - last_bx_insns);
    fprintf(stderr, "PUSH/POP high regs:  %12ld\n", pop_high_regs - last_pop_high_regs);
    fprintf(stderr, "PUSH/POP SP:         %12ld\n", pop_sp - last_pop_sp);
    fprintf(stderr, "PUSH/POP PC/LR:      %12ld\n", pop_pc - last_pop_pc);

    fprintf(stderr, "Opcode statistics:\n");
    for (i = 0; i < 64; i++)
    {
        fprintf(stderr, "%2d: %9ld", i, primary_opcode_stats[i] - last_primary_opcode_stats[i]);
        printOpcodeCountDeltas(opcode_stats[i], last_opcode_stats[i], 16);
    }
}

void printOpcodeCountsCSV(FILE *f, u64 subcode_stats[], u32 count)
{
    u32 i;
    for (i = 0; i < count - 1; i++)
        fprintf(f, "%ld, ", subcode_stats[i]);

    fprintf(f, "%ld\n", subcode_stats[i]);
}

// Print execution statistics.
void printStatsCSV(void)
{
    u32 i;
    char filename[256];
    char overallStatsCsv[500];
 
    static int statsReportCounter = 0;
    char *str1 = "simulationStats";
    char *str2 = ".csv\0";

    sprintf(filename,"%s%d%s", str1, statsReportCounter++, str2);
    sprintf(overallStatsCsv, "%s%s%s", simulatingFilePath, "counters", ".csv");

    FILE *f = fopen(filename, "w");
    FILE *f1 = fopen(overallStatsCsv, "a");
    if (f==NULL || f1==NULL){
      fprintf(stderr, "File for writing stats can't be opened\n");
      exit(-1);
    }
 #if MEM_COUNT_INST
    fprintf(stderr, "Loads: %u\nStores: %u\nCheckpoints: %u\n", load_count, store_count, cp_count);
 #endif
    if (statsReportCounter == 1)
      fprintf(f1, "RAM_data_reads, RAM_insn_reads, RAM_writes, Flash_data_reads, Flash_insn_reads, Flash_writes,"
                  " Taken_branches, Nonword_branch_targets, Nonword taken branches, RAM prefetch hits, Flash prefetch hits, Branch_fetch_stalls,"
                  " Arbitration_conflicts, Load after load, Load after store, Store after load, Store after store,"
                  " Use after load in LD, Use after load in ST, Use after load in ALU, Use after load CMP,"
                  " Burst loads, Burst stores, BL insns, BL word aligned, BLX insns, BX insns,"
                  " PUSH/POP high regs, PUSH/POP SP, PUSH/POP PC/LR\n");
    fprintf(f1, "%12ld, %12ld, %12ld, %12ld, %12ld, %12ld,"
                " %12ld, %12ld, %12ld, %12ld, %12ld, %12ld,"
                " %12ld, %12ld, %12ld, %12ld, %12ld,"
                " %12ld, %12ld, %12ld, %12ld,"
                " %12ld, %12ld, %12ld, %12ld, %12ld, %12ld,"
                " %12ld, %12ld, %12ld\n",
                ram_data_reads, ram_insn_reads, ram_writes, flash_data_reads, flash_insn_reads, flash_writes,
                taken_branches, nonword_branch_destinations, nonword_taken_branches, ram_insn_prefetch_hits, flash_insn_prefetch_hits, branch_fetch_stalls,
                arbitration_conflicts, load_after_load, load_after_store, store_after_load, store_after_store,
                use_after_load_ld, use_after_load_st, use_after_load_alu, use_after_load_cmp,
                burst_loads, burst_stores, bl_insns, word_aligned_bl, blx_insns, bx_insns,
                pop_high_regs, pop_sp, pop_pc);

    fprintf(f, "Opcode, total_count, var1, var2, var3, var4, var5, var6, var7, var8, var9, var10, var11, var12, var13, var14, var15, var16\n");
    for (i = 0; i < 64; i++)
    {
        fprintf(f, "%2d, %9ld,", i, primary_opcode_stats[i]);
        printOpcodeCountsCSV(f, opcode_stats[i], 16);
    }
    fclose(f);
    fclose(f1);
}

// Reset CPU state in accordance with B1.5.5 and B3.2.2
void cpu_reset(void)
{
  // Initialize the special-purpose registers
  cpu.apsr = 0;       // No flags set
  cpu.ipsr = 0;       // No exception number
  cpu.espr = ESPR_T;  // Thumb mode
  cpu.primask = 0;    // No except priority boosting
  cpu.control = 0;    // Priv mode and main stack
  cpu.sp_main = 0;    // Stack pointer for exception handling
  cpu.sp_process = 0; // Stack pointer for process

  // Clear the general purpose registers
  memset(cpu.gpr, 0, sizeof(cpu.gpr));

  // Set the reserved GPRs
  cpu.gpr[GPR_LR] = 0;

  // May need to add logic to send writes and reads to the
  // correct stack pointer
  // Set the stack pointers
  simLoadData(0, &cpu.sp_main);
  cpu.sp_main &= 0xFFFFFFFC;
  cpu.sp_process = 0;
  cpu_set_sp(cpu.sp_main);

  // Set the program counter to the address of the reset exception vector
  u32 startAddr;
  simLoadData(0x4, &startAddr);
  cpu_set_pc(startAddr);

  // No pending exceptions
  cpu.exceptmask = 0;

  // Check for attempts to go to ARM mode
  if((cpu_get_pc() & 0x1) == 0)
  {
    printf("Error: Reset PC to an ARM address 0x%08X\n", cpu_get_pc());
    sim_exit(1);
  }

  // Reset the systick unit
  systick.control = 0x4;
  systick.reload = 0x0;
  systick.value = 0x0;
  systick.calib = CPU_FREQ/100 | 0x80000000;

  // Reset counters
  wastedCycles += cyclesSinceCP;
  cyclesSinceReset = 0;
  cyclesSinceCP = 0;
  wdt_val = 0;
}

void sim_command(void)
{
  // Reset the CPU
  if(do_reset != 0)
  {
    cpu_reset();
    cpu_set_pc(cpu_get_pc() + 0x4);
    do_reset = 0;
  }

  // Compute MD5 of memory
#if MD5
  if(md5[0] != 0)
  {
    printf("Computing MD5!");
    MD5_CTX c;
    unsigned char digest[16];
    int length;
    char *ptr;

    MD5_Init(&c);

    // Do RAM first
    length = RAM_SIZE-1;
    ptr = (char*) ram;
    while (length > 0) {
      if (length > 512) {
        MD5_Update(&c, ptr, 512);
      } else {
        MD5_Update(&c, ptr, length);
      }
      length -= 512;
      ptr += 512;
    }
    
    // Do flash next 
    length = FLASH_SIZE-1;
    ptr = (char*) flash;
    while (length > 0) {
      if (length > 512) {
        MD5_Update(&c, ptr, 512);
      } else {
        MD5_Update(&c, ptr, length);
      }
      length -= 512;
      ptr += 512;
    }

    //// Now low registers
    //length = 8*4;
    //ptr = (char*) cpu.gpr;
    //MD5_Update(&c, ptr, length);
    
    // PC, SP, LR
    length = 3*4;
    ptr = (char*) &(cpu.gpr[13]);
    MD5_Update(&c, ptr, length);

    MD5_Final((unsigned char*) &(md5[1]), &c);

    md5[0]=0;
  }
#endif

}


#if HOOK_GPR_ACCESSES
  void do_nothing(void){;}

  void report_sp(void)
  {
    #if 0  // This is a hard-coded value, applicable only to specific targets...
    if(cpu_get_sp() < 0X40010000)
    {
      fprintf(stderr, "SP crosses heap: 0x%8.8X\n", cpu_get_sp());
      fprintf(stderr, "PC: 0x%8.8X\n", cpu_get_pc());
    }
    #endif
  }

  void (* gprReadHooks[16])(void) = { \
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing\
  };

  void (* gprWriteHooks[16])(void) = { \
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    report_sp,\
    do_nothing\
  };
#endif

// Returns 1 if the passed list of addresses contains the passed
// address, returns 0 otherwise
char containsAddress(const ADDRESS_LIST *pList, const u32 pAddress)
{
  if(pList->address == pAddress)
    return 1;

  if(pList->next == NULL)
    return 0;

  // Tail recursion FTW
  return containsAddress(pList->next, pAddress);
}

// Adds the passed address to the end of the list
// Returns 0 if already present, 1 if added to end
char addAddress(const ADDRESS_LIST *pList, const u32 pAddress)
{
  ADDRESS_LIST *temp = pList;
  ADDRESS_LIST *temp_prev;

  // Go to the end of the list
  do
  {
    if(temp->address == pAddress)
      return 0;
    temp_prev = temp;
    temp = temp->next;
  } while(temp != NULL);

  // Create a new entry and link to it
  temp_prev->next = malloc(sizeof(ADDRESS_LIST));
  temp_prev->next->address = pAddress;
  temp_prev->next->next = NULL;

  return 1;
}

// Clears the list
void clearList(ADDRESS_LIST *pList)
{
  if(pList->next == NULL)
    return;

  clearList(pList->next);

  free(pList->next);

  pList->next = NULL;
}

// Called by branch and links
int insnsPerConflict = 0;
void reportAndReset(char pNumRegsPushed)
{
  if(!REPORT_IDEM_BREAKS)
    return;

  insnsPerConflict = cycleCount - insnsPerConflict;
  fprintf(stderr, "%d,%d,%d,%d,%d,%d,%d\n", addressWrites, addressReads, addressConflicts, addressConflicts - addressConflictsStack, insnsPerConflict, pNumRegsPushed, cpu_get_pc());
  addressConflicts = 0;
  addressConflictsStack = 0;
  addressWrites = 0;
  addressReads = 0;
  insnsPerConflict = cycleCount;
  clearList(&addressConflictList);
  clearList(&addressReadBeforeWriteList);
  clearList(&addressWriteBeforeReadList);
}

// MRU/LRU queue management: MRU always at index 0.
void prefetch_set_mru(u32 index)
{
  int i;

  if (prefetch_mru_q[0] == index)
    // If index is already MRU, there's nothing to do.
    return;

  if (prefetch_mru_q[1] == index)
  {
    // Index in position 1: swap values at 0 and 1.
    prefetch_mru_q[1] = prefetch_mru_q[0];
    prefetch_mru_q[0] = index;
  }

  if (prefetch_mru_q[2] == index)
  {
    // Index in position 2: rotate the buffer to the right.
    prefetch_mru_q[2] = prefetch_mru_q[1];
    prefetch_mru_q[1] = prefetch_mru_q[0];
    prefetch_mru_q[0] = index;
  }
}

// Memory access functions assume that RAM has a higher address than Flash
char simLoadInsn(u32 address, u16 *value)
{
  u32 fromMem;

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      fprintf(stderr, "Error: ILR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    if(REPORT_IDEM_BREAKS)
    {
      // Add addresses to the read list if they weren't written to first
      if(!containsAddress(&addressWriteBeforeReadList, address))
        addressReads += addAddress(&addressReadBeforeWriteList, address);
    }
    // TODO: Add support of RAM word buffer.
    if (prefetch_mode == PREFETCH_MODE_WORD || prefetch_mode == PREFETCH_MODE_BUFFER)
    {
      if ((address & ~0x3) == last_fetched_address)
      {
        // Request address corresponds to the last fetched word.
        fromMem = last_fetched_word;
        if (tracingActive && logAllEvents)
          ram_insn_prefetch_hits++;
      }
      else
      {
        // Request address does not match the last fetched word.
        if (tracingActive && logAllEvents)
        {
          ram_insn_reads++;
          ram_access = 1;
          if (data_access_in_cur_cycle)
            arbitration_conflicts++;
        }
        fromMem = ram[(address & RAM_ADDRESS_MASK) >> 2];
          last_fetched_address = address & ~0x3;
        last_fetched_word = fromMem;
      }
    }
    else  // Neither PREFETCH_MODE_WORD nor PREFETCH_MODE_BUFFER
    {
      // Original behavior: Plain load from RAM on every instruction.
      fromMem = ram[(address & RAM_ADDRESS_MASK) >> 2];
      if (tracingActive && logAllEvents)
      {
        ram_insn_reads++;
        ram_access = 1;
        if (data_access_in_cur_cycle)
          arbitration_conflicts++;
      }
    }
  }
  else  // Not a RAM access, so it should be inside Flash address range.
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: ILF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    if (prefetch_mode == PREFETCH_MODE_WORD)
    {
      if ((address & ~0x3) == last_fetched_address)
      {
        // Request address corresponds to the last fetched word.
        fromMem = last_fetched_word;
        if (tracingActive && logAllEvents)
          flash_insn_prefetch_hits++;
      }
      else
      {
        // Request address does not match the last fetched word.
        if (tracingActive && logAllEvents)
        {
          flash_insn_reads++;
          flash_access = 1;
          if (data_access_in_cur_cycle)
            arbitration_conflicts++;
        }
        fromMem = flash[(address & FLASH_ADDRESS_MASK) >> 2];
        last_fetched_address = address & ~0x3;
        last_fetched_word = fromMem;
      }
    }
    else if (prefetch_mode == PREFETCH_MODE_BUFFER)
    {
      int i;
      bool is_a_hit = 0;
      // Compare address against the content of tags.
      for (i = 0 ; i < 3; i++)
      {
        if ((address & ~0x3) == prefetch_addresses[i])
        {
          // Address was found in tags: use the corresponding PF buffer word
          // update the MRU queue and the hit counter.
          fromMem = prefetch_words[i];
          prefetch_set_mru(i);
          is_a_hit = 1;
          if (tracingActive && logAllEvents)
            flash_insn_prefetch_hits++;
          break;
        }
      }

      // If word is not in PF, we need to fetch it.
      if (!is_a_hit)
      {
        // Evict the LRU entry from the PF buffer.
        u32 victim = prefetch_mru_q[2];
        prefetch_set_mru(victim);
        fromMem = flash[(address & FLASH_ADDRESS_MASK) >> 2];
        prefetch_words[victim] = fromMem;
        prefetch_addresses[victim] = address & ~0x3;
        if (tracingActive && logAllEvents)
        {
          flash_insn_reads++;
          flash_access = 1;
          if (data_access_in_cur_cycle)
            arbitration_conflicts++;
        }
      }
    }
    else  // Neither PREFETCH_MODE_WORD nor PREFETCH_MODE_BUFFER
    {
      // Original behavior: Plain load from flash on every instruction.
      fromMem = flash[(address & FLASH_ADDRESS_MASK) >> 2];
      if (tracingActive && logAllEvents)
      {
        flash_insn_reads++;
        flash_access = 1;
        if (data_access_in_cur_cycle)
          arbitration_conflicts++;
      }
    }
  }

  // Data 32-bits, but instruction 16-bits
  *value = ((address & 0x2) != 0) ? (u16)(fromMem >> 16) : (u16)fromMem;

  return 0;
}

// Normal interface for a program to load data from memory
// Increments data load counter
char simLoadData(u32 address, u32 *value)
{
  #if MEM_COUNT_INST
    ++load_count;
  #endif
  return simLoadData_internal(address, value, 0);
}

char simLoadData_internal(u32 address, u32 *value, u32 falseRead)
{
  #if MEM_CHECKS
    if((address & 0x3) != 0)
    {
      fprintf(stderr, "Unalinged data memory read: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
        *value = 0;
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01)
      {
        *value = ((u32 *)&systick)[(address >> 2) & 0x3];
        if(address == 0xE000E010)
          systick.control &= 0x00010000;

        return 0;
      }

      // Check for general GPIO.
      if (address >= MEMMAPIO_START && address < MEMMAPIO_START + MEMMAPIO_SIZE)
      {
        fprintf(stderr,  "WARNING: arbitrary GPIO read: 0x%8.8X, pc=%x, returning 0...\n", address, cpu_get_pc());
        *value = 0;
        return 0;
      }

        // Check for general Cortex-M0+ peripheral access.
      if (address >= M0PLUSPERIPHS_START && address < M0PLUSPERIPHS_START + M0PLUSPERIPHS_SIZE)
      {
        fprintf(stderr,  "WARNING: arbitrary Cortex-M0+ peripherals read: 0x%8.8X, pc=%x, returning 0...\n", address, cpu_get_pc());
        *value = 0;
        return 0;
      }

      fprintf(stderr, "Error: LoadData_internal DLR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    // Implicitly (but confirmed by flow analysis) at this point we have 'address < (RAM_START + RAM_SIZE)'.
    // Add addresses to the read list if they weren't written to first
    if(REPORT_IDEM_BREAKS && !falseRead)
    {
      if(!containsAddress(&addressWriteBeforeReadList, address))
        addressReads += addAddress(&addressReadBeforeWriteList, address);
    }
    if ((tracingActive && logAllEvents) && !falseRead)
    {
      ram_data_reads++;
      ram_access = 1;
      data_access_in_three_cycles = 1;
    }
    *value = ram[(address & RAM_ADDRESS_MASK) >> 2];

#if PRINT_MEM_OPS
    if(!falseRead)
      printf("%llu\t%llu\tR\t%8.8X\t%d\n", cycleCount, insnCount, address, *value);
#endif

#if PRINT_ALL_MEM
    if(!falseRead)
      fprintf(stderr, "%8.8X: Ram read at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, *value);
#endif
  }
  else
  {
    // Here we assume FLASH_START < RAM_START...
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DLF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }
    if ((tracingActive && logAllEvents) && !falseRead)
    {
      flash_data_reads++;
      flash_access = 1;
      data_access_in_three_cycles = 1;
    }
    *value = flash[(address & FLASH_ADDRESS_MASK) >> 2];

#if PRINT_MEM_OPS
    if(!falseRead)
      printf("%llu\t%llu\tR\t%8.8X\t%d\n", cycleCount, insnCount, address, *value);
#endif
      
#if PRINT_ALL_MEM
    if(!falseRead)
      fprintf(stderr, "%8.8X: Flash read at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, *value);
#endif
  }

  return 0;
}

char simStoreData(u32 address, u32 value)
{
  unsigned int word;

#if MEM_CHECKS
    if((address & 0x3) != 0) // Thumb-mode requires LSB = 1
    {
      fprintf(stderr, "Unalinged data memory write: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }
#endif

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
#if !DISABLE_PROGRAM_PRINTING
        printf("%c", value & 0xFF);
        fflush(stdout);
#endif
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01 && address != 0xE000E01C)
      {
        if(address == 0xE000E010)
        {
          systick.control = (value & 0x1FFFD) | 0x4; // No external tick source, no interrupt
          if(value & 0x2)
            fprintf(stderr, "ERROR: SYSTICK interrupts not implemented...ignoring\n");
        }
        else if(address == 0xE000E014)
          systick.reload = value & 0xFFFFFF;
        else if(address == 0xE000E018)
          systick.value = 0; // Reads clears current value

        return 0;
      }

      // Check for cycle count
      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START + MEMMAPIO_MAPPEDSIZE))
      {
        word = *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]);
        word &= ~(0xff << (8*(address%4)));
        word |= (value << (8*(address%4)));
        *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]) = word;

        // If the variable updated is a request to reset, then do it
        if(do_reset != 0)
        {
          cpu_reset();
          cpu_set_pc(cpu_get_pc() + 0x4);
          do_reset = 0;
        }

        return 0;
      }

// Shortcuts for GPIO control register offsets
#define GPIOB_BSRR 0x08000418
#define GPIOB_BRR  0x08000428
#define GPIOC_BSRR 0x08000818
#define GPIOC_BRR  0x08000828

      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START + MEMMAPIO_SIZE))
      {
        // TeamPlay specific: Raising/clearing GPIOB[0]/GPIOC[0] starts/stops event counting
        // and produces a cycle and insn count message on console.
        // On CameraPill, the trigger pin is GPIOB[0].
        // On F0-Discovery and Nucleo-F0 boards the trigger pin is GPIOC[0].
        if ((value == 0x1 && address == (MEMMAPIO_START + GPIOB_BSRR))
            || (value == 0x1 && address == (MEMMAPIO_START + GPIOC_BSRR)))
        {
          // Write 0x1 to GPIOB_BSRR[0] (resp. GPIOC_BSRR[0]): Set the GPIOB[0] (resp. GPIOC[0]) pin.
          fprintf(stderr, "TeamPlay: trigger pin raised at cycle %lld, insn count %lld, pc = %x\n", cycleCount, insnCount, cpu_get_pc());
          // Start the logging of events.
          if (logAllEvents)
          {
            tracingActive = 1;
            if (useCSVoutput)
              printStatsCSV();
            else
              saveStats();
          }
          return 0;
        }
        else if ((value == 0x1
                  && (address == (MEMMAPIO_START + GPIOB_BRR)
                      || address == (MEMMAPIO_START + GPIOC_BRR)))
                 || (value == 0x00010000
                     && (address == (MEMMAPIO_START + GPIOB_BSRR))
                         || address == (MEMMAPIO_START + GPIOC_BSRR)))
        {
          // Write 0x1 to GPIOB_BRR[0]/GPIOC or 0x1 to GPIOB_BSRR[0]/GPIOC_BSRR[16]: clear the GPIOB[0]/GPIOC[0] pin.
          fprintf(stderr, "TeamPlay: trigger pin cleared at cycle %lld, insn count %lld, pc = %x\n", cycleCount, insnCount, cpu_get_pc());
          if (tracingActive && logAllEvents)
          {
            // Stop the logging of events.
            tracingActive = 0;
            if (useCSVoutput)
              printStatsCSV();
            else
	            // Print differential statistics.
              printStatsDelta();
          }
	        else if (logAllEvents)
	          printStats();
          return 0;
        }

        fprintf(stderr, "WARNING: Writing to MMIO space: 0x%08x@0x%8.8X, pc=%x, operation IGNORED\n", value, address, cpu_get_pc());
        return 0;

      }

      // Check for general Cortex-M0+ peripheral access.
      if (address >= M0PLUSPERIPHS_START && address < M0PLUSPERIPHS_START + M0PLUSPERIPHS_SIZE)
      {
        fprintf(stderr,  "WARNING: arbitrary Cortex-M0+ peripherals write: 0x%08x@0x%8.8X, pc=%x, operation IGNORED\n", value, address, cpu_get_pc());
        return 0;
      }

      fprintf(stderr, "Error: DStore Memory access out of range: 0x%08x@0x%8.8X, pc=%x\n", value, address, cpu_get_pc());
      sim_exit(1);
    }

    if(REPORT_IDEM_BREAKS && address != IGNORE_ADDRESS)
    {
      // Conflict if we are writting to an address that was read
      // from before being written to
      if(containsAddress(&addressReadBeforeWriteList, address))
      {
        fprintf(stderr, "Error: Idempotency violation: address=0x%8.8X, pc=0x%8.8X\n", address, cpu_get_pc());
        int addressConflictsOrig = addressConflicts;

        addressConflicts += addAddress(&addressConflictList, address);
        if(address >= (cpu_get_sp() - 0x20) /*0x40001C00*/ && addressConflicts != addressConflictsOrig)
          ++addressConflictsStack;
      }
      // Only track new write addresses that were not read from first
      else
        addressWrites += addAddress(&addressWriteBeforeReadList, address);
    }

    rsp_check_watch(address);

    #if PRINT_RAM_WRITES || PRINT_ALL_MEM
      fprintf(stderr, "%8.8X: Ram write at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, value);
    #endif

    #if PRINT_MEM_OPS
      printf("%llu\t%llu\tW\t%8.8X\t%d\t%d\n", cycleCount, insnCount, address, ram[(address & RAM_ADDRESS_MASK) >> 2], value);
    #endif

    ram[(address & RAM_ADDRESS_MASK) >> 2] = value;
    if (tracingActive && logAllEvents)
    {
      ram_writes++;
      data_access_in_three_cycles = 1;
      // ram_access = 1;
    }

    #if MEM_COUNT_INST
      ++store_count;
    #endif
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DSF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    rsp_check_watch(address);

    #if PRINT_FLASH_WRITES || PRINT_ALL_MEM
      fprintf(stderr, "%8.8X: Flash write at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, value);
    #endif

    #if PRINT_MEM_OPS
      printf("%llu\t%llu\tW\t%8.8X\t%d\t%d\n", cycleCount, insnCount, address, flash[(address & FLASH_ADDRESS_MASK) >> 2], value);
    #endif
      
    flash[(address & FLASH_ADDRESS_MASK) >> 2] = value;
    if (tracingActive && logAllEvents)
    {
      flash_writes++;
      flash_access = 1;
      data_access_in_three_cycles = 1;
    }

    #if MEM_COUNT_INST
      ++store_count;
    #endif
  }

  return 0;
}

//
// Code below here is used by GDB interface
//

char simDebugRead(u32 address, unsigned char* value)
{
  unsigned int word;

  #if MEM_CHECKS
    if((address & 0x3) != 0)
    {
      fprintf(stderr, "Unalinged data memory read: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif
    
  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
        *value = 0;
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01)
      {
        *value = ((u32 *)&systick)[(address >> 2) & 0x3];
        if(address == 0xE000E010)
          systick.control &= 0x00010000;

        return 0;
      }

      // Check for cycle count
      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START + MEMMAPIO_MAPPEDSIZE))
      {
        //word = *((u32 *) (((void*)mmio)+((address & 0xfffffffc)-MEMMAPIO_START))) & 0xffffffff;
        word = *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]);
        //if(address < (MEMMAPIO_START+4))
        //  word = cyclesSinceReset & 0xffffffff;
        //else if(address < (MEMMAPIO_START+8))
        //  word = resetAfterCycles & 0xffffffff;
        //else if(address < (MEMMAPIO_START+12))
        //  word = cycleCount & 0xffffffff;
        //else if(address < (MEMMAPIO_START+16))
        //  word = (cycleCount >> 32) & 0xffffffff;

        *value = (word >> (8*(address%4)));
        return 0;
      }

      fprintf(stderr, "Error: DebugRead DSR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = ram[(address & RAM_ADDRESS_MASK) >> 2]; 
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DSF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = flash[(address & FLASH_ADDRESS_MASK) >> 2];
  }

  *value = (word >> (8*(address %4))) & 0xff;

  return 0;
}

char simDebugWrite(u32 address, unsigned char value)
{
  unsigned int word;

  #if MEM_CHECKS
    if((address & 0x3) != 0)
    {
      fprintf(stderr, "Unalinged data memory read: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01)
      {
        if(address == 0xE000E010)
          systick.control &= 0x00010000;

        return 0;
      }

      // Check for cycle count
      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START+MEMMAPIO_MAPPEDSIZE))
      {
        word = *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]);
        word &= ~(0xff << (8*(address%4)));
        word |= (value << (8*(address%4)));
        *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]) = word;

        sim_command();

        return 0;
      }
      else if (address >= MEMMAPIO_START && address < (MEMMAPIO_START + MEMMAPIO_SIZE))
      {
        fprintf(stderr, "WARNING: Writing to MMIO space: 0x%8.8X, pc=%x, operation IGNORED\n");
        return 0;
      }

      fprintf(stderr, "Error: DSR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = ram[(address & RAM_ADDRESS_MASK) >> 2]; 
    word &= ~(0xff << (8*(address%4)));
    word |= (value << (8*(address%4)));
    ram[(address & RAM_ADDRESS_MASK) >> 2] = word; 
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DLF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = flash[(address & FLASH_ADDRESS_MASK) >> 2];
    word &= ~(0xff << (8*(address%4)));
    word |= (value << (8*(address%4)));
    flash[(address & FLASH_ADDRESS_MASK) >> 2] = word;
  }

  return 0;
}

char simValidMem(u32 address)
{
  if((address >= RAM_START && address <= (RAM_START+RAM_SIZE)) ||
      (address >= FLASH_START && address <= (FLASH_START+FLASH_SIZE))||
      (address >= MEMMAPIO_START && address < MEMMAPIO_START+MEMMAPIO_SIZE))
    return 1;
  else
    return 0;
}

