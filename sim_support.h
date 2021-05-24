#ifndef SIMSUPPORT_HEADER
#define SIMSUPPORT_HEADER

#include <stdio.h>

#define RAM_START           0x20000000
#define RAM_SIZE            (1 << 16) // 64KiB
#define RAM_ADDRESS_MASK    (((~0) << 16) ^ (~0))
#define FLASH_START         0x08000000
#define FLASH_SIZE          (1 << 20) // 1 MiB
#define FLASH_ADDRESS_MASK  (((~0) << 20) ^ (~0))
#define CPU_FREQ            48000000
#define MEMMAPIO_START      0x40000000
#define MEMMAPIO_MAPPEDSIZE (4*18)      // Size of actual memory image
#define MEMMAPIO_SIZE       0x08001800  // Size of MMIO address window in memory map
#define M0PLUSPERIPHS_START 0xE0000000
#define M0PLUSPERIPHS_SIZE  0x00100000
#define WATCHPOINT_ADDR     0x40000010

typedef __uint32_t u32;
typedef __uint64_t u64;
typedef __int32_t i32;
typedef __int64_t i64;
typedef __uint8_t u8;
typedef __uint16_t u16;
typedef char bool;

// Core CPU compenents
extern u32 ram[RAM_SIZE >> 2];
extern u32 flash[FLASH_SIZE >> 2];

// Performance counter start/stop PC values.
extern u32 trace_start_pc;
extern u32 trace_stop_pc;

// Performance counters
extern u64 ram_data_reads;
extern u64 ram_insn_reads;
extern u64 ram_writes;
extern u64 flash_data_reads;
extern u64 flash_insn_reads;
extern u64 flash_writes;
extern u64 taken_branches;
extern u64 nonword_branch_destinations;
extern u64 nonword_branch_insns;
extern u64 nonword_taken_branches;
extern bool branch_fetch_stall; // Boolean to represent branch-induced stall/cancellation condition
extern bool ram_access; // Boolean to mark a RAM request in the current decode cycle
extern bool flash_access; // Boolean to mark a Flash request in the current decode cycle
extern u64 arbitration_conflicts; // Count of potential RAM/Flash arbitration conflicts
extern u64 branch_fetch_stalls; // Count of branch-induced fetch delays (caused by stalls and/or cancellations)
extern bool data_access_in_cur_cycle, data_access_in_next_cycle, data_access_in_two_cycles, data_access_in_three_cycles;
extern bool load_in_cur_insn, load_in_prev_insn, store_in_cur_insn, store_in_prev_insn, cmp_in_cur_insn;
extern char reg_loaded_in_cur_insn, reg_loaded_in_prev_insn;
extern bool use_after_load_seen;
extern bool store_addr_reg_load_in_prev_insn;
extern u64 load_after_load, load_after_store, store_after_load, store_after_store;
extern u64 use_after_load_ld, use_after_load_st, use_after_load_alu, use_after_load_cmp;
extern u64 burst_loads, burst_stores;
extern u64 bl_insns, blx_insns, bx_insns;
extern u64 pop_high_regs, pop_sp, pop_pc;
extern u64 word_aligned_bl;

#define SHIFT_ISSUED_DATA_ACCESSES \
  { \
    data_access_in_cur_cycle = data_access_in_next_cycle; \
    data_access_in_next_cycle = data_access_in_two_cycles; \
    data_access_in_two_cycles = data_access_in_three_cycles; \
    data_access_in_three_cycles = 0; \
  }
#define FLUSH_ISSUED_DATA_ACCESSES \
{ \
  data_access_in_cur_cycle = 0; \
  data_access_in_next_cycle = 0; \
  data_access_in_two_cycles = 0; \
  data_access_in_three_cycles = 0; \
}
// Prefetch buffering: none, 1-word direct-associative, or 3-word direct-associative buffer
#define PREFETCH_MODE_NONE 0
#define PREFETCH_MODE_WORD 1
#define PREFETCH_MODE_BUFFER 2
extern u32 prefetch_mode;
// Enable differential results in start-stop mode.
extern bool doTrace;
extern bool tracingActive;
extern bool logAllEvents;
extern bool useCSVoutput;
extern char *simulatingFilePath;

