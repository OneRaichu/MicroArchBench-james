#include <iostream>
#include <vector>
#include <cstring>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdint>
#include <cmath>
#include <iomanip>


size_t PAGE_SIZE = 4096;


const uint32_t ARM_B_OPCODE = 0x14000000; 
const uint32_t ARM_RET = 0xD65F03C0; 
const uint32_t ARM_NOP = 0xD503201F;

typedef void (*test_func_t)();

class ITLBTestARM {
private:
    uint8_t* code_buffer;
    size_t buffer_size;
    
public:
    ITLBTestARM() : code_buffer(nullptr), buffer_size(0) {
        long sz = sysconf(_SC_PAGESIZE);
        if (sz > 0) PAGE_SIZE = (size_t)sz;
    }

    ~ITLBTestARM() {
        if (code_buffer) {
            munmap(code_buffer, buffer_size);
        }
    }

    void allocate_memory(size_t num_pages) {
        if (code_buffer) {
            munmap(code_buffer, buffer_size);
        }
        
        buffer_size = num_pages * PAGE_SIZE;
        
        code_buffer = (uint8_t*)mmap(NULL, buffer_size, 
                                     PROT_READ | PROT_WRITE | PROT_EXEC, 
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        
        if (code_buffer == MAP_FAILED) {
            perror("mmap failed");
            exit(1);
        }
        

        uint32_t* ptr = (uint32_t*)code_buffer;
        size_t num_instrs = buffer_size / 4;
        for(size_t i=0; i<num_instrs; ++i) {
            ptr[i] = ARM_NOP;
        }
    }

    void generate_code(int num_jumps) {
        uint8_t* cursor = code_buffer;
        
        for (int i = 0; i < num_jumps; ++i) {
            uint8_t* page_start = code_buffer + (i * PAGE_SIZE);
            uint8_t* next_page_start = code_buffer + ((i + 1) * PAGE_SIZE);
            
            uint32_t* current_instr_ptr = (uint32_t*)page_start;

            if (i < num_jumps - 1) {

                int64_t offset_bytes = next_page_start - (uint8_t*)current_instr_ptr;
                
                int32_t offset_instr = (int32_t)(offset_bytes / 4);
                

                uint32_t b_instr = ARM_B_OPCODE | (offset_instr & 0x03FFFFFF);
                
                *current_instr_ptr = b_instr;
            } else {
                *current_instr_ptr = ARM_RET; 
            }
        }


        __builtin___clear_cache((char*)code_buffer, (char*)code_buffer + buffer_size);
    }

    double run_test(int num_jumps, long long iterations) {
        allocate_memory(num_jumps + 1); 
        generate_code(num_jumps);

        test_func_t func = (test_func_t)code_buffer;
        
        // Warmup
        func(); 

        struct timeval tv_begin, tv_end;
        gettimeofday(&tv_begin, NULL);
        
        for (long long k = 0; k < iterations; k++) {
            func();
        }

        gettimeofday(&tv_end, NULL);

        long long timeSpan = (tv_end.tv_sec - tv_begin.tv_sec) * 1000000LL + (tv_end.tv_usec - tv_begin.tv_usec);
        
        double latency = (double)(timeSpan * 1000.0) / (iterations * num_jumps);
        return latency;
    }
};

int main(int argc, char* argv[]) {
    ITLBTestARM tester;
    
    int min_num = 1;
    int max_num = 1024 * 4; 
    
    const long long TARGET_TOTAL_OPS = 20000000LL; 

    std::cout << "Pages,Latency_ns" << std::endl;

    int br_num = min_num;
    while (br_num < max_num) {
        
        long long repeat = TARGET_TOTAL_OPS / br_num;
        if (repeat < 100) repeat = 100;

        double latency = tester.run_test(br_num, repeat);
        
        std::cout << br_num << "," << latency << std::endl;

        if (br_num < 128) br_num += 1;
        else if (br_num < 512) br_num += 8;
        else br_num += 32;
    }

    return 0;
}
