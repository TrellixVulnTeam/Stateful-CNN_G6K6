#pragma once

#include <stdint.h>
#include <stdlib.h>

/* offsets for data on NVM */

// growing up (like heap). Not starting from zero as first few 16 bytes are for testing (see testSPI() function)
#define INTERMEDIATE_VALUES_OFFSET 256
#define SAMPLES_OFFSET (INTERMEDIATE_VALUES_OFFSET + NUM_SLOTS * INTERMEDIATE_VALUES_SIZE)

// growing down (like stack)
#define COUNTERS_OFFSET (NVM_SIZE - sizeof(Counters))
#define FIRST_RUN_OFFSET (COUNTERS_OFFSET - 2)
#define MODEL_OFFSET (FIRST_RUN_OFFSET - 2 * MODEL_DATA_LEN)
#define INTERMEDIATE_PARAMETERS_INFO_OFFSET (MODEL_OFFSET - INTERMEDIATE_PARAMETERS_INFO_DATA_LEN)
#define NODES_OFFSET (INTERMEDIATE_PARAMETERS_INFO_OFFSET - NODES_DATA_LEN)

class Model;

extern Model model_vm;

void read_from_nvm(void* vm_buffer, uint32_t nvm_offset, size_t n);
void write_to_nvm(const void* vm_buffer, uint32_t nvm_offset, size_t n);
void my_erase(void);
void copy_samples_data(void);
