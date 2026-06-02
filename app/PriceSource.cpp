#include "SharedMarketData.hpp"
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <random>
#include <chrono>
#include <thread>
#include <csignal>

using namespace Exchange;

SharedMarketData* shm_ptr = nullptr;

void signal_handler(int signum) {
    if (shm_ptr) {
        shm_ptr->running = false;
    }
    std::cout << "\nShutting down PriceSource..." << std::endl;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(fd, sizeof(SharedMarketData)) == -1) {
        perror("ftruncate");
        return 1;
    }

    shm_ptr = (SharedMarketData*)mmap(NULL, sizeof(SharedMarketData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    // Initialize
    shm_ptr->sequence = 0;
    shm_ptr->curr_price = 5000.0;
    shm_ptr->last_price = 5000.0;
    shm_ptr->running = true;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> d(0, 1.5);

    double price = shm_ptr->curr_price;

    std::cout << "PriceSource started. Writing to SHM: " << SHM_NAME << std::endl;

    while (shm_ptr->running) {
        price += d(gen);
        // Clamp price between 4000 and 6000
        if (price < 4000.0) price = 4000.0;
        else if (price > 6000.0) price = 6000.0;

        shm_ptr->update_price(price);
        
        // Print every 10 updates
        static int count = 0;
        if (++count % 10 == 0) {
            std::cout << "Price Update: Curr=" << shm_ptr->curr_price 
                      << " Last=" << shm_ptr->last_price << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    munmap(shm_ptr, sizeof(SharedMarketData));
    shm_unlink(SHM_NAME);
    return 0;
}
