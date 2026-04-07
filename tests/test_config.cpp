#include "../src/common/config.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <cstdio>

int main() {
    // Write a temp config
    const char* tmpFile = "test_tmp.conf";
    {
        std::ofstream f(tmpFile);
        f << "[gsm]\n"
          << "arfcn = 75\n"
          << "bsic  = 20\n"
          << "mcc   = 250\n"
          << "[lte]\n"
          << "pci   = 123\n"
          << "earfcn= 1850\n";
    }

    auto& cfg = rbs::Config::instance();
    assert(cfg.loadFile(tmpFile));

    assert(cfg.getInt("gsm", "arfcn", 0) == 75);
    assert(cfg.getInt("gsm", "bsic",  0) == 20);
    assert(cfg.getInt("gsm", "mcc",   0) == 250);
    assert(cfg.getInt("lte", "pci",   0) == 123);
    assert(cfg.getString("lte", "earfcn", "") == "1850");
    // Missing key → default
    assert(cfg.getInt("lte", "missing_key", 99) == 99);

    std::remove(tmpFile);
    std::puts("test_config PASSED");
    return 0;
}
