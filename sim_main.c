#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "exmemwb.h"
#include "except.h"
#include "decode.h"
#include "rsp-server.h"
#include "sim_support.h"

char *simulatingFilePath = 0;
// Used to track resource usage and execution time.
// The end values are set (and the differences computed)
// in function 'b()' in exmemwb_branch.c.
struct timeval start_time, end_time;
struct rusage start_rusage, end_rusage;

// Load a program into the simulator's RAM
static void fillState(const char *pFileName)
{
    FILE *fd;
    int res;

    fd = fopen(pFileName, "r");
    
    // Check that the file opened correctly
    if(fd == NULL)
    {
        fprintf(stderr, "Error: Could not open file %s\n", pFileName);
        sim_exit(1);
    }

    res = fread(&flash, sizeof(u32), sizeof(flash)/sizeof(u32), fd);
    if (res < 0)
    {
      perror("fillState");
      exit(1);
    }

    fclose(fd);
}

//Read the simulated file path
char* getPath(char *filename) {
  char *ssc;
  char *path;
  int file_length = 0;
  int path_length = 0;
  ssc = strrchr(filename, '/');
  file_length = strlen(ssc);
  path_length = strlen(filename);
  path = (char*) malloc((path_length - file_length + 2)*sizeof(char)); /* +1 for '\0' character */
  if (path == NULL){
    printf("Failed to allocate string in getPath. Exiting...\n");
    exit(-1);
  }
  strncpy(path,filename, path_length - file_length);
  path[path_length - file_length] = '/';
  path[path_length - file_length+1] = '\0';
  return path;
}

struct CPU cpu;
struct SYSTICK systick;

void printStateDiff(const struct CPU * pState1, const struct CPU * pState2)
{
    int reg;
    for (reg = 0; reg < 13; ++reg)
    {
        if(pState1->gpr[reg] != pState2->gpr[reg])
            diff_printf("r%d:\t%8.8X to %8.8X\n", reg, pState1->gpr[reg], pState2->gpr[reg]);
    }
    
    if(pState1->gpr[13] != pState2->gpr[13])
        diff_printf("SP:\t%8.8X to %8.8X\n", pState1->gpr[13], pState2->gpr[13]);
    
    if(pState1->gpr[14] != pState2->gpr[14])
        diff_printf("LR:\t%8.8X to %8.8X\n", pState1->gpr[14], pState2->gpr[14]);
    
    if(pState1->gpr[15] != pState2->gpr[15])
        diff_printf("PC:\t%8.8X to %8.8X\n", pState1->gpr[15], pState2->gpr[15]);

    if((pState1->apsr & FLAG_Z_MASK) != (pState2->apsr & FLAG_Z_MASK))
        diff_printf("Z:\t%d\n", cpu_get_flag_z());
    if((pState1->apsr & FLAG_N_MASK) != (pState2->apsr & FLAG_N_MASK))
        diff_printf("N:\t%d\n", cpu_get_flag_n());
    if((pState1->apsr & FLAG_C_MASK) != (pState2->apsr & FLAG_C_MASK))
        diff_printf("C:\t%d\n", cpu_get_flag_c());
    if((pState1->apsr & FLAG_V_MASK) != (pState2->apsr & FLAG_V_MASK))
        diff_printf("V:\t%d\n", cpu_get_flag_v());
}

void printState()
{
    int reg;
    
    for(reg = 0; reg < 13; ++reg)
        printf("R%d:\t%08X\n", reg, cpu.gpr[reg]);

    printf("R%d:\t%08X\n", 13, cpu.gpr[13] & 0xFFFFFFFC);
    printf("R%d:\t%08X\n", 14, cpu.gpr[14] & 0xFFFFFFFC);
    printf("Z:\t%d\n", cpu_get_flag_z());
    printf("N:\t%d\n", cpu_get_flag_n());
    printf("C:\t%d\n", cpu_get_flag_c());
    printf("V:\t%d\n", cpu_get_flag_v());
}

