#include <iostream>
#include <omp.h>

int main() {
    std::cout << "Smart Grid OpenMP Setup Check\n";
    std::cout << "Processors available: "
              << omp_get_num_procs() << "\n";

    omp_set_dynamic(0);

    #pragma omp parallel num_threads(4)
    {
        #pragma omp single
        {
            std::cout << "Threads in this test: "
                      << omp_get_num_threads() << "\n";
        }

        #pragma omp critical
        {
            std::cout << "Thread " << omp_get_thread_num()
                      << " is working\n";
        }
    }

    std::cout << "OpenMP test completed\n";
    return 0;
}