extern bool takenBranch;    // Informs fetch that previous instruction caused a control flow change
extern void sim_exit(int);  // All sim ends lead through here
void cpu_reset();           // Resets the CPU according to the specification
char simLoadInsn(u32 address, u16 *value);  // All memory accesses one simulation starts should be through these interfaces
char simLoadData(u32 address, u32 *value);
char simLoadData_internal(u32 address, u32 *value, u32 falseRead); // falseRead says whether this is a read due to anything other than the program
char simStoreData(u32 address, u32 value);

// Controls whether the program output prints to the simulator's console or is not printed at all
#define DISABLE_PROGRAM_PRINTING 1

// Simulator debugging
#define PRINT_INST 1                                    // diss_printf(): disassembly printing?
#define PRINT_ALL_STATE 0                               // Print all registers after each instruction? Used for comparing to original Thumbulator.
#define PRINT_STATE_DIFF_INIT (0 & (PRINT_ALL_STATE))   // Print changed registers after each instruction?
#define PRINT_STORES_WITH_STATE (0 & (PRINT_ALL_STATE)) // Print memory written with state updates?
#define PRINT_ALL_MEM 0                                 // Print all memory accesses?
#define PRINT_FLASH_WRITES 0                            // Print all writes to flash?
#define PRINT_RAM_WRITES 0                              // Print all writes to ram?

// Simulator correctness checks: tradeoff speed for safety
#define MEM_CHECKS 0                                    // Check memory access alignment
#define VERIFY_BRANCHES_TAGGED 1                        // Make sure that all control flow changes come from known paths
#define THUMB_CHECK 1                                   // Verify that the PC stays in thumb mode

#define diff_printf(format, ...) do{ fprintf(stderr, "%08X:\t", cpu_get_pc() - 0x5); fprintf(stderr, format, __VA_ARGS__); } while(0)
#define diss_printf(format, ...) do{ if (PRINT_INST && (tracingActive || logAllEvents) && doTrace) { fprintf(stderr, "%08X:\t", cpu_get_pc() - 0x5); fprintf(stderr, format, __VA_ARGS__); } } while(0)

// Hooks to run code every time a GPR is accessed
#define HOOK_GPR_ACCESSES 1         // Currently set to see if stack crosses heap

// Macros for Ratchet
#define PRINT_CHECKPOINTS 0                 // Print checkpoint info
#define MEM_COUNT_INST 0                    // Track and report program loads, stores, and checkpoints
#define PRINT_MEM_OPS 0                     // Prints detailed info for each program-generated memory access (Clank)
#define INCREMENT_CYCLES(x) {\
  cycleCount += x;           \
  cyclesSinceReset += x;     \
  cyclesSinceCP += x;        \
  if(wdt_seed != 0) {        \
    wdt_val +=x;             \
    if(wdt_val >= wdt_seed) {\
      wdt_val = 0;           \
      cpu_set_except(16);    \
    }                        \
  }                          \
}

// Macros for Clank
#define REPORT_IDEM_BREAKS 0
#define IGNORE_ADDRESS 0x40000000
void reportAndReset(char pNumRegsPushed);   // Reports on status of current idempotent section, then clears buffers
struct ADDRESS_LIST {
    u32 address;
    struct ADDRESS_LIST *next;
};
typedef struct ADDRESS_LIST ADDRESS_LIST;


extern u64 cycleCount;
extern u64 insnCount;
extern u32 cyclesSinceReset;
extern u32 resetAfterCycles;
extern u64 wastedCycles;
extern u32 cyclesSinceCP;
extern u32 addrOfCP;
extern u32 addrOfRestoreCP;
extern u32 do_reset;
extern u32 wdt_val;
extern u32 wdt_seed;
extern u32 PRINT_STATE_DIFF;
#if MEM_COUNT_INST
  extern u32 store_count;
  extern u32 load_count;
  extern u32 cp_count;
#endif
#if HOOK_GPR_ACCESSES
    extern void do_nothing(void);
    extern void report_sp(void);
    extern void (* gprReadHooks[16])(void);
    extern void (* gprWriteHooks[16])(void);
#endif
extern char simValidMem(u32 address); // Interface for rsp (GDB) server
extern void saveStats(void);
extern void printStats(void);
extern void printStatsCSV(void);
extern void printStatsDelta(void);

//struct MEMMAPIO {
//  u32 *cycleCountLSB;
//  u32 *cycleCountMSB;
//  u32 *cyclesSince;
//  u32 *resetAfter;
//
//  u32 *do_reset;
//  u32 *do_logging;
//};
//typedef struct MEMMAPIO MEMMAPIO;

#endif
