#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exmemwb.h"
#include "except.h"
#include "decode.h"
#include "rsp-server.h"
#include "sim_support.h"

char *simulatingFilePath = 0;

// Load a program into the simulator's RAM
static void fillState(const char *pFileName)
{
    FILE *fd;

    fd = fopen(pFileName, "r");
    
    // Check that the file opened correctly
    if(fd == NULL)
    {
        fprintf(stderr, "Error: Could not open file %s\n", pFileName);
        sim_exit(1);
    }

    fread(&flash, sizeof(u32), sizeof(flash)/sizeof(u32), fd);

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
  if(cpu.debug)
  {
    rsp.stalled = cpu.debug;
    rsp_trap(); 

    if(i != 0)
      fprintf(stderr, "Simulator exiting due to error...\n");
    
    while(rsp.stalled)
      handle_rsp();
  }

  exit(i);
}


int main(int argc, char *argv[])
{
    char *file = 0;
    int debug = 0;
    int i;

    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s [-g] [-pw|-pb] memory_file\n", argv[0]);
        return 1;
    }

    // Name of the binary file must come last on command line
    file = argv[argc - 1];
    for (i = 1; i < argc - 1; i++)
    {
      if(0 == strncmp("-g", argv[i], strlen("-g")))
      {
        // Enable debugging.
        debug = 1;
      }
      else if (0 == strcmp("-pw", argv[i]))
      {
        // Prefetch mode: WORD (single 32-bit word).
        prefetch_mode = PREFETCH_MODE_WORD;
      }
      else if (0 == strcmp("-pb", argv[i]))
      {
        // Prefetch mode: BUFFER (three 32-bit words)
        prefetch_mode = PREFETCH_MODE_BUFFER;
      }
      else if (0 == strcmp("-s",argv[i]))
      {
        // SUMMARY mode: Disable trace, enable differential event reporting.
        doTrace = 0;
	logAllEvents = 0;
      }
      else if (0 == strcmp("-t", argv[i]))
      {
        // Enable trace, disable differential event reporting.
        doTrace = 1;
      }
      else if (0 == strcmp("-c", argv[i]))
      {
        // Enable CSV output.
        useCSVoutput = 1;
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
    while(1)
    {
        struct CPU lastCPU;
        
        u16 insn;
        takenBranch = 0;
        branch_fetch_stall = 0;
        ram_access = 0;
        flash_access = 0;
        
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
        
        simLoadInsn(cpu_get_pc() - 0x4, &insn);
        diss_printf("%04X\n", insn);
        
        decode(insn);
        exwbmem(insn);

        // Track RAM/Flash arbitration conflicts.
        if (ram_access && flash_access)
          arbitration_conflicts++;

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
            if (lastCPU.gpr[15] & 0x2 == 0)
              canceled_fetches++;

            // Count actual stalls caused by cancellation of either fallthru prefetch on cond branch
            // or a non word-aligned branch destination.
            if (branch_fetch_stall)
              branch_fetch_stalls++;
          }
          cpu_set_pc(cpu_get_pc() + 0x4);
        }

        // Increment counters
        if(((cpu_get_pc() - 6)&0xfffffffe) == addrOfCP)
          cyclesSinceCP = 0;

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