void sim_exit(int i)
{
#ifdef __linux__
    long long wall_secs, user_secs, system_secs, wall_usecs, user_usecs, system_usecs;

    // Determine time and resource usage at end of simulation.
    // Get time information first as it is duration-sensitive
    // and has a low resource usage.
    if (gettimeofday(&end_time, NULL) < 0)
      perror("b(): gettimeofday");
    if (getrusage(RUSAGE_SELF, &end_rusage) < 0)
      perror("b(): getrusage");

    // wallclock time: struct timeval difference (tv_sec, tv_usec fields)
    // user time: struct rusage field ru_utime (itself a struct timeval)
    // system time: struct rusage field ru_stime (itself a struct timeval)

#define DELTA_TIME(START_TVAL,END_TVAL,SECS,USECS)      \
    SECS = (END_TVAL).tv_sec - (START_TVAL).tv_sec;     \
    USECS = (END_TVAL).tv_usec - (START_TVAL).tv_usec;  \
    if (USECS < 0)                                      \
    {                                                   \
        (SECS)--;                                       \
        (USECS) += 1000000;                             \
    }

    DELTA_TIME(start_time, end_time, wall_secs, wall_usecs);
    DELTA_TIME(start_rusage.ru_utime, end_rusage.ru_utime, user_secs, user_usecs);
    DELTA_TIME(start_rusage.ru_stime, end_rusage.ru_stime, system_secs, system_usecs);

    // Print performance information.
    fprintf(stderr,
            "Simulation speed:\n%12.1f ticks/s\n%12.1f insns/s\n%10d.%06ds elapsed\n%10d.%06ds user\n%10d.%06ds system\n",
            ((float)cycleCount) / ((float)wall_secs + wall_usecs / 1000000.0),
            ((float)insnCount) / ((float)wall_secs + wall_usecs / 1000000.0),
            wall_secs, wall_usecs,
            user_secs, user_usecs,
            system_secs, system_usecs);
#endif
    if (cpu.debug)
    {
      rsp.stalled = cpu.debug;
      rsp_trap();

      if (i != 0)
        fprintf(stderr, "Simulator exiting due to error...\n");

      while (rsp.stalled)
        handle_rsp();
    }

    exit(i);
}


