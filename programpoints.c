#include "programpoints.h"

#include "sim_support.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

program_point_t program_points[PROGRAM_POINTS];

static int address2index (uint32_t addr)
{
    assert(addr >= FLASH_START);
    assert(addr < (FLASH_START + FLASH_SIZE));

    return (addr - FLASH_START) >> 1;
}

static uint32_t index2address (int index)
{
    assert(index >= 0);
    assert(index < PROGRAM_POINTS);

    return FLASH_START + (index << 1);
}

void program_point_init (void)
{
    for (int i = 0; i < PROGRAM_POINTS; i++) {
        program_points[i].address = index2address(i);
        program_points[i].count = 0;
    }
}

void program_point_update (uint32_t address)
{
    int index = address2index(address);

    assert(index >= 0);
    assert(index < PROGRAM_POINTS);
    assert(program_points[index].address == address);

    program_points[index].count++;
}

void program_point_print (void)
{
    /* find first and last program point with positive execution count */
    int first = 0;
    int last = 0;
    for (int i = 0; i < PROGRAM_POINTS; i++) {
        if (program_points[i].count) {
            if (first == 0)
                first = i;
            last = i;
        }
    }

    /* open file */
    errno = 0;
    FILE * file = fopen("flow_constraints.ais", "w");
    if (errno != 0)
        fprintf(stderr, "Could not open file for writing.\n");

    /* dump flow constraints to file */
    fprintf(file, "# flow constraints\n");
    /* we dump only for those program points that are likely part of the CFG */
    for (int i = first; i <= last; i++) {
        fprintf(file, "try flow sum: point(thumb::0x%x) == %u;\n", program_points[i].address, program_points[i].count);
    }

    /* close file */
    errno = 0;
    fclose(file);
    if (errno != 0)
        fprintf(stderr, "Error while trying to close file.\n");
}