int main(int argc, char *argv[])
{
    char *file = 0;
    int debug = 0;
    long int value;
    char *value_end;
    int i;

    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s [-g] [-pw|-pb] [-t|-s] [--from-pc ADDR] [--to-pc ADDR] memory_file\n", argv[0]);
        return 1;
    }

    // Name of the binary file must come last on command line
    file = argv[argc - 1];
    for (i = 1; i < argc - 1; /* increments in each branch...  */)
    {
      if(0 == strncmp("-g", argv[i], strlen("-g")))
      {
        // Enable debugging.
        debug = 1;
        i++;
      }
      else if (0 == strcmp("-pw", argv[i]))
      {
        // Prefetch mode: WORD (single 32-bit word).
        prefetch_mode = PREFETCH_MODE_WORD;
        i++;
      }
      else if (0 == strcmp("-pb", argv[i]))
      {
        // Prefetch mode: BUFFER (three 32-bit words)
        prefetch_mode = PREFETCH_MODE_BUFFER;
        i++;
      }
      else if (0 == strcmp("-s",argv[i]))
      {
        // SUMMARY mode: Disable trace, enable differential event reporting.
        doTrace = 0;
        logAllEvents = 0;
        i++;
      }
      else if (0 == strcmp("-t", argv[i]))
      {
        // Enable trace, disable differential event reporting.
        doTrace = 1;
        i++;
      }
      else if (0 == strcmp("-c", argv[i]))
      {
        // Enable CSV output.
        useCSVoutput = 1;
        i++;
      }
      else if (0 == strcmp("--from-pc", argv[i]))
      {
        if (i == argc - 2)
        {
          // No argument for sure.
          fprintf(stderr, "*** Argument to '--from-pc' missing, aborting!\n");
          exit(2);
        }

        // Use a "universal" conversion function.
        value = strtol(argv[i+1], &value_end, 0);
        if (value_end - argv[i+1] < strlen(argv[i+1]))
        {
          fprintf(stderr, "*** Argument '%s' following '--from-pc' is not a valid number, aborting!\n", argv[i+1]);
          exit(2);
        }

        if (value < 0L)
        {
          fprintf(stderr, "*** Negative value %ld passed as argument to '--from-pc', aborting!\n", value);
          exit(2);
        }

        // ARM PC values are always even in dumps, but Thumb mode uses bit 0 as Thumb mode marker.
        // The highest supported value is 0xfffffffe, and we must add the Thumb marker once the
        // address is deemed valid.
        if (value > 0xfffffffe)
        {
          fprintf(stderr, "*** Value 0x%lx passed as argument to '--from-pc' is too large, aborting!\n", value);
          exit(2);
        }
        trace_start_pc = (u32) (value + 1);
        i += 2;
      }
      else if (0 == strcmp("--to-pc", argv[i]))
      {
        if (i == argc - 2)
        {
          // No argument for sure.
          fprintf(stderr, "*** Argument to '--to-pc' missing, aborting!\n");
          exit(2);
        }

        // Use a "universal" conversion function.
        value = strtol(argv[i+1], &value_end, 0);
        if (value_end - argv[i+1] < strlen(argv[i+1]))
        {
          fprintf(stderr, "*** Argument '%s' following '--to-pc' is not a valid number, aborting!\n", argv[i+1]);
          exit(2);
        }

        if (value < 0L)
        {
          fprintf(stderr, "*** Negative value %ld passed as argument to '--to-pc', aborting!\n", value);
          exit(2);
        }

        if (value > 0xfffffffe)
        {
          fprintf(stderr, "*** Value 0x%lx passed as argument to '--to-pc' is too large, aborting!\n", value);
          exit(2);
        }
        trace_stop_pc = (u32) (value + 1);
        i += 2;
      }
      else
      {
        fprintf(stderr, "Unknown option '%s'\n",argv[i]);
        return 1;
      }
    }

    fprintf(stderr, "Simulating file %s\n", file);
    fprintf(stderr, "Flash start:\t0x%8.8X\n", FLASH_START);
    fprintf(stderr, "Flash end:\t0x%8.8X\n", (FLASH_START + FLASH_SIZE));
    fprintf(stderr, "Ram start:\t0x%8.8X\n", RAM_START);
    fprintf(stderr, "Ram end:\t0x%8.8X\n", (RAM_START + RAM_SIZE));

    // Reset memory, then load program to memory
    memset(ram, 0, sizeof(ram));
    memset(flash, 0, sizeof(flash));
    fillState(file);
    if (useCSVoutput)
      simulatingFilePath = getPath(file);
    
    // Initialize CPU state
    cpu_reset();
    cpu.debug = debug;
    
    // PC seen is PC + 4
    cpu_set_pc(cpu_get_pc() + 0x4);

    if(cpu.debug){
    rsp_init();
    while(rsp.stalled)
      handle_rsp();
    }

    // Execute the program
    // Simulation will terminate when it executes insn == 0xBFAA or jump-to-self.
    bool addToWasted = 0;

    // Track execution time and resource usage
    if (getrusage(RUSAGE_SELF, &start_rusage) < 0)
      perror("sim_main: getrusage");
    if (gettimeofday(&start_time, NULL) < 0)
      perror("sim_main:gettimeofday");

    while(1)
    {
        struct CPU lastCPU;
        u32 fetch_address;
        
        u16 insn;
        takenBranch = 0;
        branch_fetch_stall = 0;
        ram_access = 0;
        flash_access = 0;
        // Advance the data access pipeline model
        SHIFT_ISSUED_DATA_ACCESSES;
        
        if(PRINT_ALL_STATE)
        {
            printf("%08X\n", cpu_get_pc() - 0x3);
            printState();
        }

        // Backup CPU state
        //if(PRINT_STATE_DIFF)
            memcpy(&lastCPU, &cpu, sizeof(struct CPU));
        
        #if THUMB_CHECK
          if((cpu_get_pc() & 0x1) == 0)
          {
              fprintf(stderr, "ERROR: PC moved out of thumb mode: %08X\n", (cpu_get_pc() - 0x4));
              sim_exit(1);
          }
        #endif
        
        fetch_address = cpu_get_pc() - 0x4;

        // Start differential logging of events when reaching the start PC address.
        // The default value of trace_start_pc (0) cannot be reached in Thumb mode.
        if (fetch_address == trace_start_pc)
        {
          if (!tracingActive)
          {
            // Start the logging of events.
            fprintf(stderr, "### Trace started at PC=%08X\n", trace_start_pc & (~1U));
                tracingActive = 1;
            if (useCSVoutput)
              printStatsCSV();
            else
              // Save baseline for differential statistics.
              saveStats();
          }
        }

        // Stop differential logging of events when reaching the stop PC address.
        // The default value of trace_stop_pc (0) cannot be reached in Thumb mode.
        if (fetch_address == trace_stop_pc)
        {
          if (tracingActive)
          {
            // Stop the logging of events.
            fprintf(stderr, "### Trace stopped at PC=%08X\n", trace_stop_pc & (~1U));
            tracingActive = 0;
            if (useCSVoutput)
              printStatsCSV();
            else
              // Print differential statistics.
              printStatsDelta();
          }
        }

        simLoadInsn(fetch_address, &insn);
        diss_printf("%04X\n", insn);
        
        decode(insn);
        exwbmem(insn);

        // Print any differences caused by the last instruction
        if(PRINT_STATE_DIFF)
            printStateDiff(&lastCPU, &cpu);
 
        if (cpu_get_except() != 0)
        {
          lastCPU.exceptmask = cpu.exceptmask;
          memcpy(&cpu, &lastCPU, sizeof(struct CPU));
          check_except();
        }

        // Hacky way to advance PC if no jumps
        if(!takenBranch)
        {
          #if VERIFY_BRANCHES_TAGGED
            if(cpu_get_pc() != lastCPU.gpr[15])
            {
                fprintf(stderr, "Error: Break in control flow not accounted for\n");
                sim_exit(1);
            }
          #endif
          cpu_set_pc(cpu_get_pc() + 0x2);
        }
        else
        {
          if(tracingActive || logAllEvents)
          {
            taken_branches++;
            // Branching to a target not aligned on a word boundary incurs a penalty on 32-bit-only
            // memories as the insn that follows the branch target will require a full insn memory access.
            if (cpu_get_pc() & 0x2)
              {
                nonword_branch_destinations++;
                branch_fetch_stall = 1;
              }

            // Branching from an insn that ends on a word boundary may incur a penalty on 32-bit only
            // memories as the insn to follow the branch will already have been fetched and the fetch result
            // will have to be discarded if the branch was taken.
            // NOTE 1: cpu_get_pc() has already the value of the target, but lastCPU.gpr[15] points to the insn
            //         that follows the branch, i.e., the fallthru address.
            // NOTE 2: PC values are in Thumb mode (bit 0 set).
            if ((lastCPU.gpr[15] & 0x2) == 0x2)
              nonword_taken_branches++;

            // Count actual stalls caused by cancellation of either fallthru prefetch on cond branch
            // or a non word-aligned branch destination.
            if (branch_fetch_stall)
              {
                // if (data_access_in_cur_cycle)
                //  arbitration_conflicts++;
                SHIFT_ISSUED_DATA_ACCESSES;
                branch_fetch_stalls++;
              }
          }
          cpu_set_pc(cpu_get_pc() + 0x4);
        }

        // Increment counters
        if(((cpu_get_pc() - 6)&0xfffffffe) == addrOfCP)
          cyclesSinceCP = 0;

        // Update Load/Store sequence counters.
        if (load_in_cur_insn)
        {
          if (load_in_prev_insn)
            load_after_load++;
          else if (store_in_prev_insn)
            load_after_store++;
        }

        if (store_in_cur_insn)
        {
          if (load_in_prev_insn)
            store_after_load++;
          else if (store_in_prev_insn)
            store_after_store++;
        }

        // Aggregate use-after-load information.
        if (use_after_load_seen)
        {
          if (load_in_cur_insn)
            use_after_load_ld++;
          else if (store_in_cur_insn)
            use_after_load_st++;
          else if (cmp_in_cur_insn)
            use_after_load_cmp++;
          else
            use_after_load_alu++;
        }
        store_addr_reg_load_in_prev_insn = 0;

        // Shift the load/store information by one instruction.
        load_in_prev_insn = load_in_cur_insn;
        load_in_cur_insn = 0;
        store_in_prev_insn = store_in_cur_insn;
        store_in_cur_insn = 0;
        reg_loaded_in_prev_insn = reg_loaded_in_cur_insn;
        reg_loaded_in_cur_insn = -1;
        cmp_in_cur_insn = 0;
        use_after_load_seen = 0;

        unsigned cp_addr = (cpu.gpr[15] - 4) & (~0x1);
        switch(cp_addr) {
          case 0x000000d8:
          case 0x000000f0:
          case 0x00000102:
          case 0x00000116:
          case 0x0000012a:
          case 0x0000013e:
          case 0x00000152:
          case 0x00000168:
          case 0x00000180:
          case 0x0000019c:
            #if MEM_COUNT_INST
              cp_count++;
            #endif
            reportAndReset(0);
            #if PRINT_CHECKPOINTS
              fprintf(stderr, "%08X: CP: %lu, Caller: %08X\n", cp_addr, cycleCount, (lastCPU.gpr[15]-4 &(~0x1)));
            #endif
            break;
          default:
            break;
        }

        if(addToWasted)
        {
          addToWasted = 0;
          wastedCycles += cyclesSinceCP; 
          cyclesSinceCP = 0;
        }

        if(((cpu_get_pc() - 4)&0xfffffffe) == addrOfRestoreCP)
          addToWasted = 1;

        // Wait for commands from GDB
        if(debug){
          rsp_check_stall();

        while(rsp.stalled)
          handle_rsp();
      }
    }


    return 0;
}
